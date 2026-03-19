#include <cnerium/json/json.hpp>

#include <cassert>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ============================================================
// Mini-framework de test
// ============================================================

namespace
{
  int g_pass = 0;
  int g_fail = 0;

  void suite(const char *name)
  {
    std::cout << "\n  [" << name << "]\n";
  }

  void check(bool ok, const char *expr, int line)
  {
    if (ok)
    {
      ++g_pass;
      std::cout << "    OK   " << expr << '\n';
    }
    else
    {
      ++g_fail;
      std::cerr << "    FAIL " << expr << "  (ligne " << line << ")\n";
    }
  }

#define OK(expr) check((expr), #expr, __LINE__)

#define THROWS_PARSE(expr)                           \
  do                                                 \
  {                                                  \
    bool _ok = false;                                \
    try                                              \
    {                                                \
      (void)(expr);                                  \
    }                                                \
    catch (const cnerium::json::parse_error &)       \
    {                                                \
      _ok = true;                                    \
    }                                                \
    catch (...)                                      \
    {                                                \
    }                                                \
    check(_ok, "THROWS_PARSE(" #expr ")", __LINE__); \
  } while (false)

#define THROWS_TYPE(expr)                           \
  do                                                \
  {                                                 \
    bool _ok = false;                               \
    try                                             \
    {                                               \
      (void)(expr);                                 \
    }                                               \
    catch (const cnerium::json::type_error &)       \
    {                                               \
      _ok = true;                                   \
    }                                               \
    catch (...)                                     \
    {                                               \
    }                                               \
    check(_ok, "THROWS_TYPE(" #expr ")", __LINE__); \
  } while (false)

#define THROWS_ACCESS(expr)                           \
  do                                                  \
  {                                                   \
    bool _ok = false;                                 \
    try                                               \
    {                                                 \
      (void)(expr);                                   \
    }                                                 \
    catch (const cnerium::json::access_error &)       \
    {                                                 \
      _ok = true;                                     \
    }                                                 \
    catch (...)                                       \
    {                                                 \
    }                                                 \
    check(_ok, "THROWS_ACCESS(" #expr ")", __LINE__); \
  } while (false)

#define THROWS_ANY(expr)                           \
  do                                               \
  {                                                \
    bool _ok = false;                              \
    try                                            \
    {                                              \
      (void)(expr);                                \
    }                                              \
    catch (...)                                    \
    {                                              \
      _ok = true;                                  \
    }                                              \
    check(_ok, "THROWS_ANY(" #expr ")", __LINE__); \
  } while (false)

} // anonymous namespace

using namespace cnerium::json;

// ============================================================
// 1. Construction & requêtes de type
// ============================================================

void test_construction()
{
  suite("construction & type queries");

  // null
  value vn;
  OK(vn.is_null());
  value vnull(null);
  OK(vnull.is_null());

  // bool
  value vbt(true);
  OK(vbt.is_bool());
  OK(vbt.as_bool() == true);
  value vbf(false);
  OK(vbf.as_bool() == false);

  // integer (plusieurs types intégraux)
  value vi_int(42);
  OK(vi_int.is_integer());
  OK(vi_int.as_integer() == 42);
  value vi_long(int64_t{-7});
  OK(vi_long.as_integer() == -7);
  value vi_uint(uint32_t{5});
  OK(vi_uint.as_integer() == 5);

  // double
  value vd(3.14);
  OK(vd.is_double());
  OK(vd.as_double() == 3.14);
  value vf(2.0f);
  OK(vf.is_double());

  // is_number() = integer OU double
  OK(vi_int.is_number());
  OK(vd.is_number());
  OK(!vbt.is_number());

  // string
  value vs("hello");
  OK(vs.is_string());
  OK(vs.as_string() == "hello");
  value vsv(std::string_view("sv"));
  OK(vsv.as_string() == "sv");
  value vss(std::string{"std"});
  OK(vss.as_string() == "std");
  value vsc((const char *)"cstr");
  OK(vsc.as_string() == "cstr");

  // array
  value va = array{1, 2, 3};
  OK(va.is_array());
  OK(va.size() == 3);

  // object
  value vo = object{{"a", 1}, {"b", 2}};
  OK(vo.is_object());
  OK(vo.size() == 2);

  // copy / move
  value cp = vo;
  OK(cp == vo);
  value mv = std::move(cp);
  OK(mv == vo);

  // type_name()
  OK(value{}.type_name() == "null");
  OK(value{true}.type_name() == "bool");
  OK(value{42}.type_name() == "integer");
  OK(value{1.5}.type_name() == "double");
  OK(value{"x"}.type_name() == "string");
  OK(value{array{}}.type_name() == "array");
  OK(value{object{}}.type_name() == "object");

  // value_type enum
  OK(value{}.type() == value_type::null_v);
  OK(value{true}.type() == value_type::bool_v);
  OK(value{42}.type() == value_type::integer_v);
  OK(value{1.5}.type() == value_type::double_v);
  OK(value{"x"}.type() == value_type::string_v);
  OK(value{array{}}.type() == value_type::array_v);
  OK(value{object{}}.type() == value_type::object_v);
}

// ============================================================
// 2. Parse — scalaires
// ============================================================

void test_parse_scalars()
{
  suite("parse scalars");

  OK(parse("null").is_null());
  OK(parse("true").as_bool() == true);
  OK(parse("false").as_bool() == false);
  OK(parse("0").as_integer() == 0);
  OK(parse("42").as_integer() == 42);
  OK(parse("-7").as_integer() == -7);
  OK(parse("3.14").as_double() == 3.14);
  OK(parse("0.0").is_double());
  OK(parse("\"\"").as_string() == "");
  OK(parse("\"hello\"").as_string() == "hello");

  // as_number() : integer → double sans erreur
  OK(parse("42").as_number() == 42.0);
  OK(parse("1.5").as_number() == 1.5);

  // as_double() strict : lève type_error sur integer
  THROWS_TYPE(parse("42").as_double());
}

// ============================================================
// 3. Parse — strings et escapes Unicode
// ============================================================

void test_parse_strings()
{
  suite("string escapes & unicode");

  OK(parse("\"\\n\"").as_string() == "\n");
  OK(parse("\"\\t\"").as_string() == "\t");
  OK(parse("\"\\r\"").as_string() == "\r");
  OK(parse("\"\\b\"").as_string() == "\b");
  OK(parse("\"\\f\"").as_string() == "\f");
  OK(parse("\"\\\\\"").as_string() == "\\");
  OK(parse("\"\\\"\"").as_string() == "\"");
  OK(parse("\"\\/\"").as_string() == "/");

  // \uXXXX ASCII
  OK(parse("\"\\u0041\"").as_string() == "A");
  OK(parse("\"\\u0020\"").as_string() == " ");

  // \u00e9 = é (U+00E9, 2 octets UTF-8)
  OK(!parse("\"\\u00e9\"").as_string().empty());

  // Paire de substitution : U+1F600 = \uD83D\uDE00 (4 octets UTF-8)
  auto emoji = parse("\"\\uD83D\\uDE00\"");
  OK(emoji.is_string() && emoji.as_string().size() == 4);
}

// ============================================================
// 4. Parse — tableaux
// ============================================================

void test_parse_arrays()
{
  suite("parse arrays");

  auto empty = parse("[]");
  OK(empty.is_array() && empty.empty());

  auto arr = parse("[1,2,3]");
  OK(arr.is_array() && arr.size() == 3);
  OK(arr[size_t{0}].as_integer() == 1);
  OK(arr[size_t{2}].as_integer() == 3);

  // Types mixtes
  auto mixed = parse("[null,true,42,3.14,\"str\",[],{}]");
  OK(mixed.size() == 7);
  OK(mixed[size_t{0}].is_null());
  OK(mixed[size_t{1}].as_bool() == true);
  OK(mixed[size_t{2}].as_integer() == 42);
  OK(mixed[size_t{3}].as_double() == 3.14);
  OK(mixed[size_t{4}].as_string() == "str");
  OK(mixed[size_t{5}].is_array());
  OK(mixed[size_t{6}].is_object());

  // Imbriqué
  auto nested = parse("[[1,2],[3,4]]");
  OK(nested[size_t{0}][size_t{0}].as_integer() == 1);
  OK(nested[size_t{1}][size_t{1}].as_integer() == 4);
}

// ============================================================
// 5. Parse — objets
// ============================================================

void test_parse_objects()
{
  suite("parse objects");

  auto empty = parse("{}");
  OK(empty.is_object() && empty.empty());

  auto obj = parse("{\"name\":\"Alice\",\"age\":30}");
  OK(obj["name"].as_string() == "Alice");
  OK(obj["age"].as_integer() == 30);

  // Accès const
  const value &cobj = obj;
  OK(cobj["name"].as_string() == "Alice");
}

// ============================================================
// 6. Parse — imbriqué profond
// ============================================================

void test_parse_nested()
{
  suite("parse nested");

  const char *json = R"({
        "user": {
            "name":   "Gaspard",
            "age":    25,
            "skills": ["C++", "Systems", "JSON"],
            "meta": {
                "active": true,
                "score":  99.5,
                "tags":   [{"id":1,"label":"core"},{"id":2,"label":"perf"}]
            }
        }
    })";

  auto v = parse(json);
  OK(v["user"]["name"].as_string() == "Gaspard");
  OK(v["user"]["age"].as_integer() == 25);
  OK(v["user"]["skills"][size_t{0}].as_string() == "C++");
  OK(v["user"]["skills"][size_t{2}].as_string() == "JSON");
  OK(v["user"]["meta"]["active"].as_bool() == true);
  OK(v["user"]["meta"]["score"].as_double() == 99.5);
  OK(v["user"]["meta"]["tags"][size_t{0}]["label"].as_string() == "core");
  OK(v["user"]["meta"]["tags"][size_t{1}]["id"].as_integer() == 2);
}

// ============================================================
// 7. Sérialisation — compact, pretty, dump_to
// ============================================================

void test_dump()
{
  suite("serialization");

  value v = object{
      {"name", "Bob"},
      {"scores", array{10, 20, 30}}};

  // Roundtrip compact
  auto s = v.dump();
  auto v2 = parse(s);
  OK(v2["name"].as_string() == "Bob");
  OK(v2["scores"][size_t{1}].as_integer() == 20);

  // Pretty : contient des sauts de ligne
  auto pretty = v.dump(true, 2);
  OK(pretty.find('\n') != std::string::npos);
  OK(parse(pretty)["name"].as_string() == "Bob");

  // dump_to()
  std::string out;
  v.dump_to(out, false);
  OK(parse(out)["name"].as_string() == "Bob");

  // dump_to() pretty
  std::string out2;
  v.dump_to(out2, true, 4);
  OK(out2.find('\n') != std::string::npos);

  // Valeurs spéciales
  OK(value{true}.dump() == "true");
  OK(value{false}.dump() == "false");
  OK(value{}.dump() == "null");
  OK(value{42}.dump() == "42");

  // NaN → "null"
  OK(value{std::numeric_limits<double>::quiet_NaN()}.dump() == "null");

  // double 2.0 : doit avoir un point ou exposant
  std::string d2s = value{2.0}.dump();
  OK(d2s.find('.') != std::string::npos || d2s.find('e') != std::string::npos);

  // operator<<
  std::ostringstream oss;
  oss << v;
  OK(parse(oss.str())["name"].as_string() == "Bob");
}

// ============================================================
// 8. Roundtrip stress
// ============================================================

void test_roundtrip()
{
  suite("roundtrip parse → dump → parse");

  const char *cases[] = {
      "null",
      "true",
      "false",
      "0",
      "1",
      "-1",
      "42",
      "\"\"",
      "\"hello\"",
      "[]",
      "[1,2,3]",
      "{}",
      "{\"a\":1}",
      "{\"a\":{\"b\":{\"c\":null}}}",
      "[{\"x\":1},{\"x\":2}]",
  };
  for (auto json : cases)
  {
    auto v1 = parse(json);
    auto v2 = parse(v1.dump());
    OK(v1 == v2);
  }
}

// ============================================================
// 9. Mutation — insertion, suppression, modification
// ============================================================

void test_mutation()
{
  suite("mutation");

  value v = object{{"a", 1}, {"b", 2}};

  // Écraser une clé existante
  v["a"] = value{99};
  OK(v["a"].as_integer() == 99);

  // Insérer une nouvelle clé via operator[]
  v["c"] = value{"new"};
  OK(v["c"].as_string() == "new");
  OK(v.size() == 3);

  // object::set()
  v.as_object().set("a", value{-1});
  OK(v["a"].as_integer() == -1);

  // erase via value::erase
  OK(v.erase("b") == true);
  OK(!v.contains("b"));
  OK(v.size() == 2);
  OK(v.erase("ghost") == false);

  // push_back
  value arr = array{1, 2, 3};
  arr.push_back(value{4});
  OK(arr.size() == 4);
  OK(arr[size_t{3}].as_integer() == 4);

  // emplace_back
  arr.emplace_back(value{5});
  OK(arr.size() == 5);

  // clear array
  arr.clear();
  OK(arr.empty());

  // clear object
  value obj2 = object{{"x", 1}};
  obj2.clear();
  OK(obj2.empty());
}

// ============================================================
// 10. Accès chaîné operator[]
// ============================================================

void test_chained_access()
{
  suite("chained operator[]");

  auto v = parse(R"({"a":{"b":{"c":[10,20,{"d":42}]}}})");

  OK(v["a"]["b"]["c"][size_t{0}].as_integer() == 10);
  OK(v["a"]["b"]["c"][size_t{1}].as_integer() == 20);
  OK(v["a"]["b"]["c"][size_t{2}]["d"].as_integer() == 42);

  const value &cv = v;
  OK(cv["a"]["b"]["c"][size_t{2}]["d"].as_integer() == 42);
}

// ============================================================
// 11. try_get<T>
// ============================================================

void test_try_get()
{
  suite("try_get<T>");

  value vi(int64_t{100});
  OK(vi.try_get<int64_t>().value_or(0) == 100);
  OK(!vi.try_get<std::string>().has_value());
  OK(!vi.try_get<bool>().has_value());
  // integer → coercé en double
  OK(vi.try_get<double>().value_or(0.0) == 100.0);

  value vs("hello");
  OK(vs.try_get<std::string>().value_or("") == "hello");
  OK(!vs.try_get<double>().has_value());

  value vd(2.5);
  OK(vd.try_get<double>().value_or(0.0) == 2.5);

  value vb(true);
  OK(vb.try_get<bool>().value_or(false) == true);

  value va = array{1, 2};
  OK(va.try_get<array>().has_value());
  OK(va.try_get<object>() == std::nullopt);

  value vo = object{{"k", 1}};
  OK(vo.try_get<object>().has_value());
  OK(vo.try_get<array>() == std::nullopt);
}

// ============================================================
// 12. API object : contains / find / set / erase
// ============================================================

void test_object_api()
{
  suite("object API");

  value v = object{{"x", 1}, {"y", 2}};
  OK(v.contains("x"));
  OK(!v.contains("z"));

  auto &obj = v.as_object();

  // find
  OK(obj.find("x") != obj.end());
  OK(obj.find("z") == obj.end());
  OK(obj.find("x")->second.as_integer() == 1);

  // set : overwrite
  obj.set("x", value{99});
  OK(obj.find("x")->second.as_integer() == 99);

  // set : insert
  obj.set("new_key", value{"added"});
  OK(obj.contains("new_key"));
  OK(obj.size() == 3);

  // object::erase
  OK(obj.erase("y") == true);
  OK(obj.erase("ghost") == false);
  OK(obj.size() == 2);
}

// ============================================================
// 13. Gestion des erreurs (parse_error / type_error / access_error)
// ============================================================

void test_errors()
{
  suite("parse errors");

  THROWS_PARSE(parse(""));
  THROWS_PARSE(parse("{"));
  THROWS_PARSE(parse("["));
  THROWS_PARSE(parse("]"));
  THROWS_PARSE(parse("{\"a\"}"));
  THROWS_PARSE(parse("{\"a\":}"));
  THROWS_PARSE(parse("[1,2,]"));
  THROWS_PARSE(parse("{\"a\":1,}"));
  THROWS_PARSE(parse("\"unterminated"));
  THROWS_PARSE(parse("truee"));
  THROWS_PARSE(parse("nulll"));
  THROWS_PARSE(parse("1 2"));
  THROWS_PARSE(parse("\"\\uXXXX\""));
  THROWS_PARSE(parse("\"\\q\""));

  // parse_error porte offset / line / column
  try
  {
    parse("{bad");
  }
  catch (const parse_error &e)
  {
    OK(e.offset > 0 || e.line >= 1);
    OK(e.column >= 1);
  }

  suite("type errors");

  value vi(42);
  THROWS_TYPE(vi.as_string());
  THROWS_TYPE(vi.as_bool());
  THROWS_TYPE(vi.as_array());
  THROWS_TYPE(vi.as_object());
  THROWS_TYPE(vi.as_double()); // integer strict ≠ double
  THROWS_TYPE(vi.push_back(value{}));
  THROWS_TYPE(vi.contains("k"));
  THROWS_TYPE(vi.erase("k"));
  THROWS_TYPE(vi.size()); // entier scalaire

  suite("access errors");

  // Clé absente : object const
  const value cv = object{{"a", 1}};
  THROWS_ACCESS(cv["missing"]);

  // Index hors-borne
  value arr = array{1, 2};
  THROWS_ACCESS(arr[size_t{99}]);
  const value &carr = arr;
  THROWS_ACCESS(carr[size_t{99}]);

  // operator[string] sur non-objet
  THROWS_TYPE(arr["key"]);
  // operator[size_t] sur non-tableau
  THROWS_TYPE(cv[size_t{0}]);
}

// ============================================================
// 14. Égalité profonde
// ============================================================

void test_equality()
{
  suite("deep equality");

  value a = object{{"x", 1}, {"y", array{1, 2, 3}}};
  value b = object{{"x", 1}, {"y", array{1, 2, 3}}};
  value c = object{{"x", 2}, {"y", array{1, 2, 3}}};

  OK(a == b);
  OK(a != c);

  OK(value{} == value{null});
  OK(value{true} != value{false});
  OK(value{"hi"} == value{std::string("hi")});

  // Cross-type : integer == double si même valeur numérique
  OK(value{int64_t{1}} == value{1.0});
  OK(value{int64_t{2}} != value{3.0});

  // deep_equal() free function
  OK(deep_equal(a, b));
  OK(!deep_equal(a, c));
}

// ============================================================
// 15. Itération range-for
// ============================================================

void test_iteration()
{
  suite("range-for iteration");

  // Objet (members)
  value obj = object{{"a", 1}, {"b", 2}, {"c", 3}};
  std::size_t cnt = 0;
  int64_t sum = 0;
  for (const auto &[k, v] : obj.as_object())
  {
    ++cnt;
    OK(!k.empty());
    sum += v.as_integer();
  }
  OK(cnt == 3);
  OK(sum == 6);

  // Tableau (items)
  value arr = array{10, 20, 30};
  int64_t asum = 0;
  for (const auto &v : arr.as_array())
    asum += v.as_integer();
  OK(asum == 60);

  // front / back sur array struct
  OK(arr.as_array().front().as_integer() == 10);
  OK(arr.as_array().back().as_integer() == 30);
}

// ============================================================
// 16. json_pointer (RFC 6901 — free function)
// ============================================================

void test_json_pointer_fn()
{
  suite("json_pointer (RFC 6901)");

  auto v = parse(R"({"a":{"b":{"c":42}},"arr":[10,20,30]})");

  OK(json_pointer(v, "/a/b/c").as_integer() == 42);
  OK(json_pointer(v, "/arr/1").as_integer() == 20);
  OK(json_pointer(v, "/arr/0").as_integer() == 10);

  // Racine vide
  OK(json_pointer(v, "").is_object());

  // Chemin absent → access_error
  THROWS_ACCESS(json_pointer(v, "/a/b/d"));
  THROWS_ACCESS(json_pointer(v, "/arr/99"));

  // Chemin invalide (pas de '/')
  THROWS_PARSE(json_pointer(v, "no_slash"));

  // Version mutable
  value v2 = parse(R"({"x":{"y":1}})");
  json_pointer(v2, "/x/y") = value{999};
  OK(v2["x"]["y"].as_integer() == 999);

  // ~0 = '~', ~1 = '/'
  auto v3 = parse(R"({"~0":true,"/slash":99})");
  OK(json_pointer(v3, "/~00").as_bool() == true);
  OK(json_pointer(v3, "/~1slash").as_integer() == 99);
}

// ============================================================
// 17. merge_patch (RFC 7396)
// ============================================================

void test_merge_patch()
{
  suite("merge_patch (RFC 7396)");

  value dst = parse(R"({"a":1,"b":2,"c":3})");
  value patch = parse(R"({"b":99,"c":null,"d":4})");
  merge_patch(dst, patch);

  OK(dst["a"].as_integer() == 1);  // inchangé
  OK(dst["b"].as_integer() == 99); // remplacé
  OK(!dst.contains("c"));          // supprimé (null dans patch)
  OK(dst["d"].as_integer() == 4);  // ajouté

  // Patch scalaire remplace tout
  value dst2 = parse(R"({"x":1})");
  merge_patch(dst2, value{42});
  OK(dst2.as_integer() == 42);

  // Patch sur valeur nulle crée un objet
  value dst3 = value{};
  merge_patch(dst3, parse(R"({"k":"v"})"));
  OK(dst3["k"].as_string() == "v");
}

// ============================================================
// 18. diff & apply_patch (RFC 6902)
// ============================================================

void test_diff_apply()
{
  suite("diff & apply_patch (RFC 6902)");

  value from = parse(R"({"a":1,"b":2,"c":3})");
  value to = parse(R"({"a":1,"b":99,"d":4})");

  auto ops = diff(from, to);
  OK(!ops.empty());
  for (const auto &op : ops)
  {
    OK(op.is_object());
    OK(op.contains("op"));
    OK(op.contains("path"));
  }

  // Appliquer le patch → doit donner `to`
  value target = from;
  apply_patch(target, ops);
  OK(target == to);

  // Tableau
  value fa = parse("[1,2,3]");
  value ta = parse("[1,2,3,4]");
  auto p2 = diff(fa, ta);
  value fa2 = fa;
  apply_patch(fa2, p2);
  OK(fa2 == ta);

  // Valeurs identiques → patch vide
  auto p3 = diff(from, from);
  OK(p3.empty());
}

// ============================================================
// 19. streaming_parser
// ============================================================

void test_streaming_parser()
{
  suite("streaming_parser");

  streaming_parser sp;

  // Fragmentation : objet
  auto r1 = sp.feed("{\"a\":", 5);
  OK(!r1.has_value());
  auto r2 = sp.feed("42}", 3);
  OK(r2.has_value());
  OK((*r2)["a"].as_integer() == 42);

  // Reset et réutilisation
  sp.reset();
  auto r3 = sp.feed("[1,2,3]", 7);
  OK(r3.has_value() && r3->size() == 3);

  // raw_buffer accessible
  sp.reset();
  sp.feed("{\"x\":1}", 7);
  OK(!sp.raw_buffer().empty());
}

// ============================================================
// 20. parse_ndjson → ndjson_result
// ============================================================

void test_ndjson()
{
  suite("parse_ndjson (ndjson_result)");

  const char *nd = "{\"id\":1}\n{\"id\":2}\n{\"id\":3}\n";
  auto res = parse_ndjson(nd);

  OK(res.values.size() == 3);
  OK(res.errors.empty());
  OK(res.values[0]["id"].as_integer() == 1);
  OK(res.values[2]["id"].as_integer() == 3);

  // Lignes vides ignorées
  auto res2 = parse_ndjson("\n{\"x\":9}\n\n");
  OK(res2.values.size() == 1);
  OK(res2.values[0]["x"].as_integer() == 9);

  // Ligne invalide → errors[], pas d'exception
  auto res3 = parse_ndjson("{\"ok\":true}\nBAD_JSON\n{\"z\":7}\n");
  OK(res3.values.size() == 2);
  OK(!res3.errors.empty());
  OK(res3.errors[0].first == 2); // ligne 2

  // Commentaires // ignorés
  auto res4 = parse_ndjson("// commentaire\n{\"v\":5}\n");
  OK(res4.values.size() == 1);
  OK(res4.values[0]["v"].as_integer() == 5);
}

// ============================================================
// 21. visit
// ============================================================

void test_visit()
{
  suite("visit");

  auto type_of = [](const value &v) -> std::string
  {
    return visit(v, [](auto &&x) -> std::string
                 {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, null_t>)           return "null";
            else if constexpr (std::is_same_v<T, bool>)         return "bool";
            else if constexpr (std::is_same_v<T, int64_t>)      return "integer";
            else if constexpr (std::is_same_v<T, double>)       return "double";
            else if constexpr (std::is_same_v<T, std::string>)  return "string";
            else if constexpr (std::is_same_v<T, array>)        return "array";
            else if constexpr (std::is_same_v<T, object>)       return "object";
            else return "?"; });
  };

  OK(type_of(value{}) == "null");
  OK(type_of(value{true}) == "bool");
  OK(type_of(value{42}) == "integer");
  OK(type_of(value{1.5}) == "double");
  OK(type_of(value{"x"}) == "string");
  OK(type_of(value{array{}}) == "array");
  OK(type_of(value{object{}}) == "object");
}

// ============================================================
// 22. schema_node & validate
// ============================================================

void test_schema_validate()
{
  suite("schema_node & validate");

  // Schéma objet requis avec deux champs
  schema_node root = schema_node::object_node(true);
  root.children.emplace_back("name", schema_node::string_node(true));
  root.children.emplace_back("age", schema_node::integer_node(true));

  // Valide
  OK(validate(parse(R"({"name":"Alice","age":30})"), root).empty());

  // Champ manquant
  OK(!validate(parse(R"({"name":"Alice"})"), root).empty());

  // Mauvais type pour "age"
  OK(!validate(parse(R"({"name":"Alice","age":"trente"})"), root).empty());

  // Non-objet pour un schéma objet
  OK(!validate(value{42}, root).empty());

  // Schéma any : toujours OK
  schema_node any;
  OK(validate(value{}, any).empty());
  OK(validate(value{true}, any).empty());
  OK(validate(value{42}, any).empty());

  // Schéma array avec item schema integer
  schema_node arr_schema = schema_node::array_node(true);
  arr_schema.array_item_schema = std::make_shared<schema_node>(
      schema_node::integer_node());
  OK(validate(parse("[1,2,3]"), arr_schema).empty());
  OK(!validate(parse("[1,\"two\",3]"), arr_schema).empty());
}

// ============================================================
// 23. make_object / make_array
// ============================================================

void test_make_helpers()
{
  suite("make_object / make_array");

  auto vo = make_object({{"a", value{1}}, {"b", value{"hello"}}});
  OK(vo.is_object());
  OK(vo["a"].as_integer() == 1);
  OK(vo["b"].as_string() == "hello");

  auto va = make_array({value{1}, value{2}, value{3}});
  OK(va.is_array() && va.size() == 3);
  OK(va[size_t{2}].as_integer() == 3);

  // Vides
  OK(make_object({}).is_object());
  OK(make_array({}).is_array());
}

// ============================================================
// 24. from_vector / from_map / to_vector
// ============================================================

void test_vector_map_conversions()
{
  suite("from_vector / from_map / to_vector");

  // from_vector<int64_t>
  std::vector<int64_t> ints{1, 2, 3};
  auto va = from_vector(ints);
  OK(va.is_array() && va.size() == 3);
  OK(va[size_t{0}].as_integer() == 1);
  OK(va[size_t{2}].as_integer() == 3);

  // from_vector<std::string>
  std::vector<std::string> strs{"a", "b", "c"};
  auto vs = from_vector(strs);
  OK(vs.size() == 3);
  OK(vs[size_t{1}].as_string() == "b");

  // from_map<int64_t>
  std::map<std::string, int64_t> m{{"x", 10}, {"y", 20}};
  auto vm = from_map(m);
  OK(vm.is_object());
  OK(vm["x"].as_integer() == 10);
  OK(vm["y"].as_integer() == 20);

  // to_vector<int64_t>
  auto v_ints = to_vector<int64_t>(va);
  OK(v_ints.size() == 3);
  OK(v_ints[0] == 1 && v_ints[2] == 3);

  // to_vector<std::string>
  auto v_strs = to_vector<std::string>(vs);
  OK(v_strs.size() == 3);
  OK(v_strs[1] == "b");
}

// ============================================================
// 25. similarity / deep_clone
// ============================================================

void test_similarity_clone()
{
  suite("similarity / deep_clone");

  auto a = parse(R"({"x":1,"y":2,"z":3})");
  auto b = parse(R"({"x":1,"y":99,"z":3})");

  // "x" et "z" correspondent → 2
  OK(similarity(a, b) == 2);
  OK(similarity(a, a) == 3);

  // deep_clone
  auto c = deep_clone(a);
  OK(c == a);
  c["x"] = value{999};
  OK(a["x"].as_integer() == 1); // original intact
  OK(c["x"].as_integer() == 999);
}

// ============================================================
// 26. extract_http_body / parse_http_response_body
// ============================================================

void test_http_helpers()
{
  suite("extract_http_body / parse_http_response_body");

  // CRLF
  std::string raw_crlf =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "\r\n"
      "{\"status\":\"ok\"}";
  auto body = extract_http_body(raw_crlf);
  OK(body.has_value());

  auto v = parse_http_response_body(raw_crlf);
  OK(v.has_value());
  OK((*v)["status"].as_string() == "ok");

  // LF seul
  std::string raw_lf =
      "GET / HTTP/1.1\n"
      "Host: example.com\n"
      "\n"
      "{\"ok\":true}";
  auto v2 = parse_http_response_body(raw_lf);
  OK(v2.has_value());
  OK((*v2)["ok"].as_bool() == true);

  // Pas de séparateur → nullopt
  OK(!extract_http_body("no separator here").has_value());

  // Corps JSON invalide → nullopt (pas d'exception)
  std::string bad_body = "HTTP/1.1 200 OK\r\n\r\nNOT_JSON";
  OK(!parse_http_response_body(bad_body).has_value());
}

// ============================================================
// 27. from_string / to_string / to_string_pretty
// ============================================================

void test_aliases()
{
  suite("from_string / to_string / to_string_pretty");

  auto v = from_string("{\"k\":42}");
  OK(v["k"].as_integer() == 42);

  auto s = to_string(v);
  OK(parse(s)["k"].as_integer() == 42);

  auto sp = to_string_pretty(v, 4);
  OK(sp.find('\n') != std::string::npos);
  OK(parse(sp)["k"].as_integer() == 42);
}

// ============================================================
// 28. parse(const char*, size_t)
// ============================================================

void test_parse_buf_len()
{
  suite("parse(const char*, size_t)");

  const char *buf = "{\"n\":7}trailing_garbage";
  auto v = parse(buf, 8);

  OK(v["n"].as_integer() == 7);
}

// ============================================================
// 29. Nombres — cas limites
// ============================================================

void test_number_edge_cases()
{
  suite("nombres : cas limites");

  OK(parse("0").as_integer() == 0);
  OK(parse("-0").as_integer() == 0);
  OK(parse("1e10").is_double());
  OK(parse("1E10").is_double());
  OK(parse("1.0").is_double());
  OK(parse("-3.14").is_double());

  // INT64_MAX
  OK(parse("9223372036854775807").as_integer() == std::numeric_limits<int64_t>::max());

  // as_number() : coercion integer → double
  OK(parse("42").as_number() == 42.0);
  OK(parse("1.5").as_number() == 1.5);

  // NaN
  OK(value{std::numeric_limits<double>::quiet_NaN()}.dump() == "null");
}

// ============================================================
// 30. Tolérance whitespace
// ============================================================

void test_whitespace()
{
  suite("whitespace tolerance");

  OK(parse("  null  ").is_null());
  OK(parse("\t\r\ntrue").as_bool() == true);
  OK(parse("\n42\n").as_integer() == 42);

  auto arr = parse("\t[\r\n  1 ,\n  2\n]\n");
  OK(arr.size() == 2);

  auto obj = parse("{\n  \"key\" :\n  \"value\"\n}");
  OK(obj["key"].as_string() == "value");
}

// ============================================================
// 31. Grand JSON (1 000 éléments)
// ============================================================

void test_large_json()
{
  suite("large JSON (1 000 éléments)");

  std::string big = "[";
  for (int i = 0; i < 1000; ++i)
  {
    if (i > 0)
      big += ',';
    big += "{\"id\":" + std::to_string(i) + ",\"name\":\"item" + std::to_string(i) + "\"}";
  }
  big += "]";

  auto v = parse(big);
  OK(v.is_array() && v.size() == 1000);
  OK(v[size_t{0}]["id"].as_integer() == 0);
  OK(v[size_t{999}]["id"].as_integer() == 999);
  OK(v[size_t{42}]["name"].as_string() == "item42");

  // Roundtrip
  auto v2 = parse(v.dump());
  OK(v2.size() == 1000);
  OK(v2[size_t{500}]["id"].as_integer() == 500);
}

// ============================================================
// 32. struct array — API directe
// ============================================================

void test_array_struct()
{
  suite("struct array (API directe)");

  array a{1, 2, 3};
  OK(a.size() == 3 && !a.empty());
  OK(a.front().as_integer() == 1);
  OK(a.back().as_integer() == 3);

  a.push_back(value{4});
  OK(a.back().as_integer() == 4);

  a.emplace_back(value{5});
  OK(a.size() == 5);

  a.reserve(20); // ne doit pas planter

  a.clear();
  OK(a.empty());

  // at() hors-borne
  array b;
  b.emplace_back(value{99});
  THROWS_ANY(b.at(99));

  // Égalité
  array c{1, 2};
  array d{1, 2};
  array e{1, 3};
  OK(c == d);
  OK(c != e);
}

// ============================================================
// 33. struct object — API directe
// ============================================================

void test_object_struct()
{
  suite("struct object (API directe)");

  object o{{"a", value{1}}, {"b", value{2}}};
  OK(o.size() == 2 && !o.empty());
  OK(o.contains("a") && !o.contains("z"));

  // operator[] mutable : insert null si absent
  o["c"];
  OK(o.contains("c") && o["c"].is_null());

  // operator[] const : lève access_error si absent
  const object &co = o;
  THROWS_ACCESS(co["missing"]);

  // set : overwrite + insert
  o.set("a", value{99});
  OK(o["a"].as_integer() == 99);
  o.set("d", value{"x"});
  OK(o.contains("d"));

  // erase
  OK(o.erase("b") == true);
  OK(o.erase("ghost") == false);

  o.clear();
  OK(o.empty());

  o.reserve(16); // ne doit pas planter

  // Égalité
  object p{{"x", value{1}}};
  object q{{"x", value{1}}};
  object r{{"x", value{2}}};
  OK(p == q);
  OK(p != r);
}

// ============================================================
// Main
// ============================================================

int main()
{
  std::cout << "=== cnerium::json — test_basic.cpp ===\n";

  test_construction();
  test_parse_scalars();
  test_parse_strings();
  test_parse_arrays();
  test_parse_objects();
  test_parse_nested();
  test_dump();
  test_roundtrip();
  test_mutation();
  test_chained_access();
  test_try_get();
  test_object_api();
  test_errors();
  test_equality();
  test_iteration();
  test_json_pointer_fn();
  test_merge_patch();
  test_diff_apply();
  test_streaming_parser();
  test_ndjson();
  test_visit();
  test_schema_validate();
  test_make_helpers();
  test_vector_map_conversions();
  test_similarity_clone();
  test_http_helpers();
  test_aliases();
  test_parse_buf_len();
  test_number_edge_cases();
  test_whitespace();
  test_large_json();
  test_array_struct();
  test_object_struct();

  std::cout << "\n=== RÉSULTATS : "
            << g_pass << " OK, "
            << g_fail << " FAIL ===\n";

  return g_fail == 0 ? 0 : 1;
}
