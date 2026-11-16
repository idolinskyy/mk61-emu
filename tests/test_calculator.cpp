#include "core/calculator.hpp"
#include "core/opcodes.hpp"
#include "core/presets.hpp"
#include "core/types.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using mk61::core::AngleMode;
using mk61::core::Calculator;
using mk61::core::Key;
using mk61::core::OperatingMode;

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

  // Перевіримо, що В↑ перед П→Х не піднімає стек двічі
  calc.clear_registers();
  calc.set_memory_reg(0, 5.0);
  REQUIRE(calc.press_tokens("2 В↑ П→Х 0 +", err));
  REQUIRE(calc.reg_x() == Approx(7.0));
}

TEST_CASE("Scientific functions, x^y order, and angle modes", "[math]") {
  Calculator calc;
  std::string err;

  // На МК-61 для F x^y основа знаходиться у Y, а показник у X: 2 В↑ 10 F x^y = 1024
  REQUIRE(calc.press_tokens("2 В↑ 10 F ↔", err));
  REQUIRE(calc.reg_x() == Approx(1024.0));

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
}
