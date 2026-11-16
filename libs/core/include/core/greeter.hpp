#pragma once

#include <string>

namespace mk61::core {

class Greeter {
public:
  explicit Greeter(std::string name);
  [[nodiscard]] std::string greet() const;

private:
  std::string name_;
};

} // namespace mk61::core