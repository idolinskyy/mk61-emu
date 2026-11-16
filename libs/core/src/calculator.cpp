#include "core/calculator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>
#include <numbers>
#include <sstream>

namespace mk61::core {

namespace {

constexpr double kPiMk61 = 3.1415926;
constexpr double kMaxMk61Value = 9.9999999e99;
constexpr double kMinMk61Positive = 1.0e-99;
constexpr double kZeroEps = 1.0e-99;

bool is_zero_mk61(double v) noexcept { return std::abs(v) < kZeroEps; }

} // namespace

Calculator::Calculator() noexcept { reset(); }

void Calculator::reset() noexcept {
  x_ = RegisterValue{0.0};
  y_ = RegisterValue{0.0};
  z_ = RegisterValue{0.0};
  t_ = RegisterValue{0.0};
  x1_ = RegisterValue{0.0};
  regs_.fill(RegisterValue{0.0});
  prog_.fill(0x00U);
  pc_ = 0;
  return_stack_.fill(0);
  return_sp_ = 0;
  angle_mode_ = AngleMode::Radians;
  op_mode_ = OperatingMode::Automatic;
  prefix_ = PrefixState::None;
  pending_jump_opcode_ = 0;
  pending_addr_hi_ = 0;
  pending_jump_step_ = 0;
  error_ = false;
  dark_ = false;
  stack_lift_ = false;
  steps_executed_ = 0;
  rng_state_ = 0x40406700ULL;
  reset_entry();
}

void Calculator::clear_program() noexcept {
  prog_.fill(0x00U);
  pc_ = 0;
  return_stack_.fill(0);
  return_sp_ = 0;
  prefix_ = PrefixState::None;
}

void Calculator::clear_registers() noexcept {
  x_ = RegisterValue{0.0};
  y_ = RegisterValue{0.0};
  z_ = RegisterValue{0.0};
  t_ = RegisterValue{0.0};
  x1_ = RegisterValue{0.0};
  regs_.fill(RegisterValue{0.0});
  reset_entry();
  stack_lift_ = false;
  error_ = false;
  dark_ = false;
}

void Calculator::reset_entry() noexcept {
  entering_ = false;
  mantissa_digits_.clear();
  has_dot_ = false;
  mantissa_neg_ = false;
  entering_exp_ = false;
  exp_digits_.clear();
  exp_neg_ = false;
}

double Calculator::parse_entry_value() const noexcept {
  if (!entering_ || mantissa_digits_.empty()) {
    return 0.0;
  }
  double val = std::strtod(mantissa_digits_.c_str(), nullptr);
  if (mantissa_neg_) {
    val = -val;
  }
  if (entering_exp_ && !exp_digits_.empty()) {
    int exp_val = 0;
    for (char c : exp_digits_) {
      if (c >= '0' && c <= '9') {
        exp_val = exp_val * 10 + (c - '0');
      }
    }
    if (exp_neg_) {
      exp_val = -exp_val;
    }
    val *= std::pow(10.0, static_cast<double>(exp_val));
  }
  return round_mk61(val);
}

void Calculator::commit_entry() noexcept {
  if (entering_) {
    x_ = RegisterValue{parse_entry_value()};
    reset_entry();
    stack_lift_ = true;
  }
}

void Calculator::push_stack() noexcept {
  t_ = z_;
  z_ = y_;
  y_ = x_;
}

void Calculator::drop_stack() noexcept {
  // На МК-61 при опусканні стека регістр T зберігає своє значення і дублюється в Z
  y_ = z_;
  z_ = t_;
}

void Calculator::trigger_error() noexcept {
  reset_entry();
  error_ = true;
  prefix_ = PrefixState::None;
  stack_lift_ = false;
}

double Calculator::round_mk61(double val) noexcept {
  if (!std::isfinite(val)) {
    return val;
  }
  if (std::abs(val) < kMinMk61Positive) {
    return 0.0;
  }
  // Округлення до 8 значущих десяткових цифр мантиси (формат МК-61)
  char buf[64]{};
  std::snprintf(buf, sizeof(buf), "%.7e", val);
  const double rounded = std::strtod(buf, nullptr);
  if (std::abs(rounded) < kMinMk61Positive) {
    return 0.0;
  }
  return rounded;
}

void Calculator::set_operating_mode(OperatingMode mode) noexcept {
  if (op_mode_ != mode) {
    commit_entry();
    op_mode_ = mode;
    prefix_ = PrefixState::None;
  }
}

void Calculator::set_pc(int new_pc) noexcept {
  int normalized = new_pc % static_cast<int>(kProgramSize);
  if (normalized < 0) {
    normalized += static_cast<int>(kProgramSize);
  }
  pc_ = normalized;
}

double Calculator::reg_x() const noexcept { return x_.numeric; }

void Calculator::set_reg_x(double val) noexcept {
  reset_entry();
  if (!std::isfinite(val) || std::abs(val) > kMaxMk61Value) {
    trigger_error();
    return;
  }
  x_ = RegisterValue{round_mk61(val)};
  stack_lift_ = true;
}

double Calculator::memory_reg(std::size_t idx) const noexcept {
  if (idx < kRegisterCount) {
    return regs_[idx].numeric;
  }
  return 0.0;
}

const RegisterValue &Calculator::raw_memory_reg(std::size_t idx) const noexcept {
  static constexpr RegisterValue kZero{0.0};
  if (idx < kRegisterCount) {
    return regs_[idx];
  }
  return kZero;
}

void Calculator::set_memory_reg(std::size_t idx, double val) noexcept {
  if (idx < kRegisterCount) {
    regs_[idx] = RegisterValue{round_mk61(val)};
  }
}

void Calculator::set_program_byte(int step, std::uint8_t code) noexcept {
  if (step >= 0 && step < static_cast<int>(kProgramSize)) {
    prog_[static_cast<std::size_t>(step)] = code;
  }
}

void Calculator::load_program(std::span<const std::uint8_t> bytes) noexcept {
  prog_.fill(0x00U);
  const std::size_t count = std::min(bytes.size(), kProgramSize);
  for (std::size_t i = 0; i < count; ++i) {
    prog_[i] = bytes[i];
  }
  pc_ = 0;
  return_sp_ = 0;
  prefix_ = PrefixState::None;
}

AssembleResult Calculator::assemble_and_load(std::string_view source) {
  AssembleResult res = assemble_program(source);
  if (res.ok) {
    load_program(res.program);
  }
  return res;
}

void Calculator::enter_number(double value) noexcept {
  if (error_ || dark_) {
    return;
  }
  commit_entry();
  if (!std::isfinite(value) || std::abs(value) > kMaxMk61Value) {
    trigger_error();
    return;
  }
  if (stack_lift_) {
    push_stack();
  }
  x_ = RegisterValue{round_mk61(value)};
  stack_lift_ = true;
}

void Calculator::handle_digit_key(std::uint8_t digit) {
  const char dch = static_cast<char>('0' + digit);
  if (!entering_) {
    if (stack_lift_) {
      push_stack();
    }
    reset_entry();
    entering_ = true;
    mantissa_digits_.push_back(dch);
  } else if (entering_exp_) {
    if (exp_digits_.size() < 2) {
      exp_digits_.push_back(dch);
    } else {
      exp_digits_[0] = exp_digits_[1];
      exp_digits_[1] = dch;
    }
  } else {
    // Порахуємо кількість цифр у мантисі (без крапки)
    std::size_t digit_count = 0;
    for (char c : mantissa_digits_) {
      if (c >= '0' && c <= '9') {
        ++digit_count;
      }
    }
    if (mantissa_digits_ == "0" && !has_dot_) {
      mantissa_digits_[0] = dch;
    } else if (digit_count < 8) {
      mantissa_digits_.push_back(dch);
    }
  }
  x_ = RegisterValue{parse_entry_value()};
}

void Calculator::handle_dot_key() {
  if (!entering_) {
    if (stack_lift_) {
      push_stack();
    }
    reset_entry();
    entering_ = true;
    mantissa_digits_ = "0.";
    has_dot_ = true;
  } else if (!entering_exp_ && !has_dot_) {
    if (mantissa_digits_.empty()) {
      mantissa_digits_ = "0.";
    } else {
      mantissa_digits_.push_back('.');
    }
    has_dot_ = true;
  }
  x_ = RegisterValue{parse_entry_value()};
}

void Calculator::handle_sign_key() {
  if (entering_) {
    if (entering_exp_) {
      exp_neg_ = !exp_neg_;
    } else {
      mantissa_neg_ = !mantissa_neg_;
    }
    x_ = RegisterValue{parse_entry_value()};
  } else {
    x_.numeric = is_zero_mk61(x_.numeric) ? 0.0 : -x_.numeric;
    x_.is_logical = false;
    stack_lift_ = true;
  }
}

void Calculator::handle_exp_key() {
  if (!entering_) {
    if (stack_lift_) {
      push_stack();
    }
    reset_entry();
    entering_ = true;
    mantissa_digits_ = "1";
    entering_exp_ = true;
    exp_digits_ = "00";
  } else if (!entering_exp_) {
    if (mantissa_digits_.empty() || is_zero_mk61(parse_entry_value())) {
      mantissa_digits_ = "1";
      has_dot_ = false;
    }
    entering_exp_ = true;
    exp_digits_ = "00";
    exp_neg_ = false;
  }
  x_ = RegisterValue{parse_entry_value()};
}

void Calculator::handle_clear_x_key() {
  reset_entry();
  error_ = false;
  dark_ = false;
  x_ = RegisterValue{0.0};
  stack_lift_ = false;
}

void Calculator::handle_enter_key() {
  commit_entry();
  push_stack();
  stack_lift_ = false;
}

void Calculator::write_prog_byte(std::uint8_t code) noexcept {
  prog_[static_cast<std::size_t>(pc_)] = code;
  set_pc(pc_ + 1);
}

double Calculator::to_radians(double angle) const noexcept {
  switch (angle_mode_) {
  case AngleMode::Radians:
    return angle;
  case AngleMode::Degrees:
    return angle * (std::numbers::pi / 180.0);
  case AngleMode::Grads:
    return angle * (std::numbers::pi / 200.0);
  }
  return angle;
}

double Calculator::from_radians(double rad) const noexcept {
  switch (angle_mode_) {
  case AngleMode::Radians:
    return rad;
  case AngleMode::Degrees:
    return rad * (180.0 / std::numbers::pi);
  case AngleMode::Grads:
    return rad * (200.0 / std::numbers::pi);
  }
  return rad;
}

std::optional<int> Calculator::resolve_indirect_address(std::uint8_t reg_idx) noexcept {
  if (reg_idx >= kRegisterCount) {
    trigger_error();
    return std::nullopt;
  }
  double val = regs_[reg_idx].numeric;
  double int_part = std::trunc(val);
  double frac_part = val - int_part;
  if (reg_idx <= 3) {
    // Регістри 0..3: спочатку зменшуються на 1 (декремент)
    int_part -= 1.0;
    regs_[reg_idx] = RegisterValue{round_mk61(int_part + frac_part)};
  } else if (reg_idx <= 6) {
    // Регістри 4..6: спочатку збільшуються на 1 (інкремент)
    int_part += 1.0;
    regs_[reg_idx] = RegisterValue{round_mk61(int_part + frac_part)};
  }
  const int addr = static_cast<int>(std::abs(int_part));
  if (addr < 0 || addr >= static_cast<int>(kProgramSize)) {
    trigger_error();
    return std::nullopt;
  }
  return addr;
}

std::optional<std::uint8_t> Calculator::resolve_indirect_reg(std::uint8_t reg_idx) noexcept {
  if (reg_idx >= kRegisterCount) {
    trigger_error();
    return std::nullopt;
  }
  double val = regs_[reg_idx].numeric;
  double int_part = std::trunc(val);
  double frac_part = val - int_part;
  if (reg_idx <= 3) {
    int_part -= 1.0;
    regs_[reg_idx] = RegisterValue{round_mk61(int_part + frac_part)};
  } else if (reg_idx <= 6) {
    int_part += 1.0;
    regs_[reg_idx] = RegisterValue{round_mk61(int_part + frac_part)};
  }
  const int target_reg = static_cast<int>(std::abs(int_part));
  if (target_reg < 0 || target_reg >= static_cast<int>(kRegisterCount)) {
    trigger_error();
    return std::nullopt;
  }
  return static_cast<std::uint8_t>(target_reg);
}

std::array<std::uint8_t, 8> Calculator::extract_mk61_nibbles(const RegisterValue &v) noexcept {
  if (v.is_logical) {
    return v.nibbles;
  }
  std::array<std::uint8_t, 8> nibs{8, 0, 0, 0, 0, 0, 0, 0};
  const double abs_v = std::abs(v.numeric);
  if (is_zero_mk61(abs_v)) {
    return nibs;
  }
  char buf[64]{};
  std::snprintf(buf, sizeof(buf), "%.7e", abs_v);
  // Формат "%.7e": "d.ddddddde+dd" -> перша цифра m0 = buf[0], далі m1..m7 = buf[2..8]
  if (buf[0] >= '0' && buf[0] <= '9') {
    nibs[0] = static_cast<std::uint8_t>(buf[0] - '0');
  }
  for (std::size_t i = 0; i < 7; ++i) {
    const char c = buf[2 + i];
    if (c >= '0' && c <= '9') {
      nibs[1 + i] = static_cast<std::uint8_t>(c - '0');
    }
  }
  return nibs;
}

void Calculator::execute_opcode(std::uint8_t opcode, std::optional<std::uint8_t> arg_byte) {
  if (error_ || dark_) {
    if (opcode == 0x0DU) {
      handle_clear_x_key();
    }
    return;
  }

  // Посимвольне введення цифр, крапки, знака, порядку, скидання та В↑
  if (opcode <= 0x09U) {
    handle_digit_key(opcode);
    return;
  }
  switch (opcode) {
  case 0x0AU:
    handle_dot_key();
    return;
  case 0x0BU:
    handle_sign_key();
    return;
  case 0x0CU:
    handle_exp_key();
    return;
  case 0x0DU:
    handle_clear_x_key();
    return;
  case 0x0EU:
    handle_enter_key();
    return;
  default:
    break;
  }

  // Усі інші операції завершують режим посимвольного набору числа
  commit_entry();

  const auto hi = static_cast<std::uint8_t>((opcode >> 4U) & 0x0FU);
  const auto lo = static_cast<std::uint8_t>(opcode & 0x0FU);

  auto set_unary_result = [&](double res) {
    if (!std::isfinite(res) || std::abs(res) > kMaxMk61Value) {
      trigger_error();
      return;
    }
    x1_ = x_;
    x_ = RegisterValue{round_mk61(res)};
    stack_lift_ = true;
  };

  auto set_binary_result = [&](double res, bool drop = true) {
    if (!std::isfinite(res) || std::abs(res) > kMaxMk61Value) {
      trigger_error();
      return;
    }
    x1_ = x_;
    x_ = RegisterValue{round_mk61(res)};
    if (drop) {
      drop_stack();
    }
    stack_lift_ = true;
  };

  switch (opcode) {
  case 0x0FU: { // F Вх (виклик X1)
    if (stack_lift_) {
      push_stack();
    }
    x_ = x1_;
    stack_lift_ = true;
    return;
  }
  case 0x10U: // +
    set_binary_result(y_.numeric + x_.numeric);
    return;
  case 0x11U: // -
    set_binary_result(y_.numeric - x_.numeric);
    return;
  case 0x12U: // ×
    set_binary_result(y_.numeric * x_.numeric);
    return;
  case 0x13U: // ÷
    if (is_zero_mk61(x_.numeric)) {
      trigger_error();
      return;
    }
    set_binary_result(y_.numeric / x_.numeric);
    return;
  case 0x14U: { // ↔ (обмін X та Y)
    std::swap(x_, y_);
    stack_lift_ = true;
    return;
  }
  case 0x15U: // F 10^x
    set_unary_result(std::pow(10.0, x_.numeric));
    return;
  case 0x16U: // F e^x
    set_unary_result(std::exp(x_.numeric));
    return;
  case 0x17U: // F lg
    if (x_.numeric <= 0.0 || is_zero_mk61(x_.numeric)) {
      trigger_error();
      return;
    }
    set_unary_result(std::log10(x_.numeric));
    return;
  case 0x18U: // F ln
    if (x_.numeric <= 0.0 || is_zero_mk61(x_.numeric)) {
      trigger_error();
      return;
    }
    set_unary_result(std::log(x_.numeric));
    return;
  case 0x19U: // F arcsin
    if (std::abs(x_.numeric) > 1.0) {
      trigger_error();
      return;
    }
    set_unary_result(from_radians(std::asin(x_.numeric)));
    return;
  case 0x1AU: // F arccos
    if (std::abs(x_.numeric) > 1.0) {
      trigger_error();
      return;
    }
    set_unary_result(from_radians(std::acos(x_.numeric)));
    return;
  case 0x1BU: // F arctg
    set_unary_result(from_radians(std::atan(x_.numeric)));
    return;
  case 0x1CU: // F sin
    set_unary_result(std::sin(to_radians(x_.numeric)));
    return;
  case 0x1DU: // F cos
    set_unary_result(std::cos(to_radians(x_.numeric)));
    return;
  case 0x1EU: { // F tg
    const double rad = to_radians(x_.numeric);
    const double c = std::cos(rad);
    if (std::abs(c) < 1.0e-12) {
      trigger_error();
      return;
    }
    set_unary_result(std::sin(rad) / c);
    return;
  }
  case 0x20U: { // F π
    if (stack_lift_) {
      push_stack();
    }
    x_ = RegisterValue{kPiMk61};
    stack_lift_ = true;
    return;
  }
  case 0x21U: // F √
    if (x_.numeric < 0.0) {
      trigger_error();
      return;
    }
    set_unary_result(std::sqrt(x_.numeric));
    return;
  case 0x22U: // F x²
    set_unary_result(x_.numeric * x_.numeric);
    return;
  case 0x23U: // F 1/x
    if (is_zero_mk61(x_.numeric)) {
      trigger_error();
      return;
    }
    set_unary_result(1.0 / x_.numeric);
    return;
  case 0x24U: { // F x^y (у МК-61 основа у Y, показник у X: Y^X)
    if (y_.numeric <= 0.0 && is_zero_mk61(y_.numeric) && x_.numeric <= 0.0) {
      trigger_error();
      return;
    }
    if (y_.numeric < 0.0) {
      // На МК-61 x^y обчислюється через exp(X * ln(Y)), тому при Y <= 0 видає ЕГГОГ
      trigger_error();
      return;
    }
    set_binary_result(std::pow(y_.numeric, x_.numeric));
    return;
  }
  case 0x25U: { // F В↻ (кільцевий зсув стека: X -> T, Y -> X, Z -> Y, T -> Z)
    const RegisterValue old_x = x_;
    x_ = y_;
    y_ = z_;
    z_ = t_;
    t_ = old_x;
    stack_lift_ = true;
    return;
  }
  case 0x26U: { // К М→Г (градуси та мінути ГГ.ММм -> ГГ.гггг)
    const double v = x_.numeric;
    const double deg = std::trunc(v);
    const double mins = std::abs(v - deg) * 100.0;
    if (mins >= 60.0 - 1.0e-9) {
      trigger_error();
      return;
    }
    const double res = deg + std::copysign(mins / 60.0, v);
    set_unary_result(res);
    return;
  }
  case 0x2AU: { // К МС→Г (градуси, мінути та секунди ГГ.ММССс -> ГГ.гггг)
    const double v = x_.numeric;
    const double deg = std::trunc(v);
    const double rem = std::abs(v - deg) * 100.0;
    const double mins = std::floor(rem + 1.0e-9);
    const double secs = (rem - mins) * 100.0;
    if (mins >= 60.0 || secs >= 60.0 - 1.0e-7) {
      trigger_error();
      return;
    }
    const double res = deg + std::copysign(mins / 60.0 + secs / 3600.0, v);
    set_unary_result(res);
    return;
  }
  case 0x30U: { // К Г→МС (частки градуса ГГ.гггг -> ГГ.ММССс)
    const double v = x_.numeric;
    const double deg = std::trunc(v);
    const double total_mins = std::abs(v - deg) * 60.0;
    const double mins = std::floor(total_mins + 1.0e-9);
    const double secs = std::max(0.0, (total_mins - mins) * 60.0);
    const double res = deg + std::copysign(mins / 100.0 + secs / 10000.0, v);
    set_unary_result(res);
    return;
  }
  case 0x31U: // К |x|
    set_unary_result(std::abs(x_.numeric));
    return;
  case 0x32U: { // К ЗН
    double s = 0.0;
    if (!is_zero_mk61(x_.numeric)) {
      s = (x_.numeric > 0.0) ? 1.0 : -1.0;
    }
    set_unary_result(s);
    return;
  }
  case 0x33U: { // К Г→М (частки градуса ГГ.гггг -> ГГ.ММм)
    const double v = x_.numeric;
    const double deg = std::trunc(v);
    const double mins = std::abs(v - deg) * 60.0;
    const double res = deg + std::copysign(mins / 100.0, v);
    set_unary_result(res);
    return;
  }
  case 0x34U: // К [x] (ціла частина)
    set_unary_result(std::trunc(x_.numeric));
    return;
  case 0x35U: { // К {x} (дробова частина)
    const double v = x_.numeric;
    set_unary_result(v - std::trunc(v));
    return;
  }
  case 0x36U: // К max (максимум із X та Y без опускання стека!)
    set_binary_result(std::max(x_.numeric, y_.numeric), false);
    return;
  case 0x37U:
  case 0x38U:
  case 0x39U: { // К ∧ (37), К ∨ (38), К ⊕ (39)
    const auto nx = extract_mk61_nibbles(x_);
    const auto ny = extract_mk61_nibbles(y_);
    RegisterValue out;
    out.is_logical = true;
    out.nibbles[0] = 8;
    double numeric_approx = 8.0;
    double scale = 0.1;
    for (std::size_t i = 1; i < 8; ++i) {
      std::uint8_t r = 0;
      if (opcode == 0x37U) {
        r = static_cast<std::uint8_t>(ny[i] & nx[i]);
      } else if (opcode == 0x38U) {
        r = static_cast<std::uint8_t>(ny[i] | nx[i]);
      } else {
        r = static_cast<std::uint8_t>(ny[i] ^ nx[i]);
      }
      out.nibbles[i] = static_cast<std::uint8_t>(r & 0x0FU);
      numeric_approx += static_cast<double>(out.nibbles[i] % 10) * scale;
      scale *= 0.1;
    }
    out.numeric = numeric_approx;
    x1_ = x_;
    x_ = out;
    drop_stack();
    stack_lift_ = true;
    return;
  }
  case 0x3AU: { // К инв (3A)
    const auto nx = extract_mk61_nibbles(x_);
    RegisterValue out;
    out.is_logical = true;
    out.nibbles[0] = 8;
    double numeric_approx = 8.0;
    double scale = 0.1;
    for (std::size_t i = 1; i < 8; ++i) {
      out.nibbles[i] = static_cast<std::uint8_t>((~nx[i]) & 0x0FU);
      numeric_approx += static_cast<double>(out.nibbles[i] % 10) * scale;
      scale *= 0.1;
    }
    out.numeric = numeric_approx;
    x1_ = x_;
    x_ = out;
    stack_lift_ = true;
    return;
  }
  case 0x3BU: { // К СЧ (генератор псевдовипадкових чисел [0, 1))
    if (stack_lift_) {
      push_stack();
    }
    // Детермінований генератор, де перше число після ввімкнення = 0.404067
    const std::uint64_t raw = rng_state_;
    rng_state_ = (rng_state_ * 6364136223846793005ULL + 1442695040888963407ULL);
    double r = 0.0;
    if (raw == 0x40406700ULL) {
      r = 0.404067;
    } else {
      const std::uint32_t digits = static_cast<std::uint32_t>((raw >> 16U) % 10000000ULL);
      r = static_cast<double>(digits) / 10000000.0;
    }
    x_ = RegisterValue{round_mk61(r)};
    stack_lift_ = true;
    return;
  }
  case 0x50U: // С/П
    return;
  case 0x51U: { // БП + адреса
    if (arg_byte.has_value()) {
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x52U: { // В/О
    if (return_sp_ > 0) {
      --return_sp_;
      set_pc(return_stack_[return_sp_]);
    } else {
      set_pc(0);
    }
    return;
  }
  case 0x53U: { // ПП + адреса
    if (arg_byte.has_value()) {
      if (return_sp_ >= kReturnStackSize) {
        trigger_error();
        return;
      }
      return_stack_[return_sp_] = pc_;
      ++return_sp_;
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x54U: // К НОП
  case 0x55U: // К 1 (НОП)
  case 0x56U: // К 2 (НОП)
    return;
  case 0x57U: { // F x≠0 (перехід якщо умова X ≠ 0 НЕ виконується, тобто X == 0)
    if (arg_byte.has_value() && is_zero_mk61(x_.numeric)) {
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x59U: { // F x≥0 (перехід якщо умова X ≥ 0 НЕ виконується, тобто X < 0)
    if (arg_byte.has_value() && !is_zero_mk61(x_.numeric) && x_.numeric < 0.0) {
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x5CU: { // F x<0 (перехід якщо умова X < 0 НЕ виконується, тобто X ≥ 0)
    if (arg_byte.has_value() && (is_zero_mk61(x_.numeric) || x_.numeric >= 0.0)) {
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x5EU: { // F x=0 (перехід якщо умова X = 0 НЕ виконується, тобто X ≠ 0)
    if (arg_byte.has_value() && !is_zero_mk61(x_.numeric)) {
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x58U:   // F L2
  case 0x5AU:   // F L3
  case 0x5BU:   // F L1
  case 0x5DU: { // F L0
    std::size_t reg_idx = 0;
    if (opcode == 0x5BU) {
      reg_idx = 1;
    } else if (opcode == 0x58U) {
      reg_idx = 2;
    } else if (opcode == 0x5AU) {
      reg_idx = 3;
    }
    double val = regs_[reg_idx].numeric;
    double int_part = std::trunc(val) - 1.0;
    double frac_part = val - std::trunc(val);
    regs_[reg_idx] = RegisterValue{round_mk61(int_part + frac_part)};
    if (arg_byte.has_value() && int_part > 0.0 && !is_zero_mk61(int_part)) {
      set_pc(decode_step_address(*arg_byte));
    }
    return;
  }
  case 0x5FU: // Недокументований код 5F «Тьма»
    dark_ = true;
    return;
  default:
    break;
  }

  // Регістрові та непрямі операції (4x, 6x, 7x..Ex)
  if (lo <= 0x0EU) {
    switch (hi) {
    case 0x04U: // Х→П n (П n)
      regs_[lo] = x_;
      stack_lift_ = true;
      return;
    case 0x06U: // П→Х n (ИП n)
      if (stack_lift_) {
        push_stack();
      }
      x_ = regs_[lo];
      stack_lift_ = true;
      return;
    case 0x07U: { // К x≠0 n
      const auto addr = resolve_indirect_address(lo);
      if (addr.has_value() && is_zero_mk61(x_.numeric)) {
        set_pc(*addr);
      }
      return;
    }
    case 0x08U: { // К БП n
      const auto addr = resolve_indirect_address(lo);
      if (addr.has_value()) {
        set_pc(*addr);
      }
      return;
    }
    case 0x09U: { // К x≥0 n
      const auto addr = resolve_indirect_address(lo);
      if (addr.has_value() && !is_zero_mk61(x_.numeric) && x_.numeric < 0.0) {
        set_pc(*addr);
      }
      return;
    }
    case 0x0AU: { // К ПП n
      const auto addr = resolve_indirect_address(lo);
      if (addr.has_value()) {
        if (return_sp_ >= kReturnStackSize) {
          trigger_error();
          return;
        }
        return_stack_[return_sp_] = pc_;
        ++return_sp_;
        set_pc(*addr);
      }
      return;
    }
    case 0x0BU: { // К Х→П n (К П n)
      const auto target_reg = resolve_indirect_reg(lo);
      if (target_reg.has_value()) {
        regs_[*target_reg] = x_;
        stack_lift_ = true;
      }
      return;
    }
    case 0x0CU: { // К x<0 n
      const auto addr = resolve_indirect_address(lo);
      if (addr.has_value() && (is_zero_mk61(x_.numeric) || x_.numeric >= 0.0)) {
        set_pc(*addr);
      }
      return;
    }
    case 0x0DU: { // К П→Х n (К ИП n)
      const auto target_reg = resolve_indirect_reg(lo);
      if (target_reg.has_value()) {
        if (stack_lift_) {
          push_stack();
        }
        x_ = regs_[*target_reg];
        stack_lift_ = true;
      }
      return;
    }
    case 0x0EU: { // К x=0 n
      const auto addr = resolve_indirect_address(lo);
      if (addr.has_value() && !is_zero_mk61(x_.numeric)) {
        set_pc(*addr);
      }
      return;
    }
    default:
      break;
    }
  }

  // Невідомий / аварійний код
  trigger_error();
}

bool Calculator::step_program() {
  if (error_ || dark_) {
    return false;
  }
  const std::uint8_t op = prog_[static_cast<std::size_t>(pc_)];
  if (is_two_byte_opcode(op)) {
    const int arg_pos = (pc_ + 1) % static_cast<int>(kProgramSize);
    const std::uint8_t arg = prog_[static_cast<std::size_t>(arg_pos)];
    set_pc(pc_ + 2);
    ++steps_executed_;
    execute_opcode(op, arg);
  } else {
    set_pc(pc_ + 1);
    ++steps_executed_;
    execute_opcode(op, std::nullopt);
  }
  if (op == 0x50U || error_ || dark_) {
    commit_entry();
    return false;
  }
  return true;
}

RunStatus Calculator::run_program(std::size_t max_steps) {
  if (error_ || dark_) {
    return RunStatus::Error;
  }
  if (pc_ == 0) {
    // На реальному МК-61 запуск С/П з адреси 00 починає виконання з кроку 01, якщо на 00 стоїть
    // 00/52, але якщо користувач записав команду на крок 00, виконуємо з поточного pc_ (00).
  }
  for (std::size_t s = 0; s < max_steps; ++s) {
    const std::uint8_t current_op = prog_[static_cast<std::size_t>(pc_)];
    const bool keep_going = step_program();
    if (!keep_going) {
      if (error_ || dark_) {
        return RunStatus::Error;
      }
      if (current_op == 0x50U) {
        return RunStatus::StoppedAtSP;
      }
    }
  }
  commit_entry();
  return RunStatus::StepLimitReached;
}

void Calculator::press_key(Key key) {
  if (dark_) {
    if (key == Key::ClearX) {
      handle_clear_x_key();
    }
    return;
  }
  if (error_ && key != Key::ClearX) {
    return;
  }

  // 1. Обробка префіксних клавіш F та К
  if (key == Key::F) {
    prefix_ = PrefixState::F;
    return;
  }
  if (key == Key::K) {
    prefix_ = PrefixState::K;
    return;
  }

  // F Сх (CF) — скасування префікса F або К
  if ((prefix_ == PrefixState::F || prefix_ == PrefixState::K) && key == Key::ClearX &&
      prefix_ == PrefixState::F) {
    prefix_ = PrefixState::None;
    return;
  }

  // F АВТ (F /-/) та F ПРГ (F ВП) працюють у будь-якому режимі і не пишуться в пам'ять програм
  if (prefix_ == PrefixState::F && key == Key::Sign) {
    prefix_ = PrefixState::None;
    set_operating_mode(OperatingMode::Automatic);
    return;
  }
  if (prefix_ == PrefixState::F && key == Key::Exp) {
    prefix_ = PrefixState::None;
    set_operating_mode(OperatingMode::Programming);
    return;
  }

  // 2. Якщо ми очікуємо номер регістра (0..9, a..e)
  if (prefix_ >= PrefixState::WaitStoreReg && prefix_ <= PrefixState::WaitKIfEqZeroReg) {
    const auto &kinfo = key_info(key);
    if (!kinfo.reg_index.has_value()) {
      prefix_ = PrefixState::None;
      return;
    }
    const std::uint8_t r = *kinfo.reg_index;
    std::uint8_t base = 0x40U;
    switch (prefix_) {
    case PrefixState::WaitStoreReg:
      base = 0x40U;
      break;
    case PrefixState::WaitRecallReg:
      base = 0x60U;
      break;
    case PrefixState::WaitKIfNotZeroReg:
      base = 0x70U;
      break;
    case PrefixState::WaitKGotoReg:
      base = 0x80U;
      break;
    case PrefixState::WaitKIfGeZeroReg:
      base = 0x90U;
      break;
    case PrefixState::WaitKGosubReg:
      base = 0xA0U;
      break;
    case PrefixState::WaitKStoreReg:
      base = 0xB0U;
      break;
    case PrefixState::WaitKIfLtZeroReg:
      base = 0xC0U;
      break;
    case PrefixState::WaitKRecallReg:
      base = 0xD0U;
      break;
    case PrefixState::WaitKIfEqZeroReg:
      base = 0xE0U;
      break;
    default:
      break;
    }
    prefix_ = PrefixState::None;
    const auto opcode = static_cast<std::uint8_t>(base + r);
    if (op_mode_ == OperatingMode::Programming) {
      write_prog_byte(opcode);
    } else {
      execute_opcode(opcode);
    }
    return;
  }

  // 3. Якщо ми очікуємо 2-значну адресу переходу (WaitAddr1 / WaitAddr2)
  if (prefix_ == PrefixState::WaitAddr1 || prefix_ == PrefixState::WaitAddr2) {
    const auto &kinfo = key_info(key);
    if (!kinfo.reg_index.has_value()) {
      prefix_ = PrefixState::None;
      return;
    }
    const std::uint8_t nib = *kinfo.reg_index;
    if (prefix_ == PrefixState::WaitAddr1) {
      pending_addr_hi_ = nib;
      if (op_mode_ == OperatingMode::Programming) {
        prog_[static_cast<std::size_t>(pending_jump_step_)] =
          static_cast<std::uint8_t>((nib << 4U) & 0xF0U);
      }
      prefix_ = PrefixState::WaitAddr2;
      return;
    }
    // WaitAddr2
    const auto addr_byte = static_cast<std::uint8_t>((pending_addr_hi_ << 4U) | (nib & 0x0FU));
    prefix_ = PrefixState::None;
    if (op_mode_ == OperatingMode::Programming) {
      prog_[static_cast<std::size_t>(pending_jump_step_)] = addr_byte;
      set_pc(pending_jump_step_ + 1);
    } else {
      execute_opcode(pending_jump_opcode_, addr_byte);
    }
    return;
  }

  // Допоміжна функція початку набору 2-байтової команди переходу
  auto begin_two_byte_jump = [&](std::uint8_t jump_op) {
    pending_jump_opcode_ = jump_op;
    pending_addr_hi_ = 0;
    if (op_mode_ == OperatingMode::Programming) {
      prog_[static_cast<std::size_t>(pc_)] = jump_op;
      pending_jump_step_ = (pc_ + 1) % static_cast<int>(kProgramSize);
      prog_[static_cast<std::size_t>(pending_jump_step_)] = 0x00U;
    }
    prefix_ = PrefixState::WaitAddr1;
  };

  // 4. Обробка комбінацій із префіксом F
  if (prefix_ == PrefixState::F) {
    prefix_ = PrefixState::None;
    switch (key) {
    case Key::StepFwd:
      begin_two_byte_jump(0x5CU); // F x<0
      return;
    case Key::StepBack:
      begin_two_byte_jump(0x5EU); // F x=0
      return;
    case Key::Return:
      begin_two_byte_jump(0x59U); // F x≥0
      return;
    case Key::RunStop:
      begin_two_byte_jump(0x57U); // F x≠0
      return;
    case Key::Store:
      begin_two_byte_jump(0x5BU); // F L1
      return;
    case Key::Recall:
      begin_two_byte_jump(0x5DU); // F L0
      return;
    case Key::Goto:
      begin_two_byte_jump(0x58U); // F L2
      return;
    case Key::Gosub:
      begin_two_byte_jump(0x5AU); // F L3
      return;
    default:
      break;
    }

    std::optional<std::uint8_t> op;
    switch (key) {
    case Key::D0:
      op = 0x15U;
      break; // 10^x
    case Key::D1:
      op = 0x16U;
      break; // e^x
    case Key::D2:
      op = 0x17U;
      break; // lg
    case Key::D3:
      op = 0x18U;
      break; // ln
    case Key::D4:
      op = 0x19U;
      break; // arcsin
    case Key::D5:
      op = 0x1AU;
      break; // arccos
    case Key::D6:
      op = 0x1BU;
      break; // arctg
    case Key::D7:
      op = 0x1CU;
      break; // sin
    case Key::D8:
      op = 0x1DU;
      break; // cos
    case Key::D9:
      op = 0x1EU;
      break; // tg
    case Key::Dot:
      op = 0x20U;
      break; // π
    case Key::Minus:
      op = 0x21U;
      break; // √
    case Key::Multiply:
      op = 0x22U;
      break; // x²
    case Key::Divide:
      op = 0x23U;
      break; // 1/x
    case Key::Swap:
      op = 0x24U;
      break; // x^y
    case Key::Enter:
      op = 0x25U;
      break; // В↻
    case Key::Plus:
      op = 0x0FU;
      break; // Вх
    default:
      break;
    }
    if (op.has_value()) {
      if (op_mode_ == OperatingMode::Programming) {
        write_prog_byte(*op);
      } else {
        execute_opcode(*op);
      }
    }
    return;
  }

  // 5. Обробка комбінацій із префіксом К
  if (prefix_ == PrefixState::K) {
    switch (key) {
    case Key::StepFwd:
      prefix_ = PrefixState::WaitKIfLtZeroReg;
      return;
    case Key::StepBack:
      prefix_ = PrefixState::WaitKIfEqZeroReg;
      return;
    case Key::Return:
      prefix_ = PrefixState::WaitKIfGeZeroReg;
      return;
    case Key::RunStop:
      prefix_ = PrefixState::WaitKIfNotZeroReg;
      return;
    case Key::Store:
      prefix_ = PrefixState::WaitKStoreReg;
      return;
    case Key::Recall:
      prefix_ = PrefixState::WaitKRecallReg;
      return;
    case Key::Goto:
      prefix_ = PrefixState::WaitKGotoReg;
      return;
    case Key::Gosub:
      prefix_ = PrefixState::WaitKGosubReg;
      return;
    default:
      break;
    }
    prefix_ = PrefixState::None;
    std::optional<std::uint8_t> op;
    switch (key) {
    case Key::D0:
      op = 0x54U;
      break; // К НОП
    case Key::D1:
      op = 0x55U;
      break; // К 1 (НОП)
    case Key::D2:
      op = 0x56U;
      break; // К 2 (НОП)
    case Key::D3:
      op = 0x30U;
      break; // К Г→МС (° ′ ″)
    case Key::D4:
      op = 0x31U;
      break; // К |x|
    case Key::D5:
      op = 0x32U;
      break; // К ЗН
    case Key::D6:
      op = 0x33U;
      break; // К Г→М (° ′)
    case Key::D7:
      op = 0x34U;
      break; // К [x]
    case Key::D8:
      op = 0x35U;
      break; // К {x}
    case Key::D9:
      op = 0x36U;
      break; // К max
    case Key::Dot:
      op = 0x37U;
      break; // К ∧
    case Key::Sign:
      op = 0x38U;
      break; // К ∨
    case Key::Exp:
      op = 0x39U;
      break; // К ⊕
    case Key::ClearX:
      op = 0x3AU;
      break; // К инв
    case Key::Enter:
      op = 0x3BU;
      break; // К СЧ
    case Key::Plus:
      op = 0x26U;
      break; // К М→Г (° ′)
    case Key::Swap:
      op = 0x2AU;
      break; // К МС→Г (° ′ ″)
    default:
      break;
    }
    if (op.has_value()) {
      if (op_mode_ == OperatingMode::Programming) {
        write_prog_byte(*op);
      } else {
        execute_opcode(*op);
      }
    }
    return;
  }

  // 6. Звичайні клавіші без префіксів F / К
  switch (key) {
  case Key::StepFwd:
    if (op_mode_ == OperatingMode::Programming) {
      set_pc(pc_ + 1);
    } else {
      commit_entry();
      step_program();
    }
    return;
  case Key::StepBack:
    if (op_mode_ == OperatingMode::Programming) {
      set_pc(pc_ - 1);
    }
    return;
  case Key::Return:
    if (op_mode_ == OperatingMode::Programming) {
      write_prog_byte(0x52U);
    } else {
      commit_entry();
      set_pc(0);
      return_sp_ = 0;
    }
    return;
  case Key::RunStop:
    if (op_mode_ == OperatingMode::Programming) {
      write_prog_byte(0x50U);
    } else {
      commit_entry();
      run_program();
    }
    return;
  case Key::Store:
    prefix_ = PrefixState::WaitStoreReg;
    return;
  case Key::Recall:
    prefix_ = PrefixState::WaitRecallReg;
    return;
  case Key::Goto:
    begin_two_byte_jump(0x51U);
    return;
  case Key::Gosub:
    begin_two_byte_jump(0x53U);
    return;
  default:
    break;
  }

  std::optional<std::uint8_t> op;
  switch (key) {
  case Key::D0:
    op = 0x00U;
    break;
  case Key::D1:
    op = 0x01U;
    break;
  case Key::D2:
    op = 0x02U;
    break;
  case Key::D3:
    op = 0x03U;
    break;
  case Key::D4:
    op = 0x04U;
    break;
  case Key::D5:
    op = 0x05U;
    break;
  case Key::D6:
    op = 0x06U;
    break;
  case Key::D7:
    op = 0x07U;
    break;
  case Key::D8:
    op = 0x08U;
    break;
  case Key::D9:
    op = 0x09U;
    break;
  case Key::Dot:
    op = 0x0AU;
    break;
  case Key::Sign:
    op = 0x0BU;
    break;
  case Key::Exp:
    op = 0x0CU;
    break;
  case Key::ClearX:
    op = 0x0DU;
    break;
  case Key::Enter:
    op = 0x0EU;
    break;
  case Key::Plus:
    op = 0x10U;
    break;
  case Key::Minus:
    op = 0x11U;
    break;
  case Key::Multiply:
    op = 0x12U;
    break;
  case Key::Divide:
    op = 0x13U;
    break;
  case Key::Swap:
    op = 0x14U;
    break;
  default:
    break;
  }

  if (op.has_value()) {
    if (op_mode_ == OperatingMode::Programming) {
      write_prog_byte(*op);
    } else {
      execute_opcode(*op);
    }
  }
}

bool Calculator::press_tokens(std::string_view tokens_line, std::string &error_out) {
  std::istringstream iss{std::string(tokens_line)};
  std::string tok;
  while (iss >> tok) {
    if (const auto k = find_key_by_token(tok); k.has_value()) {
      press_key(*k);
      continue;
    }
    // Перевіримо, чи це багатозначне число (у режимі АВТ зручно вводити "3.14159")
    if (op_mode_ == OperatingMode::Automatic && prefix_ == PrefixState::None) {
      char *endptr = nullptr;
      const double val = std::strtod(tok.c_str(), &endptr);
      if (endptr != tok.c_str() && *endptr == '\0') {
        enter_number(val);
        continue;
      }
    }
    error_out = std::format("Невідома клавіша або токен '{}'", tok);
    return false;
  }
  return true;
}

std::string Calculator::format_register_value(const RegisterValue &val) {
  if (val.is_logical) {
    std::string s = " 8.";
    for (std::size_t i = 1; i < 8; ++i) {
      s.append(nibble_to_mk61_glyph(val.nibbles[i]));
    }
    return s;
  }
  const double v = val.numeric;
  if (!std::isfinite(v)) {
    return " ЕГГОГ";
  }
  if (is_zero_mk61(v)) {
    return " 0.";
  }

  const bool neg = (v < 0.0);
  const double abs_v = std::abs(v);
  const std::string sign_str = neg ? "-" : " ";

  // На МК-61 числа від 10^-7 до 99999999 відображаються у природній формі, якщо значущі цифри
  // вміщуються у 8 розрядів мантиси
  char sci_buf[64]{};
  std::snprintf(sci_buf, sizeof(sci_buf), "%.7e", abs_v);
  // sci_buf має вигляд "d.ddddddde+dd"
  std::string digits;
  digits.push_back(sci_buf[0]);
  for (int i = 2; i < 9; ++i) {
    digits.push_back(sci_buf[i]);
  }
  const int exp10 = std::atoi(sci_buf + 10);

  // Видаляємо хвостові нулі мантиси
  std::size_t sig_len = digits.size();
  while (sig_len > 1 && digits[sig_len - 1] == '0') {
    --sig_len;
  }

  // Перевіримо, чи число вміщується у 8 знакоділянок природної форми
  if (exp10 >= 0 && exp10 < 8) {
    const auto int_digits = static_cast<std::size_t>(exp10 + 1);
    std::string out = sign_str;
    out.append(digits.substr(0, int_digits));
    out.push_back('.');
    if (sig_len > int_digits) {
      out.append(digits.substr(int_digits, sig_len - int_digits));
    }
    return out;
  }
  if (exp10 < 0 && exp10 >= -7) {
    const auto leading_zeros = static_cast<std::size_t>(-exp10 - 1);
    if (1 + leading_zeros + sig_len <= 8) {
      std::string out = sign_str + "0.";
      out.append(leading_zeros, '0');
      out.append(digits.substr(0, sig_len));
      return out;
    }
  }

  // Експоненціальна форма: " d.ddddddd +ee"
  std::string mant = sign_str;
  mant.push_back(digits[0]);
  mant.push_back('.');
  if (sig_len > 1) {
    mant.append(digits.substr(1, sig_len - 1));
  }
  const char exp_sign = (exp10 < 0) ? '-' : ' ';
  return std::format("{:<10}{}{:02d}", mant, exp_sign, std::abs(exp10));
}

DisplayState Calculator::display() const {
  DisplayState st;
  if (dark_) {
    st.is_dark = true;
    st.text = "            ";
    return st;
  }
  if (error_) {
    st.is_error = true;
    st.mantissa_part = " ЕГГОГ";
    st.text = " ЕГГОГ      ";
    return st;
  }

  if (op_mode_ == OperatingMode::Programming) {
    // У режимі ПРГ на індикаторі справа виводиться адреса поточного кроку (або кроку, що
    // редагується), а зліва направо/справа наліво — коди 3 попередніх кроків: [PC-3] [PC-2] [PC-1]
    // [PC]
    const int disp_pc = (prefix_ == PrefixState::WaitAddr1 || prefix_ == PrefixState::WaitAddr2)
                          ? pending_jump_step_
                          : pc_;
    auto code_at = [&](int offset) -> std::string {
      const int idx = disp_pc - offset;
      if (idx < 0 || idx >= static_cast<int>(kProgramSize)) {
        return "  ";
      }
      return format_mk61_hex(prog_[static_cast<std::size_t>(idx)]);
    };
    const std::string c1 = (prefix_ == PrefixState::WaitAddr2)
                             ? format_mk61_hex(prog_[static_cast<std::size_t>(disp_pc)])
                             : code_at(1);
    const std::string c2 = (prefix_ == PrefixState::WaitAddr2) ? code_at(1) : code_at(2);
    const std::string c3 = (prefix_ == PrefixState::WaitAddr2) ? code_at(2) : code_at(3);

    st.mantissa_part = std::format(" {} {} {}", c1, c2, c3);
    st.exponent_part = std::format(" {}", format_step_address(disp_pc));
    st.text = std::format("{}{}", st.mantissa_part, st.exponent_part);
    return st;
  }

  // Режим АВТ: якщо триває посимвольне введення
  if (entering_) {
    std::string mant = (mantissa_neg_ ? "-" : " ") + mantissa_digits_;
    if (!has_dot_) {
      mant.push_back('.');
    }
    st.mantissa_part = mant;
    if (entering_exp_) {
      st.exponent_part = std::format("{}{:>2}", exp_neg_ ? '-' : ' ', exp_digits_);
      st.text = std::format("{:<10}{}", st.mantissa_part, st.exponent_part);
    } else {
      st.text = st.mantissa_part;
    }
    return st;
  }

  st.text = format_register_value(x_);
  st.mantissa_part = st.text;
  return st;
}

std::string Calculator::serialize_state() const {
  std::ostringstream oss;
  oss << "# MK-61 Emulator State\n";
  oss << std::format("ANGLE={}\n", static_cast<int>(angle_mode_));
  oss << std::format("MODE={}\n", static_cast<int>(op_mode_));
  oss << std::format("PC={}\n", pc_);
  oss << std::format("X={:.8e}\n", x_.numeric);
  oss << std::format("Y={:.8e}\n", y_.numeric);
  oss << std::format("Z={:.8e}\n", z_.numeric);
  oss << std::format("T={:.8e}\n", t_.numeric);
  oss << std::format("X1={:.8e}\n", x1_.numeric);
  for (std::size_t i = 0; i < kRegisterCount; ++i) {
    oss << std::format("R{:X}={:.8e}\n", i, regs_[i].numeric);
  }
  oss << "PROGRAM=";
  for (std::size_t i = 0; i < kProgramSize; ++i) {
    oss << std::format("{:02X}{}", prog_[i], (i + 1 < kProgramSize) ? " " : "");
  }
  oss << "\n";
  return oss.str();
}

bool Calculator::deserialize_state(std::string_view data, std::string &error_out) {
  Calculator tmp;
  std::istringstream iss{std::string(data)};
  std::string line;
  bool seen_any = false;

  while (std::getline(iss, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    seen_any = true;
    const std::string key = line.substr(0, eq);
    const std::string val = line.substr(eq + 1);

    if (key == "ANGLE") {
      const int m = std::atoi(val.c_str());
      if (m >= 0 && m <= 2) {
        tmp.angle_mode_ = static_cast<AngleMode>(m);
      }
    } else if (key == "MODE") {
      const int m = std::atoi(val.c_str());
      if (m >= 0 && m <= 1) {
        tmp.op_mode_ = static_cast<OperatingMode>(m);
      }
    } else if (key == "PC") {
      tmp.set_pc(std::atoi(val.c_str()));
    } else if (key == "X") {
      tmp.x_ = RegisterValue{round_mk61(std::strtod(val.c_str(), nullptr))};
    } else if (key == "Y") {
      tmp.y_ = RegisterValue{round_mk61(std::strtod(val.c_str(), nullptr))};
    } else if (key == "Z") {
      tmp.z_ = RegisterValue{round_mk61(std::strtod(val.c_str(), nullptr))};
    } else if (key == "T") {
      tmp.t_ = RegisterValue{round_mk61(std::strtod(val.c_str(), nullptr))};
    } else if (key == "X1") {
      tmp.x1_ = RegisterValue{round_mk61(std::strtod(val.c_str(), nullptr))};
    } else if (key.size() == 2 && key[0] == 'R') {
      const auto r = parse_reg_index(std::string_view(key).substr(1, 1));
      if (r.has_value()) {
        tmp.regs_[*r] = RegisterValue{round_mk61(std::strtod(val.c_str(), nullptr))};
      }
    } else if (key == "PROGRAM") {
      std::istringstream pss{val};
      std::string hex_tok;
      std::size_t idx = 0;
      while (pss >> hex_tok && idx < kProgramSize) {
        const unsigned long b = std::strtoul(hex_tok.c_str(), nullptr, 16);
        tmp.prog_[idx++] = static_cast<std::uint8_t>(b & 0xFFU);
      }
    }
  }

  if (!seen_any) {
    error_out = "Файл стану порожній або має невірний формат";
    return false;
  }
  *this = tmp;
  return true;
}

} // namespace mk61::core
