#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  value dst = parse(R"({"name":"Ada","age":20})");
  value patch = parse(R"({"age":21,"city":"Paris"})");

  merge_patch(dst, patch);

  std::cout << dst.dump(true) << "\n";
}
