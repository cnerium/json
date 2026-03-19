#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  std::string input = R"(
{"a":1}
{"b":2}
invalid
{"c":3}
)";

  auto res = parse_ndjson(input);

  for (auto &v : res.values)
    std::cout << v.dump() << "\n";

  for (auto &[line, err] : res.errors)
    std::cout << "Line " << line << ": " << err << "\n";
}
