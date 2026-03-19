#include <iostream>
#include <map>
#include <vector>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  std::vector<int> vec = {1, 2, 3};
  value v = from_vector(vec);

  auto back = to_vector<int>(v);

  for (auto x : back)
    std::cout << x << "\n";

  std::map<std::string, int> m = {{"a", 1}, {"b", 2}};
  value obj = from_map(m);

  std::cout << obj.dump(true) << "\n";
}
