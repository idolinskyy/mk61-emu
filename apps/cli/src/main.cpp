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
    return "[Адреса: 1-ша цифра]";
  case PrefixState::WaitAddr2:
    return "[Адреса: 2-га цифра]";
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
  const auto disp = calc.display();
  const auto mode_str = (calc.operating_mode() == OperatingMode::Automatic) ? "АВТ" : "ПРГ";
  const auto pc_str = mk61::core::format_step_address(calc.pc());
  const auto op_at_pc = calc.program_memory()[static_cast<std::size_t>(calc.pc())];
  const auto op_info = mk61::core::opcode_info(op_at_pc);

  std::println("┌──────────────────────────────────────────────────────────────────┐");
  std::println("│ ЕЛЕКТРОНІКА МК-61   Режим: {:<4} Кути: {:<13} Префікс: {:<3} │", mode_str,
               angle_mode_label(calc.angle_mode()), prefix_label(calc.prefix_state()));
  std::println("├──────────────────────────────────────────────────────────────────┤");
  std::println("│   ІНДИКАТОР ВЛІ:  [ {:<16} ]   Крок PC: {} ({}: {}) │", disp.text, pc_str,
               op_info.display_code, op_info.mnemonic);
  std::println("├──────────────────────────────────────────────────────────────────┤");
  std::println("│ Стек:  T = {:<12}  Z = {:<12}  Y = {:<12}      │",
               Calculator::format_register_value(calc.raw_t()),
               Calculator::format_register_value(calc.raw_z()),
               Calculator::format_register_value(calc.raw_y()));
  std::println("│        X = {:<12}  X1= {:<12}  Виконано кроків: {:<5}│",
               Calculator::format_register_value(calc.raw_x()),
               Calculator::format_register_value(calc.raw_x1()), calc.total_steps_executed());
  std::println("└──────────────────────────────────────────────────────────────────┘");
}

void print_registers(const Calculator &calc) {
  std::println("┌──────────────────── РЕГІСТРИ ПАМ'ЯТІ (R0..RE) ───────────────────┐");
  for (std::size_t row = 0; row < 5; ++row) {
    const std::size_t i0 = row;
    const std::size_t i1 = row + 5;
    const std::size_t i2 = row + 10;
    std::println("│  R{}: {:<14}   R{}: {:<14}   R{} ({}): {:<14} │",
                 mk61::core::reg_index_to_char(static_cast<std::uint8_t>(i0)),
                 Calculator::format_register_value(calc.raw_memory_reg(i0)),
                 mk61::core::reg_index_to_char(static_cast<std::uint8_t>(i1)),
                 Calculator::format_register_value(calc.raw_memory_reg(i1)),
                 mk61::core::reg_index_to_char(static_cast<std::uint8_t>(i2)),
                 mk61::core::reg_index_to_mk61_glyph(static_cast<std::uint8_t>(i2)),
                 Calculator::format_register_value(calc.raw_memory_reg(i2)));
  }
  std::print("│ Стек повернень ПП (SP={}): ", calc.return_sp());
  if (calc.return_sp() == 0) {
    std::print("—");
  } else {
    for (std::size_t i = 0; i < calc.return_sp(); ++i) {
      std::print("{} ", mk61::core::format_step_address(calc.return_stack()[i]));
    }
  }
  std::println("\n└──────────────────────────────────────────────────────────────────┘");
}

void print_keyboard_layout() {
  std::println("┌─────────────────── КЛАВІАТУРА МК-61 (30 КЛАВІШ) ─────────────────┐");
  std::println("│ Формат: [Основна клавіша]  (F: жовта функція | К: синя функція)  │");
  std::println("├──────────────────────────────────────────────────────────────────┤");
  const auto &keys = mk61::core::all_keys();
  for (std::size_t r = 0; r < mk61::core::kKeyRows; ++r) {
    std::print("│ Ряд {}: ", r + 1);
    for (std::size_t c = 0; c < mk61::core::kKeyCols; ++c) {
      const auto &k = keys[r * mk61::core::kKeyCols + c];
      std::string cell = std::format("[{}]", k.primary_label);
      if (!k.f_label.empty() || !k.k_label.empty()) {
        cell += std::format("(F:{}/К:{})", k.f_label.empty() ? "-" : k.f_label,
                            k.k_label.empty() ? "-" : k.k_label);
      }
      std::print("{:<18} ", cell);
    }
    std::println("");
  }
  std::println("│ Буквені регістри a..e на нижньому ряді: .=a, /-/=b, ВП=c, Сх=d, В↑=e │");
  std::println("└──────────────────────────────────────────────────────────────────┘");
}

void print_program_listing(const Calculator &calc, bool all_steps = false) {
  const auto steps = mk61::core::disassemble_program(calc.program_memory(), all_steps);
  std::println("┌──────┬─────────┬──────────────────┬────────────────────────────────────────┐");
  std::println("│ Крок │ Код ВЛІ │ Мнемоніка        │ Опис операції                          │");
  std::println("├──────┼─────────┼──────────────────┼────────────────────────────────────────┤");
  for (const auto &s : steps) {
    const std::string marker = (s.step == calc.pc()) ? "►" : " ";
    const std::string code_col =
      s.arg_byte.has_value() ? std::format("{} {:02X}", s.opcode_str, *s.arg_byte) : s.opcode_str;
    std::println("│ {}{}  │ {:<7} │ {:<16} │ {:<38} │", marker, s.step_str, code_col, s.mnemonic,
                 s.description);
  }
  std::println("└──────┴─────────┴──────────────────┴────────────────────────────────────────┘");
}

void print_presets() {
  std::println("┌──────────────────── ВБУДОВАНІ ПРОГРАМИ ДЛЯ МК-61 ────────────────────┐");
  const auto presets = mk61::core::builtin_programs();
  for (std::size_t i = 0; i < presets.size(); ++i) {
    std::println("  {}. [{}] — {}", i + 1, presets[i].id, presets[i].title);
    std::println("     Опис: {}", presets[i].description);
    std::println("     Як запускати: {}", presets[i].usage_hint);
    std::println("     Команда завантаження:  :p {}   (або :preset {})", i + 1, presets[i].id);
  }
  std::println("└──────────────────────────────────────────────────────────────────────┘");
}

void print_repl_help() {
  std::println(R"(┌─────────────────── ДОВІДКА КОНСОЛЬНОГО ЕМУЛЯТОРА МК-61 ───────────────────┐
│                                                                           │
│ А. ШВИДКИЙ СТАРТ (ЯК ЦЕ ПРАЦЮЄ):                                          │
│   У консолі ви можете вводити або натискання клавіш калькулятора          │
│   (через пробіл), або службові команди емулятора (починаються з ':').     │
│                                                                           │
│   1) Обчислення вручну (зворотний польський запис — ОПЗ, без клавіші '='):│
│      МК-61> 2 В↑ 3 +                 (результат: 5.)                      │
│      МК-61> 2 ^ 10 F ↔               (2^10 = 1024., бо ^ це В↑, F ↔ = x^y)│
│      МК-61> :deg                     (перемкнути кути в градуси ГРД)      │
│      МК-61> 30 F 7                   (sin 30° = 0.5, бо над '7' — sin)    │
│                                                                           │
│   2) Як переглянути, завантажити й виконати готові приклади програм:      │
│      МК-61> :p                       (показати список 7 прикладів)        │
│      МК-61> :p 1                     (завантажити приклад №1 «Пасхалія»)  │
│      МК-61> 2026 В/О С/П             (ввести 2026 рік, скинути PC=00 і    │
│                                       запустити: X=4 квітень, Y=12 число) │
│      МК-61> :p 2                     (завантажити приклад №2 «Факторіал») │
│      МК-61> 6 В/О :run               (обчислити 6! = 720.)                │
│                                                                           │
│   3) Як написати або вставити власну програму (мнемоніками чи 10×10):     │
│      МК-61> :asm                     (увімкнеться багаторядковий ввід;    │
│                                       вставте текст програми і натисніть  │
│                                       Enter на порожньому рядку або END)  │
│      Або в один рядок:                                                    │
│      МК-61> :asm Х→П 0 1 loop: П→Х 0 * F L0 loop С/П                      │
│                                                                           │
│   4) Як покроково відлагоджувати програму (Debugger):                     │
│      МК-61> В/О                      (встановити PC на початок 00)        │
│      МК-61> :s                       (виконати 1 крок і показати трасу)   │
│      МК-61> :s 5                     (виконати 5 кроків поспіль)          │
│                                                                           │
├───────────────────────────────────────────────────────────────────────────┤
│ Б. ПОВНИЙ ПЕРЕЛІК СЛУЖБОВИХ КОМАНД (з короткими синонімами):              │
│   :help  (:h, ?)          — показати цю довідку                           │
│   :keys  (:k)             — показати схему всіх 30 клавіш МК-61 (F та К)  │
│   :presets (:p)           — список вбудованих прикладів програм (1..7)    │
│   :preset <№|id> (:p <№>) — завантажити приклад за номером (1..7) або id  │
│   :list  (:l, :prog)      — детальний лістинг програми в пам'яті з описом │
│   :grid  (:g)             — компактна таблиця програми 10×10 (формат ПМК) │
│   :asm [код]              — ввести програму (багаторядково або в 1 рядок) │
│   :run                    — виконати програму від поточного PC до С/П     │
│   :step [N] (:s [N])      — покрокове відлагодження: 1 або N кроків       │
│   :pc <крок>              — переставити лічильник PC (00..104 або -0..-4) │
│   :regs  (:r)             — показати всі 15 регістрів пам'яті R0..RE      │
│   :reg <N>=<знач>         — записати число у регістр R0..RE (напр. :r 1=5)│
│   :rad | :deg | :grad     — перемикач одиниць кутів: Р / ГРД / Г          │
│   :avt | :prg             — перемикач режиму роботи: АВТ / ПРГ            │
│   :save <файл>            — зберегти стан і програму у файл (.mk61)       │
│   :load <файл>            — завантажити програму (.txt) або стан (.mk61)  │
│   :clear                  — очистити тільки пам'ять програм (00..104)     │
│   :reset                  — повне скидання калькулятора                   │
│   :quit  (:q, :exit)      — вийти з програми                              │
├───────────────────────────────────────────────────────────────────────────┤
│ В. ЯК ВВОДИТИ КЛАВІШІ ТА КОМАНДИ МК-61 У РЯДКУ:                           │
│   • Стек і введення: В↑ (або ^, ENT), ↔ (або <->, XY), Сх (CX), ВП (EE),  │
│                      /-/ (або +/-, CHS), . (крапка).                      │
│   • Пам'ять (пряма): П 0..П E (або Х→П 0..Х→П E, П0..ПE) — запис у R0..RE│
│                      ИП 0..ИП E (або П→Х 0..П→Х E, ИП0..ИПE) — читання.   │
│   • Пам'ять (непряма): К П 0..E (КП0..КПE), К ИП 0..E (КИП0..КИПE).       │
│   • Керування: В/О (або В/0, RTN), С/П (СП, SP), БП <адр>, ПП <адр>,      │
│                ШГ→ (SST), ←ШГ (BST).                                      │
│   • Умови та цикли (в режимі ПРГ або :asm):                               │
│     x=0 <адр>, x!=0 <адр>, x<0 <адр>, x>=0 <адр>, L0..L3 <адр>.           │
└───────────────────────────────────────────────────────────────────────────┘)");
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
    calc.set_memory_reg(1, 1000.0); // H = 1000 м
    calc.set_memory_reg(2, -50.0);  // V = -50 м/с
    calc.set_memory_reg(3, 250.0);  // M = 250 кг палива
  } else if (preset->id == "indirect_sum") {
    calc.set_memory_reg(5, 10.0);
    calc.set_memory_reg(6, 20.0);
    calc.set_memory_reg(7, 30.0);
    calc.set_memory_reg(8, 40.0);
    calc.set_memory_reg(9, 50.0);
  } else if (preset->id == "quadratic") {
    calc.set_memory_reg(1, 1.0);  // a = 1
    calc.set_memory_reg(2, -5.0); // b = -5
    calc.set_memory_reg(3, 6.0);  // c = 6 (корені 3 та 2)
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

  // Інтерактивний консольний режим (REPL)
  std::println("╔══════════════════════════════════════════════════════════════════╗");
  std::println("║     ЕМУЛЯТОР МІКРОКАЛЬКУЛЯТОРА «ЕЛЕКТРОНІКА МК-61» (КОНСОЛЬ)     ║");
  std::println("╠══════════════════════════════════════════════════════════════════╣");
  std::println("║ • Обчислення ОПЗ:  2 В↑ 3 +   |   2 ^ 10 F ↔   |   30 :deg F 7   ║");
  std::println("║ • Приклади (1..7): :p (список) -> :p 1 (завантажити) -> 2026 С/П ║");
  std::println("║ • Своя програма:   :asm (ввід) | :l (лістинг)  | :g (табл. 10×10)║");
  std::println("║ • Відлагодження:   :s (1 крок) | :s 5 (5 кр.)  | :r (регістри)   ║");
  std::println("║ • Повна довідка:   :help (або ?) | Схема клавіш: :keys | Вихід: :q║");
  std::println("╚══════════════════════════════════════════════════════════════════╝");
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

    // Інакше трактуємо введений рядок як послідовність клавіш / чисел МК-61
    std::string err;
    if (!calc.press_tokens(sv, err)) {
      std::println("Помилка: {} (введіть ':help' для списку клавіш і команд)", err);
    }
    print_panel(calc);
  }

  return 0;
}
