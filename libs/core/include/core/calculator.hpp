#pragma once

#include "core/opcodes.hpp"
#include "core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mk61::core {

/// Значення регістра МК-61 (підтримує як десяткові числа з рухомою крапкою,
/// так і логічний BCD/HEX-формат "8.xxxxxxx" для порозрядних операцій)
struct RegisterValue {
  double numeric{0.0};
  bool is_logical{false};
  std::array<std::uint8_t, 8> nibbles{0, 0, 0, 0, 0, 0, 0, 0};
  // Скільки знаків після крапки лишити, навіть якщо там нулі.
  // Для градусів і мінут 12.30 інакше зливалося б із 12.3, а 45.00 — із 45.
  int min_fraction_digits{0};

  constexpr RegisterValue() noexcept = default;
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr RegisterValue(double val) noexcept : numeric(val) {}
};

/// Повна модель програмованого мікрокалькулятора «Електроніка МК-61»
class Calculator {
public:
  Calculator() noexcept;

  /// Повне скидання стану калькулятора (вимкнення/увімкнення живлення)
  void reset() noexcept;

  /// Очищення лише пам'яті програм та лічильника команд
  void clear_program() noexcept;

  /// Очищення стека та регістрів пам'яті
  void clear_registers() noexcept;

  /// Натискання однієї з 30 фізичних клавіш МК-61
  void press_key(Key key);

  /// Натискання послідовності клавіш за текстовими токенами (для CLI/скриптів)
  bool press_tokens(std::string_view tokens_line, std::string &error_out);

  /// Введення числа безпосередньо в регістр X (із дотриманням логіки автопідняття стека)
  void enter_number(double value) noexcept;

  /// Виконання одного кроку програми з поточної адреси PC (еквівалент ШГ→ в АВТ або такту С/П)
  /// Повертає true, якщо виконання триває, або false, якщо досягнуто С/П чи ЕГГОГ.
  bool step_program();

  /// Автоматичне виконання програми з поточного кроку PC до С/П, ЕГГОГ або ліміту кроків
  RunStatus run_program(std::size_t max_steps = 100000);

  /// Виконання одного конкретного коду операції в режимі обчислень
  void execute_opcode(std::uint8_t opcode, std::optional<std::uint8_t> arg_byte = std::nullopt);

  /// Завантаження готового масиву байтів у пам'ять програм
  void load_program(std::span<const std::uint8_t> bytes) noexcept;

  /// Асемблювання текстового коду програми та завантаження в пам'ять
  AssembleResult assemble_and_load(std::string_view source);

  // --- Керування перемикачами ---
  [[nodiscard]] AngleMode angle_mode() const noexcept { return angle_mode_; }
  void set_angle_mode(AngleMode mode) noexcept { angle_mode_ = mode; }

  [[nodiscard]] OperatingMode operating_mode() const noexcept { return op_mode_; }
  void set_operating_mode(OperatingMode mode) noexcept;

  [[nodiscard]] PrefixState prefix_state() const noexcept { return prefix_; }

  // --- Стан процесора та індикатора ---
  [[nodiscard]] bool is_error() const noexcept { return error_; }
  [[nodiscard]] bool is_dark() const noexcept { return dark_; }
  [[nodiscard]] bool is_entering() const noexcept { return entering_; }
  [[nodiscard]] int pc() const noexcept { return pc_; }
  void set_pc(int new_pc) noexcept;

  [[nodiscard]] std::size_t total_steps_executed() const noexcept { return steps_executed_; }

  // --- Доступ до стека (X, Y, Z, T) та X1 ---
  [[nodiscard]] double reg_x() const noexcept;
  [[nodiscard]] double reg_y() const noexcept { return y_.numeric; }
  [[nodiscard]] double reg_z() const noexcept { return z_.numeric; }
  [[nodiscard]] double reg_t() const noexcept { return t_.numeric; }
  [[nodiscard]] double reg_x1() const noexcept { return x1_.numeric; }

  [[nodiscard]] const RegisterValue &raw_x() const noexcept { return x_; }
  [[nodiscard]] const RegisterValue &raw_y() const noexcept { return y_; }
  [[nodiscard]] const RegisterValue &raw_z() const noexcept { return z_; }
  [[nodiscard]] const RegisterValue &raw_t() const noexcept { return t_; }
  [[nodiscard]] const RegisterValue &raw_x1() const noexcept { return x1_; }

  void set_reg_x(double val) noexcept;
  void set_reg_y(double val) noexcept { y_ = RegisterValue{round_mk61(val)}; }
  void set_reg_z(double val) noexcept { z_ = RegisterValue{round_mk61(val)}; }
  void set_reg_t(double val) noexcept { t_ = RegisterValue{round_mk61(val)}; }

  // --- Доступ до 15 регістрів пам'яті (R0..R9, RA..RE) ---
  [[nodiscard]] double memory_reg(std::size_t idx) const noexcept;
  [[nodiscard]] const RegisterValue &raw_memory_reg(std::size_t idx) const noexcept;
  void set_memory_reg(std::size_t idx, double val) noexcept;
  [[nodiscard]] const std::array<RegisterValue, kRegisterCount> &memory_regs() const noexcept {
    return regs_;
  }

  // --- Доступ до пам'яті програм та стека повернень ---
  [[nodiscard]] const std::array<std::uint8_t, kProgramSize> &program_memory() const noexcept {
    return prog_;
  }
  void set_program_byte(int step, std::uint8_t code) noexcept;

  [[nodiscard]] const std::array<int, kReturnStackSize> &return_stack() const noexcept {
    return return_stack_;
  }
  [[nodiscard]] std::size_t return_sp() const noexcept { return return_sp_; }

  // --- Індикатор ---
  [[nodiscard]] DisplayState display() const;
  [[nodiscard]] static std::string format_register_value(const RegisterValue &val);
  [[nodiscard]] static double round_mk61(double val) noexcept;

  // --- Серіалізація стану (збереження/завантаження файлу .mk61) ---
  [[nodiscard]] std::string serialize_state() const;
  bool deserialize_state(std::string_view data, std::string &error_out);

private:
  // Операційний стек та регістр попереднього результату
  RegisterValue x_{0.0};
  RegisterValue y_{0.0};
  RegisterValue z_{0.0};
  RegisterValue t_{0.0};
  RegisterValue x1_{0.0};

  // 15 адресних регістрів пам'яті (0..9, A..E)
  std::array<RegisterValue, kRegisterCount> regs_{};

  // Пам'ять програм (105 кроків: 00..104)
  std::array<std::uint8_t, kProgramSize> prog_{};

  // Лічильник команд та 5-рівневий стек адрес повернення підпрограм
  int pc_{0};
  std::array<int, kReturnStackSize> return_stack_{};
  std::size_t return_sp_{0};

  // Перемикачі та режими
  AngleMode angle_mode_{AngleMode::Radians};
  OperatingMode op_mode_{OperatingMode::Automatic};
  PrefixState prefix_{PrefixState::None};

  // Очікувана двобайтова команда при наборі адреси переходу (БП, ПП, F x.., F L..)
  std::uint8_t pending_jump_opcode_{0};
  std::uint8_t pending_addr_hi_{0};
  int pending_jump_step_{0}; // Крок, куди записується байт адреси в режимі ПРГ

  // Прапорці стану
  bool error_{false};
  bool dark_{false};
  bool stack_lift_{true};
  std::size_t steps_executed_{0};

  // Стан генератора псевдовипадкових чисел (К СЧ)
  std::uint64_t rng_state_{0x40406700ULL};

  // Стан посимвольного введення числа в регістр X
  bool entering_{false};
  std::string mantissa_digits_;
  bool has_dot_{false};
  bool mantissa_neg_{false};
  bool entering_exp_{false};
  std::string exp_digits_;
  bool exp_neg_{false};

  // Внутрішні допоміжні методи
  void reset_entry() noexcept;
  void commit_entry() noexcept;
  [[nodiscard]] double parse_entry_value() const noexcept;
  void push_stack() noexcept;
  void drop_stack() noexcept;
  void trigger_error() noexcept;

  void handle_digit_key(std::uint8_t digit);
  void handle_dot_key();
  void handle_sign_key();
  void handle_exp_key();
  void handle_clear_x_key();
  void handle_enter_key();

  void write_prog_byte(std::uint8_t code) noexcept;
  [[nodiscard]] double to_radians(double angle) const noexcept;
  [[nodiscard]] double from_radians(double rad) const noexcept;

  [[nodiscard]] std::optional<int> resolve_indirect_address(std::uint8_t reg_idx) noexcept;
  [[nodiscard]] std::optional<std::uint8_t> resolve_indirect_reg(std::uint8_t reg_idx) noexcept;

  static std::array<std::uint8_t, 8> extract_mk61_nibbles(const RegisterValue &v) noexcept;
};

} // namespace mk61::core
