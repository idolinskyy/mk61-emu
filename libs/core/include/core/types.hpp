#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mk61::core {

// Це не запас «на всяк випадок», а формат самої команди.
// Номер регістра — півбайт 0..E у коді Х→П / П→Х. Адреса кроку — один байт:
// 00..99 і A0..A4, тобто рівно 105 кроків. Шостий виклик ПП уже дає ЕГГОГ.
inline constexpr std::size_t kProgramSize = 105;
inline constexpr std::size_t kRegisterCount = 15;
inline constexpr std::size_t kReturnStackSize = 5;
inline constexpr std::size_t kDisplayDigits = 12;
inline constexpr std::size_t kKeyRows = 6;
inline constexpr std::size_t kKeyCols = 5;
inline constexpr std::size_t kKeyCount = kKeyRows * kKeyCols;

/// Перемикач розмірності кутів (Р — ГРД — Г)
enum class AngleMode : std::uint8_t {
  Radians, // Р (радіани)
  Degrees, // ГРД (градуси)
  Grads,   // Г (гради, 100 град = 90°)
};

/// Режим роботи мікрокалькулятора
enum class OperatingMode : std::uint8_t {
  Automatic,   // АВТ (обчислення / виконання програми)
  Programming, // ПРГ (введення / редагування програми)
};

/// Стан префікса та очікування аргументу багатоходової команди
enum class PrefixState : std::uint8_t {
  None,
  F,                 // Натиснуто жовту клавішу F
  K,                 // Натиснуто синю клавішу К
  WaitStoreReg,      // Очікування номера регістра (0..9, A..E) після Х→П
  WaitRecallReg,     // Очікування номера регістра (0..9, A..E) після П→Х
  WaitKStoreReg,     // Очікування номера регістра після К Х→П (непрямий запис)
  WaitKRecallReg,    // Очікування номера регістра після К П→Х (непряме читання)
  WaitKGotoReg,      // Очікування номера регістра після К БП (непрямий перехід)
  WaitKGosubReg,     // Очікування номера регістра після К ПП (непряма підпрограма)
  WaitKIfNotZeroReg, // Очікування номера регістра після К x≠0
  WaitKIfGeZeroReg,  // Очікування номера регістра після К x≥0
  WaitKIfLtZeroReg,  // Очікування номера регістра після К x<0
  WaitKIfEqZeroReg,  // Очікування номера регістра після К x=0
  WaitAddr1,         // Очікування першої цифри адреси переходу (БП, ПП, F x.., F L..)
  WaitAddr2,         // Очікування другої цифри адреси переходу
};

/// Чим закінчилось виконання програми в автоматичному режимі
enum class RunStatus : std::uint8_t {
  StoppedAtSP,      // Зупинено на команді С/П (50)
  Error,            // Сталася помилка (ЕГГОГ)
  StepLimitReached, // Досягнуто ліміту кроків (захист від нескінченного циклу)
};

/// Усі 30 фізичних клавіш МК-61 (6 рядів по 5 клавіш)
enum class Key : std::uint8_t {
  // Ряд 1 (верхній): керування програмою та умовні переходи
  F = 0,    // F (жовта префіксна клавіша)
  StepFwd,  // ШГ→  | F: x<0  | К: x<0 (непрямий)
  StepBack, // ←ШГ  | F: x=0  | К: x=0 (непрямий)
  Return,   // В/О  | F: x≥0  | К: x≥0 (непрямий)
  RunStop,  // С/П  | F: x≠0  | К: x≠0 (непрямий)

  // Ряд 2: пам'ять, переходи та префікс К
  K,      // К (синя префіксна клавіша)
  Store,  // Х→П  | F: L1   | К: Х→П (непрямий)
  Recall, // П→Х  | F: L0   | К: П→Х (непрямий)
  Goto,   // БП   | F: L2   | К: БП (непрямий)
  Gosub,  // ПП   | F: L3   | К: ПП (непрямий)

  // Ряд 3: цифри 7-9, віднімання, ділення
  D7,     // 7    | F: sin    | К: [x]
  D8,     // 8    | F: cos    | К: {x}
  D9,     // 9    | F: tg     | К: max
  Minus,  // -    | F: √      | К: —
  Divide, // ÷    | F: 1/x    | К: —

  // Ряд 4: цифри 4-6, додавання, множення
  D4,       // 4    | F: sin⁻¹  | К: |x|
  D5,       // 5    | F: cos⁻¹  | К: ЗН
  D6,       // 6    | F: tg⁻¹   | К: ° ′ (Г→М)
  Plus,     // +    | F: Вх     | К: ° ′ (М→Г)
  Multiply, // ×    | F: x²     | К: —

  // Ряд 5: цифри 1-3, обмін, введення в стек
  D1,    // 1    | F: e^x    | К: (НОП 55)
  D2,    // 2    | F: lg     | К: (НОП 56)
  D3,    // 3    | F: ln     | К: ° ′ ″ (Г→МС)
  Swap,  // ↔    | F: x^y    | К: ° ′ ″ (МС→Г)
  Enter, // В↑   | F: В↻     | К: СЧ   | Регістр: e

  // Ряд 6 (нижній): 0, крапка, знак, порядок, скидання
  D0,     // 0    | F: 10^x   | К: НОП
  Dot,    // .    | F: π      | К: ∧    | Регістр: a (-)
  Sign,   // /-/  | F: АВТ    | К: ∨    | Регістр: b (L)
  Exp,    // ВП   | F: ПРГ    | К: ⊕    | Регістр: c (С)
  ClearX, // Сх   | F: CF     | К: инв  | Регістр: d (Г)
};

/// Метадані клавіші на панелі МК-61
struct KeyInfo {
  Key key;
  std::uint8_t row;                      // 0..5
  std::uint8_t col;                      // 0..4
  std::string_view primary_label;        // Напис на самій клавіші
  std::string_view f_label;              // Жовтий напис над клавішею (префікс F)
  std::string_view k_label;              // Синій напис над/під клавішею (префікс К)
  std::string_view reg_label;            // Позначення регістра (0..9, a..e)
  std::string_view cli_token;            // Коротка мнемоніка для консолі
  std::optional<std::uint8_t> reg_index; // Індекс регістра 0..14 (якщо клавіша задає регістр)
};

/// Стан 12-розрядного вакуумно-люмінесцентного індикатора (ВЛІ) МК-61
struct DisplayState {
  /// Текстове представлення індикатора (наприклад, " 3.1415926   " або "0E 10 50  03")
  std::string text;
  /// Мантиса / основна частина (знак + до 8 символів з крапкою)
  std::string mantissa_part;
  /// Порядок або адреса кроку програми (знак + 2 цифри)
  std::string exponent_part;
  /// Чи світиться індикатор у режимі помилки («ЕГГОГ»)
  bool is_error{false};
  /// Чи вимкнено дисплей (недокументований код 5F «Тьма»)
  bool is_dark{false};
};

} // namespace mk61::core
