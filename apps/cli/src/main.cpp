#include <core/greeter.hpp>
#include <print>

int main() {
  mk61::core::Greeter greeter("Світ C++23");
  std::println("{}", greeter.greet());
  return 0;
}