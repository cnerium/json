#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  value a = parse(R"({"x":1,"y":2})");
  value b = parse(R"({"x":1,"y":3,"z":4})");

  auto patch = diff(a, b);

  for (const auto &op : patch)
    std::cout << op.dump() << "\n";

  apply_patch(a, patch);

  std::cout << a.dump(true) << "\n";
}
