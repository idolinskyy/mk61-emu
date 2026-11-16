#include "core/calculator.hpp"
#include "core/opcodes.hpp"
#include "core/presets.hpp"
#include "core/types.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mk61::core::AngleMode;
using mk61::core::Calculator;
using mk61::core::Key;
using mk61::core::OperatingMode;
using mk61::core::PrefixState;

/// Доповнює або обрізає UTF-8 рядок рівно до вказаної кількості видимих знакоділянок термінала
std::string pad_col(std::string_view sv, std::size_t width) {
  std::size_t cur_cols = 0;
  std::size_t byte_end = 0;
  while (byte_end < sv.size()) {
    const auto b = static_cast<unsigned char>(sv[byte_end]);
    std::size_t char_len = 1;
    if ((b & 0x80U) == 0U) {
      char_len = 1;
    } else if ((b & 0xE0U) == 0xC0U) {
      char_len = 2;
    } else if ((b & 0xF0U) == 0xE0U) {
      char_len = 3;
    } else if ((b & 0xF8U) == 0xF0U) {
      char_len = 4;
    }
    if (cur_cols + 1 > width) {
      break;
    }
    byte_end += std::min(char_len, sv.size() - byte_end);
    ++cur_cols;
  }
  std::string out(sv.substr(0, byte_end));
  if (cur_cols < width) {
    out.append(width - cur_cols, ' ');
  }
  return out;
}

std::string repeat_utf8(std::string_view glyph, std::size_t count) {
  std::string out;
  out.reserve(glyph.size() * count);
  for (std::size_t i = 0; i < count; ++i) {
    out.append(glyph);
  }
  return out;
}

std::string_view angle_mode_label(AngleMode m) noexcept {
  switch (m) {
  case AngleMode::Radians:
    return "Р (радіани)";
  case AngleMode::Degrees:
    return "ГРД (градуси)";
  case AngleMode::Grads:
    return "Г (гради)";
  }
  return "Р";
}

std::string_view prefix_label(PrefixState p) noexcept {
  switch (p) {
  case PrefixState::None:
    return "—";
  case PrefixState::F:
    return "[F]";
  case PrefixState::K:
    return "[К]";
  case PrefixState::WaitStoreReg:
    return "[Х→П ?]";
  case PrefixState::WaitRecallReg:
    return "[П→Х ?]";
  case PrefixState::WaitKStoreReg:
    return "[К Х→П ?]";
  case PrefixState::WaitKRecallReg:
    return "[К П→Х ?]";
  case PrefixState::WaitKGotoReg:
    return "[К БП ?]";
  case PrefixState::WaitKGosubReg:
    return "[К ПП ?]";
  case PrefixState::WaitKIfNotZeroReg:
    return "[К x≠0 ?]";
  case PrefixState::WaitKIfGeZeroReg:
    return "[К x≥0 ?]";
  case PrefixState::WaitKIfLtZeroReg:
    return "[К x<0 ?]";
  case PrefixState::WaitKIfEqZeroReg:
    return "[К x=0 ?]";
  case PrefixState::WaitAddr1:
    return "[Адреса: 1-ша]";
  case PrefixState::WaitAddr2:
    return "[Адреса: 2-га]";
  }
  return "—";
}

std::string_view trim_sv(std::string_view sv) noexcept {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
    sv.remove_suffix(1);
  }
  return sv;
}

void print_panel(const Calculator &calc) {
  constexpr std::size_t kInnerW = 68;
  const auto disp = calc.display();
  const auto mode_str = (calc.operating_mode() == OperatingMode::Automatic) ? "АВТ" : "ПРГ";
  const auto pc_str = mk61::core::format_step_address(calc.pc());
  const auto op_at_pc = calc.program_memory()[static_cast<std::size_t>(calc.pc())];
  const auto op_info = mk61::core::opcode_info(op_at_pc);

  const std::string hline = repeat_utf8("─", kInnerW);
  const std::string row1 =
    std::format(" ЕЛЕКТРОНІКА МК-61   Режим: {}   Кути: {}   Префікс: {}", pad_col(mode_str, 4),
                pad_col(angle_mode_label(calc.angle_mode()), 13),
                pad_col(prefix_label(calc.prefix_state()), 8));
  const std::string row2 =
    std::format("   ІНДИКАТОР ВЛІ:  [ {} ]   Крок PC: {} ({}: {})", pad_col(disp.text, 16), pc_str,
                op_info.display_code, op_info.mnemonic);
  const std::string row3 = std::format(
    " Стек:  T = {}  Z = {}  Y = {}", pad_col(Calculator::format_register_value(calc.raw_t()), 13),
    pad_col(Calculator::format_register_value(calc.raw_z()), 13),
    pad_col(Calculator::format_register_value(calc.raw_y()), 13));
  const std::string row4 = std::format(
    "        X = {}  X1= {}  Виконано кроків: {}",
    pad_col(Calculator::format_register_value(calc.raw_x()), 13),
    pad_col(Calculator::format_register_value(calc.raw_x1()), 13), calc.total_steps_executed());

  std::println("┌{}┐", hline);
  std::println("│{}│", pad_col(row1, kInnerW));
  std::println("├{}┤", hline);
  std::println("│{}│", pad_col(row2, kInnerW));
  std::println("├{}┤", hline);
  std::println("│{}│", pad_col(row3, kInnerW));
  std::println("│{}│", pad_col(row4, kInnerW));
  std::println("└{}┘", hline);
}

void print_registers(const Calculator &calc) {
  constexpr std::size_t kInnerW = 68;
  const std::string hline = repeat_utf8("─", kInnerW);
  std::println("┌{}┐", hline);
  std::println("│{}│", pad_col("                   РЕГІСТРИ ПАМ'ЯТІ (R0..RE)", kInnerW));
  std::println("├{}┤", hline);
  for (std::size_t row = 0; row < 5; ++row) {
    const std::size_t i0 = row;
    const std::size_t i1 = row + 5;
    const std::size_t i2 = row + 10;
    const std::string c0 =
      std::format("R{}: {}", mk61::core::reg_index_to_char(static_cast<std::uint8_t>(i0)),
                  pad_col(Calculator::format_register_value(calc.raw_memory_reg(i0)), 14));
    const std::string c1 =
      std::format("R{}: {}", mk61::core::reg_index_to_char(static_cast<std::uint8_t>(i1)),
                  pad_col(Calculator::format_register_value(calc.raw_memory_reg(i1)), 14));
    const std::string c2 =
      std::format("R{}({}): {}", mk61::core::reg_index_to_char(static_cast<std::uint8_t>(i2)),
                  mk61::core::reg_index_to_mk61_glyph(static_cast<std::uint8_t>(i2)),
                  pad_col(Calculator::format_register_value(calc.raw_memory_reg(i2)), 14));
    const std::string line = std::format("  {}   {}   {}", c0, c1, c2);
    std::println("│{}│", pad_col(line, kInnerW));
  }
  std::string ret_line = std::format(" Стек повернень ПП (SP={}): ", calc.return_sp());
  if (calc.return_sp() == 0) {
    ret_line += "—";
  } else {
    for (std::size_t i = 0; i < calc.return_sp(); ++i) {
      ret_line += mk61::core::format_step_address(calc.return_stack()[i]) + " ";
    }
  }
  std::println("├{}┤", hline);
  std::println("│{}│", pad_col(ret_line, kInnerW));
  std::println("└{}┘", hline);
}

void print_keyboard_layout() {
  std::println("{}", R"(Клавіші МК-61 у консолі

Живого натискання тут немає. Слово в рядку — це і є клавіша, а Enter
виконує весь рядок зліва направо. Літера F на клавіатурі комп'ютера сама
по собі калькулятор не чіпає.

Кілька клавіш пишуть через пробіл:  2 В↑ 3 +
Жовтий напис над клавішею: спочатку F, потім клавіша.   30 F 7  — це sin.
Синій напис: спочатку К (латинська K теж годиться).   К 7  — ціла частина.
Випадковий F або К знімається словами F Сх.

Після Х→П, П→Х, БП, ПП і після умовних переходів калькулятор чекає
наступну клавішу: номер регістра або дві цифри адреси. Це теж просто
наступні слова в рядку.
  Регістри 0..9 — цифри.
  a (RA) — крапка .     на індикаторі це «-»
  b (RB) — /-/          на індикаторі «L»
  c (RC) — ВП           на індикаторі «C»
  d (RD) — Сх           на індикаторі «Г»
  e (RE) — В↑           на індикаторі «E»
  Адреса кроку 00..99 — дві цифри: 0 2 це крок 02.
  Кроки 100..104 на індикаторі виглядають як -0..-4. У рядку: . 0 .. . 4

У режимі АВТ слово виконується. Після :prg ті самі слова записуються
в пам'ять і нічого не рахують, поки не повернутись командою :avt.

--- Ряд 1. Кроки програми і переходи ---

F
  Писати: F
  Жовтий префікс. Сам не рахує. Наступна клавіша бере жовтий напис над собою.
  Окремого жовтого чи синього в F немає.

ШГ→
  Писати: ШГ→    ШГ>    SST
  АВТ: виконати один крок програми. ПРГ: перейти на наступний крок, не виконуючи його.
  F ШГ→  — умова x<0, далі дві цифри адреси. Якщо X від'ємний, програма йде
           далі. Якщо ні — стрибок. На МК-61 умова спрацьовує навпаки, ніж
           у звичних мовах: істина означає «не стрибати».
  К ШГ→  — те саме x<0, але адресу беруть із регістра, чию клавішу натиснути
           слідом. Для R0..R3 регістр перед цим ще й зменшується на 1,
           для R4..R6 — збільшується.

←ШГ
  Писати: ←ШГ    <ШГ    BST
  ПРГ: крок назад. В АВТ ця клавіша сама по собі нічого не робить.
  F ←ШГ  — умова x=0, далі адреса. Нуль — ідемо далі, не нуль — стрибок.
  К ←ШГ  — непряме x=0, далі регістр з адресою.

В/О
  Писати: В/О    ВО    RTN
  АВТ: поставити програму на крок 00 і очистити стек підпрограм.
       Так роблять перед пуском: 2026 В/О С/П
  ПРГ: записати команду повернення з підпрограми. Коли програма до неї
       дійде, вона вернеться туди, звідки була ПП. Якщо виклику не було —
       теж стане на 00.
  F В/О  — умова x≥0, далі адреса.
  К В/О  — непряме x≥0.

С/П
  Писати: С/П    СП    R/S
  АВТ: запустити програму з поточного кроку. Вона йде, поки в пам'яті
       не трапиться команда С/П, помилка ЕГГОГ або ліміт кроків.
  ПРГ: записати в програму зупинку.
  Те саме вміє команда емулятора :run. Її можна дописати в кінець рядка
  з клавішами: 6 В/О :run
  F С/П  — умова x≠0, далі адреса.
  К С/П  — непряме x≠0.

--- Ряд 2. Пам'ять, переходи, цикли ---

К
  Писати: К    K
  Синій префікс. Наступна клавіша бере синій напис.

Х→П
  Писати: Х→П    П    STO
  Далі обов'язково регістр. Число з X записується туди, на індикаторі
  лишається те саме. Приклад: 25 Х→П 1  кладе 25 в R1.
  Х у цьому слові можна писати латинською.
  F Х→П  — цикл L1. Крутить регістр R1: віднімає 1, і якщо ще більше нуля,
           стрибає на адресу з двох цифр.
  К Х→П  — записати X у регістр, номер якого лежить в іншому регістрі.
           Далі клавіша того регістра-вказівника.

П→Х
  Писати: П→Х    ИП    ІП    RCL
  Далі регістр. Число з нього з'являється в X. Якщо перед цим був В↑,
  стек вдруге не піднімається.
  F П→Х  — цикл L0, регістр R0. У прикладі факторіала це якраз «F П→Х 0 2»:
           L0 і адреса 02.
  К П→Х  — прочитати регістр за номером з іншого регістра.

БП
  Писати: БП    GOTO
  Безумовний перехід. Далі дві цифри адреси, і програма стрибає туди.
  F БП   — цикл L2, регістр R2.
  К БП   — перехід за адресою з регістра.

ПП
  Писати: ПП    GSB
  Виклик підпрограми. Далі адреса. Зворотна адреса лягає в стек ПП
  (до п'яти вкладень). Повертає команда В/О всередині програми.
  F ПП   — цикл L3, регістр R3.
  К ПП   — виклик за адресою з регістра.

--- Ряд 3. Цифри 7–9, мінус, ділення ---

7
  Писати: 7
  Цифра 7. Після Х→П або П→Х це регістр R7, не нова цифра.
  F 7  — синус. Кут у тих одиницях, які зараз стоять (Р, ГРД або Г).
  К 7  — ціла частина.  -3.7 дає -3.

8
  Писати: 8
  Цифра 8, або регістр R8.
  F 8  — косинус.
  К 8  — дробова частина.  -3.7 дає -0.7

9
  Писати: 9
  Цифра 9, або регістр R9.
  F 9  — тангенс.
  К 9  — більше з X і Y. Стек при цьому не опускається, Y лишається.

-
  Писати: -
  Віднімання: Y мінус X. Результат у X, старий X ховається в X1.
  F -  — квадратний корінь. З від'ємного буде ЕГГОГ, скидання — Сх.
  Синього напису над мінусом немає.

÷
  Писати: /    ÷    :
  Ділення: Y поділити на X.
  F /  — обернене число 1/x. Ділення на нуль дає ЕГГОГ.
  Синього напису немає.
  Двокрапка тут саме клавіша ділення, не команда емулятора.
  Команда емулятора починається з двокрапки на початку слова: :help

--- Ряд 4. Цифри 4–6, плюс, множення ---

4
  Писати: 4
  Цифра 4, або регістр R4.
  F 4  — арксинус. Якщо модуль X більший за 1 — ЕГГОГ.
  К 4  — модуль числа.

5
  Писати: 5
  Цифра 5, або регістр R5.
  F 5  — арккосинус.
  К 5  — знак: -1, 0 або 1.

6
  Писати: 6
  Цифра 6, або регістр R6.
  F 6  — арктангенс.
  К 6  — градуси з десятковою часткою (12.5) у градуси і мінути (12.30).

+
  Писати: +
  Додавання Y + X.
  F +  — повернути попереднє значення з X1 на індикатор. Ним користуються,
         коли дію натиснули не ту і хочуть аргумент назад.
  К +  — мінути (12.30) назад у десяткові частки градуса (12.5).
         Якщо мінут 60 або більше — ЕГГОГ.

×
  Писати: *    ×
  Множення.
  F *  — квадрат числа.
  Синього напису немає.

--- Ряд 5. Цифри 1–3, обмін, підйом стека ---

1
  Писати: 1
  Цифра 1, або регістр R1.
  F 1  — e в степені X.
  К 1  — порожня команда, код 55. У програмі просто займає крок.

2
  Писати: 2
  Цифра 2, або регістр R2.
  F 2  — десятковий логарифм. X ≤ 0 дає ЕГГОГ.
  К 2  — теж порожня команда, код 56.

3
  Писати: 3
  Цифра 3, або регістр R3.
  F 3  — натуральний логарифм.
  К 3  — десяткові частки градуса у градуси, мінути і секунди.

↔
  Писати: ↔    <->    XY    SWAP
  Міняє місцями X і Y.
  F ↔  — степінь: Y в степені X. Основа має вже лежати в Y, показник у X.
         2 ^ 10 F ↔  рахує 2^10. Знак ^ тут не степінь, а скорочення В↑.
         Якщо основа нуль або від'ємна — ЕГГОГ.
  К ↔  — градуси, мінути і секунди назад у десяткові частки.

В↑
  Писати: В↑    ^    ENT    ENTER
  Піднімає стек: X копіюється в Y. Наступне введене число замінить X
  і вдруге стек не підніме. Без В↑ між двома числами друге зітре перше.
  F В↑  — прокрутити стек по колу, щоб подивитися Z і T.
  К В↑  — випадкове число від 0 до 1, не включаючи 1.
          Перше після ввімкнення — 0.404067
  Після Х→П або П→Х ця клавіша означає регістр e (RE), а не підйом стека.

--- Ряд 6. Нуль, крапка, знак, порядок, скидання ---

0
  Писати: 0
  Цифра 0, або регістр R0.
  F 0  — 10 у степені X.
  К 0  — порожня команда НОП, код 54.

.
  Писати: .    ,
  Десяткова крапка.
  F .  — занести π (3.1415926), стек при цьому піднімається.
  К .  — порозрядне І. Працює з числом вигляду 8.xxxxxxx
  Після Х→П або П→Х це регістр a. На індикаторі його номер виглядає як «-».
  Крапка першою цифрою адреси — це не дріб, а кроки 100..104:
  . 0 означає крок 100.

/-/
  Писати: /-/    +/-    CHS
  Змінити знак числа на індикаторі. Тиснуть після цифр, не перед ними:
  25 /- /  це -25.
  F /-/  — перейти в режим рахунку АВТ. Те саме, що команда :avt
  К /-/  — порозрядне АБО.
  Після Х→П або П→Х це регістр b. На індикаторі — «L».

ВП
  Писати: ВП    EE    EXP
  Далі порядок числа, дві цифри. Від'ємний порядок — ще один /-/ після них.
  1 . 6 /-/ ВП 1 9 /-/  заносить -1.6×10⁻¹⁹
  F ВП  — перейти в режим програмування ПРГ. Те саме, що :prg
  К ВП  — порозрядне виключне АБО.
  Після Х→П або П→Х це регістр c. На індикаторі — «C».

Сх
  Писати: Сх    CX    CLX
  Стерти X в нуль. Решта стека і регістри не чіпаються.
  Якщо на індикаторі ЕГГОГ, тільки Сх знімає помилку. Інші клавіші мовчать,
  поки помилку не скинути.
  F Сх  — скасувати жовтий або синій префікс, якщо його натиснули даремно.
  К Сх  — порозрядна інверсія. 123456 К Сх дає на індикаторі 8.ГCL-9
  Після Х→П або П→Х це регістр d. На індикаторі — «Г».
)");
}

void print_program_listing(const Calculator &calc, bool all_steps = false) {
  const auto steps = mk61::core::disassemble_program(calc.program_memory(), all_steps);
  constexpr std::size_t kWStep = 6;
  constexpr std::size_t kWCode = 9;
  constexpr std::size_t kWMnem = 16;
  constexpr std::size_t kWDesc = 64;

  const std::string h0 = repeat_utf8("─", kWStep);
  const std::string h1 = repeat_utf8("─", kWCode);
  const std::string h2 = repeat_utf8("─", kWMnem);
  const std::string h3 = repeat_utf8("─", kWDesc);

  std::println("┌{}┬{}┬{}┬{}┐", h0, h1, h2, h3);
  std::println("│{}│{}│{}│{}│", pad_col(" Крок", kWStep), pad_col(" Код ВЛІ", kWCode),
               pad_col(" Мнемоніка", kWMnem), pad_col(" Опис операції", kWDesc));
  std::println("├{}┼{}┼{}┼{}┤", h0, h1, h2, h3);
  for (const auto &s : steps) {
    const std::string marker = (s.step == calc.pc()) ? "►" : " ";
    const std::string step_col = std::format(" {}{}", marker, s.step_str);
    const std::string code_col = s.arg_byte.has_value()
                                   ? std::format(" {} {:02X}", s.opcode_str, *s.arg_byte)
                                   : std::format(" {}", s.opcode_str);
    const std::string mnem_col = std::format(" {}", s.mnemonic);
    const std::string desc_col = std::format(" {}", s.description);

    std::println("│{}│{}│{}│{}│", pad_col(step_col, kWStep), pad_col(code_col, kWCode),
                 pad_col(mnem_col, kWMnem), pad_col(desc_col, kWDesc));
  }
  std::println("└{}┴{}┴{}┴{}┘", h0, h1, h2, h3);
}

void print_presets() {
  constexpr std::size_t kInnerW = 76;
  const std::string hline = repeat_utf8("─", kInnerW);
  std::println("┌{}┐", hline);
  std::println("│{}│",
               pad_col("                     ВБУДОВАНІ ПРОГРАМИ ДЛЯ МК-61 (1..7)", kInnerW));
  std::println("└{}┘", hline);
  const auto presets = mk61::core::builtin_programs();
  for (std::size_t i = 0; i < presets.size(); ++i) {
    std::println("  {}. [{}] — {}", i + 1, presets[i].id, presets[i].title);
    std::println("     Опис: {}", presets[i].description);
    std::println("     Як запускати: {}", presets[i].usage_hint);
    std::println("     Команда завантаження:  :p {}   (або :preset {})\n", i + 1, presets[i].id);
  }
}

void print_startup_banner() {
  constexpr std::size_t kInnerW = 68;
  const std::string dline = repeat_utf8("═", kInnerW);
  const std::array<std::string_view, 5> lines = {
    " • Клавіші — слова в рядку, потім Enter. Живого натискання немає.",
    " • Рахунок: 2 В↑ 3 +    степінь: 2 ^ 10 F ↔    sin: :deg і 30 F 7",
    " • Приклади: :p , потім :p 1 , потім 2026 В/О С/П",
    " • Програма: :asm | лістинг :l | крок :s | регістри :r",
    " • Кожна клавіша: :keys    довідка: :help    вихід: :q",
  };
  std::println("╔{}╗", dline);
  std::println("║{}║",
               pad_col("      ЕМУЛЯТОР МІКРОКАЛЬКУЛЯТОРА «ЕЛЕКТРОНІКА МК-61» (КОНСОЛЬ)", kInnerW));
  std::println("╠{}╣", dline);
  for (const auto &l : lines) {
    std::println("║{}║", pad_col(l, kInnerW));
  }
  std::println("╚{}╝", dline);
}

void print_repl_help() {
  std::println("{}", R"(Довідка консолі «Електроніка МК-61»

Як тут натискають клавіші
  Живого натискання немає. Поки рядок не закінчений і не натиснуто Enter,
  калькулятор стоїть. Клавіша F на клавіатурі комп'ютера нічого не шле
  на МК-61: треба написати літеру F у рядку.

  Після запрошення МК-61 [АВТ | PC=00]> пишуть слова через пробіл.
  Enter виконує їх зліва направо, ніби ці клавіші натиснули на корпусі.

  Один рядок і чотири окремі рядки — те саме:
    2 В↑ 3 +
  або
    2
    В↑
    3
    +
  На індикаторі буде 5. Знака рівності на МК-61 немає. Спочатку числа,
  потім дія. Між двома числами потрібен В↑, інакше друге зітре перше.
  В↑ можна писати як ^ або ENT.

  Слова з двокрапки на початку — не клавіші калькулятора, а команди
  цієї програми: :help, :keys, :q. Їх теж підтверджують Enter.

Жовте і синє
  Майже кожна клавіша має три ролі: напис на ній, жовтий над нею, синій над нею.
  Жовтий вмикають словом F, синій — словом К (латинська K теж працює).
    30 F 7
  це цифри 3 і 0, потім жовта F, потім клавіша 7. Над сімкою написано sin.
  Кути спочатку переводять у градуси, інакше 30 означає радіани:
    :deg
  :deg — не клавіша, а тумблер ГРД. На корпусі він під індикатором.
  Радіани — :rad, гради — :grad.

  Знак ^ сам по собі не підносить до степеня. У рядку ^ — це скорочення В↑.
  Степінь — жовте над ↔:
    2 ^ 10 F ↔
  Спочатку 2 копіюється в Y, потім набирається 10, потім F ↔ рахує Y^X.
  Виходить 1024.

  Повний список, що писати замість кожної з 30 клавіш і що вона робить: :keys
  Найкоротші заміни, якщо знака немає на клавіатурі:
    В↑  ^ ENT      Сх  CX         /-/  +/-        ↔  <->  XY
    ×   *          ÷  /  :        ШГ→  ШГ>        ←ШГ  <ШГ
    В/О ВО RTN     С/П СП R/S     Х→П П STO       П→Х ИП RCL
    БП  GOTO       ПП  GSB
  Кирилиця і латиниця тут не різні клавіші.

Що спробувати одразу
  2 В↑ 3 +                 5
  2 В↑ 3 + 4 *             20, тобто (2+3)×4
  2 ^ 10 F ↔               1024
  :deg
  30 F 7                   0.5, синус 30 градусів
  123456 К Сх              порозрядна інверсія, на індикаторі 8.ГCL-9

Готова програма
  :p                       список із семи прикладів і як їх пускати
  :p 1                     записати в пам'ять дату Великодня
  2026 В/О С/П             рік, програма на крок 00, пуск
                           у X буде місяць, у Y — день
  :p 2
  6 В/О :run               720, це 6!
  :run робить те саме, що клавіша С/П у режимі рахунку, і її можна
  поставити в той самий рядок після клавіш.

Своя програма
  Спосіб як на живому калькуляторі. :prg вмикає запис у пам'ять, :avt
  повертає рахунок. У ПРГ ті самі слова не рахують, а лягають кроками.
    :prg
    Х→П 0 1 П→Х 0 * F П→Х 0 2 С/П
    :avt
    5 В/О С/П
  Вийде 120, це 5!. Х→П 0 ховає число в R0, одиниця — початок добутку,
  П→Х 0 дістає множник, зірочка множить, F П→Х — цикл L0 по регістру R0,
  цифри 0 2 — адреса кроку 02, С/П — стоп у програмі.

  Спосіб текстом, з мітками. Порожній рядок або слово END закінчує ввід.
    :asm
    Х→П 0
    1
    loop:
    П→Х 0
    *
    F L0 loop
    С/П
  В один рядок те саме: :asm Х→П 0 1 loop: П→Х 0 * F L0 loop С/П

  :l    лістинг з поясненням кожного кроку
  :g    та сама програма таблицею 10×10
  :s    один крок і рядок, що змінилось
  :s 5  п'ять кроків
  :r    регістри R0..RE
  :r 1=25    записати 25 в R1 без клавіш Х→П

Команди програми, не клавіші калькулятора
  :help    :h    ?      ця довідка
  :keys    :k           кожна клавіша: що писати і що вона робить
  :p                    список прикладів
  :p <номер або ім'я>   завантажити: 1..7, easter, factorial, quadratic,
                        hypotenuse, indirect_sum, simpson, moon_lander
  :asm                  ввести програму текстом
  :l       :prog        лістинг
  :g                    таблиця 10×10
  :run                  пуск до команди С/П у програмі
  :s       :step [N]    один крок або N кроків
  :pc <адреса>          поставити лічильник, 00..104 або -0..-4
  :r       :regs        регістри і стек підпрограм
  :r N=число            записати регістр, наприклад :r A=3.14
  :rad :deg :grad       радіани, градуси, гради
  :avt :prg             рахунок або запис програми
  :save файл            зберегти стан у файл
  :load файл            прочитати .mk61 або текст програми
  :clear                стерти тільки програму
  :reset                як вимкнути живлення і ввімкнути знову
  :q       :quit        вихід
)");
}

void print_cli_usage(std::string_view prog_name) {
  std::println("Емулятор програмованого мікрокалькулятора «Електроніка МК-61» (консольна версія)");
  std::println("\nВикористання:");
  std::println("  {}                    Запуск в інтерактивному режимі (REPL)", prog_name);
  std::println("  {} [опції]            Пакетне виконання з аргументами командного рядка\n",
               prog_name);
  std::println("Опції командного рядка:");
  std::println("  -h, --help              Показати цю довідку та вийти");
  std::println("  -p, --preset <id|1..7>  Завантажити вбудовану програму:");
  std::println("                            1 або easter       — Дата Великодня (Пасхалія, 10×10)");
  std::println("                            2 або factorial    — Факторіал числа (n!)");
  std::println("                            3 або quadratic    — Квадратне рівняння (ax²+bx+c=0)");
  std::println("                            4 або hypotenuse   — Гіпотенуза і кут трикутника");
  std::println("                            5 або indirect_sum — Сума масиву R5..R9 (К П→Х 4)");
  std::println("                            6 або simpson      — Інтеграл sin(x) методом Сімпсона");
  std::println("                            7 або moon_lander  — Гра «Лунолёт-1»");
  std::println(
    "  -f, --file <шлях>       Завантажити програму (.txt/.asm) або стан (.mk61) із файлу");
  std::println("  -a, --asm <код>         Асемблювати програму з текстового рядка");
  std::println("  --angle <rad|deg|grad>  Задати перемикач кутів: rad (Р), deg (ГРД), grad (Г)");
  std::println("  -x <число>              Задати початкове значення регістра X");
  std::println("  -y <число>              Задати початкове значення регістра Y");
  std::println(
    "  --reg <N>=<число>       Задати значення регістра пам'яті R0..RE (напр. --reg 1=2.5)");
  std::println("  -k, --keys <клавіші>    Натиснути послідовність клавіш (напр. \"2 В↑ 3 +\")");
  std::println("  -r, --run               Виконати програму в пам'яті від поточного PC до С/П");
  std::println("  -l, --list              Вивести детальний дизасемблерний лістинг програми");
  std::println("  -g, --grid              Вивести програму компактною таблицею 10×10");
  std::println(
    "  --batch                 Завершити роботу після виконання аргументів (без REPL)\n");
  std::println("Приклади запуску з командного рядка:");
  std::println("  {} --keys \"2 В↑ 10 F ↔\"", prog_name);
  std::println("  {} --preset easter -x 2026 --run", prog_name);
  std::println("  {} --preset factorial -x 7 --list --run", prog_name);
  std::println("  {} --preset quadratic --reg 1=1 --reg 2=-5 --reg 3=6 --run", prog_name);
}

bool load_file_into_calculator(Calculator &calc, const std::string &path, std::string &err) {
  std::ifstream ifs(path);
  if (!ifs) {
    err = std::format("Не вдалося відкрити файл '{}'", path);
    return false;
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  const std::string content = oss.str();

  if (content.find("PROGRAM=") != std::string::npos ||
      content.find("ANGLE=") != std::string::npos) {
    return calc.deserialize_state(content, err);
  }
  const auto res = calc.assemble_and_load(content);
  if (!res.ok) {
    err = res.error_message;
    return false;
  }
  return true;
}

bool apply_preset_to_calculator(Calculator &calc, std::string_view name) {
  const auto *preset = mk61::core::find_builtin_program(name);
  if (preset == nullptr) {
    return false;
  }
  const auto res = calc.assemble_and_load(preset->source_code);
  if (!res.ok) {
    return false;
  }
  if (preset->id == "moon_lander") {
    calc.set_memory_reg(1, 1000.0);
    calc.set_memory_reg(2, -50.0);
    calc.set_memory_reg(3, 250.0);
  } else if (preset->id == "indirect_sum") {
    calc.set_memory_reg(5, 10.0);
    calc.set_memory_reg(6, 20.0);
    calc.set_memory_reg(7, 30.0);
    calc.set_memory_reg(8, 40.0);
    calc.set_memory_reg(9, 50.0);
  } else if (preset->id == "quadratic") {
    calc.set_memory_reg(1, 1.0);
    calc.set_memory_reg(2, -5.0);
    calc.set_memory_reg(3, 6.0);
  }
  return true;
}

bool handle_reg_assignment(Calculator &calc, std::string_view spec) {
  spec = trim_sv(spec);
  const auto eq = spec.find('=');
  if (eq == std::string_view::npos) {
    return false;
  }
  std::string_view r_part = trim_sv(spec.substr(0, eq));
  if (r_part.size() == 2 && (r_part[0] == 'R' || r_part[0] == 'r')) {
    r_part.remove_prefix(1);
  }
  const auto r = mk61::core::parse_reg_index(r_part);
  if (!r.has_value()) {
    return false;
  }
  const std::string val_str(trim_sv(spec.substr(eq + 1)));
  char *endptr = nullptr;
  const double val = std::strtod(val_str.c_str(), &endptr);
  if (endptr == val_str.c_str()) {
    return false;
  }
  calc.set_memory_reg(*r, val);
  std::println("Регістр R{} = {}", mk61::core::reg_index_to_char(*r),
               Calculator::format_register_value(calc.raw_memory_reg(*r)));
  return true;
}

} // namespace

int main(int argc, char *argv[]) {
  Calculator calc;
  bool batch_mode = false;
  bool do_run = false;
  bool do_list = false;
  bool do_grid = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      print_cli_usage(argv[0]);
      return 0;
    }
    if (arg == "--batch") {
      batch_mode = true;
    } else if (arg == "-r" || arg == "--run") {
      do_run = true;
      batch_mode = true;
    } else if (arg == "-l" || arg == "--list") {
      do_list = true;
      batch_mode = true;
    } else if (arg == "-g" || arg == "--grid") {
      do_grid = true;
      batch_mode = true;
    } else if ((arg == "-p" || arg == "--preset") && i + 1 < argc) {
      const std::string_view pid(argv[++i]);
      if (!apply_preset_to_calculator(calc, pid)) {
        std::println(stderr, "Помилка: невідома вбудована програма '{}'", pid);
        return 1;
      }
    } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
      std::string err;
      if (!load_file_into_calculator(calc, argv[++i], err)) {
        std::println(stderr, "Помилка: {}", err);
        return 1;
      }
    } else if ((arg == "-a" || arg == "--asm") && i + 1 < argc) {
      const auto res = calc.assemble_and_load(argv[++i]);
      if (!res.ok) {
        std::println(stderr, "Помилка асемблювання: {}", res.error_message);
        return 1;
      }
    } else if (arg == "--angle" && i + 1 < argc) {
      const std::string_view m(argv[++i]);
      if (m == "rad" || m == "Р" || m == "р") {
        calc.set_angle_mode(AngleMode::Radians);
      } else if (m == "deg" || m == "ГРД" || m == "грд") {
        calc.set_angle_mode(AngleMode::Degrees);
      } else if (m == "grad" || m == "Г" || m == "г") {
        calc.set_angle_mode(AngleMode::Grads);
      } else {
        std::println(stderr, "Невідомий режим кутів '{}'", m);
        return 1;
      }
    } else if (arg == "-x" && i + 1 < argc) {
      calc.enter_number(std::strtod(argv[++i], nullptr));
    } else if (arg == "-y" && i + 1 < argc) {
      calc.set_reg_y(std::strtod(argv[++i], nullptr));
    } else if (arg == "--reg" && i + 1 < argc) {
      if (!handle_reg_assignment(calc, argv[++i])) {
        std::println(stderr, "Невірний формат --reg (очікується N=значення, напр. 1=5)");
        return 1;
      }
    } else if ((arg == "-k" || arg == "--keys") && i + 1 < argc) {
      std::string err;
      if (!calc.press_tokens(argv[++i], err)) {
        std::println(stderr, "Помилка вводу клавіш: {}", err);
        return 1;
      }
      batch_mode = true;
    } else {
      std::println(stderr, "Невідомий аргумент '{}'. Використайте --help.", arg);
      return 1;
    }
  }

  if (do_list) {
    print_program_listing(calc);
  }
  if (do_grid) {
    std::print("{}", mk61::core::format_program_compact_grid(calc.program_memory()));
  }

  if (do_run) {
    const auto status = calc.run_program();
    if (status == mk61::core::RunStatus::StepLimitReached) {
      std::println(stderr, "Увага: перевищено ліміт кроків виконання програми.");
    }
  }

  if (batch_mode) {
    print_panel(calc);
    return calc.is_error() ? 2 : 0;
  }

  print_startup_banner();
  print_panel(calc);

  std::string line;
  while (true) {
    const auto mode_tag = (calc.operating_mode() == OperatingMode::Automatic) ? "АВТ" : "ПРГ";
    std::print("\nМК-61 [{} | PC={}]> ", mode_tag, mk61::core::format_step_address(calc.pc()));
    std::cout.flush();
    if (!std::getline(std::cin, line)) {
      std::println("");
      break;
    }

    const std::string_view sv = trim_sv(line);
    if (sv.empty()) {
      print_panel(calc);
      continue;
    }

    if (sv == ":q" || sv == ":quit" || sv == ":exit") {
      break;
    }
    if (sv == "?" || sv == ":help" || sv == ":h") {
      print_repl_help();
      continue;
    }
    if (sv == ":keys" || sv == ":k") {
      print_keyboard_layout();
      continue;
    }
    if (sv == ":regs" || sv == ":r") {
      print_registers(calc);
      continue;
    }
    if (sv.starts_with(":reg ") ||
        (sv.starts_with(":r ") && sv.find('=') != std::string_view::npos)) {
      const auto space_pos = sv.find(' ');
      if (!handle_reg_assignment(calc, sv.substr(space_pos + 1))) {
        std::println("Помилка: вкажіть регістр і значення у форматі ':reg 1=25' або ':r A=3.14'.");
      }
      continue;
    }
    if (sv == ":list" || sv == ":l" || sv == ":prog") {
      print_program_listing(calc);
      continue;
    }
    if (sv == ":grid" || sv == ":g") {
      std::println("Таблиця пам'яті програм МК-61 (по 10 кроків у рядку):");
      std::print("{}", mk61::core::format_program_compact_grid(calc.program_memory()));
      continue;
    }
    if (sv == ":presets" || sv == ":p" || sv == ":preset") {
      print_presets();
      continue;
    }
    if (sv.starts_with(":load-preset ") || sv.starts_with(":preset ") || sv.starts_with(":p ")) {
      const auto space_pos = sv.find(' ');
      const std::string_view arg = trim_sv(sv.substr(space_pos + 1));
      const auto *preset = mk61::core::find_builtin_program(arg);
      if (preset == nullptr) {
        std::println("Невідомий приклад '{}'. Введіть ':p' для списку (1..7).", arg);
        continue;
      }
      apply_preset_to_calculator(calc, arg);
      std::println("Завантажено приклад: «{}»", preset->title);
      std::println("Підказка: {}", preset->usage_hint);
      print_program_listing(calc);
      print_panel(calc);
      continue;
    }
    if (sv == ":asm" || sv.starts_with(":asm ")) {
      std::string_view inline_src = (sv.size() > 4) ? trim_sv(sv.substr(4)) : std::string_view{};
      std::string full_src;
      if (!inline_src.empty()) {
        full_src = std::string(inline_src);
      } else {
        std::println("Введіть або вставте програму (мнемоніками чи таблицею 10×10).");
        std::println("Для завершення вводу натисніть Enter на порожньому рядку або введіть END:");
        std::string aline;
        while (std::getline(std::cin, aline)) {
          const std::string_view trimmed = trim_sv(aline);
          if (trimmed == "END" || trimmed == "end" || trimmed.empty()) {
            break;
          }
          full_src += aline + "\n";
        }
      }
      const auto res = calc.assemble_and_load(full_src);
      if (!res.ok) {
        std::println("Помилка асемблювання: {}", res.error_message);
      } else {
        std::println("Програму успішно записано в пам'ять ({} кроків).", res.steps_used);
        print_program_listing(calc);
        print_panel(calc);
      }
      continue;
    }
    if (sv == ":run") {
      calc.set_operating_mode(OperatingMode::Automatic);
      const auto st = calc.run_program();
      if (st == mk61::core::RunStatus::StepLimitReached) {
        std::println("Зупинено: досягнуто ліміту 100000 кроків.");
      }
      print_panel(calc);
      continue;
    }
    if (sv == ":step" || sv == ":s" || sv.starts_with(":step ") || sv.starts_with(":s ")) {
      int count = 1;
      const auto space_pos = sv.find(' ');
      if (space_pos != std::string_view::npos) {
        count = std::max(1, std::atoi(std::string(trim_sv(sv.substr(space_pos + 1))).c_str()));
      }
      calc.set_operating_mode(OperatingMode::Automatic);
      for (int s = 0; s < count; ++s) {
        const int cur_pc = calc.pc();
        const auto op = calc.program_memory()[static_cast<std::size_t>(cur_pc)];
        const auto info = mk61::core::opcode_info(op);
        const bool cont = calc.step_program();
        std::println("  [Крок {}] {} ({:<8}) -> PC={}, X={}, Y={}",
                     mk61::core::format_step_address(cur_pc), info.display_code, info.mnemonic,
                     mk61::core::format_step_address(calc.pc()),
                     Calculator::format_register_value(calc.raw_x()),
                     Calculator::format_register_value(calc.raw_y()));
        if (!cont) {
          break;
        }
      }
      print_panel(calc);
      continue;
    }
    if (sv.starts_with(":pc ")) {
      const auto addr = mk61::core::parse_step_address(sv.substr(4));
      if (addr.has_value()) {
        calc.set_pc(*addr);
        print_panel(calc);
      } else {
        std::println("Невірна адреса кроку (допустимо 00..104 або -0..-4).");
      }
      continue;
    }
    if (sv == ":rad") {
      calc.set_angle_mode(AngleMode::Radians);
      print_panel(calc);
      continue;
    }
    if (sv == ":deg") {
      calc.set_angle_mode(AngleMode::Degrees);
      print_panel(calc);
      continue;
    }
    if (sv == ":grad") {
      calc.set_angle_mode(AngleMode::Grads);
      print_panel(calc);
      continue;
    }
    if (sv == ":avt") {
      calc.set_operating_mode(OperatingMode::Automatic);
      print_panel(calc);
      continue;
    }
    if (sv == ":prg") {
      calc.set_operating_mode(OperatingMode::Programming);
      print_panel(calc);
      continue;
    }
    if (sv == ":reset") {
      calc.reset();
      print_panel(calc);
      continue;
    }
    if (sv == ":clear") {
      calc.clear_program();
      print_panel(calc);
      continue;
    }
    if (sv.starts_with(":save ")) {
      const std::string path(trim_sv(sv.substr(6)));
      std::ofstream ofs(path);
      if (!ofs) {
        std::println("Не вдалося відкрити файл '{}' для запису.", path);
      } else {
        ofs << calc.serialize_state();
        std::println("Стан збережено у файл '{}'.", path);
      }
      continue;
    }
    if (sv.starts_with(":load ")) {
      const std::string path(trim_sv(sv.substr(6)));
      std::string err;
      if (!load_file_into_calculator(calc, path, err)) {
        std::println("Помилка завантаження: {}", err);
      } else {
        std::println("Завантажено з файлу '{}'.", path);
        print_program_listing(calc);
        print_panel(calc);
      }
      continue;
    }

    std::string err;
    if (!calc.press_tokens(sv, err)) {
      std::println("Помилка: {} (введіть ':help' для списку клавіш і команд)", err);
    }
    print_panel(calc);
  }

  return 0;
}
