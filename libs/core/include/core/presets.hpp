#pragma once

#include <span>
#include <string_view>

namespace mk61::core {

/// Вбудована класична програма для мікрокалькулятора МК-61
struct PresetProgram {
  std::string_view id;
  std::string_view title;
  std::string_view description;
  std::string_view usage_hint;
  std::string_view source_code;
};

/// Повертає список усіх вбудованих прикладів програм для МК-61
[[nodiscard]] std::span<const PresetProgram> builtin_programs() noexcept;

/// Шукає вбудовану програму за ідентифікатором (id) або номером ("1".."6")
[[nodiscard]] const PresetProgram *find_builtin_program(std::string_view id_or_index) noexcept;

} // namespace mk61::core
