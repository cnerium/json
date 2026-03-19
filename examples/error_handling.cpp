#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  try
  {
    auto v = parse(R"({"x": 1,})"); // invalid JSON
  }
  catch (const parse_error &e)
  {
    std::cout << "Parse error: " << e.what()
              << " at line " << e.line
              << ", col " << e.column << "\n";
  }
}
