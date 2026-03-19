#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  value v = parse(R"({"name":"Ada","age":20})");

  schema_node schema = schema_node::object_node(true);
  schema.children = {
      {"name", schema_node::string_node(true)},
      {"age", schema_node::integer_node(true)}};

  auto err = validate(v, schema);

  if (err.empty())
    std::cout << "Valid\n";
  else
    std::cout << "Error: " << err << "\n";
}
