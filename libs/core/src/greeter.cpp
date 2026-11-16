#include "core/greeter.hpp"
#include <format>
#include <utility>

namespace mk61::core {

Greeter::Greeter(std::string name) : name_(std::move(name)) {}

std::string Greeter::greet() const { return std::format("Привіт, {}!", name_); }

} // namespace mk61::core