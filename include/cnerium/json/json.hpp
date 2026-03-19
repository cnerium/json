/**
 * @file json.hpp
 * @brief cnerium::json — High-performance, header-only JSON library for C++20
 *
 * @version 1.0.0
 * @author cnerium contributors
 * @copyright Apache-2.0 License
 *
 * @details
 * A production-grade JSON library designed for high-throughput HTTP servers
 * and latency-sensitive applications. Implements a state-machine parser,
 * move-optimized value type, and zero-copy string_view paths where safe.
 *
 * Architecture:
 *   - cnerium::json          — Public API (value, object, array, parse, dump)
 *   - cnerium::json::detail  — Internal parser, serializer, traits, helpers
 *
 * Design goals:
 *   - Minimal heap allocations during parsing
 *   - Cache-friendly internal layout via std::variant
 *   - Compile-time trait resolution for type conversions
 *   - No external dependencies, no UB, strict RFC 8259 compliance
 *
 * Usage:
 * @code
 *   using namespace cnerium::json;
 *
 *   value data = object{
 *       {"name", "Gaspard"},
 *       {"age",  25},
 *       {"skills", array{"C++", "Systems"}},
 *       {"meta", object{
 *           {"active", value{true}},
 *           {"score",  99.5}
 *       }}
 *   };
 *
 *   std::string json_text = data.dump(true);   // pretty print
 *   value parsed = parse(json_text);
 *   std::cout << parsed["name"].as_string();   // "Gaspard"
 * @endcode
 */

#pragma once

#include <algorithm>
#include <cassert>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <limits>
#include <map>
#include <memory>

// Compiler hints
#if defined(__GNUC__) || defined(__clang__)
#define CNERIUM_LIKELY(x) __builtin_expect(!!(x), 1)
#define CNERIUM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define CNERIUM_FORCEINLINE __attribute__((always_inline)) inline
#define CNERIUM_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define CNERIUM_LIKELY(x) (x)
#define CNERIUM_UNLIKELY(x) (x)
#define CNERIUM_FORCEINLINE __forceinline
#define CNERIUM_NOINLINE __declspec(noinline)
#else
#define CNERIUM_LIKELY(x) (x)
#define CNERIUM_UNLIKELY(x) (x)
#define CNERIUM_FORCEINLINE inline
#define CNERIUM_NOINLINE
#endif

namespace cnerium::json
{
  class value;
  struct object;
  struct array;

  /// @brief Represents a JSON null literal.
  struct null_t
  {
    constexpr bool operator==(null_t) const noexcept { return true; }
    constexpr bool operator!=(null_t) const noexcept { return false; }
  };

  /// @brief Global null constant.
  inline constexpr null_t null{};

  /**
   * @brief Exception thrown on JSON parse errors.
   *
   * Carries the byte offset in the source buffer where the error occurred,
   * plus a human-readable description.
   */
  class parse_error : public std::runtime_error
  {
  public:
    /// @brief Byte offset in the input where the error occurred.
    std::size_t offset{};
    /// @brief Source line (1-based) if newline tracking was enabled.
    std::size_t line{1};
    /// @brief Column within the line (1-based).
    std::size_t column{1};

    explicit parse_error(std::string msg, std::size_t off = 0,
                         std::size_t ln = 1, std::size_t col = 1)
        : std::runtime_error(std::move(msg)), offset(off), line(ln), column(col) {}
  };

  /**
   * @brief Exception thrown on invalid type access.
   *
   * E.g., calling as_string() on a number value.
   */
  class type_error : public std::runtime_error
  {
  public:
    explicit type_error(std::string msg) : std::runtime_error(std::move(msg)) {}
  };

  /**
   * @brief Exception thrown on out-of-bounds or missing key access.
   */
  class access_error : public std::runtime_error
  {
  public:
    explicit access_error(std::string msg) : std::runtime_error(std::move(msg)) {}
  };

  /// @brief Identifies the runtime type held by a value.
  enum class value_type : uint8_t
  {
    null_v = 0,
    bool_v = 1,
    integer_v = 2,
    double_v = 3,
    string_v = 4,
    array_v = 5,
    object_v = 6,
  };

  namespace detail
  {
    /// @brief Primary template — not a JSON-convertible type.
    template <typename T>
    struct is_json_scalar : std::false_type
    {
    };
    template <>
    struct is_json_scalar<bool> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<int> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<long> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<long long> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<unsigned> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<unsigned long> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<unsigned long long> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<float> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<double> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<std::string> : std::true_type
    {
    };
    template <>
    struct is_json_scalar<std::string_view> : std::true_type
    {
    };
    template <std::size_t N>
    struct is_json_scalar<char[N]> : std::true_type
    {
    };

    template <typename T>
    inline constexpr bool is_json_scalar_v = is_json_scalar<std::remove_cvref_t<T>>::value;

    /// @brief Detect integer types, excluding bool.
    template <typename T>
    inline constexpr bool is_integer_v =
        std::is_integral_v<std::remove_cvref_t<T>> &&
        !std::is_same_v<std::remove_cvref_t<T>, bool>;

    /// @brief Detect float types.
    template <typename T>
    inline constexpr bool is_float_v =
        std::is_floating_point_v<std::remove_cvref_t<T>>;

    /**
     * @brief A string type with Small Buffer Optimization.
     *
     * Stores strings of up to 23 characters without heap allocation.
     * For longer strings falls back to std::string heap storage.
     * Layout is carefully packed to 32 bytes on x86-64.
     *
     * This is used internally for JSON object keys which are very
     * often short identifiers (< 24 chars).
     */
    class sbo_string
    {
      static constexpr std::size_t SBO_CAP = 23;

      union Storage
      {
        struct Heap
        {
          char *ptr;
          std::size_t size;
          std::size_t cap;
        } heap;
        struct Inline
        {
          char buf[SBO_CAP];
          uint8_t len; // length when inline; 0xFF = heap mode
        } inl;
      } storage_{};

      [[nodiscard]] bool is_heap() const noexcept
      {
        return storage_.inl.len == 0xFF;
      }

      void set_inline_len(std::size_t n) noexcept
      {
        storage_.inl.len = static_cast<uint8_t>(n);
      }

    public:
      sbo_string() noexcept
      {
        storage_.inl.buf[0] = '\0';
        storage_.inl.len = 0;
      }

      explicit sbo_string(std::string_view sv)
      {
        assign(sv);
      }

      sbo_string(const sbo_string &o)
      {
        assign(o.view());
      }

      sbo_string(sbo_string &&o) noexcept
      {
        std::memcpy(&storage_, &o.storage_, sizeof(Storage));
        o.storage_.inl.buf[0] = '\0';
        o.storage_.inl.len = 0;
      }

      sbo_string &operator=(const sbo_string &o)
      {
        if (this != &o)
        {
          destroy();
          assign(o.view());
        }
        return *this;
      }

      sbo_string &operator=(sbo_string &&o) noexcept
      {
        if (this != &o)
        {
          destroy();
          std::memcpy(&storage_, &o.storage_, sizeof(Storage));
          o.storage_.inl.buf[0] = '\0';
          o.storage_.inl.len = 0;
        }
        return *this;
      }

      ~sbo_string() { destroy(); }

      void assign(std::string_view sv)
      {
        destroy();
        if (sv.size() <= SBO_CAP)
        {
          std::memcpy(storage_.inl.buf, sv.data(), sv.size());
          storage_.inl.buf[sv.size()] = '\0';
          set_inline_len(sv.size());
        }
        else
        {
          storage_.inl.len = 0xFF;
          storage_.heap.size = sv.size();
          storage_.heap.cap = sv.size() + 1;
          storage_.heap.ptr = new char[storage_.heap.cap];
          std::memcpy(storage_.heap.ptr, sv.data(), sv.size());
          storage_.heap.ptr[sv.size()] = '\0';
        }
      }

      [[nodiscard]] std::string_view view() const noexcept
      {
        if (is_heap())
          return {storage_.heap.ptr, storage_.heap.size};
        return {storage_.inl.buf, storage_.inl.len};
      }

      [[nodiscard]] const char *c_str() const noexcept
      {
        return is_heap() ? storage_.heap.ptr : storage_.inl.buf;
      }

      [[nodiscard]] std::size_t size() const noexcept
      {
        return is_heap() ? storage_.heap.size : storage_.inl.len;
      }

      [[nodiscard]] bool empty() const noexcept { return size() == 0; }

      bool operator==(const sbo_string &o) const noexcept { return view() == o.view(); }
      bool operator!=(const sbo_string &o) const noexcept { return !(*this == o); }
      bool operator<(const sbo_string &o) const noexcept { return view() < o.view(); }

      bool operator==(std::string_view sv) const noexcept { return view() == sv; }

      std::string to_string() const { return std::string(view()); }

    private:
      void destroy() noexcept
      {
        if (is_heap())
        {
          delete[] storage_.heap.ptr;
        }
      }
    };

    /**
     * @brief Compact numeric type that distinguishes integer vs float.
     *
     * Avoids double round-trips for integral JSON values.
     * Stored as a tagged union (8 bytes total on 64-bit).
     */
    struct number
    {
      enum class kind : uint8_t
      {
        integer,
        floating
      } tag;
      union
      {
        int64_t i;
        double d;
      };

      number() noexcept : tag(kind::integer), i(0) {}
      explicit number(int64_t v) noexcept : tag(kind::integer), i(v) {}
      explicit number(double v) noexcept : tag(kind::floating), d(v) {}

      [[nodiscard]] bool is_integer() const noexcept { return tag == kind::integer; }
      [[nodiscard]] bool is_floating() const noexcept { return tag == kind::floating; }

      [[nodiscard]] int64_t as_int() const noexcept { return is_integer() ? i : static_cast<int64_t>(d); }
      [[nodiscard]] double as_double() const noexcept { return is_integer() ? static_cast<double>(i) : d; }

      bool operator==(const number &o) const noexcept
      {
        if (tag == o.tag)
        {
          return tag == kind::integer ? (i == o.i) : (d == o.d);
        }
        return as_double() == o.as_double();
      }
      bool operator!=(const number &o) const noexcept { return !(*this == o); }
    };
  } // namespace detail

  /**
   * @brief A JSON array: an ordered sequence of json values.
   *
   * Backed by std::vector<value> for O(1) random access and
   * amortized O(1) append.
   */
  struct array;

  /**
   * @brief A JSON object: an ordered map from string keys to values.
   *
   * Uses std::vector<pair<string, value>> for insertion-order
   * preservation and good cache locality on small objects.
   * Key lookup is O(n) which is acceptable for typical JSON objects
   * (< 64 keys). For large objects consider using the find() API.
   */
  struct object;

  /**
   * @brief Polymorphic JSON value type.
   *
   * Wraps one of: null, bool, integer (int64), double, string, array, object.
   * Uses std::variant for type-safe storage.
   *
   * The type stores heap objects (array, object) as value (not pointer)
   * inside the variant; the variant itself is allocated on the heap when
   * the value is part of a larger structure, giving good cache locality
   * for small JSON trees.
   *
   * Thread safety: none (caller responsibility, same as STL containers).
   */
  class value
  {
  public:
    using null_type = null_t;
    using bool_type = bool;
    using integer_type = int64_t;
    using double_type = double;
    using string_type = std::string;

    // array and object are defined below; value uses pointers to them
    // via unique_ptr to avoid incomplete-type issues with variant.
    // We use raw pointers + manual RAII for performance
    // (unique_ptr has non-trivial move in some std-lib implementations).

  private:
    enum class tag_t : uint8_t
    {
      null_v = 0,
      bool_v = 1,
      integer_v = 2,
      double_v = 3,
      string_v = 4,
      array_v = 5,
      object_v = 6,
    };

    tag_t tag_{tag_t::null_v};

    union Storage
    {
      bool b;
      int64_t i;
      double d;
      std::string *s; // heap allocated
      array *a;       // heap allocated
      object *o;      // heap allocated
      Storage() noexcept : i(0) {}
    } u_;

    void destroy() noexcept;
    void copy_from(const value &o);
    void move_from(value &&o) noexcept;

  public:
    /// @brief Default constructs a null value.
    value() noexcept : tag_(tag_t::null_v) { u_.i = 0; }

    /// @brief Construct from null sentinel.
    value(null_t) noexcept : tag_(tag_t::null_v) { u_.i = 0; }

    /// @brief Construct from bool.
    value(bool b) noexcept : tag_(tag_t::bool_v) { u_.b = b; }

    // Avoid bool ambiguity with integral types:
    /// @brief Construct from integer types.
    template <typename T>
      requires(detail::is_integer_v<T>)
    value(T v) noexcept : tag_(tag_t::integer_v)
    {
      u_.i = static_cast<int64_t>(v);
    }

    /// @brief Construct from floating-point types.
    template <typename T>
      requires(detail::is_float_v<T>)
    value(T v) noexcept : tag_(tag_t::double_v)
    {
      u_.d = static_cast<double>(v);
    }

    /// @brief Construct from std::string (copy).
    value(const std::string &s) : tag_(tag_t::string_v)
    {
      u_.s = new std::string(s);
    }

    /// @brief Construct from std::string (move).
    value(std::string &&s) : tag_(tag_t::string_v)
    {
      u_.s = new std::string(std::move(s));
    }

    /// @brief Construct from string_view.
    value(std::string_view sv) : tag_(tag_t::string_v)
    {
      u_.s = new std::string(sv);
    }

    /// @brief Construct from string literal.
    value(const char *cstr) : tag_(tag_t::string_v)
    {
      u_.s = new std::string(cstr ? cstr : "");
    }

    /// @brief Construct from array (copy).
    value(const array &a);

    /// @brief Construct from array (move).
    value(array &&a);

    /// @brief Construct from object (copy).
    value(const object &o);

    /// @brief Construct from object (move).
    value(object &&o);

    value(const value &o) { copy_from(o); }
    value(value &&o) noexcept { move_from(std::move(o)); }

    value(std::initializer_list<std::pair<std::string_view, value>> il);

    value &operator=(const value &o)
    {
      if (this != &o)
      {
        destroy();
        copy_from(o);
      }
      return *this;
    }

    value &operator=(value &&o) noexcept
    {
      if (this != &o)
      {
        destroy();
        move_from(std::move(o));
      }
      return *this;
    }

    ~value() { destroy(); }

    /// @brief Returns the runtime type tag.
    [[nodiscard]] value_type type() const noexcept
    {
      return static_cast<value_type>(static_cast<uint8_t>(tag_));
    }

    [[nodiscard]] bool is_null() const noexcept { return tag_ == tag_t::null_v; }
    [[nodiscard]] bool is_bool() const noexcept { return tag_ == tag_t::bool_v; }
    [[nodiscard]] bool is_integer() const noexcept { return tag_ == tag_t::integer_v; }
    [[nodiscard]] bool is_double() const noexcept { return tag_ == tag_t::double_v; }
    [[nodiscard]] bool is_number() const noexcept { return tag_ == tag_t::integer_v || tag_ == tag_t::double_v; }
    [[nodiscard]] bool is_string() const noexcept { return tag_ == tag_t::string_v; }
    [[nodiscard]] bool is_array() const noexcept { return tag_ == tag_t::array_v; }
    [[nodiscard]] bool is_object() const noexcept { return tag_ == tag_t::object_v; }

    /// @brief Returns the boolean value.
    /// @throws type_error if not bool.
    [[nodiscard]] bool as_bool() const
    {
      if (CNERIUM_UNLIKELY(tag_ != tag_t::bool_v))
        throw type_error(std::string("value is not bool (type=") + std::string(type_name()) + ")");
      return u_.b;
    }

    /// @brief Returns the integer value.
    /// @throws type_error if not integer.
    [[nodiscard]] int64_t as_integer() const
    {
      if (CNERIUM_UNLIKELY(tag_ != tag_t::integer_v))
        throw type_error(std::string("value is not integer (type=") + std::string(type_name()) + ")");
      return u_.i;
    }

    /// @brief Returns the double value.
    /// @throws type_error if not double.
    [[nodiscard]] double as_double() const
    {
      if (CNERIUM_UNLIKELY(tag_ != tag_t::double_v))
        throw type_error(std::string("value is not double (type=") + std::string(type_name()) + ")");
      return u_.d;
    }

    /// @brief Returns numeric value as double regardless of internal storage.
    /// @throws type_error if not a number.
    [[nodiscard]] double as_number() const
    {
      if (tag_ == tag_t::integer_v)
        return static_cast<double>(u_.i);
      if (tag_ == tag_t::double_v)
        return u_.d;
      throw type_error(std::string("value is not a number (type=") + std::string(type_name()) + ")");
    }

    /// @brief Returns string value by const reference.
    /// @throws type_error if not string.
    [[nodiscard]] const std::string &as_string() const
    {
      if (CNERIUM_UNLIKELY(tag_ != tag_t::string_v))
        throw type_error(std::string("value is not string (type=") + std::string(type_name()) + ")");
      return *u_.s;
    }

    /// @brief Returns mutable string reference.
    [[nodiscard]] std::string &as_string()
    {
      if (CNERIUM_UNLIKELY(tag_ != tag_t::string_v))
        throw type_error(std::string("value is not string (type=") + std::string(type_name()) + ")");
      return *u_.s;
    }

    /// @brief Returns const reference to the array.
    /// @throws type_error if not array.
    [[nodiscard]] const array &as_array() const;

    /// @brief Returns mutable reference to the array.
    [[nodiscard]] array &as_array();

    /// @brief Returns const reference to the object.
    /// @throws type_error if not object.
    [[nodiscard]] const object &as_object() const;

    /// @brief Returns mutable reference to the object.
    [[nodiscard]] object &as_object();

    /**
     * @brief Attempts to extract a value of type T.
     * @tparam T One of: bool, int64_t, double, std::string, array, object.
     * @return std::optional<T> — nullopt if type mismatch.
     */
    template <typename T>
    [[nodiscard]] std::optional<T> try_get() const noexcept;

    /**
     * @brief Object key access (inserts null if key absent, mutable).
     * @throws type_error if not an object.
     */
    value &operator[](std::string_view key);

    /**
     * @brief Object key access (const; throws if key absent).
     * @throws type_error, access_error.
     */
    const value &operator[](std::string_view key) const;

    /**
     * @brief Array index access (mutable).
     * @throws type_error if not array; access_error if out of bounds.
     */
    value &operator[](std::size_t idx);

    /**
     * @brief Array index access (const).
     * @throws type_error, access_error.
     */
    const value &operator[](std::size_t idx) const;

    /**
     * @brief Returns the number of elements (array) or key-value pairs (object).
     * @throws type_error for scalar types.
     */
    [[nodiscard]] std::size_t size() const;

    /**
     * @brief Returns true if size() == 0.
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief Returns true if object contains the given key.
     * @throws type_error if not object.
     */
    [[nodiscard]] bool contains(std::string_view key) const;

    /**
     * @brief Erases a key from the object.
     * @returns true if key was found and erased.
     * @throws type_error if not object.
     */
    bool erase(std::string_view key);

    /**
     * @brief Clears all elements from array or object; sets null to null.
     */
    void clear();

    /**
     * @brief Appends a value to the array.
     * @throws type_error if not array.
     */
    void push_back(value v);

    /**
     * @brief Emplaces a value at the back of the array.
     * @throws type_error if not array.
     */
    template <typename... Args>
    value &emplace_back(Args &&...args);

    bool operator==(const value &o) const noexcept;
    bool operator!=(const value &o) const noexcept { return !(*this == o); }

    /**
     * @brief Serializes this value to a JSON string.
     * @param pretty  If true, output is indented with newlines.
     * @param indent  Indentation width (spaces) when pretty=true.
     * @return UTF-8 JSON string.
     */
    [[nodiscard]] std::string dump(bool pretty = false, int indent = 4) const;

    /**
     * @brief Appends JSON representation to an existing string.
     *
     * More efficient than dump() when building a larger payload.
     */
    void dump_to(std::string &out, bool pretty = false, int indent = 4, int depth = 0) const;

    [[nodiscard]] std::string_view type_name() const noexcept
    {
      switch (tag_)
      {
      case tag_t::null_v:
        return "null";
      case tag_t::bool_v:
        return "bool";
      case tag_t::integer_v:
        return "integer";
      case tag_t::double_v:
        return "double";
      case tag_t::string_v:
        return "string";
      case tag_t::array_v:
        return "array";
      case tag_t::object_v:
        return "object";
      }
      return "unknown";
    }
  };

  /**
   * @brief JSON array — ordered sequence of cnerium::json::value.
   *
   * Thin wrapper over std::vector<value> that supports
   * initializer_list construction for ergonomic usage:
   *
   * @code
   *   array skills{"C++", "Rust", 42, true};
   * @endcode
   */
  struct array
  {
    using storage_t = std::vector<value>;
    using iterator = storage_t::iterator;
    using const_iterator = storage_t::const_iterator;

    storage_t items;

    array() = default;

    /// @brief Construct from initializer list of values.
    array(std::initializer_list<value> il) : items(il) {}

    /// @brief Construct from existing vector (move).
    explicit array(storage_t &&v) : items(std::move(v)) {}

    /// @brief Construct from existing vector (copy).
    explicit array(const storage_t &v) : items(v) {}

    template <typename T>
      requires(!std::is_same_v<std::remove_cvref_t<T>, value>)
    array(std::initializer_list<T> il)
    {
      items.reserve(il.size());
      for (const auto &v : il)
      {
        items.emplace_back(value{v});
      }
    }

    [[nodiscard]] std::size_t size() const noexcept { return items.size(); }
    [[nodiscard]] bool empty() const noexcept { return items.empty(); }

    value &operator[](std::size_t i) { return items[i]; }
    const value &operator[](std::size_t i) const { return items[i]; }

    /// @brief Bounds-checked access.
    value &at(std::size_t i) { return items.at(i); }
    const value &at(std::size_t i) const { return items.at(i); }

    void push_back(value v) { items.push_back(std::move(v)); }

    template <typename... Args>
    value &emplace_back(Args &&...args)
    {
      return items.emplace_back(std::forward<Args>(args)...);
    }

    void clear() { items.clear(); }

    void reserve(std::size_t n) { items.reserve(n); }

    iterator begin() noexcept { return items.begin(); }
    iterator end() noexcept { return items.end(); }
    const_iterator begin() const noexcept { return items.begin(); }
    const_iterator end() const noexcept { return items.end(); }
    const_iterator cbegin() const noexcept { return items.cbegin(); }
    const_iterator cend() const noexcept { return items.cend(); }

    value &front() { return items.front(); }
    const value &front() const { return items.front(); }
    value &back() { return items.back(); }
    const value &back() const { return items.back(); }

    bool operator==(const array &o) const noexcept { return items == o.items; }
    bool operator!=(const array &o) const noexcept { return !(*this == o); }
  };

  /**
   * @brief JSON object — ordered map from string keys to json values.
   *
   * Uses std::vector<pair<std::string, value>> to maintain insertion order
   * and maximize cache locality for typical small objects.
   *
   * Key lookup is O(n). For high-throughput read-heavy workloads with
   * large objects, consider using find() which short-circuits on first match.
   *
   * Construction:
   * @code
   *   object meta{
   *       {"active", value{true}},
   *       {"score",  99.5}
   *   };
   * @endcode
   */
  struct object
  {
    using pair_t = std::pair<std::string, value>;
    using storage_t = std::vector<pair_t>;
    using iterator = storage_t::iterator;
    using const_iterator = storage_t::const_iterator;

    storage_t members;

    object() = default;

    /// @brief Construct from initializer list of key-value pairs.
    object(std::initializer_list<std::pair<std::string_view, value>> il)
    {
      members.reserve(il.size());
      for (const auto &p : il)
      {
        members.emplace_back(
            std::string(p.first),
            p.second);
      }
    }
    /// @brief Construct from existing storage (move).
    explicit object(storage_t &&v) : members(std::move(v)) {}

    /// @brief Construct from existing storage (copy).
    explicit object(const storage_t &v) : members(v) {}

    [[nodiscard]] std::size_t size() const noexcept { return members.size(); }
    [[nodiscard]] bool empty() const noexcept { return members.empty(); }

    /// @brief Find iterator to key.
    [[nodiscard]] iterator find(std::string_view key) noexcept
    {
      for (auto it = members.begin(); it != members.end(); ++it)
        if (it->first == key)
          return it;
      return members.end();
    }

    [[nodiscard]] const_iterator find(std::string_view key) const noexcept
    {
      for (auto it = members.begin(); it != members.end(); ++it)
        if (it->first == key)
          return it;
      return members.end();
    }

    /// @brief Check if key exists.
    [[nodiscard]] bool contains(std::string_view key) const noexcept
    {
      return find(key) != members.end();
    }

    /**
     * @brief Mutable access; inserts null if key absent.
     */
    value &operator[](std::string_view key)
    {
      auto it = find(key);
      if (it != members.end())
        return it->second;
      members.emplace_back(std::string(key), value{});
      return members.back().second;
    }

    /**
     * @brief Const access; throws access_error if key absent.
     */
    const value &operator[](std::string_view key) const
    {
      auto it = find(key);
      if (it == members.end())
        throw access_error("key not found: " + std::string(key));
      return it->second;
    }

    /**
     * @brief Inserts or overwrites a key.
     */
    void set(std::string key, value val)
    {
      auto it = find(key);
      if (it != members.end())
      {
        it->second = std::move(val);
      }
      else
      {
        members.emplace_back(std::move(key), std::move(val));
      }
    }

    /**
     * @brief Erases a key.
     * @return true if the key existed.
     */
    bool erase(std::string_view key)
    {
      auto it = find(key);
      if (it == members.end())
        return false;
      members.erase(it);
      return true;
    }

    void clear() { members.clear(); }

    void reserve(std::size_t n) { members.reserve(n); }

    iterator begin() noexcept { return members.begin(); }
    iterator end() noexcept { return members.end(); }
    const_iterator begin() const noexcept { return members.begin(); }
    const_iterator end() const noexcept { return members.end(); }
    const_iterator cbegin() const noexcept { return members.cbegin(); }
    const_iterator cend() const noexcept { return members.cend(); }

    bool operator==(const object &o) const noexcept
    {
      if (members.size() != o.members.size())
        return false;
      for (const auto &[k, v] : members)
      {
        auto it = o.find(k);
        if (it == o.end())
          return false;
        if (it->second != v)
          return false;
      }
      return true;
    }
    bool operator!=(const object &o) const noexcept { return !(*this == o); }
  };

  inline value::value(std::initializer_list<std::pair<std::string_view, value>> il)
      : tag_(tag_t::object_v)
  {
    u_.o = new object(il);
  }

  inline void value::destroy() noexcept
  {
    switch (tag_)
    {
    case tag_t::string_v:
      delete u_.s;
      break;
    case tag_t::array_v:
      delete u_.a;
      break;
    case tag_t::object_v:
      delete u_.o;
      break;
    default:
      break;
    }
    tag_ = tag_t::null_v;
    u_.i = 0;
  }

  inline void value::copy_from(const value &o)
  {
    tag_ = o.tag_;
    switch (o.tag_)
    {
    case tag_t::null_v:
      u_.i = 0;
      break;
    case tag_t::bool_v:
      u_.b = o.u_.b;
      break;
    case tag_t::integer_v:
      u_.i = o.u_.i;
      break;
    case tag_t::double_v:
      u_.d = o.u_.d;
      break;
    case tag_t::string_v:
      u_.s = new std::string(*o.u_.s);
      break;
    case tag_t::array_v:
      u_.a = new array(*o.u_.a);
      break;
    case tag_t::object_v:
      u_.o = new object(*o.u_.o);
      break;
    }
  }

  inline void value::move_from(value &&o) noexcept
  {
    tag_ = o.tag_;
    u_ = o.u_;
    o.tag_ = tag_t::null_v;
    o.u_.i = 0;
  }

  // -- array / object constructors --

  inline value::value(const array &a) : tag_(tag_t::array_v)
  {
    u_.a = new array(a);
  }
  inline value::value(array &&a) : tag_(tag_t::array_v)
  {
    u_.a = new array(std::move(a));
  }
  inline value::value(const object &o) : tag_(tag_t::object_v)
  {
    u_.o = new object(o);
  }
  inline value::value(object &&o) : tag_(tag_t::object_v)
  {
    u_.o = new object(std::move(o));
  }

  // -- as_array / as_object --

  inline const array &value::as_array() const
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::array_v))
      throw type_error("value is not array (type=" + std::string(type_name()) + ")");
    return *u_.a;
  }
  inline array &value::as_array()
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::array_v))
      throw type_error("value is not array (type=" + std::string(type_name()) + ")");
    return *u_.a;
  }
  inline const object &value::as_object() const
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::object_v))
      throw type_error("value is not object (type=" + std::string(type_name()) + ")");
    return *u_.o;
  }
  inline object &value::as_object()
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::object_v))
      throw type_error("value is not object (type=" + std::string(type_name()) + ")");
    return *u_.o;
  }

  // -- try_get specializations --

  template <>
  inline std::optional<bool> value::try_get<bool>() const noexcept
  {
    if (tag_ == tag_t::bool_v)
      return u_.b;
    return std::nullopt;
  }
  template <>
  inline std::optional<int64_t> value::try_get<int64_t>() const noexcept
  {
    if (tag_ == tag_t::integer_v)
      return u_.i;
    return std::nullopt;
  }
  template <>
  inline std::optional<double> value::try_get<double>() const noexcept
  {
    if (tag_ == tag_t::double_v)
      return u_.d;
    if (tag_ == tag_t::integer_v)
      return static_cast<double>(u_.i);
    return std::nullopt;
  }
  template <>
  inline std::optional<std::string> value::try_get<std::string>() const noexcept
  {
    if (tag_ == tag_t::string_v)
      return *u_.s;
    return std::nullopt;
  }
  template <>
  inline std::optional<array> value::try_get<array>() const noexcept
  {
    if (tag_ == tag_t::array_v)
      return *u_.a;
    return std::nullopt;
  }
  template <>
  inline std::optional<object> value::try_get<object>() const noexcept
  {
    if (tag_ == tag_t::object_v)
      return *u_.o;
    return std::nullopt;
  }

  // -- subscript operators --

  inline value &value::operator[](std::string_view key)
  {
    if (tag_ != tag_t::object_v)
      throw type_error("operator[string] requires object, got " + std::string(type_name()));
    return (*u_.o)[key];
  }

  inline const value &value::operator[](std::string_view key) const
  {
    if (tag_ != tag_t::object_v)
      throw type_error("operator[string] requires object, got " + std::string(type_name()));

    auto it = u_.o->find(key);
    if (it == u_.o->members.end())
      throw access_error("key not found: " + std::string(key));

    return it->second;
  }

  inline value &value::operator[](std::size_t idx)
  {
    if (tag_ != tag_t::array_v)
      throw type_error("operator[size_t] requires array, got " + std::string(type_name()));
    if (CNERIUM_UNLIKELY(idx >= u_.a->size()))
      throw access_error("array index " + std::to_string(idx) + " out of bounds (size=" + std::to_string(u_.a->size()) + ")");
    return (*u_.a)[idx];
  }

  inline const value &value::operator[](std::size_t idx) const
  {
    if (tag_ != tag_t::array_v)
      throw type_error("operator[size_t] requires array, got " + std::string(type_name()));
    if (CNERIUM_UNLIKELY(idx >= u_.a->size()))
      throw access_error("array index " + std::to_string(idx) + " out of bounds");
    return (*u_.a)[idx];
  }

  // -- collection API --

  inline std::size_t value::size() const
  {
    if (tag_ == tag_t::array_v)
      return u_.a->size();
    if (tag_ == tag_t::object_v)
      return u_.o->size();
    if (tag_ == tag_t::string_v)
      return u_.s->size();
    throw type_error("size() not applicable to " + std::string(type_name()));
  }

  inline bool value::empty() const
  {
    if (tag_ == tag_t::array_v)
      return u_.a->empty();
    if (tag_ == tag_t::object_v)
      return u_.o->empty();
    if (tag_ == tag_t::string_v)
      return u_.s->empty();
    if (tag_ == tag_t::null_v)
      return true;
    return false;
  }

  inline bool value::contains(std::string_view key) const
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::object_v))
      throw type_error("contains() requires object, got " + std::string(type_name()));
    return u_.o->contains(key);
  }

  inline bool value::erase(std::string_view key)
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::object_v))
      throw type_error("erase() requires object, got " + std::string(type_name()));
    return u_.o->erase(key);
  }

  inline void value::clear()
  {
    switch (tag_)
    {
    case tag_t::null_v:
      break;
    case tag_t::array_v:
      u_.a->clear();
      break;
    case tag_t::object_v:
      u_.o->clear();
      break;
    case tag_t::string_v:
      u_.s->clear();
      break;
    default:
      break;
    }
  }

  inline void value::push_back(value v)
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::array_v))
      throw type_error("push_back() requires array, got " + std::string(type_name()));
    u_.a->push_back(std::move(v));
  }

  template <typename... Args>
  inline value &value::emplace_back(Args &&...args)
  {
    if (CNERIUM_UNLIKELY(tag_ != tag_t::array_v))
      throw type_error("emplace_back() requires array, got " + std::string(type_name()));
    return u_.a->emplace_back(std::forward<Args>(args)...);
  }

  // -- comparison --

  inline bool value::operator==(const value &o) const noexcept
  {
    if (tag_ != o.tag_)
    {
      // Allow integer == double comparison
      if (tag_ == tag_t::integer_v && o.tag_ == tag_t::double_v)
        return static_cast<double>(u_.i) == o.u_.d;
      if (tag_ == tag_t::double_v && o.tag_ == tag_t::integer_v)
        return u_.d == static_cast<double>(o.u_.i);
      return false;
    }
    switch (tag_)
    {
    case tag_t::null_v:
      return true;
    case tag_t::bool_v:
      return u_.b == o.u_.b;
    case tag_t::integer_v:
      return u_.i == o.u_.i;
    case tag_t::double_v:
      return u_.d == o.u_.d;
    case tag_t::string_v:
      return *u_.s == *o.u_.s;
    case tag_t::array_v:
      return *u_.a == *o.u_.a;
    case tag_t::object_v:
      return *u_.o == *o.u_.o;
    }
    return false;
  }

  namespace detail
  {
    /**
     * @brief High-performance JSON serializer.
     *
     * Writes directly into a pre-reserved std::string to avoid
     * multiple small allocations. Uses numeric-to-string via
     * std::to_chars (locale-independent, no heap).
     */
    struct serializer
    {
      /// @brief Appends a JSON-escaped string (with surrounding quotes).
      static void write_string(std::string &out, std::string_view sv)
      {
        out.push_back('"');
        const char *p = sv.data();
        const char *end = p + sv.size();
        while (p < end)
        {
          unsigned char c = static_cast<unsigned char>(*p);
          switch (c)
          {
          case '"':
            out += "\\\"";
            break;
          case '\\':
            out += "\\\\";
            break;
          case '\b':
            out += "\\b";
            break;
          case '\f':
            out += "\\f";
            break;
          case '\n':
            out += "\\n";
            break;
          case '\r':
            out += "\\r";
            break;
          case '\t':
            out += "\\t";
            break;
          default:
            if (c < 0x20u)
            {
              // Control characters: \uXXXX
              char buf[8];
              buf[0] = '\\';
              buf[1] = 'u';
              buf[2] = '0';
              buf[3] = '0';
              static constexpr char hex[] = "0123456789abcdef";
              buf[4] = hex[(c >> 4) & 0xF];
              buf[5] = hex[c & 0xF];
              out.append(buf, 6);
            }
            else
            {
              out.push_back(static_cast<char>(c));
            }
            break;
          }
          ++p;
        }
        out.push_back('"');
      }

      /// @brief Appends integer to output string using std::to_chars.
      static void write_integer(std::string &out, int64_t v)
      {
        char buf[24];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v);
        out.append(buf, ptr);
      }

      /// @brief Appends double to output string. Uses to_chars with shortest repr.
      static void write_double(std::string &out, double v)
      {
        // Special values
        if (v != v)
        {
          out += "null";
          return;
        } // NaN → null (JSON spec)
        if (v == std::numeric_limits<double>::infinity())
        {
          out += "1e308";
          return;
        }
        if (v == -std::numeric_limits<double>::infinity())
        {
          out += "-1e308";
          return;
        }

        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v,
                                       std::chars_format::general, 17);
        if (ec == std::errc{})
        {
          // Check if output looks like an integer; add .0 for clarity
          std::string_view sv(buf, ptr - buf);
          bool has_dot = sv.find('.') != std::string_view::npos;
          bool has_exp = sv.find('e') != std::string_view::npos ||
                         sv.find('E') != std::string_view::npos;
          out.append(buf, ptr);
          if (!has_dot && !has_exp)
          {
            out += ".0";
          }
        }
        else
        {
          // Fallback (should not happen for normal doubles)
          out += std::to_string(v);
        }
      }

      static void write_indent(std::string &out, int depth, int indent_width)
      {
        out.push_back('\n');
        int spaces = depth * indent_width;
        // Efficiently write spaces
        static constexpr std::string_view spaces_buf =
            "                                                                ";
        while (spaces > 0)
        {
          int chunk = std::min(spaces, (int)spaces_buf.size());
          out.append(spaces_buf.data(), chunk);
          spaces -= chunk;
        }
      }

      // -- main entry point --

      static void write(std::string &out, const value &v,
                        bool pretty, int indent_w, int depth)
      {
        using tag = value_type;
        switch (v.type())
        {
        case tag::null_v:
          out += "null";
          break;

        case tag::bool_v:
          out += (v.as_bool() ? "true" : "false");
          break;

        case tag::integer_v:
          write_integer(out, v.as_integer());
          break;

        case tag::double_v:
          write_double(out, v.as_double());
          break;

        case tag::string_v:
          write_string(out, v.as_string());
          break;

        case tag::array_v:
        {
          const auto &arr = v.as_array();
          if (arr.empty())
          {
            out += "[]";
            break;
          }
          out.push_back('[');
          bool first = true;
          for (const auto &elem : arr)
          {
            if (!first)
              out.push_back(',');
            if (pretty)
              write_indent(out, depth + 1, indent_w);
            write(out, elem, pretty, indent_w, depth + 1);
            first = false;
          }
          if (pretty)
            write_indent(out, depth, indent_w);
          out.push_back(']');
          break;
        }

        case tag::object_v:
        {
          const auto &obj = v.as_object();
          if (obj.empty())
          {
            out += "{}";
            break;
          }
          out.push_back('{');
          bool first = true;
          for (const auto &[k, val] : obj)
          {
            if (!first)
              out.push_back(',');
            if (pretty)
              write_indent(out, depth + 1, indent_w);
            write_string(out, k);
            out.push_back(':');
            if (pretty)
              out.push_back(' ');
            write(out, val, pretty, indent_w, depth + 1);
            first = false;
          }
          if (pretty)
            write_indent(out, depth, indent_w);
          out.push_back('}');
          break;
        }
        }
      }
    };

  } // namespace detail

  // -- value::dump --

  inline std::string value::dump(bool pretty, int indent) const
  {
    std::string out;
    // Pre-reserve: heuristic based on type
    switch (tag_)
    {
    case tag_t::array_v:
      if (u_.a)
        out.reserve(u_.a->size() * 16 + 2);
      break;
    case tag_t::object_v:
      if (u_.o)
        out.reserve(u_.o->size() * 32 + 2);
      break;
    case tag_t::string_v:
      if (u_.s)
        out.reserve(u_.s->size() + 2);
      break;
    default:
      out.reserve(16);
      break;
    }
    detail::serializer::write(out, *this, pretty, indent, 0);
    return out;
  }

  inline void value::dump_to(std::string &out, bool pretty, int indent, int depth) const
  {
    detail::serializer::write(out, *this, pretty, indent, depth);
  }

  /**
   * @brief Writes the value as compact JSON to an output stream.
   */
  inline std::ostream &operator<<(std::ostream &os, const value &v)
  {
    os << v.dump(false);
    return os;
  }

  namespace detail
  {
    /**
     * @brief Cursor over an immutable byte buffer.
     *
     * Tracks current position and provides helpers for
     * peeking, consuming, and span extraction.
     */
    struct cursor
    {
      const char *buf;
      std::size_t len;
      std::size_t pos{0};
      std::size_t line{1};
      std::size_t col{1};

      cursor(const char *b, std::size_t l) noexcept : buf(b), len(l) {}

      [[nodiscard]] bool done() const noexcept { return pos >= len; }
      [[nodiscard]] bool has(std::size_t n) const noexcept { return pos + n <= len; }

      [[nodiscard]] char peek() const noexcept
      {
        return CNERIUM_LIKELY(pos < len) ? buf[pos] : '\0';
      }
      [[nodiscard]] char peek(std::size_t ahead) const noexcept
      {
        std::size_t p = pos + ahead;
        return p < len ? buf[p] : '\0';
      }

      char consume() noexcept
      {
        char c = buf[pos++];
        if (c == '\n')
        {
          ++line;
          col = 1;
        }
        else
        {
          ++col;
        }
        return c;
      }

      void advance(std::size_t n) noexcept
      {
        for (std::size_t i = 0; i < n && pos < len; ++i)
          consume();
      }

      void skip_whitespace() noexcept
      {
        while (pos < len)
        {
          char c = buf[pos];
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
          {
            consume();
          }
          else
          {
            break;
          }
        }
      }

      [[nodiscard]] parse_error make_error(std::string msg) const
      {
        return parse_error(std::move(msg), pos, line, col);
      }
    };

    /**
     * @brief Decodes a 4-hex-digit Unicode escape \\uXXXX.
     * @return The codepoint, or -1 on malformed input.
     */
    [[nodiscard]] static inline int32_t decode_hex4(const char *p) noexcept
    {
      int32_t cp = 0;
      for (int i = 0; i < 4; ++i)
      {
        char c = p[i];
        cp <<= 4;
        if (c >= '0' && c <= '9')
          cp |= (c - '0');
        else if (c >= 'a' && c <= 'f')
          cp |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
          cp |= (c - 'A' + 10);
        else
          return -1;
      }
      return cp;
    }

    /**
     * @brief Encodes a Unicode codepoint to UTF-8 and appends to str.
     */
    static inline void encode_utf8(std::string &out, uint32_t cp)
    {
      if (cp <= 0x7F)
      {
        out.push_back(static_cast<char>(cp));
      }
      else if (cp <= 0x7FF)
      {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
      else if (cp <= 0xFFFF)
      {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
      else if (cp <= 0x10FFFF)
      {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
      }
      // Surrogates and out-of-range values are silently dropped
    }

    /**
     * @brief RFC 8259 compliant JSON parser.
     *
     * Design:
     *   - Single-pass over the input buffer via a cursor.
     *   - Recursive descent but iterative inner loops for
     *     objects/arrays to reduce call depth on flat structures.
     *   - Number parsing via std::from_chars (locale-independent).
     *   - String parsing builds std::string via direct append,
     *     reserving capacity based on substring length.
     *   - Max nesting depth configurable (default 512) to
     *     resist stack-overflow attacks.
     *
     * Thread safety: parser instances are not shared; each parse()
     * call creates a local parser.
     */
    struct parser
    {
      cursor cur_;
      int depth_{0};
      int max_depth_;

      explicit parser(const char *buf, std::size_t len, int max_depth = 512)
          : cur_(buf, len), max_depth_(max_depth) {}

      value parse_root()
      {
        cur_.skip_whitespace();

        if (CNERIUM_UNLIKELY(cur_.done()))
          throw cur_.make_error("empty input");

        value v = parse_value();

        cur_.skip_whitespace();

        if (!cur_.done())
        {
          throw cur_.make_error(
              "unexpected trailing character '" +
              std::string(1, cur_.peek()) + "'");
        }

        return v;
      }

      value parse_value()
      {
        cur_.skip_whitespace();
        if (CNERIUM_UNLIKELY(cur_.done()))
          throw cur_.make_error("unexpected end of input");

        char c = cur_.peek();
        switch (c)
        {
        case 'n':
          return parse_null();
        case 't':
          return parse_true();
        case 'f':
          return parse_false();
        case '"':
          return parse_string_value();
        case '[':
          return parse_array_value();
        case '{':
          return parse_object_value();
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          return parse_number();
        default:
          throw cur_.make_error(std::string("unexpected character '") + c + "'");
        }
      }

      value parse_null()
      {
        if (!cur_.has(4) ||
            cur_.peek(0) != 'n' || cur_.peek(1) != 'u' ||
            cur_.peek(2) != 'l' || cur_.peek(3) != 'l')
          throw cur_.make_error("invalid literal (expected 'null')");
        cur_.advance(4);
        return value{};
      }

      value parse_true()
      {
        if (!cur_.has(4) ||
            cur_.peek(0) != 't' || cur_.peek(1) != 'r' ||
            cur_.peek(2) != 'u' || cur_.peek(3) != 'e')
          throw cur_.make_error("invalid literal (expected 'true')");
        cur_.advance(4);
        return value{true};
      }

      value parse_false()
      {
        if (!cur_.has(5) ||
            cur_.peek(0) != 'f' || cur_.peek(1) != 'a' ||
            cur_.peek(2) != 'l' || cur_.peek(3) != 's' ||
            cur_.peek(4) != 'e')
          throw cur_.make_error("invalid literal (expected 'false')");
        cur_.advance(5);
        return value{false};
      }

      value parse_number()
      {
        const char *start = cur_.buf + cur_.pos;
        std::size_t start_pos = cur_.pos;

        // Walk through the number per RFC 8259 grammar
        bool is_negative = false;
        if (cur_.peek() == '-')
        {
          is_negative = true;
          cur_.advance(1);
        }

        if (CNERIUM_UNLIKELY(cur_.done() ||
                             (cur_.peek() < '0' || cur_.peek() > '9')))
          throw cur_.make_error("invalid number: expected digit");

        bool is_float = false;

        // Integer part
        if (cur_.peek() == '0')
        {
          cur_.advance(1);
        }
        else
        {
          while (!cur_.done() && cur_.peek() >= '0' && cur_.peek() <= '9')
            cur_.advance(1);
        }

        // Fraction
        if (!cur_.done() && cur_.peek() == '.')
        {
          is_float = true;
          cur_.advance(1);
          if (CNERIUM_UNLIKELY(cur_.done() ||
                               cur_.peek() < '0' || cur_.peek() > '9'))
            throw cur_.make_error("invalid number: expected digit after decimal point");
          while (!cur_.done() && cur_.peek() >= '0' && cur_.peek() <= '9')
            cur_.advance(1);
        }

        // Exponent
        if (!cur_.done() && (cur_.peek() == 'e' || cur_.peek() == 'E'))
        {
          is_float = true;
          cur_.advance(1);
          if (!cur_.done() && (cur_.peek() == '+' || cur_.peek() == '-'))
            cur_.advance(1);
          if (CNERIUM_UNLIKELY(cur_.done() ||
                               cur_.peek() < '0' || cur_.peek() > '9'))
            throw cur_.make_error("invalid number: expected digit in exponent");
          while (!cur_.done() && cur_.peek() >= '0' && cur_.peek() <= '9')
            cur_.advance(1);
        }

        const char *end_ptr = cur_.buf + cur_.pos;
        std::size_t num_len = cur_.pos - start_pos;
        (void)num_len; // used below via start / end_ptr

        if (is_float)
        {
          double d{};
          auto [ptr, ec] = std::from_chars(start, end_ptr, d);
          if (CNERIUM_UNLIKELY(ec != std::errc{}))
            throw cur_.make_error("malformed floating-point number");
          return value{d};
        }
        else
        {
          int64_t i{};
          auto [ptr, ec] = std::from_chars(start, end_ptr, i);
          if (ec == std::errc{})
            return value{i};
          // May overflow int64; try as uint64 then fall back to double
          if (is_negative)
          {
            // negative overflow — try double
            double d{};
            auto [p2, ec2] = std::from_chars(start, end_ptr, d);
            if (ec2 == std::errc{})
              return value{d};
          }
          else
          {
            uint64_t u{};
            auto [p2, ec2] = std::from_chars(start, end_ptr, u);
            if (ec2 == std::errc{})
            {
              // Fits in uint64 but not int64; store as double
              return value{static_cast<double>(u)};
            }
          }
          throw cur_.make_error("number out of representable range");
        }
      }

      /**
       * @brief Parses a JSON string and returns it as std::string.
       *
       * Handles all escape sequences including \\uXXXX and
       * UTF-16 surrogate pairs (\\uD800\\uDC00 → single codepoint).
       *
       * Optimization: if the string contains no escapes, appends
       * the raw substring directly (single memcpy path).
       */
      std::string parse_string_raw()
      {
        if (CNERIUM_UNLIKELY(cur_.peek() != '"'))
          throw cur_.make_error("expected '\"'");
        cur_.advance(1); // consume opening quote

        std::string result;
        const char *segment_start = cur_.buf + cur_.pos;
        std::size_t segment_len = 0;

        while (true)
        {
          if (CNERIUM_UNLIKELY(cur_.done()))
            throw cur_.make_error("unterminated string");

          char c = cur_.peek();

          if (CNERIUM_LIKELY(c != '"' && c != '\\' &&
                             (unsigned char)c >= 0x20))
          {
            cur_.advance(1);
            ++segment_len;
            continue;
          }

          // Flush pending segment
          if (segment_len > 0)
          {
            result.append(segment_start, segment_len);
            segment_len = 0;
          }

          if (c == '"')
          {
            cur_.advance(1); // consume closing quote
            break;
          }

          if (CNERIUM_UNLIKELY((unsigned char)c < 0x20))
            throw cur_.make_error("unescaped control character in string");

          // c == '\\'
          cur_.advance(1); // consume backslash
          if (CNERIUM_UNLIKELY(cur_.done()))
            throw cur_.make_error("escape sequence at end of input");

          char esc = cur_.consume();
          switch (esc)
          {
          case '"':
            result.push_back('"');
            break;
          case '\\':
            result.push_back('\\');
            break;
          case '/':
            result.push_back('/');
            break;
          case 'b':
            result.push_back('\b');
            break;
          case 'f':
            result.push_back('\f');
            break;
          case 'n':
            result.push_back('\n');
            break;
          case 'r':
            result.push_back('\r');
            break;
          case 't':
            result.push_back('\t');
            break;
          case 'u':
          {
            if (CNERIUM_UNLIKELY(!cur_.has(4)))
              throw cur_.make_error("incomplete \\uXXXX escape");
            int32_t cp = decode_hex4(cur_.buf + cur_.pos);
            if (CNERIUM_UNLIKELY(cp < 0))
              throw cur_.make_error("invalid hex in \\uXXXX escape");
            cur_.advance(4);

            // Handle UTF-16 surrogate pairs
            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
              // High surrogate; expect low surrogate
              if (!cur_.has(6) ||
                  cur_.peek(0) != '\\' || cur_.peek(1) != 'u')
                throw cur_.make_error("expected low surrogate after high surrogate");
              cur_.advance(2); // consume \\u
              int32_t lo = decode_hex4(cur_.buf + cur_.pos);
              if (lo < 0xDC00 || lo > 0xDFFF)
                throw cur_.make_error("invalid low surrogate");
              cur_.advance(4);
              uint32_t full = 0x10000u +
                              ((static_cast<uint32_t>(cp - 0xD800) << 10) |
                               static_cast<uint32_t>(lo - 0xDC00));
              encode_utf8(result, full);
            }
            else if (cp >= 0xDC00 && cp <= 0xDFFF)
            {
              throw cur_.make_error("unexpected low surrogate");
            }
            else
            {
              encode_utf8(result, static_cast<uint32_t>(cp));
            }
            break;
          }
          default:
            throw cur_.make_error(std::string("invalid escape character '\\") + esc + "'");
          }

          // Reset segment start after escape
          segment_start = cur_.buf + cur_.pos;
          segment_len = 0;
        }

        // Flush remaining segment
        if (segment_len > 0)
          result.append(segment_start, segment_len);

        return result;
      }

      value parse_string_value()
      {
        return value{parse_string_raw()};
      }

      value parse_array_value()
      {
        if (CNERIUM_UNLIKELY(++depth_ > max_depth_))
          throw cur_.make_error("maximum nesting depth exceeded");

        cur_.advance(1); // consume '['
        cur_.skip_whitespace();

        array arr;

        if (!cur_.done() && cur_.peek() == ']')
        {
          cur_.advance(1);
          --depth_;
          return value{std::move(arr)};
        }

        while (true)
        {
          cur_.skip_whitespace();
          arr.items.push_back(parse_value());
          cur_.skip_whitespace();

          if (cur_.done())
            throw cur_.make_error("unterminated array");

          char c = cur_.peek();
          if (c == ']')
          {
            cur_.advance(1);
            break;
          }
          if (CNERIUM_UNLIKELY(c != ','))
            throw cur_.make_error(std::string("expected ',' or ']', got '") + c + "'");
          cur_.advance(1); // consume ','

          // Trailing comma check (strict mode)
          cur_.skip_whitespace();
          if (!cur_.done() && cur_.peek() == ']')
            throw cur_.make_error("trailing comma not allowed in array");
        }

        --depth_;
        return value{std::move(arr)};
      }

      value parse_object_value()
      {
        if (CNERIUM_UNLIKELY(++depth_ > max_depth_))
          throw cur_.make_error("maximum nesting depth exceeded");

        cur_.advance(1); // consume '{'
        cur_.skip_whitespace();

        object obj;

        if (!cur_.done() && cur_.peek() == '}')
        {
          cur_.advance(1);
          --depth_;
          return value{std::move(obj)};
        }

        while (true)
        {
          cur_.skip_whitespace();

          if (CNERIUM_UNLIKELY(cur_.peek() != '"'))
            throw cur_.make_error("expected string key, got '" +
                                  std::string(1, cur_.peek()) + "'");

          std::string key = parse_string_raw();

          cur_.skip_whitespace();

          if (CNERIUM_UNLIKELY(cur_.done() || cur_.peek() != ':'))
            throw cur_.make_error("expected ':' after object key");
          cur_.advance(1); // consume ':'

          cur_.skip_whitespace();
          value val = parse_value();
          obj.members.emplace_back(std::move(key), std::move(val));

          cur_.skip_whitespace();
          if (cur_.done())
            throw cur_.make_error("unterminated object");

          char c = cur_.peek();
          if (c == '}')
          {
            cur_.advance(1);
            break;
          }
          if (CNERIUM_UNLIKELY(c != ','))
            throw cur_.make_error(std::string("expected ',' or '}', got '") + c + "'");
          cur_.advance(1); // consume ','

          // Trailing comma check (strict mode)
          cur_.skip_whitespace();
          if (!cur_.done() && cur_.peek() == '}')
            throw cur_.make_error("trailing comma not allowed in object");
        }

        --depth_;
        return value{std::move(obj)};
      }
    };

  } // namespace detail

  // --- 1. string_view (principal) ---
  [[nodiscard]] inline value parse(std::string_view sv, int max_depth = 512)
  {
    detail::parser p(sv.data(), sv.size(), max_depth);
    return p.parse_root();
  }

  // --- 2. const char* (évite ambiguïté) ---
  [[nodiscard]] inline value parse(const char *cstr, int max_depth = 512)
  {
    return parse(std::string_view{cstr}, max_depth);
  }

  // --- 3. buffer + len ---
  [[nodiscard]] inline value parse(const char *buf, std::size_t len,
                                   int max_depth = 512)
  {
    detail::parser p(buf, len, max_depth);
    return p.parse_root();
  }
  /**
   * @brief Parses from a std::string (via string_view overload).
   * @note Use parse(std::string_view{s}) explicitly to avoid ambiguity.
   */
  // Note: parse(const std::string&) is intentionally omitted to avoid
  // overload ambiguity with parse(std::string_view). Use:
  //   parse(std::string_view{my_string})
  // or the const char* + len overload.

  /**
   * @brief Recursively merges src into dst (RFC 7396 JSON Merge Patch).
   *
   * - If src key exists and is not null, it overwrites dst.
   * - If src key is null, it removes the key from dst.
   * - Non-object src replaces dst entirely.
   *
   * @param dst  Target value (modified in place).
   * @param src  Patch value.
   */
  inline void merge_patch(value &dst, const value &src)
  {
    if (!src.is_object())
    {
      dst = src;
      return;
    }
    if (!dst.is_object())
    {
      dst = object{};
    }
    for (const auto &[k, v] : src.as_object())
    {
      if (v.is_null())
      {
        dst.erase(k);
      }
      else if (dst.contains(k))
      {
        merge_patch(dst[k], v);
      }
      else
      {
        dst[k] = v;
      }
    }
  }

  /**
   * @brief Deep-clones a value.
   *
   * Equivalent to the copy constructor but expressed as a free function
   * for explicit intent at call sites.
   */
  [[nodiscard]] inline value deep_clone(const value &v)
  {
    return v; // copy constructor performs deep copy
  }

  /**
   * @brief Resolves a JSON Pointer path to a value reference.
   *
   * JSON Pointer syntax: "/foo/0/bar"
   *
   * @param root  Root JSON value.
   * @param ptr   JSON Pointer string (must start with '/' or be empty).
   * @return Const reference to the resolved value.
   * @throws access_error if path does not exist.
   * @throws parse_error if pointer syntax is invalid.
   *
   * @code
   *   auto& v = json_pointer(root, "/user/addresses/0/city");
   * @endcode
   */
  [[nodiscard]] inline const value &json_pointer(const value &root,
                                                 std::string_view ptr)
  {
    if (ptr.empty())
      return root;
    if (ptr[0] != '/')
      throw parse_error("JSON Pointer must start with '/' or be empty");

    const value *cur = &root;
    std::size_t i = 1;
    while (i <= ptr.size())
    {
      // Extract token
      std::size_t slash = ptr.find('/', i);
      std::string_view token = (slash == std::string_view::npos)
                                   ? ptr.substr(i)
                                   : ptr.substr(i, slash - i);
      i = (slash == std::string_view::npos) ? ptr.size() + 1 : slash + 1;

      // Unescape ~1 → '/' and ~0 → '~'
      std::string unescaped;
      if (token.find('~') != std::string_view::npos)
      {
        unescaped.reserve(token.size());
        for (std::size_t j = 0; j < token.size(); ++j)
        {
          if (token[j] == '~' && j + 1 < token.size())
          {
            if (token[j + 1] == '1')
            {
              unescaped.push_back('/');
              ++j;
            }
            else if (token[j + 1] == '0')
            {
              unescaped.push_back('~');
              ++j;
            }
            else
            {
              unescaped.push_back('~');
            }
          }
          else
          {
            unescaped.push_back(token[j]);
          }
        }
        token = unescaped;
      }

      if (cur->is_object())
      {
        auto &obj = cur->as_object();
        auto it = obj.find(token);
        if (it == obj.end())
          throw access_error("JSON Pointer: key '" + std::string(token) + "' not found");
        cur = &it->second;
      }
      else if (cur->is_array())
      {
        // Parse index
        std::size_t idx = 0;
        auto [p, ec] = std::from_chars(token.data(), token.data() + token.size(), idx);
        if (ec != std::errc{} || p != token.data() + token.size())
          throw access_error("JSON Pointer: invalid array index '" + std::string(token) + "'");
        if (idx >= cur->as_array().size())
          throw access_error("JSON Pointer: array index out of bounds");
        cur = &cur->as_array()[idx];
      }
      else
      {
        throw access_error("JSON Pointer: cannot index into " + std::string(cur->type_name()));
      }
    }
    return *cur;
  }

  /**
   * @brief Mutable version of json_pointer.
   */
  [[nodiscard]] inline value &json_pointer(value &root, std::string_view ptr)
  {
    return const_cast<value &>(json_pointer(const_cast<const value &>(root), ptr));
  }

  /**
   * @brief Creates an object value from a list of key-value pairs.
   *
   * @code
   *   auto v = make_object({{"a", 1}, {"b", "hello"}});
   * @endcode
   */
  [[nodiscard]] inline value make_object(
      std::initializer_list<std::pair<std::string, value>> pairs)
  {
    object obj;
    obj.members.reserve(pairs.size());
    for (auto &p : pairs)
      obj.members.emplace_back(p.first, p.second);
    return value{std::move(obj)};
  }

  /**
   * @brief Creates an array value from an initializer list.
   *
   * @code
   *   auto v = make_array({1, "two", true, null});
   * @endcode
   */
  [[nodiscard]] inline value make_array(std::initializer_list<value> items)
  {
    array arr;
    arr.items.reserve(items.size());
    for (auto &item : items)
      arr.items.push_back(item);
    return value{std::move(arr)};
  }

  /**
   * @brief Calls the appropriate overload of visitor based on value type.
   *
   * Provides a type-safe visit pattern similar to std::visit on variant.
   *
   * Visitor must be callable with:
   *   - null_t
   *   - bool
   *   - int64_t
   *   - double
   *   - const std::string&
   *   - const array&
   *   - const object&
   *
   * @code
   *   visit(v, [](auto&& x) { std::cout << x; });
   * @endcode
   */
  template <typename Visitor>
  decltype(auto) visit(const value &v, Visitor &&vis)
  {
    switch (v.type())
    {
    case value_type::null_v:
      return std::forward<Visitor>(vis)(null);
    case value_type::bool_v:
      return std::forward<Visitor>(vis)(v.as_bool());
    case value_type::integer_v:
      return std::forward<Visitor>(vis)(v.as_integer());
    case value_type::double_v:
      return std::forward<Visitor>(vis)(v.as_double());
    case value_type::string_v:
      return std::forward<Visitor>(vis)(v.as_string());
    case value_type::array_v:
      return std::forward<Visitor>(vis)(v.as_array());
    case value_type::object_v:
      return std::forward<Visitor>(vis)(v.as_object());
    }
    // unreachable
    return std::forward<Visitor>(vis)(null);
  }

  /**
   * @brief A simple JSON schema validator based on expected-type descriptors.
   *
   * Not a full JSON Schema (draft-07) implementation, but provides
   * enough structure for validating API request/response bodies in
   * production HTTP servers.
   *
   * Supports:
   *   - required keys
   *   - type constraints per key
   *   - nested object schemas
   *   - array element type constraints
   */
  struct schema_node
  {
    enum class kind_t
    {
      any,
      null_t,
      boolean,
      integer,
      number,
      string,
      array,
      object
    };

    kind_t kind{kind_t::any};
    bool required{false};

    // For objects: child schema per key
    std::vector<std::pair<std::string, schema_node>> children;

    // For arrays: element schema
    std::shared_ptr<schema_node> array_item_schema;

    schema_node() = default;

    explicit schema_node(kind_t k, bool req = false)
        : kind(k), required(req) {}

    static schema_node boolean_node(bool req = false)
    {
      return schema_node{kind_t::boolean, req};
    }
    static schema_node integer_node(bool req = false)
    {
      return schema_node{kind_t::integer, req};
    }
    static schema_node number_node(bool req = false)
    {
      return schema_node{kind_t::number, req};
    }
    static schema_node string_node(bool req = false)
    {
      return schema_node{kind_t::string, req};
    }
    static schema_node object_node(bool req = false)
    {
      return schema_node{kind_t::object, req};
    }
    static schema_node array_node(bool req = false)
    {
      return schema_node{kind_t::array, req};
    }
  };

  /**
   * @brief Validates a value against a schema_node tree.
   *
   * @param v       Value to validate.
   * @param schema  Schema node describing expected structure.
   * @param path    Current JSON Pointer path (for error messages).
   * @return empty string on success; human-readable error otherwise.
   */
  [[nodiscard]] inline std::string validate(const value &v,
                                            const schema_node &schema,
                                            std::string path = "")
  {
    using k = schema_node::kind_t;

    auto type_name_for_kind = [](k kk) -> std::string_view
    {
      switch (kk)
      {
      case k::any:
        return "any";
      case k::null_t:
        return "null";
      case k::boolean:
        return "bool";
      case k::integer:
        return "integer";
      case k::number:
        return "number";
      case k::string:
        return "string";
      case k::array:
        return "array";
      case k::object:
        return "object";
      }
      return "?";
    };

    auto fail = [&](std::string msg)
    { return (path.empty() ? "<root>" : path) + ": " + msg; };

    switch (schema.kind)
    {
    case k::any:
      break;
    case k::null_t:
      if (!v.is_null())
        return fail("expected null, got " + std::string(v.type_name()));
      break;
    case k::boolean:
      if (!v.is_bool())
        return fail("expected bool, got " + std::string(v.type_name()));
      break;
    case k::integer:
      if (!v.is_integer())
        return fail("expected integer, got " + std::string(v.type_name()));
      break;
    case k::number:
      if (!v.is_number())
        return fail("expected number, got " + std::string(v.type_name()));
      break;
    case k::string:
      if (!v.is_string())
        return fail("expected string, got " + std::string(v.type_name()));
      break;
    case k::array:
      if (!v.is_array())
        return fail("expected array, got " + std::string(v.type_name()));
      if (schema.array_item_schema)
      {
        std::size_t i = 0;
        for (const auto &elem : v.as_array())
        {
          auto err = validate(elem, *schema.array_item_schema,
                              path + "/" + std::to_string(i));
          if (!err.empty())
            return err;
          ++i;
        }
      }
      break;
    case k::object:
      if (!v.is_object())
        return fail("expected object, got " + std::string(v.type_name()));
      for (const auto &[key, child_schema] : schema.children)
      {
        if (child_schema.required && !v.contains(key))
          return fail("required key '" + key + "' missing");
        if (v.contains(key))
        {
          auto err = validate(v[key], child_schema, path + "/" + key);
          if (!err.empty())
            return err;
        }
      }
      break;
    }
    return {};
  }

  /**
   * @brief Incremental JSON parser for streaming input.
   *
   * Accumulates bytes until a complete top-level JSON value has
   * been received, then returns it. Suitable for HTTP chunked
   * transfer or TCP stream reading.
   *
   * Usage:
   * @code
   *   streaming_parser sp;
   *   while (recv_chunk(buf, len)) {
   *       if (auto v = sp.feed(buf, len)) {
   *           process(*v);
   *           sp.reset();
   *       }
   *   }
   * @endcode
   */
  class streaming_parser
  {
    std::string buffer_;
    int brace_depth_{0};
    int bracket_depth_{0};
    bool in_string_{false};
    bool escape_next_{false};
    bool started_{false};
    char root_char_{'\0'};

  public:
    streaming_parser() { buffer_.reserve(4096); }

    /**
     * @brief Feed bytes into the parser.
     * @return std::optional<value> — set when a complete value is available.
     * @throws parse_error on malformed input.
     */
    std::optional<value> feed(const char *data, std::size_t len)
    {
      buffer_.append(data, len);

      for (std::size_t i = 0; i < len; ++i)
      {
        char c = data[i];
        if (escape_next_)
        {
          escape_next_ = false;
          continue;
        }
        if (in_string_)
        {
          if (c == '\\')
            escape_next_ = true;
          else if (c == '"')
            in_string_ = false;
          continue;
        }
        if (c == '"')
        {
          in_string_ = true;
        }
        if (!started_)
        {
          if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            continue;
          started_ = true;
          root_char_ = c;
        }
        if (root_char_ == '{')
        {
          if (c == '{')
            ++brace_depth_;
          else if (c == '}')
          {
            --brace_depth_;
            if (brace_depth_ == 0)
              return try_complete();
          }
        }
        else if (root_char_ == '[')
        {
          if (c == '[')
            ++bracket_depth_;
          else if (c == ']')
          {
            --bracket_depth_;
            if (bracket_depth_ == 0)
              return try_complete();
          }
        }
        else
        {
          // Scalar root; ended by whitespace or end of data
          // Will be handled at end of buffer
        }
      }

      // For scalar roots, attempt parse when buffer seems complete
      if (started_ && root_char_ != '{' && root_char_ != '[')
      {
        // Try parsing the accumulated buffer
        try
        {
          auto v = parse(buffer_);
          return v;
        }
        catch (...)
        {
          return std::nullopt; // incomplete
        }
      }

      return std::nullopt;
    }

    /// @brief Reset state for next value.
    void reset()
    {
      buffer_.clear();
      brace_depth_ = 0;
      bracket_depth_ = 0;
      in_string_ = false;
      escape_next_ = false;
      started_ = false;
      root_char_ = '\0';
    }

    /// @brief Access raw accumulated buffer.
    [[nodiscard]] std::string_view raw_buffer() const noexcept { return buffer_; }

  private:
    std::optional<value> try_complete()
    {
      try
      {
        auto v = parse(buffer_);
        return v;
      }
      catch (const parse_error &)
      {
        throw; // propagate
      }
    }
  };

  /**
   * @brief Parses NDJSON (one JSON value per line).
   *
   * Returns all successfully parsed values from the input.
   * Lines that fail to parse are collected into errors (if non-null).
   *
   * @code
   *   auto values = parse_ndjson(
   *       R"({"a":1}
   *          {"b":2}
   *          {"c":3})"
   *   );
   * @endcode
   */
  struct ndjson_result
  {
    std::vector<value> values;
    std::vector<std::pair<std::size_t, std::string>> errors; // {line, message}
  };

  [[nodiscard]] inline ndjson_result parse_ndjson(std::string_view input)
  {
    ndjson_result result;
    std::size_t line_no = 1;
    const char *p = input.data();
    const char *end = p + input.size();

    while (p < end)
    {
      const char *line_start = p;
      while (p < end && *p != '\n')
        ++p;
      std::string_view line(line_start, p - line_start);
      if (p < end)
        ++p; // consume '\n'

      // Strip \r if present
      if (!line.empty() && line.back() == '\r')
        line = line.substr(0, line.size() - 1);

      // Skip blank lines and comment lines (// ...)
      if (line.empty() || (line.size() >= 2 && line[0] == '/' && line[1] == '/'))
      {
        ++line_no;
        continue;
      }

      try
      {
        result.values.push_back(parse(line));
      }
      catch (const parse_error &e)
      {
        result.errors.emplace_back(line_no, e.what());
      }
      ++line_no;
    }

    return result;
  }

  /// @brief Alias for parse(). Preferred for API clarity.
  [[nodiscard]] inline value from_string(std::string_view sv)
  {
    return parse(sv);
  }

  /// @brief Alias for v.dump(). Compact JSON string.
  [[nodiscard]] inline std::string to_string(const value &v)
  {
    return v.dump(false);
  }

  /// @brief Alias for v.dump(true). Pretty-printed JSON string.
  [[nodiscard]] inline std::string to_string_pretty(const value &v, int indent = 4)
  {
    return v.dump(true, indent);
  }

  /**
   * @brief Result of a structural JSON diff.
   *
   * Contains a JSON Patch (RFC 6902) compatible list of operations
   * describing how to transform `from` into `to`.
   *
   * Each operation is a JSON object with fields:
   *   - "op":    "add" | "remove" | "replace"
   *   - "path":  JSON Pointer string
   *   - "value": new value (for add/replace)
   */
  using json_patch = std::vector<value>;

  namespace detail
  {

    inline void diff_impl(const value &from, const value &to,
                          const std::string &path, json_patch &patch)
    {
      if (from.type() != to.type())
      {
        patch.push_back(object{
            {"op", "replace"},
            {"path", path},
            {"value", to}});
        return;
      }

      if (from.is_object() && to.is_object())
      {
        const auto &fo = from.as_object();
        const auto &to_obj = to.as_object();

        // Removals
        for (const auto &[k, v] : fo)
        {
          if (!to_obj.contains(k))
          {
            patch.push_back(object{
                {"op", "remove"},
                {"path", path + "/" + k}});
          }
        }
        // Additions and replacements
        for (const auto &[k, v] : to_obj)
        {
          std::string child_path = path + "/" + k;
          if (!fo.contains(k))
          {
            patch.push_back(object{
                {"op", "add"},
                {"path", child_path},
                {"value", v}});
          }
          else
          {
            diff_impl(fo[k], v, child_path, patch);
          }
        }
        return;
      }

      if (from.is_array() && to.is_array())
      {
        const auto &fa = from.as_array();
        const auto &ta = to.as_array();
        std::size_t common = std::min(fa.size(), ta.size());
        for (std::size_t i = 0; i < common; ++i)
        {
          diff_impl(fa[i], ta[i], path + "/" + std::to_string(i), patch);
        }
        // Additions
        for (std::size_t i = common; i < ta.size(); ++i)
        {
          patch.push_back(object{
              {"op", "add"},
              {"path", path + "/" + std::to_string(i)},
              {"value", ta[i]}});
        }
        // Removals (in reverse order to maintain indices)
        for (std::size_t i = fa.size(); i > ta.size(); --i)
        {
          patch.push_back(object{
              {"op", "remove"},
              {"path", path + "/" + std::to_string(i - 1)}});
        }
        return;
      }

      // Scalar comparison
      if (from != to)
      {
        patch.push_back(object{
            {"op", "replace"},
            {"path", path},
            {"value", to}});
      }
    }

  } // namespace detail

  /**
   * @brief Computes a JSON Patch from `from` to `to`.
   *
   * @return A json_patch (vector of operation objects).
   */
  [[nodiscard]] inline json_patch diff(const value &from, const value &to)
  {
    json_patch patch;
    detail::diff_impl(from, to, "", patch);
    return patch;
  }

  /**
   * @brief Applies a JSON Patch to a value (RFC 6902, subset).
   *
   * Supported operations: add, remove, replace.
   *
   * @param target  Value to patch (modified in place).
   * @param patch   List of patch operations.
   * @throws access_error on invalid paths.
   * @throws parse_error on malformed patch operations.
   */
  inline void apply_patch(value &target, const json_patch &patch)
  {
    for (const auto &op_val : patch)
    {
      if (!op_val.is_object())
        throw parse_error("patch operation must be an object");
      const std::string &op = op_val["op"].as_string();
      const std::string &path = op_val["path"].as_string();

      // Navigate to parent
      std::string_view sv(path);
      std::size_t last_slash = sv.rfind('/');
      if (last_slash == std::string_view::npos)
      {
        // Top-level operation
        if (op == "add" || op == "replace")
        {
          target = op_val["value"];
        }
        else if (op == "remove")
        {
          target = value{};
        }
        continue;
      }

      std::string parent_path(sv.substr(0, last_slash));
      std::string key(sv.substr(last_slash + 1));
      value &parent = parent_path.empty()
                          ? target
                          : json_pointer(target, parent_path);

      if (op == "add" || op == "replace")
      {
        const value &new_val = op_val["value"];
        if (parent.is_object())
        {
          parent[key] = new_val;
        }
        else if (parent.is_array())
        {
          std::size_t idx{};
          auto [p, ec] = std::from_chars(key.data(), key.data() + key.size(), idx);
          if (ec != std::errc{})
            throw access_error("invalid array index: " + key);
          if (idx > parent.size())
            throw access_error("array index out of bounds: " + key);
          if (idx == parent.size())
            parent.push_back(new_val);
          else
            parent[idx] = new_val;
        }
      }
      else if (op == "remove")
      {
        if (parent.is_object())
        {
          parent.erase(key);
        }
        else if (parent.is_array())
        {
          std::size_t idx{};
          auto [p, ec] = std::from_chars(key.data(), key.data() + key.size(), idx);
          if (ec != std::errc{})
            throw access_error("invalid array index: " + key);
          auto &arr = parent.as_array();
          if (idx >= arr.size())
            throw access_error("array index out of bounds");
          arr.items.erase(arr.items.begin() + static_cast<std::ptrdiff_t>(idx));
        }
      }
    }
  }

  /**
   * @brief Extracts the JSON body from an HTTP-like response string.
   *
   * Assumes the body starts after a blank line ("\r\n\r\n" or "\n\n").
   * Returns an empty optional if the separator is not found or body is empty.
   *
   * This is a zero-copy helper: returns string_view into the input buffer.
   */
  [[nodiscard]] inline std::optional<std::string_view>
  extract_http_body(std::string_view raw_response) noexcept
  {
    // Try \r\n\r\n first
    auto pos = raw_response.find("\r\n\r\n");
    if (pos != std::string_view::npos)
    {
      auto body = raw_response.substr(pos + 4);
      if (body.empty())
        return std::nullopt;
      return body;
    }
    // Try \n\n
    pos = raw_response.find("\n\n");
    if (pos != std::string_view::npos)
    {
      auto body = raw_response.substr(pos + 2);
      if (body.empty())
        return std::nullopt;
      return body;
    }
    return std::nullopt;
  }

  /**
   * @brief Parses JSON from an HTTP-like raw response.
   *
   * Convenience wrapper around extract_http_body + parse.
   */
  [[nodiscard]] inline std::optional<value>
  parse_http_response_body(std::string_view raw_response)
  {
    auto body = extract_http_body(raw_response);
    if (!body)
      return std::nullopt;
    try
    {
      return parse(*body);
    }
    catch (...)
    {
      return std::nullopt;
    }
  }

  /**
   * @brief Concept: a type that can be represented as a JSON value.
   */
  template <typename T>
  concept json_convertible =
      std::is_same_v<T, null_t> ||
      std::is_same_v<T, bool> ||
      std::is_integral_v<T> ||
      std::is_floating_point_v<T> ||
      std::is_same_v<T, std::string> ||
      std::is_same_v<T, std::string_view> ||
      std::is_same_v<T, array> ||
      std::is_same_v<T, object> ||
      std::is_same_v<T, value>;

  /**
   * @brief Converts a std::vector<T> to a JSON array (T must be json-convertible).
   */
  template <typename T>
    requires json_convertible<T>
  [[nodiscard]] inline value from_vector(const std::vector<T> &vec)
  {
    array arr;
    arr.items.reserve(vec.size());
    for (const auto &item : vec)
      arr.items.emplace_back(item);
    return value{std::move(arr)};
  }

  /**
   * @brief Converts a std::map<string, T> to a JSON object.
   */
  template <typename T>
    requires json_convertible<T>
  [[nodiscard]] inline value from_map(const std::map<std::string, T> &m)
  {
    object obj;
    obj.members.reserve(m.size());
    for (const auto &[k, v] : m)
      obj.members.emplace_back(k, value{v});
    return value{std::move(obj)};
  }

  /**
   * @brief Converts a JSON array to std::vector<T>.
   * @throws type_error on type mismatch.
   */
  template <typename T>
  [[nodiscard]] inline std::vector<T> to_vector(const value &v)
  {
    const auto &arr = v.as_array();
    std::vector<T> result;
    result.reserve(arr.size());
    for (const auto &item : arr)
    {
      if constexpr (std::is_same_v<T, std::string>)
        result.push_back(item.as_string());
      else if constexpr (std::is_same_v<T, int64_t>)
        result.push_back(item.as_integer());
      else if constexpr (std::is_same_v<T, double>)
        result.push_back(item.as_number());
      else if constexpr (std::is_same_v<T, bool>)
        result.push_back(item.as_bool());
      else
        result.push_back(static_cast<T>(item.as_number()));
    }
    return result;
  }

  namespace detail
  {

    /**
     * @brief Formats a double without trailing zeros and with minimum digits.
     *
     * Uses std::to_chars for locale-independence. Falls back gracefully.
     */
    [[nodiscard]] inline std::string format_double(double v)
    {
      char buf[32];
      auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v,
                                     std::chars_format::general, 17);
      if (ec != std::errc{})
        return std::to_string(v);
      return std::string(buf, ptr);
    }

    /**
     * @brief Parses a double from a string_view without locale interference.
     * @return std::optional<double> — nullopt on failure.
     */
    [[nodiscard]] inline std::optional<double> parse_double(std::string_view sv) noexcept
    {
      double v{};
      auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
      if (ec != std::errc{} || ptr != sv.data() + sv.size())
        return std::nullopt;
      return v;
    }

    /**
     * @brief Parses an int64 from a string_view.
     */
    [[nodiscard]] inline std::optional<int64_t> parse_int64(std::string_view sv) noexcept
    {
      int64_t v{};
      auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), v);
      if (ec != std::errc{} || ptr != sv.data() + sv.size())
        return std::nullopt;
      return v;
    }

  } // namespace detail

  /**
   * @brief Deep equality comparison of two JSON values.
   *
   * Alias for value::operator==, useful in template contexts.
   */
  [[nodiscard]] inline bool deep_equal(const value &a, const value &b) noexcept
  {
    return a == b;
  }

  /**
   * @brief Structural similarity: returns number of matching paths.
   *
   * Counts how many leaf values are equal at the same JSON Pointer paths.
   * Useful for fuzzy-matching or patch planning.
   */
  [[nodiscard]] inline std::size_t similarity(const value &a, const value &b) noexcept
  {
    std::size_t matches = 0;

    std::function<void(const value &, const value &)> walk;
    walk = [&](const value &x, const value &y)
    {
      if (x.type() != y.type())
        return;
      if (x.is_object() && y.is_object())
      {
        for (const auto &[k, v] : x.as_object())
        {
          if (y.contains(k))
            walk(v, y[k]);
        }
      }
      else if (x.is_array() && y.is_array())
      {
        std::size_t n = std::min(x.as_array().size(), y.as_array().size());
        for (std::size_t i = 0; i < n; ++i)
          walk(x[i], y[i]);
      }
      else
      {
        if (x == y)
          ++matches;
      }
    };

    walk(a, b);
    return matches;
  }
} // namespace cnerium::json
