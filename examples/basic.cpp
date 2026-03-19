#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  value data = {
      {"name", "Gaspard"},
      {"age", 25},
      {"active", true}};

  std::cout << data.dump(true) << "\n";
}
