#include "core/calculator.hpp"
#include "core/opcodes.hpp"
#include "core/presets.hpp"
#include "core/types.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cstdint>
#include <initializer_list>
#include <string>

using Catch::Approx;
using mk61::core::AngleMode;
using mk61::core::Calculator;
using mk61::core::Key;
using mk61::core::OperatingMode;

namespace {

void tap(Calculator &calc, std::initializer_list<Key> keys) {
  for (const Key key : keys) {
    calc.press_key(key);
  }
}

void expect_bytes(const Calculator &calc, std::initializer_list<std::uint8_t> bytes) {
  REQUIRE(calc.pc() == static_cast<int>(bytes.size()));
  int index = 0;
  for (const std::uint8_t byte : bytes) {
    REQUIRE(calc.program_memory()[static_cast<std::size_t>(index)] == byte);
    ++index;
  }
}

void as_prg(Calculator &calc) {
  calc.reset();
  calc.set_operating_mode(OperatingMode::Programming);
}

void load_stack(Calculator &calc, double t, double z, double y, double x) {
  calc.set_reg_t(t);
  calc.set_reg_z(z);
  calc.set_reg_y(y);
  calc.set_reg_x(x);
}

} // namespace

TEST_CASE("RPN stack, number entry, and T register duplication on drop", "[stack]") {
  Calculator calc;

  // Набір "2 В↑ 3 +" через фізичні клавіші
  calc.press_key(Key::D2);
  calc.press_key(Key::Enter);
  calc.press_key(Key::D3);
  calc.press_key(Key::Plus);

  REQUIRE(calc.reg_x() == Approx(5.0));
  REQUIRE(calc.reg_x1() == Approx(3.0));

  // Перевіримо збереження (розмноження) регістра T при опусканні стека
  calc.reset();
  std::string err;
  REQUIRE(calc.press_tokens("10 В↑ 20 В↑ 30 В↑ 40", err));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x() == Approx(40.0));

  // При додаванні (30 + 40 = 70) стек опускається, але T залишається 10 і дублюється в Z!
  calc.press_key(Key::Plus);
  REQUIRE(calc.reg_x() == Approx(70.0));
  REQUIRE(calc.reg_y() == Approx(20.0));
  REQUIRE(calc.reg_z() == Approx(10.0));
  REQUIRE(calc.reg_t() == Approx(10.0));

  // В↑ вимикає автопідняття для наступної цифри, але П→Х стек піднімає все одно.
  // 2 В↑ залишає Y=2, X=2. П→Х 0 піднімає ще раз, X стає 5, додавання дає 7, Y лишається 2.
  calc.clear_registers();
  calc.set_memory_reg(0, 5.0);
  REQUIRE(calc.press_tokens("2 В↑ П→Х 0 +", err));
  REQUIRE(calc.reg_x() == Approx(7.0));
  REQUIRE(calc.reg_y() == Approx(2.0));
}

TEST_CASE("Scientific functions, x^y order, and angle modes", "[math]") {
  Calculator calc;
  std::string err;

  // На МК-61 F x^y — це X^Y, стек не опускається: 10 В↑ 2 F x^y дає 2^10, Y лишається 10
  REQUIRE(calc.press_tokens("10 В↑ 2 F ↔", err));
  REQUIRE(calc.reg_x() == Approx(1024.0));
  REQUIRE(calc.reg_y() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(2.0));

  // Градуси (ГРД): sin(30°) = 0.5, cos(60°) = 0.5
  calc.reset();
  calc.set_angle_mode(AngleMode::Degrees);
  REQUIRE(calc.press_tokens("30 F 7", err)); // F 7 = sin
  REQUIRE(calc.reg_x() == Approx(0.5));

  // Гради (Г): sin(100 град) = 1.0
  calc.set_angle_mode(AngleMode::Grads);
  REQUIRE(calc.press_tokens("100 F 7", err));
  REQUIRE(calc.reg_x() == Approx(1.0));

  // Радіани (Р): sin(π / 2) = 1.0
  calc.set_angle_mode(AngleMode::Radians);
  REQUIRE(calc.press_tokens("F . 2 / F 7", err)); // F . = π
  REQUIRE(calc.reg_x() == Approx(1.0));
}

TEST_CASE("K-prefix functions: max without stack drop, HMS conversions, bitwise 8.xxxxxxx",
          "[k_ops]") {
  Calculator calc;
  std::string err;

  // К max (К 9) записує max(X, Y) у X, але НЕ опускає стек (Y зберігається!)
  REQUIRE(calc.press_tokens("15 В↑ 8 К 9", err));
  REQUIRE(calc.reg_x() == Approx(15.0));
  REQUIRE(calc.reg_y() == Approx(15.0));
  REQUIRE(calc.reg_x1() == Approx(8.0));

  // Переведення кутів: 26° 30' 36'' (26.3036) -> К МС→Г (К ↔) -> 26.51°
  calc.reset();
  REQUIRE(calc.press_tokens("26.3036 К ↔", err));
  REQUIRE(calc.reg_x() == Approx(26.51));

  // Зворотне переведення: 26.51° -> К Г→МС (К 3) -> 26.3036
  REQUIRE(calc.press_tokens("К 3", err));
  REQUIRE(calc.reg_x() == Approx(26.3036));

  // Якщо мінути >= 60, К М→Г (К +) видає ЕГГОГ
  calc.reset();
  REQUIRE(calc.press_tokens("12.65 К +", err));
  REQUIRE(calc.is_error());
  calc.press_key(Key::ClearX); // Сх скидає ЕГГОГ
  REQUIRE(!calc.is_error());

  // Порозрядна інверсія К инв (К Сх): 123456 -> " 8.ГCL-9  "
  REQUIRE(calc.press_tokens("123456 К Сх", err));
  REQUIRE(calc.display().text == " 8.ГCL-9  ");
}

TEST_CASE("Stack matches the real MK-61 for swap, rotate, pi, power, sign and logic", "[stack]") {
  Calculator calc;
  std::string err;

  REQUIRE(calc.press_tokens("1 В↑ 2 В↑ 3 В↑ 4 <->", err));
  REQUIRE(calc.reg_x() == Approx(3.0));
  REQUIRE(calc.reg_y() == Approx(4.0));
  REQUIRE(calc.reg_z() == Approx(2.0));
  REQUIRE(calc.reg_t() == Approx(1.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  REQUIRE(calc.press_tokens("1 В↑ 2 В↑ 3 В↑ 4 F В↑", err));
  REQUIRE(calc.reg_x() == Approx(3.0));
  REQUIRE(calc.reg_y() == Approx(2.0));
  REQUIRE(calc.reg_z() == Approx(1.0));
  REQUIRE(calc.reg_t() == Approx(4.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  REQUIRE(calc.press_tokens("4 В↑ F .", err));
  REQUIRE(calc.reg_x() == Approx(3.1415926));
  REQUIRE(calc.reg_y() == Approx(4.0));
  REQUIRE(calc.reg_z() == Approx(4.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  REQUIRE(calc.press_tokens("5 В↑ /-/ 2", err));
  REQUIRE(calc.reg_x() == Approx(2.0));
  REQUIRE(calc.reg_y() == Approx(5.0));

  calc.reset();
  REQUIRE(calc.press_tokens("1 В↑ 2 В↑ 12 В↑ 5 К .", err));
  REQUIRE(calc.reg_y() == Approx(12.0));
  REQUIRE(calc.reg_z() == Approx(2.0));
  REQUIRE(calc.reg_t() == Approx(1.0));
  REQUIRE(calc.reg_x1() == Approx(5.0));
}

TEST_CASE("K SCH replaces X, keeps Y Z T, and arms the next digit lift", "[k_ops]") {
  Calculator calc;
  std::string err;

  REQUIRE(calc.press_tokens("10 В↑ 20 В↑ 30 В↑ 40", err));
  calc.press_key(Key::K);
  calc.press_key(Key::Enter);

  REQUIRE(calc.reg_x() == Approx(0.404067));
  REQUIRE(calc.reg_x1() == Approx(40.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));

  const double first = calc.reg_x();
  calc.press_key(Key::K);
  calc.press_key(Key::Enter);
  REQUIRE(calc.reg_x1() == Approx(first));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));

  const double second = calc.reg_x();
  calc.press_key(Key::D7);
  REQUIRE(calc.reg_x() == Approx(7.0));
  REQUIRE(calc.reg_y() == Approx(second));
  REQUIRE(calc.reg_z() == Approx(30.0));
  REQUIRE(calc.reg_t() == Approx(20.0));
}

TEST_CASE("Indirect addressing auto-decrement (R0..R3) and auto-increment (R4..R6)", "[indirect]") {
  Calculator calc;
  std::string err;

  // R4 (група 4..6) спочатку збільшується на 1, а потім використовується як адреса регістра
  calc.set_memory_reg(4, 7.0);
  calc.set_memory_reg(8, 99.5);
  REQUIRE(calc.press_tokens("К П→Х 4", err));
  REQUIRE(calc.memory_reg(4) == Approx(8.0));
  REQUIRE(calc.reg_x() == Approx(99.5));

  // R0 (група 0..3) спочатку зменшується на 1, а потім використовується як адреса регістра
  calc.set_memory_reg(0, 11.0); // Після декременту стане 10 -> регістр RA (a)
  calc.set_memory_reg(10, 42.25);
  REQUIRE(calc.press_tokens("К П→Х 0", err));
  REQUIRE(calc.memory_reg(0) == Approx(10.0));
  REQUIRE(calc.reg_x() == Approx(42.25));
}

TEST_CASE("Conditional jumps polarity, loops F L0..L3, and 5-level subroutine stack", "[program]") {
  Calculator calc;

  // Перевіримо полярність умовного переходу МК-61:
  // F x=0 переходить на вказану адресу, коли умова X=0 НЕ виконується (тобто X != 0)!
  const auto res = calc.assemble_and_load(R"(
    F x=0 not_zero
    100
    С/П
    not_zero:
    200
    С/П
  )");
  REQUIRE(res.ok);

  // 1) При X = 0 умова виконується -> перехід НЕ відбувається -> отримуємо 100
  calc.enter_number(0.0);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(100.0));

  // 2) При X = 5 умова НЕ виконується -> відбувається перехід на not_zero -> отримуємо 200
  calc.enter_number(5.0);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(200.0));

  // Перевіримо переповнення 5-рівневого стека підпрограм ПП (6-й вкладений виклик -> ЕГГОГ)
  const auto sub_res = calc.assemble_and_load(R"(
    ПП s1 С/П
    s1: ПП s2 В/О
    s2: ПП s3 В/О
    s3: ПП s4 В/О
    s4: ПП s5 В/О
    s5: ПП s6 В/О
    s6: В/О
  )");
  REQUIRE(sub_res.ok);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::Error);
  REQUIRE(calc.is_error());
}

TEST_CASE("All built-in preset programs assemble and execute accurately", "[presets]") {
  Calculator calc;

  // 1. Факторіал: 6! = 720
  {
    const auto *p = mk61::core::find_builtin_program("factorial");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.enter_number(6.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(720.0));
  }

  // 2. Квадратне рівняння: x^2 - 5x + 6 = 0 -> x1 = 3, x2 = 2
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("quadratic");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 1.0);
    calc.set_memory_reg(2, -5.0);
    calc.set_memory_reg(3, 6.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(3.0));
    REQUIRE(calc.reg_y() == Approx(2.0));
  }

  // 3. Гіпотенуза: катети 3 та 4 -> гіпотенуза 5
  {
    calc.reset();
    calc.set_angle_mode(AngleMode::Degrees);
    const auto *p = mk61::core::find_builtin_program("hypotenuse");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.enter_number(3.0);
    calc.press_key(Key::Enter);
    calc.enter_number(4.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(5.0));
    REQUIRE(calc.reg_y() == Approx(53.130102).margin(1e-4));
  }

  // 4. Сума масиву через непряму адресацію К П→Х 4: 10+20+30+40+50 = 150
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("indirect_sum");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(5, 10.0);
    calc.set_memory_reg(6, 20.0);
    calc.set_memory_reg(7, 30.0);
    calc.set_memory_reg(8, 40.0);
    calc.set_memory_reg(9, 50.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(150.0));
  }

  // 5. Інтеграл Сімпсона для sin(x) на [0, π]: (π/6)*(0 + 4*1 + 0) = 2π/3 ≈ 2.0943951
  {
    calc.reset();
    calc.set_angle_mode(AngleMode::Radians);
    const auto *p = mk61::core::find_builtin_program("simpson");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(2.0943951).margin(1e-5));
  }

  // 6. Гра «Лунолёт-1»
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("moon_lander");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 1000.0);
    calc.set_memory_reg(2, -50.0);
    calc.set_memory_reg(3, 250.0);
    calc.enter_number(10.0); // Витрата 10 кг палива: прискорення a = 20 - 1.62 = 18.38 м/с^2
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(959.19));
    REQUIRE(calc.memory_reg(2) == Approx(-31.62));
    REQUIRE(calc.memory_reg(3) == Approx(240.0));
  }

  // 6b. Лунолёт-2: у польоті крок такий самий, під нулем повторний пуск нічого не міняє
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("moon_lander_2");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 1000.0);
    calc.set_memory_reg(2, -50.0);
    calc.set_memory_reg(3, 250.0);
    calc.enter_number(10.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(959.19));
    REQUIRE(calc.memory_reg(1) == Approx(959.19));

    calc.enter_number(0.0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(926.76));

    calc.reset();
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 10.0);
    calc.set_memory_reg(2, -50.0);
    calc.set_memory_reg(3, 250.0);
    calc.enter_number(0.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() < 0.0);
    const double crashed_h = calc.memory_reg(1);
    const double crashed_v = calc.memory_reg(2);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.memory_reg(1) == Approx(crashed_h));
    REQUIRE(calc.memory_reg(2) == Approx(crashed_v));
    REQUIRE(calc.reg_x() == Approx(crashed_h));
  }

  // 7. Табличний формат 10x10 зі скріншота (Пасхалія Гаусса, 98 кроків, включно з В/0 та x>=0 на
  // межі рядків)
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("easter");
    REQUIRE(p != nullptr);
    const auto res = calc.assemble_and_load(p->source_code);
    REQUIRE(res.ok);
    REQUIRE(res.steps_used == 98);

    // Перевіримо кругове форматування у таблицю 10x10 і повторне асемблювання
    const std::string grid = mk61::core::format_program_compact_grid(calc.program_memory());
    REQUIRE(calc.assemble_and_load(grid).ok);

    // 2026 рік -> Великдень 12 квітня (X = 4, Y = 12)
    calc.enter_number(2026.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(4.0));
    REQUIRE(calc.reg_y() == Approx(12.0));
  }

  REQUIRE(mk61::core::builtin_programs().size() == 14);
  REQUIRE(mk61::core::find_builtin_program("9")->id == "gcd");
  REQUIRE(mk61::core::find_builtin_program("10")->id == "fibonacci");
  REQUIRE(mk61::core::find_builtin_program("14")->id == "bisect");
  REQUIRE(mk61::core::find_builtin_program("15") == nullptr);
  REQUIRE(mk61::core::find_builtin_program("08")->id == "moon_lander_2");

  // 8. НСД: 48 і 18 → 6, а нуль у дільнику лишає саме ділене
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("gcd");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.enter_number(48.0);
    calc.press_key(Key::Enter);
    calc.enter_number(18.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(6.0));

    calc.enter_number(17.0);
    calc.press_key(Key::Enter);
    calc.enter_number(5.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(1.0));

    calc.enter_number(7.0);
    calc.press_key(Key::Enter);
    calc.enter_number(0.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(7.0));
  }

  // 9. Фібоначчі: F(1)=1, F(6)=8, F(10)=55
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("fibonacci");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.enter_number(1.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(1.0));

    calc.enter_number(6.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(8.0));

    calc.enter_number(10.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(55.0));
  }

  // 10. Герон: 3, 4, 5 → 6; 5, 5, 6 → 12
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("heron");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 3.0);
    calc.set_memory_reg(2, 4.0);
    calc.set_memory_reg(3, 5.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(6.0));

    calc.set_memory_reg(1, 5.0);
    calc.set_memory_reg(2, 5.0);
    calc.set_memory_reg(3, 6.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(12.0));
  }

  // 11. Полярні координати в градусах і зворотний перехід
  {
    calc.reset();
    calc.set_angle_mode(AngleMode::Degrees);
    const auto *p = mk61::core::find_builtin_program("polar");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(0, 0.0);
    calc.enter_number(3.0);
    calc.press_key(Key::Enter);
    calc.enter_number(4.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(5.0));
    REQUIRE(calc.reg_y() == Approx(53.130102).margin(1e-4));
    const double angle = calc.reg_y();

    calc.set_memory_reg(0, 1.0);
    calc.enter_number(5.0);
    calc.press_key(Key::Enter);
    calc.enter_number(angle);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(4.0).margin(1e-4));
    REQUIRE(calc.reg_y() == Approx(3.0).margin(1e-4));

    calc.set_memory_reg(0, 0.0);
    calc.enter_number(-3.0);
    calc.press_key(Key::Enter);
    calc.enter_number(4.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(5.0));
    REQUIRE(calc.reg_y() == Approx(126.869898).margin(1e-3));

    calc.enter_number(0.0);
    calc.press_key(Key::Enter);
    calc.enter_number(5.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(5.0));
    REQUIRE(calc.reg_y() == Approx(90.0));

    calc.enter_number(0.0);
    calc.press_key(Key::Enter);
    calc.enter_number(-5.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(5.0));
    REQUIRE(calc.reg_y() == Approx(-90.0));
  }

  // 12. Система: 2x + y = 5, x − y = 1 → x=2, y=1. Паралельні прямі — помилка.
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("linear2");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 2.0);
    calc.set_memory_reg(2, 1.0);
    calc.set_memory_reg(3, 5.0);
    calc.set_memory_reg(4, 1.0);
    calc.set_memory_reg(5, -1.0);
    calc.set_memory_reg(6, 1.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(2.0));
    REQUIRE(calc.reg_y() == Approx(1.0));

    calc.reset();
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(1, 1.0);
    calc.set_memory_reg(2, 1.0);
    calc.set_memory_reg(3, 1.0);
    calc.set_memory_reg(4, 2.0);
    calc.set_memory_reg(5, 2.0);
    calc.set_memory_reg(6, 3.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::Error);
    REQUIRE(calc.is_error());
  }

  // 13. Половинне ділення: один крок із [1, 2] дає 1.5, двадцять кроків сідають на √2
  {
    calc.reset();
    const auto *p = mk61::core::find_builtin_program("bisect");
    REQUIRE(p != nullptr);
    REQUIRE(calc.assemble_and_load(p->source_code).ok);
    calc.set_memory_reg(0, 1.0);
    calc.set_memory_reg(1, 1.0);
    calc.set_memory_reg(2, 2.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() == Approx(1.5));
    REQUIRE(calc.memory_reg(2) == Approx(1.5));

    calc.set_memory_reg(0, 20.0);
    calc.set_memory_reg(1, 1.0);
    calc.set_memory_reg(2, 2.0);
    calc.set_pc(0);
    REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
    REQUIRE(calc.reg_x() > 1.0);
    REQUIRE(calc.reg_x() < 2.0);
    REQUIRE(calc.reg_x() * calc.reg_x() == Approx(2.0).margin(1e-5));
  }
}

TEST_CASE("Every key writes the real MK-61 opcode in programming mode", "[keys]") {
  Calculator calc;

  as_prg(calc);
  tap(calc,
      {Key::D0, Key::D1, Key::D2, Key::D3, Key::D4, Key::D5, Key::D6, Key::D7, Key::D8, Key::D9});
  expect_bytes(calc, {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09});

  as_prg(calc);
  tap(calc, {Key::Dot, Key::Sign, Key::Exp, Key::ClearX, Key::Enter, Key::Plus, Key::Minus,
             Key::Multiply, Key::Divide, Key::Swap, Key::Return, Key::RunStop});
  expect_bytes(calc, {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x10, 0x11, 0x12, 0x13, 0x14, 0x52, 0x50});

  as_prg(calc);
  tap(calc, {Key::Goto, Key::D1, Key::D2});
  expect_bytes(calc, {0x51, 0x12});
  as_prg(calc);
  tap(calc, {Key::Gosub, Key::Dot, Key::D4});
  expect_bytes(calc, {0x53, 0xA4});

  as_prg(calc);
  tap(calc, {Key::Store, Key::D0, Key::Store, Key::Enter, Key::Recall, Key::Dot, Key::Recall,
             Key::Sign, Key::Recall, Key::Exp, Key::Recall, Key::ClearX});
  expect_bytes(calc, {0x40, 0x4E, 0x6A, 0x6B, 0x6C, 0x6D});

  as_prg(calc);
  tap(calc, {Key::F, Key::D0, Key::F, Key::D1, Key::F, Key::D2, Key::F, Key::D3, Key::F, Key::D4,
             Key::F, Key::D5, Key::F, Key::D6, Key::F, Key::D7, Key::F, Key::D8, Key::F, Key::D9});
  expect_bytes(calc, {0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E});

  as_prg(calc);
  tap(calc, {Key::F, Key::Dot, Key::F, Key::Minus, Key::F, Key::Multiply, Key::F, Key::Divide,
             Key::F, Key::Swap, Key::F, Key::Enter, Key::F, Key::Plus});
  expect_bytes(calc, {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x0F});

  as_prg(calc);
  tap(calc, {Key::F, Key::StepFwd, Key::D0, Key::D0});
  expect_bytes(calc, {0x5C, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::StepBack, Key::D0, Key::D0});
  expect_bytes(calc, {0x5E, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::Return, Key::D0, Key::D0});
  expect_bytes(calc, {0x59, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::RunStop, Key::D0, Key::D0});
  expect_bytes(calc, {0x57, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::Store, Key::D0, Key::D0});
  expect_bytes(calc, {0x5B, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::Recall, Key::D0, Key::D0});
  expect_bytes(calc, {0x5D, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::Goto, Key::D0, Key::D0});
  expect_bytes(calc, {0x58, 0x00});
  as_prg(calc);
  tap(calc, {Key::F, Key::Gosub, Key::D0, Key::D0});
  expect_bytes(calc, {0x5A, 0x00});

  as_prg(calc);
  tap(calc, {Key::K, Key::D0, Key::K, Key::D1, Key::K, Key::D2, Key::K, Key::D3, Key::K, Key::D4,
             Key::K, Key::D5, Key::K, Key::D6, Key::K, Key::D7, Key::K, Key::D8, Key::K, Key::D9});
  expect_bytes(calc, {0x54, 0x55, 0x56, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36});

  as_prg(calc);
  tap(calc, {Key::K, Key::Dot, Key::K, Key::Sign, Key::K, Key::Exp, Key::K, Key::ClearX, Key::K,
             Key::Enter, Key::K, Key::Plus, Key::K, Key::Swap});
  expect_bytes(calc, {0x37, 0x38, 0x39, 0x3A, 0x3B, 0x26, 0x2A});

  as_prg(calc);
  tap(calc, {Key::K, Key::StepFwd, Key::D4});
  expect_bytes(calc, {0xC4});
  as_prg(calc);
  tap(calc, {Key::K, Key::StepBack, Key::D4});
  expect_bytes(calc, {0xE4});
  as_prg(calc);
  tap(calc, {Key::K, Key::Return, Key::D4});
  expect_bytes(calc, {0x94});
  as_prg(calc);
  tap(calc, {Key::K, Key::RunStop, Key::D4});
  expect_bytes(calc, {0x74});
  as_prg(calc);
  tap(calc, {Key::K, Key::Store, Key::D4});
  expect_bytes(calc, {0xB4});
  as_prg(calc);
  tap(calc, {Key::K, Key::Recall, Key::D4});
  expect_bytes(calc, {0xD4});
  as_prg(calc);
  tap(calc, {Key::K, Key::Goto, Key::D4});
  expect_bytes(calc, {0x84});
  as_prg(calc);
  tap(calc, {Key::K, Key::Gosub, Key::D4});
  expect_bytes(calc, {0xA4});

  // У синього префікса немає функції на −, ÷ і ×: байт не пишеться, префікс скидається.
  as_prg(calc);
  tap(calc, {Key::K, Key::Minus, Key::K, Key::Divide, Key::K, Key::Multiply, Key::D3});
  expect_bytes(calc, {0x03});
  REQUIRE(calc.prefix_state() == mk61::core::PrefixState::None);

  // F Сх знімає префікс і нічого не записує. Другий F не скасовує перший.
  as_prg(calc);
  tap(calc, {Key::F, Key::ClearX, Key::D3});
  expect_bytes(calc, {0x03});
  as_prg(calc);
  tap(calc, {Key::F, Key::F, Key::D0});
  expect_bytes(calc, {0x15});
  as_prg(calc);
  tap(calc, {Key::K, Key::F, Key::D0});
  expect_bytes(calc, {0x15});
  as_prg(calc);
  tap(calc, {Key::F, Key::K, Key::D0});
  expect_bytes(calc, {0x54});

  // F /-/ і F ВП міняють режим і в програму не пишуться.
  as_prg(calc);
  tap(calc, {Key::F, Key::Sign});
  REQUIRE(calc.operating_mode() == OperatingMode::Automatic);
  REQUIRE(calc.pc() == 0);
  REQUIRE(calc.program_memory()[0] == 0x00);
  tap(calc, {Key::F, Key::Exp});
  REQUIRE(calc.operating_mode() == OperatingMode::Programming);
  REQUIRE(calc.program_memory()[0] == 0x00);

  as_prg(calc);
  tap(calc, {Key::StepFwd});
  REQUIRE(calc.pc() == 1);
  REQUIRE(calc.program_memory()[0] == 0x00);
  tap(calc, {Key::StepBack});
  REQUIRE(calc.pc() == 0);
  tap(calc, {Key::StepBack});
  REQUIRE(calc.pc() == 104);
}

TEST_CASE("Plain, F and K keys move the stack the way MK-61 does", "[keys]") {
  Calculator calc;

  // Набір, крапка, знак, порядок, Сх і В↑
  tap(calc, {Key::D2, Key::Dot, Key::D5, Key::Enter, Key::D3});
  REQUIRE(calc.reg_x() == Approx(3.0));
  REQUIRE(calc.reg_y() == Approx(2.5));
  // Знак під час набору міняє лише число, яке ще друкується. Наступна цифра дописується.
  tap(calc, {Key::Sign, Key::D7});
  REQUIRE(calc.reg_x() == Approx(-37.0));
  REQUIRE(calc.reg_y() == Approx(2.5));

  calc.reset();
  tap(calc, {Key::D5, Key::Enter, Key::Sign});
  REQUIRE(calc.reg_x() == Approx(-5.0));
  REQUIRE(calc.reg_y() == Approx(5.0));
  tap(calc, {Key::D7});
  REQUIRE(calc.reg_x() == Approx(7.0));
  REQUIRE(calc.reg_y() == Approx(5.0));

  calc.reset();
  tap(calc, {Key::D3, Key::F, Key::Multiply, Key::Sign, Key::D7});
  REQUIRE(calc.reg_x() == Approx(7.0));
  REQUIRE(calc.reg_y() == Approx(-9.0));
  REQUIRE(calc.reg_x1() == Approx(3.0));

  calc.reset();
  tap(calc, {Key::D5, Key::ClearX, Key::D7});
  REQUIRE(calc.reg_x() == Approx(7.0));
  REQUIRE(calc.reg_y() == Approx(0.0));
  REQUIRE(calc.reg_x1() == Approx(0.0));

  // ВП тримає мантису як її набрали і зсуває двозначний порядок справа. 12 ВП 2 = 12·10².
  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Exp, Key::D2});
  REQUIRE(calc.reg_x() == Approx(1200.0));
  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Exp, Key::D1, Key::D3});
  REQUIRE(calc.reg_x() == Approx(1.2e14));
  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Exp, Key::Sign, Key::D1});
  REQUIRE(calc.reg_x() == Approx(1.2));
  calc.reset();
  tap(calc, {Key::Exp, Key::Sign, Key::D1});
  REQUIRE(calc.reg_x() == Approx(0.1));

  // + − × ÷ опускають стек, T копіюється в Z, старе X іде в X1
  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::Plus});
  REQUIRE(calc.reg_x() == Approx(34.0));
  REQUIRE(calc.reg_y() == Approx(20.0));
  REQUIRE(calc.reg_z() == Approx(10.0));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::Minus});
  REQUIRE(calc.reg_x() == Approx(26.0));
  REQUIRE(calc.reg_y() == Approx(20.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::Multiply});
  REQUIRE(calc.reg_x() == Approx(120.0));
  REQUIRE(calc.reg_y() == Approx(20.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::Divide});
  REQUIRE(calc.reg_x() == Approx(7.5));
  REQUIRE(calc.reg_y() == Approx(20.0));
  REQUIRE(calc.reg_z() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::Swap});
  REQUIRE(calc.reg_x() == Approx(30.0));
  REQUIRE(calc.reg_y() == Approx(4.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  // Ділення на нуль не чіпає стек
  calc.reset();
  tap(calc, {Key::D5, Key::Enter, Key::D0, Key::Divide});
  REQUIRE(calc.is_error());
  REQUIRE(calc.reg_x() == Approx(0.0));
  REQUIRE(calc.reg_y() == Approx(5.0));
  tap(calc, {Key::Plus});
  REQUIRE(calc.is_error());
  REQUIRE(calc.reg_y() == Approx(5.0));
  tap(calc, {Key::ClearX});
  REQUIRE_FALSE(calc.is_error());
  REQUIRE(calc.reg_x() == Approx(0.0));
  REQUIRE(calc.reg_y() == Approx(5.0));

  // F-функції: Y, Z, T стоять, X1 отримує старе X
  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 9.0);
  tap(calc, {Key::F, Key::Minus});
  REQUIRE(calc.reg_x() == Approx(3.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(9.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, -3.0);
  tap(calc, {Key::F, Key::Multiply});
  REQUIRE(calc.reg_x() == Approx(9.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(-3.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::F, Key::Divide});
  REQUIRE(calc.reg_x() == Approx(0.25));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 2.0);
  tap(calc, {Key::F, Key::D0});
  REQUIRE(calc.reg_x() == Approx(100.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(2.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 0.0);
  tap(calc, {Key::F, Key::D1});
  REQUIRE(calc.reg_x() == Approx(1.0));
  REQUIRE(calc.reg_y() == Approx(30.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 1000.0);
  tap(calc, {Key::F, Key::D2});
  REQUIRE(calc.reg_x() == Approx(3.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(1000.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 1.0);
  tap(calc, {Key::F, Key::D3});
  REQUIRE(calc.reg_x() == Approx(0.0));
  REQUIRE(calc.reg_y() == Approx(30.0));

  calc.reset();
  calc.set_angle_mode(AngleMode::Degrees);
  load_stack(calc, 10.0, 20.0, 30.0, 30.0);
  tap(calc, {Key::F, Key::D7});
  REQUIRE(calc.reg_x() == Approx(0.5));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(30.0));
  load_stack(calc, 10.0, 20.0, 30.0, 60.0);
  tap(calc, {Key::F, Key::D8});
  REQUIRE(calc.reg_x() == Approx(0.5));
  REQUIRE(calc.reg_y() == Approx(30.0));
  load_stack(calc, 10.0, 20.0, 30.0, 45.0);
  tap(calc, {Key::F, Key::D9});
  REQUIRE(calc.reg_x() == Approx(1.0));
  REQUIRE(calc.reg_y() == Approx(30.0));

  load_stack(calc, 10.0, 20.0, 30.0, 0.5);
  tap(calc, {Key::F, Key::D4});
  REQUIRE(calc.reg_x() == Approx(30.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  load_stack(calc, 10.0, 20.0, 30.0, 0.5);
  tap(calc, {Key::F, Key::D5});
  REQUIRE(calc.reg_x() == Approx(60.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  load_stack(calc, 10.0, 20.0, 30.0, 1.0);
  tap(calc, {Key::F, Key::D6});
  REQUIRE(calc.reg_x() == Approx(45.0));
  REQUIRE(calc.reg_y() == Approx(30.0));

  calc.set_angle_mode(AngleMode::Grads);
  load_stack(calc, 10.0, 20.0, 30.0, 1.0);
  tap(calc, {Key::F, Key::D4});
  REQUIRE(calc.reg_x() == Approx(100.0));
  REQUIRE(calc.reg_y() == Approx(30.0));

  // Степінь: основа в X, показник у Y, стек не опускається. Від'ємна основа — ЕГГОГ, X1 уже
  // записаний.
  calc.reset();
  load_stack(calc, 10.0, 20.0, 0.5, 9.0);
  tap(calc, {Key::F, Key::Swap});
  REQUIRE(calc.reg_x() == Approx(3.0));
  REQUIRE(calc.reg_y() == Approx(0.5));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(9.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 3.0, -2.0);
  tap(calc, {Key::F, Key::Swap});
  REQUIRE(calc.is_error());
  REQUIRE(calc.reg_x() == Approx(-2.0));
  REQUIRE(calc.reg_y() == Approx(3.0));
  REQUIRE(calc.reg_x1() == Approx(-2.0));
  tap(calc, {Key::ClearX});

  // π завжди піднімає стек
  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, 4.0);
  tap(calc, {Key::F, Key::Dot});
  REQUIRE(calc.reg_x() == Approx(3.1415926));
  REQUIRE(calc.reg_y() == Approx(4.0));
  REQUIRE(calc.reg_z() == Approx(30.0));
  REQUIRE(calc.reg_t() == Approx(20.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));

  // Вх після результату піднімає стек, після В↑ замінює X
  calc.reset();
  tap(calc, {Key::D4, Key::F, Key::Multiply, Key::F, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(4.0));
  REQUIRE(calc.reg_y() == Approx(16.0));
  REQUIRE(calc.reg_x1() == Approx(4.0));
  tap(calc, {Key::Enter, Key::F, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(4.0));
  REQUIRE(calc.reg_y() == Approx(4.0));
  REQUIRE(calc.reg_z() == Approx(16.0));

  // Помилки області: стек не опускається
  calc.reset();
  tap(calc, {Key::D2, Key::Sign, Key::F, Key::Minus});
  REQUIRE(calc.is_error());
  REQUIRE(calc.reg_x() == Approx(-2.0));
  tap(calc, {Key::ClearX});
  tap(calc, {Key::D0, Key::F, Key::Divide});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});
  tap(calc, {Key::D1, Key::Sign, Key::F, Key::D2});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});
  tap(calc, {Key::D0, Key::F, Key::D3});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});
  tap(calc, {Key::D2, Key::F, Key::D4});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});
  calc.set_angle_mode(AngleMode::Degrees);
  tap(calc, {Key::D9, Key::D0, Key::F, Key::D9});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});

  // К-функції без опускання стека
  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, -12.34);
  tap(calc, {Key::K, Key::D4});
  REQUIRE(calc.reg_x() == Approx(12.34));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(-12.34));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, -3.5);
  tap(calc, {Key::K, Key::D5});
  REQUIRE(calc.reg_x() == Approx(-1.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(-3.5));
  load_stack(calc, 10.0, 20.0, 30.0, 0.0);
  tap(calc, {Key::K, Key::D5});
  REQUIRE(calc.reg_x() == Approx(0.0));
  REQUIRE(calc.reg_y() == Approx(30.0));

  calc.reset();
  load_stack(calc, 10.0, 20.0, 30.0, -12.34);
  tap(calc, {Key::K, Key::D7});
  REQUIRE(calc.reg_x() == Approx(-12.0));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(-12.34));
  load_stack(calc, 10.0, 20.0, 30.0, -12.34);
  tap(calc, {Key::K, Key::D8});
  REQUIRE(calc.reg_x() == Approx(-0.34));
  REQUIRE(calc.reg_y() == Approx(30.0));
  REQUIRE(calc.reg_x1() == Approx(-12.34));

  calc.reset();
  load_stack(calc, 10.0, 20.0, -1.0, -5.0);
  tap(calc, {Key::K, Key::D9});
  REQUIRE(calc.reg_x() == Approx(-1.0));
  REQUIRE(calc.reg_y() == Approx(-1.0));
  REQUIRE(calc.reg_z() == Approx(20.0));
  REQUIRE(calc.reg_t() == Approx(10.0));
  REQUIRE(calc.reg_x1() == Approx(-5.0));

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D3, Key::D0, Key::K, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(12.5));
  REQUIRE(calc.display().text == " 12.5");
  tap(calc, {Key::K, Key::D6});
  REQUIRE(calc.reg_x() == Approx(12.3));
  REQUIRE(calc.display().text == " 12.30");

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D5, Key::K, Key::D6});
  REQUIRE(calc.display().text == " 12.30");
  tap(calc, {Key::K, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(12.5));
  REQUIRE(calc.display().text == " 12.5");

  calc.reset();
  tap(calc, {Key::D4, Key::D5, Key::K, Key::D6});
  REQUIRE(calc.display().text == " 45.00");
  tap(calc, {Key::K, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(45.0));
  REQUIRE(calc.display().text == " 45.");

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D5, Key::K, Key::D3});
  REQUIRE(calc.display().text == " 12.3000");
  tap(calc, {Key::K, Key::Swap});
  REQUIRE(calc.reg_x() == Approx(12.5));

  calc.reset();
  tap(calc, {Key::Dot, Key::D5, Key::K, Key::D6});
  REQUIRE(calc.display().text == " 0.30");
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D5, Key::Sign, Key::K, Key::D6});
  REQUIRE(calc.display().text == "-12.30");
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D3, Key::D0, Key::D4, Key::D5, Key::K, Key::Swap});
  REQUIRE(calc.reg_x() == Approx(12.5125));
  tap(calc, {Key::K, Key::D3});
  REQUIRE(calc.reg_x() == Approx(12.3045));

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D6, Key::D5, Key::K, Key::Plus});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});
  tap(calc, {Key::D1, Key::D0, Key::Dot, Key::D5, Key::D9, Key::D6, Key::D0, Key::K, Key::Swap});
  REQUIRE(calc.is_error());
  tap(calc, {Key::ClearX});

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D3, Key::D0, Key::Sign, Key::K, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(-12.5));

  // Порозрядні: 12 і 5. Перша цифра мантиси в операцію не входить, зверху завжди 8.
  calc.reset();
  tap(calc, {Key::D1, Key::Enter, Key::D2, Key::Enter, Key::D1, Key::D2, Key::Enter, Key::D5,
             Key::K, Key::Dot});
  REQUIRE(calc.reg_x() == Approx(8.0));
  REQUIRE(calc.reg_y() == Approx(12.0));
  REQUIRE(calc.reg_z() == Approx(2.0));
  REQUIRE(calc.reg_t() == Approx(1.0));
  REQUIRE(calc.reg_x1() == Approx(5.0));
  REQUIRE(calc.display().text == " 8.0000000");

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Enter, Key::D5, Key::K, Key::Sign});
  REQUIRE(calc.reg_x() == Approx(8.2));
  REQUIRE(calc.reg_y() == Approx(12.0));
  REQUIRE(calc.display().text == " 8.2000000");
  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Enter, Key::D5, Key::K, Key::Exp});
  REQUIRE(calc.reg_x() == Approx(8.2));
  REQUIRE(calc.reg_y() == Approx(12.0));
  REQUIRE(calc.reg_x1() == Approx(5.0));

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::D3, Key::D4, Key::D5, Key::D6, Key::K, Key::ClearX});
  REQUIRE(calc.display().text == " 8.ГCL-9  ");
  REQUIRE(calc.reg_x1() == Approx(123456.0));
  REQUIRE(calc.reg_y() == Approx(0.0));

  // К СЧ замінює X і не піднімає Y
  calc.reset();
  tap(calc, {Key::D4, Key::D0, Key::Enter, Key::K, Key::Enter});
  REQUIRE(calc.reg_x() == Approx(0.404067));
  REQUIRE(calc.reg_y() == Approx(40.0));
  REQUIRE(calc.reg_x1() == Approx(40.0));

  // НОП нічого не міняє. К −, К ÷ і К × теж.
  calc.reset();
  load_stack(calc, 10.0, 20.0, 8.0, 2.0);
  tap(calc, {Key::K, Key::D0, Key::K, Key::D1, Key::K, Key::D2});
  REQUIRE(calc.reg_x() == Approx(2.0));
  REQUIRE(calc.reg_y() == Approx(8.0));
  REQUIRE(calc.reg_x1() == Approx(0.0));
  tap(calc, {Key::K, Key::Minus, Key::K, Key::Divide, Key::K, Key::Multiply, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(10.0));
  REQUIRE(calc.reg_y() == Approx(20.0));

  // F Сх під час набору лише знімає префікс: наступна цифра дописується.
  calc.reset();
  tap(calc, {Key::D5, Key::F, Key::ClearX, Key::D2});
  REQUIRE(calc.reg_x() == Approx(52.0));
  REQUIRE(calc.prefix_state() == mk61::core::PrefixState::None);

  // Х→П не чіпає X1 і не рухає стек. П→Х піднімає завжди.
  calc.reset();
  tap(calc, {Key::D6, Key::Store, Key::D2});
  REQUIRE(calc.reg_x() == Approx(6.0));
  REQUIRE(calc.memory_reg(2) == Approx(6.0));
  REQUIRE(calc.reg_x1() == Approx(0.0));
  tap(calc, {Key::D4, Key::Recall, Key::D2});
  REQUIRE(calc.reg_x() == Approx(6.0));
  REQUIRE(calc.reg_y() == Approx(4.0));
  REQUIRE(calc.reg_x1() == Approx(0.0));
}

TEST_CASE("Jumps, loops and indirect keys follow MK-61 polarity", "[keys]") {
  Calculator calc;

  const auto jump_when = [&](std::initializer_list<Key> jump_keys, double x, bool jumped) {
    calc.reset();
    REQUIRE(calc.assemble_and_load("9\nС/П\n").ok);
    calc.enter_number(x);
    calc.set_pc(0);
    tap(calc, jump_keys);
    if (jumped) {
      REQUIRE(calc.pc() == 1);
    } else {
      REQUIRE(calc.pc() == 0);
    }
    REQUIRE(calc.reg_x() == Approx(x));
    REQUIRE(calc.reg_y() == Approx(0.0));
  };

  // F x<0 стрибає, коли X >= 0. F x≥0 — коли X < 0.
  // F x=0 стрибає, коли X != 0. F x≠0 — коли X == 0.
  jump_when({Key::F, Key::StepFwd, Key::D0, Key::D1}, -1.0, false);
  jump_when({Key::F, Key::StepFwd, Key::D0, Key::D1}, 0.0, true);
  jump_when({Key::F, Key::StepFwd, Key::D0, Key::D1}, 1.0, true);
  jump_when({Key::F, Key::Return, Key::D0, Key::D1}, -1.0, true);
  jump_when({Key::F, Key::Return, Key::D0, Key::D1}, 0.0, false);
  jump_when({Key::F, Key::Return, Key::D0, Key::D1}, 1.0, false);
  jump_when({Key::F, Key::StepBack, Key::D0, Key::D1}, 0.0, false);
  jump_when({Key::F, Key::StepBack, Key::D0, Key::D1}, 2.0, true);
  jump_when({Key::F, Key::StepBack, Key::D0, Key::D1}, -2.0, true);
  jump_when({Key::F, Key::RunStop, Key::D0, Key::D1}, 0.0, true);
  jump_when({Key::F, Key::RunStop, Key::D0, Key::D1}, 2.0, false);

  // Цикл: ціла частина мінус 1, дріб лишається, стрибок доки ціла частина не нуль.
  REQUIRE(calc.assemble_and_load("F L0 skip\n1\nС/П\nskip:\n2\nС/П\n").ok);
  calc.set_memory_reg(0, 1.0);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(1.0));
  REQUIRE(calc.memory_reg(0) == Approx(0.0));

  calc.set_memory_reg(0, 2.0);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(2.0));
  REQUIRE(calc.memory_reg(0) == Approx(1.0));

  calc.set_memory_reg(0, 1.7);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(1.0));
  REQUIRE(calc.memory_reg(0) == Approx(0.7));

  calc.set_memory_reg(0, 0.0);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(2.0));
  REQUIRE(calc.memory_reg(0) == Approx(-1.0));

  calc.set_memory_reg(0, 0.4);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(2.0));
  REQUIRE(calc.memory_reg(0) == Approx(-0.6));

  // L1 чіпає R1, а не R0
  REQUIRE(calc.assemble_and_load("F L1 skip\n1\nС/П\nskip:\n2\nС/П\n").ok);
  calc.set_memory_reg(0, 9.0);
  calc.set_memory_reg(1, 2.0);
  calc.set_pc(0);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(2.0));
  REQUIRE(calc.memory_reg(1) == Approx(1.0));
  REQUIRE(calc.memory_reg(0) == Approx(9.0));

  // Непрямі стрибки: R0..R3 спочатку мінус 1, R4..R6 плюс 1, R7..RE без змін.
  // Полярність та сама, що в прямих F-стрибків.
  REQUIRE(calc.assemble_and_load("9\nС/П\n").ok);
  calc.set_memory_reg(4, 0.0);
  calc.enter_number(1.0);
  calc.set_pc(0);
  tap(calc, {Key::K, Key::StepFwd, Key::D4});
  REQUIRE(calc.memory_reg(4) == Approx(1.0));
  REQUIRE(calc.pc() == 1);
  REQUIRE(calc.reg_x() == Approx(1.0));

  calc.set_memory_reg(4, 0.0);
  calc.enter_number(-1.0);
  calc.set_pc(0);
  tap(calc, {Key::K, Key::StepFwd, Key::D4});
  REQUIRE(calc.memory_reg(4) == Approx(1.0));
  REQUIRE(calc.pc() == 0);

  calc.set_memory_reg(0, 2.0);
  calc.enter_number(0.0);
  calc.set_pc(0);
  tap(calc, {Key::K, Key::RunStop, Key::D0});
  REQUIRE(calc.memory_reg(0) == Approx(1.0));
  REQUIRE(calc.pc() == 1);

  calc.set_memory_reg(14, 1.0);
  calc.set_memory_reg(1, 77.0);
  calc.enter_number(5.0);
  calc.set_pc(0);
  tap(calc, {Key::K, Key::Recall, Key::Enter});
  REQUIRE(calc.memory_reg(14) == Approx(1.0));
  REQUIRE(calc.reg_x() == Approx(77.0));
  REQUIRE(calc.reg_y() == Approx(5.0));

  calc.set_memory_reg(4, 8.0);
  calc.set_memory_reg(9, 42.0);
  calc.enter_number(3.0);
  tap(calc, {Key::K, Key::Recall, Key::D4});
  REQUIRE(calc.memory_reg(4) == Approx(9.0));
  REQUIRE(calc.reg_x() == Approx(42.0));

  calc.enter_number(9.0);
  calc.set_memory_reg(4, 2.0);
  tap(calc, {Key::K, Key::Store, Key::D4});
  REQUIRE(calc.memory_reg(4) == Approx(3.0));
  REQUIRE(calc.memory_reg(3) == Approx(9.0));
  REQUIRE(calc.reg_x() == Approx(9.0));
  REQUIRE(calc.reg_x1() == Approx(0.0));

  calc.set_memory_reg(4, 14.0);
  tap(calc, {Key::K, Key::Recall, Key::D4});
  REQUIRE(calc.is_error());
  REQUIRE(calc.memory_reg(4) == Approx(15.0));
  tap(calc, {Key::ClearX});

  // К БП і К ПП з клавіатури
  REQUIRE(calc.assemble_and_load("0\n7\nВ/О\nС/П\n").ok);
  calc.set_memory_reg(0, 2.0);
  calc.set_pc(3);
  tap(calc, {Key::K, Key::Gosub, Key::D0});
  REQUIRE(calc.memory_reg(0) == Approx(1.0));
  REQUIRE(calc.pc() == 1);
  REQUIRE(calc.return_sp() == 1);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(7.0));
  REQUIRE(calc.return_sp() == 0);

  // Порожній В/О у програмі повертає на крок 00
  REQUIRE(calc.assemble_and_load("7\nС/П\nВ/О\n").ok);
  calc.set_pc(2);
  REQUIRE(calc.run_program() == mk61::core::RunStatus::StoppedAtSP);
  REQUIRE(calc.reg_x() == Approx(7.0));

  // ШГ→ виконує один крок, ←ШГ у режимі рахунку стоїть
  REQUIRE(calc.assemble_and_load("8\nС/П\n").ok);
  calc.set_pc(0);
  tap(calc, {Key::StepFwd});
  REQUIRE(calc.reg_x() == Approx(8.0));
  REQUIRE(calc.pc() == 1);
  const int pc_now = calc.pc();
  tap(calc, {Key::StepBack});
  REQUIRE(calc.pc() == pc_now);
}

TEST_CASE("A comma locale does not eat the fractional part", "[locale]") {
  const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
  struct Restore {
    std::string previous;
    ~Restore() { std::setlocale(LC_NUMERIC, previous.c_str()); }
  } restore{previous};
  REQUIRE(std::setlocale(LC_NUMERIC, "uk_UA.UTF-8") != nullptr);

  Calculator calc;
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D5, Key::K, Key::D6});
  REQUIRE(calc.reg_x() == Approx(12.3));
  REQUIRE(calc.display().text == " 12.30");
  REQUIRE(calc.reg_x1() == Approx(12.5));
  tap(calc, {Key::K, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(12.5));
  REQUIRE(calc.display().text == " 12.5");

  calc.reset();
  tap(calc, {Key::D1, Key::D2, Key::Dot, Key::D5, Key::Enter, Key::D1, Key::Plus});
  REQUIRE(calc.reg_x() == Approx(13.5));
}
