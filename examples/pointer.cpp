#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  auto v = parse(R"({
        "user": {
            "addresses": [
                {"city": "Paris"},
                {"city": "Berlin"}
            ]
        }
    })");

  const auto &city = json_pointer(v, "/user/addresses/1/city");
  std::cout << city.as_string() << "\n"; // Berlin
}
