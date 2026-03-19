#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  value data = object{
      {"skills", array{"C++", "Rust", "Go"}},
      {"scores", array{10, 20, 30}}};

  data["skills"].push_back("Python");

  for (const auto &s : data["skills"].as_array())
  {
    std::cout << s.as_string() << "\n";
  }
}
