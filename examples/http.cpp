#include <iostream>
#include <cnerium/json/json.hpp>

using namespace cnerium::json;

int main()
{
  std::string http =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n\r\n"
      "{\"status\":\"ok\",\"data\":123}";

  auto body = extract_http_body(http);

  if (body)
  {
    auto v = parse(*body);
    std::cout << v["status"].as_string() << "\n";
  }
}
