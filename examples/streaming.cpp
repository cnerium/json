#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  streaming_parser sp;

  const char *part1 = "{\"a\":1";
  const char *part2 = ",\"b\":2}";

  sp.feed(part1, strlen(part1));

  auto result = sp.feed(part2, strlen(part2));

  if (result)
    std::cout << result->dump(true) << "\n";
}
