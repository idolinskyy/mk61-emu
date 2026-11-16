#include <catch2/catch_test_macros.hpp>
#include <core/greeter.hpp>

TEST_CASE("Greeter formats greeting correctly", "[core]") {
  mk61::core::Greeter greeter("Dev");
  REQUIRE(greeter.greet() == "Привіт, Dev!");
}