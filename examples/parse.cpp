#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  std::string json = R"({"name":"Ada","score":99.5})";

  value v = parse(json);

  std::cout << v["name"].as_string() << "\n";
  std::cout << v["score"].as_double() << "\n";
}
