#include "core/opcodes.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <map>
#include <sstream>
#include <tuple>
#include <utility>

namespace mk61::core {

namespace {

constexpr std::array<KeyInfo, kKeyCount> kAllKeys = {{
  // Ряд 1
  {Key::F, 0, 0, "F", "", "", "", "F", std::nullopt},
  {Key::StepFwd, 0, 1, "ШГ→", "x<0", "x<0", "", "ШГ→", std::nullopt},
  {Key::StepBack, 0, 2, "←ШГ", "x=0", "x=0", "", "←ШГ", std::nullopt},
  {Key::Return, 0, 3, "В/О", "x≥0", "x≥0", "", "В/О", std::nullopt},
  {Key::RunStop, 0, 4, "С/П", "x≠0", "x≠0", "", "С/П", std::nullopt},

  // Ряд 2
  {Key::K, 1, 0, "К", "", "", "", "К", std::nullopt},
  {Key::Store, 1, 1, "Х→П", "L1", "Х→П", "", "Х→П", std::nullopt},
  {Key::Recall, 1, 2, "П→Х", "L0", "П→Х", "", "П→Х", std::nullopt},
  {Key::Goto, 1, 3, "БП", "L2", "БП", "", "БП", std::nullopt},
  {Key::Gosub, 1, 4, "ПП", "L3", "ПП", "", "ПП", std::nullopt},

  // Ряд 3
  {Key::D7, 2, 0, "7", "sin", "[x]", "7", "7", std::uint8_t{7}},
  {Key::D8, 2, 1, "8", "cos", "{x}", "8", "8", std::uint8_t{8}},
  {Key::D9, 2, 2, "9", "tg", "max", "9", "9", std::uint8_t{9}},
  {Key::Minus, 2, 3, "-", "√", "", "", "-", std::nullopt},
  {Key::Divide, 2, 4, "÷", "1/x", "", "", "/", std::nullopt},

  // Ряд 4
  {Key::D4, 3, 0, "4", "sin⁻¹", "|x|", "4", "4", std::uint8_t{4}},
  {Key::D5, 3, 1, "5", "cos⁻¹", "ЗН", "5", "5", std::uint8_t{5}},
  {Key::D6, 3, 2, "6", "tg⁻¹", "°→′", "6", "6", std::uint8_t{6}},
  {Key::Plus, 3, 3, "+", "Вх", "′→°", "", "+", std::nullopt},
  {Key::Multiply, 3, 4, "×", "x²", "", "", "*", std::nullopt},

  // Ряд 5
  {Key::D1, 4, 0, "1", "eˣ", "", "1", "1", std::uint8_t{1}},
  {Key::D2, 4, 1, "2", "lg", "", "2", "2", std::uint8_t{2}},
  {Key::D3, 4, 2, "3", "ln", "°→′″", "3", "3", std::uint8_t{3}},
  {Key::Swap, 4, 3, "↔", "xʸ", "′″→°", "", "↔", std::nullopt},
  {Key::Enter, 4, 4, "В↑", "В↻", "СЧ", "e", "В↑", std::uint8_t{14}},

  // Ряд 6
  {Key::D0, 5, 0, "0", "10ˣ", "НОП", "0", "0", std::uint8_t{0}},
  {Key::Dot, 5, 1, ".", "π", "∧", "a", ".", std::uint8_t{10}},
  {Key::Sign, 5, 2, "/-/", "АВТ", "∨", "b", "/-/", std::uint8_t{11}},
  {Key::Exp, 5, 3, "ВП", "ПРГ", "⊕", "c", "ВП", std::uint8_t{12}},
  {Key::ClearX, 5, 4, "Сх", "CF", "инв", "d", "Сх", std::uint8_t{13}},
}};

std::string to_upper_ascii(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    if (ch >= 'a' && ch <= 'z') {
      out.push_back(static_cast<char>(ch - 'a' + 'A'));
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

std::string normalize_token(std::string_view s) {
  std::string raw = to_upper_ascii(s);
  std::string out;
  out.reserve(raw.size() + 4);
  for (std::size_t i = 0; i < raw.size(); ++i) {
    const auto b0 = static_cast<unsigned char>(raw[i]);
    // 2-байтові UTF-8 символи (кирилиця, грецькі, спецсимволи)
    if (b0 == 0xD0 && i + 1 < raw.size()) {
      const auto b1 = static_cast<unsigned char>(raw[i + 1]);
      std::uint8_t up_b1 = b1;
      if (b1 >= 0xB0 && b1 <= 0xBF) {
        up_b1 = static_cast<std::uint8_t>(b1 - 0x20); // а..п -> А..П
      }
      // Уніфікуємо кириличну Х (D0 A5) -> латинську 'X', а К (D0 9A) залишаємо кириличною К
      if (up_b1 == 0xA5) {
        out.push_back('X');
        ++i;
        continue;
      }
      out.push_back(static_cast<char>(0xD0));
      out.push_back(static_cast<char>(up_b1));
      ++i;
      continue;
    }
    if (b0 == 0xD1 && i + 1 < raw.size()) {
      const auto b1 = static_cast<unsigned char>(raw[i + 1]);
      if (b1 >= 0x80 && b1 <= 0x8F) {
        const auto up_b1 = static_cast<std::uint8_t>(b1 + 0x20); // р..я -> Р..Я
        if (up_b1 == 0xA5) {
          out.push_back('X');
        } else {
          out.push_back(static_cast<char>(0xD0));
          out.push_back(static_cast<char>(up_b1));
        }
        ++i;
        continue;
      }
      if (b1 == 0x96) {
        // 'і' -> 'І'
        out.push_back(static_cast<char>(0xD0));
        out.push_back(static_cast<char>(0x86));
        ++i;
        continue;
      }
    }
    // Грецька π (CF 80) -> Π (CE A0)
    if (b0 == 0xCF && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0x80) {
      out.push_back(static_cast<char>(0xCE));
      out.push_back(static_cast<char>(0xA0));
      ++i;
      continue;
    }
    // Верхній індекс ² (C2 B2) -> "^2"
    if (b0 == 0xC2 && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xB2) {
      out.append("^2");
      ++i;
      continue;
    }
    // Верхній індекс ˣ (CB A3) -> "^X"
    if (b0 == 0xCB && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xA3) {
      out.append("^X");
      ++i;
      continue;
    }
    // Верхній індекс ʸ (CA B8) -> "^Y"
    if (b0 == 0xCA && i + 1 < raw.size() && static_cast<unsigned char>(raw[i + 1]) == 0xB8) {
      out.append("^Y");
      ++i;
      continue;
    }
    // 3-байтові символи: ≥ (E2 89 A5), ≠ (E2 89 A0), → (E2 86 92)
    if (b0 == 0xE2 && i + 2 < raw.size()) {
      const auto b1 = static_cast<unsigned char>(raw[i + 1]);
      const auto b2 = static_cast<unsigned char>(raw[i + 2]);
      if (b1 == 0x89 && b2 == 0xA5) {
        out.append(">=");
        i += 2;
        continue;
      }
      if (b1 == 0x89 && b2 == 0xA0) {
        out.append("!=");
        i += 2;
        continue;
      }
      if (b1 == 0x86 && b2 == 0x92) {
        out.append("->");
        i += 2;
        continue;
      }
    }
    out.push_back(raw[i]);
  }
  return out;
}

std::string_view trim_sv(std::string_view sv) noexcept {
  while (!sv.empty() &&
         (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() &&
         (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
    sv.remove_suffix(1);
  }
  return sv;
}

std::optional<std::uint8_t> parse_hex_byte_exact(std::string_view s) noexcept {
  if (s.size() != 2) {
    return std::nullopt;
  }
  auto hex_val = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    return -1;
  };
  const int hi = hex_val(s[0]);
  const int lo = hex_val(s[1]);
  if (hi < 0 || lo < 0) {
    return std::nullopt;
  }
  return static_cast<std::uint8_t>((hi << 4) | lo);
}

} // namespace

const std::array<KeyInfo, kKeyCount> &all_keys() noexcept { return kAllKeys; }

const KeyInfo &key_info(Key key) noexcept {
  const auto idx = static_cast<std::size_t>(key);
  return kAllKeys[idx < kKeyCount ? idx : 0];
}

std::optional<Key> find_key_by_token(std::string_view token) noexcept {
  const std::string norm = normalize_token(trim_sv(token));
  if (norm.empty()) {
    return std::nullopt;
  }
  if (norm == "F") {
    return Key::F;
  }
  if (norm == "K" || norm == "К") {
    return Key::K;
  }
  if (norm == "ШГ->" || norm == "ШГ>" || norm == "SST" || norm == "STEP" || norm == "NEXT") {
    return Key::StepFwd;
  }
  if (norm == "<-ШГ" || norm == "<ШГ" || norm == "BST" || norm == "BACK" || norm == "PREV") {
    return Key::StepBack;
  }
  if (norm == "В/О" || norm == "В/0" || norm == "ВО" || norm == "B/O" || norm == "B/0" ||
      norm == "RTN" || norm == "RET") {
    return Key::Return;
  }
  if (norm == "С/П" || norm == "СП" || norm == "C/П" || norm == "C/P" || norm == "R/S" ||
      norm == "SP" || norm == "STOP") {
    return Key::RunStop;
  }
  if (norm == "X->П" || norm == "П" || norm == "XП" || norm == "STO") {
    return Key::Store;
  }
  if (norm == "П->X" || norm == "ИП" || norm == "ІП" || norm == "ПX" || norm == "RCL") {
    return Key::Recall;
  }
  if (norm == "БП" || norm == "GOTO" || norm == "JMP") {
    return Key::Goto;
  }
  if (norm == "ПП" || norm == "GSB" || norm == "GOSUB" || norm == "CALL") {
    return Key::Gosub;
  }
  if (norm == "0") {
    return Key::D0;
  }
  if (norm == "1") {
    return Key::D1;
  }
  if (norm == "2") {
    return Key::D2;
  }
  if (norm == "3") {
    return Key::D3;
  }
  if (norm == "4") {
    return Key::D4;
  }
  if (norm == "5") {
    return Key::D5;
  }
  if (norm == "6") {
    return Key::D6;
  }
  if (norm == "7") {
    return Key::D7;
  }
  if (norm == "8") {
    return Key::D8;
  }
  if (norm == "9") {
    return Key::D9;
  }
  if (norm == "." || norm == ",") {
    return Key::Dot;
  }
  if (norm == "/-/" || norm == "+/-" || norm == "/+/" || norm == "ЗНП" || norm == "CHS" ||
      norm == "NEG") {
    return Key::Sign;
  }
  if (norm == "ВП" || norm == "VP" || norm == "EE" || norm == "EXP" || norm == "EEX") {
    return Key::Exp;
  }
  if (norm == "СX" || norm == "CX" || norm == "CLX" || norm == "CLR") {
    return Key::ClearX;
  }
  if (norm == "В↑" || norm == "B↑" || norm == "↑" || norm == "ВВЕРX" || norm == "ENT" ||
      norm == "ENTER" || norm == "^") {
    return Key::Enter;
  }
  if (norm == "+") {
    return Key::Plus;
  }
  if (norm == "-") {
    return Key::Minus;
  }
  if (norm == "*" || norm == "×") {
    return Key::Multiply;
  }
  if (norm == "/" || norm == "÷" || norm == ":") {
    return Key::Divide;
  }
  if (norm == "↔" || norm == "<->" || norm == "XY" || norm == "X<>Y" || norm == "SWAP") {
    return Key::Swap;
  }
  return std::nullopt;
}

std::string_view nibble_to_mk61_glyph(std::uint8_t nibble) noexcept {
  static constexpr std::array<std::string_view, 16> kGlyphs = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-", "L", "C", "Г", "E", " "};
  return kGlyphs[nibble & 0x0FU];
}

char reg_index_to_char(std::uint8_t reg) noexcept {
  if (reg < 10) {
    return static_cast<char>('0' + reg);
  }
  if (reg < 15) {
    return static_cast<char>('A' + (reg - 10));
  }
  return '?';
}

std::string_view reg_index_to_mk61_glyph(std::uint8_t reg) noexcept {
  if (reg < 15) {
    return nibble_to_mk61_glyph(reg);
  }
  return " ";
}

std::optional<std::uint8_t> parse_reg_index(std::string_view text) noexcept {
  text = trim_sv(text);
  if (text.empty()) {
    return std::nullopt;
  }
  if (text.size() == 1) {
    const char c = text[0];
    if (c >= '0' && c <= '9') {
      return static_cast<std::uint8_t>(c - '0');
    }
    if (c == 'a' || c == 'A' || c == '-' || c == '.') {
      return std::uint8_t{10};
    }
    if (c == 'b' || c == 'B' || c == 'l' || c == 'L') {
      return std::uint8_t{11};
    }
    if (c == 'c' || c == 'C') {
      return std::uint8_t{12};
    }
    if (c == 'd' || c == 'D') {
      return std::uint8_t{13};
    }
    if (c == 'e' || c == 'E') {
      return std::uint8_t{14};
    }
  }
  const std::string norm = normalize_token(text);
  if (norm == "А") {
    return std::uint8_t{10};
  }
  if (norm == "В" || norm == "/-/") {
    return std::uint8_t{11};
  }
  if (norm == "С" || norm == "ВП") {
    return std::uint8_t{12};
  }
  if (norm == "Г" || norm == "СX") {
    return std::uint8_t{13};
  }
  if (norm == "Е" || norm == "В↑") {
    return std::uint8_t{14};
  }
  return std::nullopt;
}

std::string format_mk61_hex(std::uint8_t code) {
  const auto hi = static_cast<std::uint8_t>((code >> 4U) & 0x0FU);
  const auto lo = static_cast<std::uint8_t>(code & 0x0FU);
  std::string res;
  res.append(nibble_to_mk61_glyph(hi));
  res.append(nibble_to_mk61_glyph(lo));
  return res;
}

std::string format_step_address(int step) {
  int normalized = step % static_cast<int>(kProgramSize);
  if (normalized < 0) {
    normalized += static_cast<int>(kProgramSize);
  }
  if (normalized < 100) {
    return std::format("{:02d}", normalized);
  }
  return std::format("-{:1d}", normalized - 100);
}

std::uint8_t encode_step_address(int step) noexcept {
  int normalized = step % static_cast<int>(kProgramSize);
  if (normalized < 0) {
    normalized += static_cast<int>(kProgramSize);
  }
  const auto tens = static_cast<std::uint8_t>(normalized / 10);
  const auto ones = static_cast<std::uint8_t>(normalized % 10);
  return static_cast<std::uint8_t>((tens << 4U) | ones);
}

int decode_step_address(std::uint8_t addr_byte) noexcept {
  const int hi = (addr_byte >> 4U) & 0x0FU;
  const int lo = addr_byte & 0x0FU;
  const int step = hi * 10 + lo;
  if (step >= 0 && step < static_cast<int>(kProgramSize)) {
    return step;
  }
  return step % static_cast<int>(kProgramSize);
}

std::optional<int> parse_step_address(std::string_view text) noexcept {
  text = trim_sv(text);
  if (text.empty()) {
    return std::nullopt;
  }
  if (text.size() == 2 && (text[0] == '-' || text[0] == 'a' || text[0] == 'A') && text[1] >= '0' &&
      text[1] <= '4') {
    return 100 + (text[1] - '0');
  }
  int val = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    val = val * 10 + (c - '0');
    if (val >= static_cast<int>(kProgramSize)) {
      return std::nullopt;
    }
  }
  return val;
}

bool is_two_byte_opcode(std::uint8_t code) noexcept {
  return code == 0x51U || code == 0x53U || (code >= 0x57U && code <= 0x5EU);
}

OpcodeInfo opcode_info(std::uint8_t code) {
  OpcodeInfo info;
  info.code = code;
  info.byte_length = is_two_byte_opcode(code) ? 2U : 1U;
  info.display_code = format_mk61_hex(code);

  const auto hi = static_cast<std::uint8_t>((code >> 4U) & 0x0FU);
  const auto lo = static_cast<std::uint8_t>(code & 0x0FU);

  if (code <= 0x09U) {
    info.mnemonic = std::string(1, static_cast<char>('0' + code));
    info.description = std::format("Цифра {}", code);
    return info;
  }

  switch (code) {
  case 0x0AU:
    info.mnemonic = ".";
    info.description = "Десяткова крапка (кома)";
    return info;
  case 0x0BU:
    info.mnemonic = "/-/";
    info.description = "Зміна знака мантиси або порядку";
    return info;
  case 0x0CU:
    info.mnemonic = "ВП";
    info.description = "Введення порядку числа (експоненти)";
    return info;
  case 0x0DU:
    info.mnemonic = "Сх";
    info.description = "Очищення регістра X (скидання)";
    return info;
  case 0x0EU:
    info.mnemonic = "В↑";
    info.description = "Занесення числа в стек (X → Y → Z → T)";
    return info;
  case 0x0FU:
    info.mnemonic = "F Вх";
    info.description = "Виклик попереднього значення X1 у регістр X";
    return info;
  case 0x10U:
    info.mnemonic = "+";
    info.description = "Додавання (Y + X)";
    return info;
  case 0x11U:
    info.mnemonic = "-";
    info.description = "Віднімання (Y - X)";
    return info;
  case 0x12U:
    info.mnemonic = "×";
    info.description = "Множення (Y × X)";
    return info;
  case 0x13U:
    info.mnemonic = "÷";
    info.description = "Ділення (Y ÷ X)";
    return info;
  case 0x14U:
    info.mnemonic = "↔";
    info.description = "Обмін вмістом регістрів X та Y";
    return info;
  case 0x15U:
    info.mnemonic = "F 10ˣ";
    info.description = "Десяткова експонента (10^X)";
    return info;
  case 0x16U:
    info.mnemonic = "F eˣ";
    info.description = "Експонента (e^X)";
    return info;
  case 0x17U:
    info.mnemonic = "F lg";
    info.description = "Десятковий логарифм lg(X)";
    return info;
  case 0x18U:
    info.mnemonic = "F ln";
    info.description = "Натуральний логарифм ln(X)";
    return info;
  case 0x19U:
    info.mnemonic = "F arcsin";
    info.description = "Арксинус arcsin(X)";
    return info;
  case 0x1AU:
    info.mnemonic = "F arccos";
    info.description = "Арккосинус arccos(X)";
    return info;
  case 0x1BU:
    info.mnemonic = "F arctg";
    info.description = "Арктангенс arctg(X)";
    return info;
  case 0x1CU:
    info.mnemonic = "F sin";
    info.description = "Синус sin(X)";
    return info;
  case 0x1DU:
    info.mnemonic = "F cos";
    info.description = "Косинус cos(X)";
    return info;
  case 0x1EU:
    info.mnemonic = "F tg";
    info.description = "Тангенс tg(X)";
    return info;
  case 0x20U:
    info.mnemonic = "F π";
    info.description = "Константа π (3.1415926)";
    return info;
  case 0x21U:
    info.mnemonic = "F √";
    info.description = "Квадратний корінь √X";
    return info;
  case 0x22U:
    info.mnemonic = "F x²";
    info.description = "Піднесення до квадрата X²";
    return info;
  case 0x23U:
    info.mnemonic = "F 1/x";
    info.description = "Обернена величина 1/X";
    return info;
  case 0x24U:
    info.mnemonic = "F xʸ";
    info.description = "Піднесення до степеня X^Y, стек не опускається";
    return info;
  case 0x25U:
    info.mnemonic = "F В↻";
    info.description = "Кільцевий зсув стека (X → T → Z → Y → X)";
    return info;
  case 0x26U:
    info.mnemonic = "К М→Г";
    info.description = "Переведення градусів і мінут (ГГ.ММм) у частки градуса";
    return info;
  case 0x2AU:
    info.mnemonic = "К МС→Г";
    info.description = "Переведення градусів, мінут і секунд (ГГ.ММССс) у частки градуса";
    return info;
  case 0x30U:
    info.mnemonic = "К Г→МС";
    info.description = "Переведення часток градуса у градуси, мінути і секунди (ГГ.ММССс)";
    return info;
  case 0x31U:
    info.mnemonic = "К |x|";
    info.description = "Модуль числа |X|";
    return info;
  case 0x32U:
    info.mnemonic = "К ЗН";
    info.description = "Знак числа sign(X) (-1, 0, 1)";
    return info;
  case 0x33U:
    info.mnemonic = "К Г→М";
    info.description = "Переведення часток градуса у градуси і мінути (ГГ.ММм)";
    return info;
  case 0x34U:
    info.mnemonic = "К [x]";
    info.description = "Виділення цілої частини числа [X]";
    return info;
  case 0x35U:
    info.mnemonic = "К {x}";
    info.description = "Виділення дробової частини числа {X}";
    return info;
  case 0x36U:
    info.mnemonic = "К max";
    info.description = "Максимум із двох чисел max(X, Y)";
    return info;
  case 0x37U:
    info.mnemonic = "К ∧";
    info.description = "Порозрядне логічне І (AND) у форматі 8.xxxxxxx";
    return info;
  case 0x38U:
    info.mnemonic = "К ∨";
    info.description = "Порозрядне логічне АБО (OR) у форматі 8.xxxxxxx";
    return info;
  case 0x39U:
    info.mnemonic = "К ⊕";
    info.description = "Порозрядне Виключне АБО (XOR) у форматі 8.xxxxxxx";
    return info;
  case 0x3AU:
    info.mnemonic = "К инв";
    info.description = "Порозрядна інверсія (NOT) у форматі 8.xxxxxxx";
    return info;
  case 0x3BU:
    info.mnemonic = "К СЧ";
    info.description = "Генерація псевдовипадкового числа [0, 1)";
    return info;
  case 0x50U:
    info.mnemonic = "С/П";
    info.description = "Стоп / Пуск програми";
    return info;
  case 0x51U:
    info.mnemonic = "БП";
    info.description = "Безумовний перехід на адресу";
    return info;
  case 0x52U:
    info.mnemonic = "В/О";
    info.description = "Повернення з підпрограми (або перехід на 00)";
    return info;
  case 0x53U:
    info.mnemonic = "ПП";
    info.description = "Перехід до підпрограми за адресою";
    return info;
  case 0x54U:
    info.mnemonic = "К НОП";
    info.description = "Немає операції (порожній крок)";
    return info;
  case 0x55U:
    info.mnemonic = "К 1";
    info.description = "Недокументований НОП (К 1)";
    return info;
  case 0x56U:
    info.mnemonic = "К 2";
    info.description = "Недокументований НОП (К 2)";
    return info;
  case 0x57U:
    info.mnemonic = "F x≠0";
    info.description = "Умовний перехід (якщо умова X ≠ 0 НЕ виконується, тобто X = 0)";
    return info;
  case 0x58U:
    info.mnemonic = "F L2";
    info.description = "Організація циклу за регістром R2";
    return info;
  case 0x59U:
    info.mnemonic = "F x≥0";
    info.description = "Умовний перехід (якщо умова X ≥ 0 НЕ виконується, тобто X < 0)";
    return info;
  case 0x5AU:
    info.mnemonic = "F L3";
    info.description = "Організація циклу за регістром R3";
    return info;
  case 0x5BU:
    info.mnemonic = "F L1";
    info.description = "Організація циклу за регістром R1";
    return info;
  case 0x5CU:
    info.mnemonic = "F x<0";
    info.description = "Умовний перехід (якщо умова X < 0 НЕ виконується, тобто X ≥ 0)";
    return info;
  case 0x5DU:
    info.mnemonic = "F L0";
    info.description = "Організація циклу за регістром R0";
    return info;
  case 0x5EU:
    info.mnemonic = "F x=0";
    info.description = "Умовний перехід (якщо умова X = 0 НЕ виконується, тобто X ≠ 0)";
    return info;
  case 0x5FU:
    info.mnemonic = "ТЬМА";
    info.description = "Недокументована команда гасіння дисплея (5F)";
    return info;
  default:
    break;
  }

  if (lo <= 0x0EU) {
    const char rch = reg_index_to_char(lo);
    switch (hi) {
    case 0x04U:
      info.mnemonic = std::format("Х→П {}", rch);
      info.description = std::format("Запис числа з регістра X у регістр пам'яті R{}", rch);
      return info;
    case 0x06U:
      info.mnemonic = std::format("П→Х {}", rch);
      info.description = std::format("Читання числа з регістра пам'яті R{} у стек X", rch);
      return info;
    case 0x07U:
      info.mnemonic = std::format("К x≠0 {}", rch);
      info.description = std::format("Непрямий перехід за R{}, якщо X = 0", rch);
      return info;
    case 0x08U:
      info.mnemonic = std::format("К БП {}", rch);
      info.description = std::format("Непрямий безумовний перехід за адресою в R{}", rch);
      return info;
    case 0x09U:
      info.mnemonic = std::format("К x≥0 {}", rch);
      info.description = std::format("Непрямий перехід за R{}, якщо X < 0", rch);
      return info;
    case 0x0AU:
      info.mnemonic = std::format("К ПП {}", rch);
      info.description = std::format("Непрямий виклик підпрограми за адресою в R{}", rch);
      return info;
    case 0x0BU:
      info.mnemonic = std::format("К Х→П {}", rch);
      info.description = std::format("Непрямий запис із X у регістр, вказаний у R{}", rch);
      return info;
    case 0x0CU:
      info.mnemonic = std::format("К x<0 {}", rch);
      info.description = std::format("Непрямий перехід за R{}, якщо X ≥ 0", rch);
      return info;
    case 0x0DU:
      info.mnemonic = std::format("К П→Х {}", rch);
      info.description = std::format("Непряме читання в X із регістра, вказаного в R{}", rch);
      return info;
    case 0x0EU:
      info.mnemonic = std::format("К x=0 {}", rch);
      info.description = std::format("Непрямий перехід за R{}, якщо X ≠ 0", rch);
      return info;
    default:
      break;
    }
  }

  info.mnemonic = std::format("КОД {:02X}", code);
  info.description = "Недокументований або службовий код операції";
  return info;
}

std::vector<DisassembledStep>
disassemble_program(std::span<const std::uint8_t, kProgramSize> memory,
                    bool include_trailing_zeros) {
  std::size_t limit = kProgramSize;
  if (!include_trailing_zeros) {
    while (limit > 0 && memory[limit - 1] == 0x00U) {
      if (limit >= 2 && is_two_byte_opcode(memory[limit - 2])) {
        break;
      }
      --limit;
    }
    if (limit == 0) {
      limit = 1;
    }
  }

  std::vector<DisassembledStep> result;
  for (std::size_t i = 0; i < limit; ++i) {
    const std::uint8_t op = memory[i];
    const OpcodeInfo info = opcode_info(op);
    DisassembledStep step;
    step.step = static_cast<int>(i);
    step.step_str = format_step_address(step.step);
    step.opcode = op;
    step.opcode_str = info.display_code;

    if (info.byte_length == 2 && i + 1 < kProgramSize) {
      const std::uint8_t arg = memory[i + 1];
      step.arg_byte = arg;
      const int target = decode_step_address(arg);
      step.arg_str = format_step_address(target);
      step.mnemonic = std::format("{} {}", info.mnemonic, step.arg_str);
      step.description = std::format("{} (на крок {})", info.description, step.arg_str);
      ++i;
    } else {
      step.mnemonic = info.mnemonic;
      step.description = info.description;
    }
    result.push_back(std::move(step));
  }
  return result;
}

namespace {

std::string compact_step_token(std::uint8_t op) {
  const auto hi = static_cast<std::uint8_t>((op >> 4U) & 0x0FU);
  const auto lo = static_cast<std::uint8_t>(op & 0x0FU);
  if (op <= 0x09U) {
    return std::string(1, static_cast<char>('0' + op));
  }
  switch (op) {
  case 0x0AU:
    return ".";
  case 0x0BU:
    return "/-/";
  case 0x0CU:
    return "ВП";
  case 0x0DU:
    return "Сх";
  case 0x0EU:
    return "^";
  case 0x0FU:
    return "Вх";
  case 0x10U:
    return "+";
  case 0x11U:
    return "-";
  case 0x12U:
    return "*";
  case 0x13U:
    return "/";
  case 0x14U:
    return "<->";
  case 0x15U:
    return "10^x";
  case 0x16U:
    return "e^x";
  case 0x17U:
    return "lg";
  case 0x18U:
    return "ln";
  case 0x19U:
    return "arcsin";
  case 0x1AU:
    return "arccos";
  case 0x1BU:
    return "arctg";
  case 0x1CU:
    return "sin";
  case 0x1DU:
    return "cos";
  case 0x1EU:
    return "tg";
  case 0x20U:
    return "π";
  case 0x21U:
    return "√";
  case 0x22U:
    return "x^2";
  case 0x23U:
    return "1/x";
  case 0x24U:
    return "x^y";
  case 0x25U:
    return "В↻";
  case 0x26U:
    return "КМ→Г";
  case 0x2AU:
    return "КМС→Г";
  case 0x30U:
    return "КГ→МС";
  case 0x31U:
    return "|x|";
  case 0x32U:
    return "ЗН";
  case 0x33U:
    return "КГ→М";
  case 0x34U:
    return "[x]";
  case 0x35U:
    return "{x}";
  case 0x36U:
    return "max";
  case 0x37U:
    return "К∧";
  case 0x38U:
    return "К∨";
  case 0x39U:
    return "К⊕";
  case 0x3AU:
    return "Кинв";
  case 0x3BU:
    return "КСЧ";
  case 0x50U:
    return "С/П";
  case 0x51U:
    return "БП";
  case 0x52U:
    return "В/О";
  case 0x53U:
    return "ПП";
  case 0x54U:
    return "НОП";
  case 0x55U:
    return "К1";
  case 0x56U:
    return "К2";
  case 0x57U:
    return "x!=0";
  case 0x58U:
    return "L2";
  case 0x59U:
    return "x>=0";
  case 0x5AU:
    return "L3";
  case 0x5BU:
    return "L1";
  case 0x5CU:
    return "x<0";
  case 0x5DU:
    return "L0";
  case 0x5EU:
    return "x=0";
  case 0x5FU:
    return "ТЬМА";
  default:
    break;
  }

  if (lo <= 0x0EU) {
    const char r = reg_index_to_char(lo);
    switch (hi) {
    case 0x04U:
      return std::format("П{}", r);
    case 0x06U:
      return std::format("ИП{}", r);
    case 0x07U:
      return std::format("Кx!=0{}", r);
    case 0x08U:
      return std::format("КБП{}", r);
    case 0x09U:
      return std::format("Кx>=0{}", r);
    case 0x0AU:
      return std::format("КПП{}", r);
    case 0x0BU:
      return std::format("КП{}", r);
    case 0x0CU:
      return std::format("Кx<0{}", r);
    case 0x0DU:
      return std::format("КИП{}", r);
    case 0x0EU:
      return std::format("Кx=0{}", r);
    default:
      break;
    }
  }
  return std::format("{:02X}", op);
}

} // namespace

std::string format_program_compact_grid(std::span<const std::uint8_t, kProgramSize> memory) {
  std::size_t limit = kProgramSize;
  while (limit > 0 && memory[limit - 1] == 0x00U) {
    if (limit >= 2 && is_two_byte_opcode(memory[limit - 2])) {
      break;
    }
    --limit;
  }
  if (limit == 0) {
    limit = 1;
  }

  std::vector<std::string> cells(limit);
  for (std::size_t i = 0; i < limit; ++i) {
    const std::uint8_t op = memory[i];
    cells[i] = compact_step_token(op);
    if (is_two_byte_opcode(op) && i + 1 < limit) {
      cells[i + 1] = format_step_address(decode_step_address(memory[i + 1]));
      ++i;
    }
  }

  std::ostringstream oss;
  for (std::size_t i = 0; i < limit; ++i) {
    oss << std::format("{:<8}", cells[i]);
    if ((i + 1) % 10 == 0 || i + 1 == limit) {
      oss << "\n";
    }
  }
  return oss.str();
}

namespace {

bool try_parse_number_literal(std::string_view tok, std::vector<std::uint8_t> &out_bytes) {
  if (tok.empty()) {
    return false;
  }
  if (tok.size() == 1 && tok[0] >= '0' && tok[0] <= '9') {
    out_bytes.push_back(static_cast<std::uint8_t>(tok[0] - '0'));
    return true;
  }
  bool has_digit = false;
  bool seen_dot = false;
  bool seen_exp = false;
  bool neg_mantissa = false;
  bool neg_exp = false;
  std::vector<std::uint8_t> mantissa_ops;
  std::vector<std::uint8_t> exp_ops;

  std::size_t idx = 0;
  if (tok[idx] == '-' || tok[idx] == '+') {
    if (tok[idx] == '-') {
      neg_mantissa = true;
    }
    ++idx;
    if (idx == tok.size()) {
      return false;
    }
  }

  for (; idx < tok.size(); ++idx) {
    const char c = tok[idx];
    if (c >= '0' && c <= '9') {
      has_digit = true;
      if (!seen_exp) {
        mantissa_ops.push_back(static_cast<std::uint8_t>(c - '0'));
      } else {
        exp_ops.push_back(static_cast<std::uint8_t>(c - '0'));
      }
    } else if ((c == '.' || c == ',') && !seen_dot && !seen_exp) {
      seen_dot = true;
      mantissa_ops.push_back(0x0AU);
    } else if ((c == 'e' || c == 'E') && has_digit && !seen_exp) {
      seen_exp = true;
      if (idx + 1 < tok.size() && (tok[idx + 1] == '-' || tok[idx + 1] == '+')) {
        if (tok[idx + 1] == '-') {
          neg_exp = true;
        }
        ++idx;
      }
    } else {
      return false;
    }
  }

  if (!has_digit) {
    return false;
  }
  if (seen_exp && exp_ops.empty()) {
    return false;
  }

  out_bytes.insert(out_bytes.end(), mantissa_ops.begin(), mantissa_ops.end());
  if (neg_mantissa) {
    out_bytes.push_back(0x0BU);
  }
  if (seen_exp) {
    out_bytes.push_back(0x0CU);
    out_bytes.insert(out_bytes.end(), exp_ops.begin(), exp_ops.end());
    if (neg_exp) {
      out_bytes.push_back(0x0BU);
    }
  }
  return true;
}

std::optional<std::uint8_t> match_single_mnemonic(const std::string &norm) {
  static const std::map<std::string, std::uint8_t, std::less<>> kMap = {
    {".", 0x0A},       {",", 0x0A},       {"/-/", 0x0B},     {"+/-", 0x0B},    {"/+/", 0x0B},
    {"ЗНП", 0x0B},     {"CHS", 0x0B},     {"ВП", 0x0C},      {"VP", 0x0C},     {"EE", 0x0C},
    {"EXP", 0x0C},     {"EEX", 0x0C},     {"СX", 0x0D},      {"CX", 0x0D},     {"CLX", 0x0D},
    {"СБРОС", 0x0D},   {"В↑", 0x0E},      {"B↑", 0x0E},      {"↑", 0x0E},      {"ВВЕРX", 0x0E},
    {"ENT", 0x0E},     {"ENTER", 0x0E},   {"^", 0x0E},       {"FВX", 0x0F},    {"FBX", 0x0F},
    {"ВX", 0x0F},      {"BX", 0x0F},      {"X1", 0x0F},      {"LASTX", 0x0F},  {"+", 0x10},
    {"-", 0x11},       {"*", 0x12},       {"×", 0x12},       {"/", 0x13},      {"÷", 0x13},
    {":", 0x13},       {"↔", 0x14},       {"<->", 0x14},     {"XY", 0x14},     {"X<>Y", 0x14},
    {"XУ", 0x14},      {"Р↔X", 0x14},     {"SWAP", 0x14},    {"F10^X", 0x15},  {"10^X", 0x15},
    {"FE^X", 0x16},    {"FЕ^X", 0x16},    {"E^X", 0x16},     {"Е^X", 0x16},    {"FLG", 0x17},
    {"LG", 0x17},      {"LOG", 0x17},     {"FLN", 0x18},     {"LN", 0x18},     {"FARCSIN", 0x19},
    {"ARCSIN", 0x19},  {"FSIN^-1", 0x19}, {"FSIN⁻¹", 0x19},  {"SIN^-1", 0x19}, {"ASIN", 0x19},
    {"FARCCOS", 0x1A}, {"ARCCOS", 0x1A},  {"FCOS^-1", 0x1A}, {"FCOS⁻¹", 0x1A}, {"COS^-1", 0x1A},
    {"ACOS", 0x1A},    {"FARCTG", 0x1B},  {"ARCTG", 0x1B},   {"FTG^-1", 0x1B}, {"FTG⁻¹", 0x1B},
    {"TG^-1", 0x1B},   {"ATAN", 0x1B},    {"FSIN", 0x1C},    {"SIN", 0x1C},    {"FCOS", 0x1D},
    {"COS", 0x1D},     {"FTG", 0x1E},     {"TG", 0x1E},      {"TAN", 0x1E},    {"FΠ", 0x20},
    {"FPI", 0x20},     {"Π", 0x20},       {"PI", 0x20},      {"FПИ", 0x20},    {"FПІ", 0x20},
    {"ПИ", 0x20},      {"ПІ", 0x20},      {"F√", 0x21},      {"√", 0x21},      {"√X", 0x21},
    {"FSQRT", 0x21},   {"SQRT", 0x21},    {"FX^2", 0x22},    {"X^2", 0x22},    {"SQR", 0x22},
    {"F1/X", 0x23},    {"1/X", 0x23},     {"INV", 0x23},     {"FX^Y", 0x24},   {"X^Y", 0x24},
    {"Y^X", 0x24},     {"FY^X", 0x24},    {"POW", 0x24},     {"FВ↻", 0x25},    {"FB↻", 0x25},
    {"В↻", 0x25},      {"F↻", 0x25},      {"↻", 0x25},       {"КР", 0x25},     {"ROLL", 0x25},
    {"КМ->Г", 0x26},   {"KM->D", 0x26},   {"M->D", 0x26},    {"М->Г", 0x26},   {"КМГ", 0x26},
    {"КМС->Г", 0x2A},  {"KMS->D", 0x2A},  {"MS->D", 0x2A},   {"МС->Г", 0x2A},  {"КМСГ", 0x2A},
    {"КГ->МС", 0x30},  {"KD->MS", 0x30},  {"D->MS", 0x30},   {"Г->МС", 0x30},  {"КГМС", 0x30},
    {"°'\"", 0x30},    {"К|X|", 0x31},    {"K|X|", 0x31},    {"|X|", 0x31},    {"KABS", 0x31},
    {"ABS", 0x31},     {"МОД", 0x31},     {"КЗН", 0x32},     {"KЗН", 0x32},    {"ЗН", 0x32},
    {"KSGN", 0x32},    {"SGN", 0x32},     {"SIGN", 0x32},    {"КГ->М", 0x33},  {"KD->M", 0x33},
    {"D->M", 0x33},    {"Г->М", 0x33},    {"КГМ", 0x33},     {"°'", 0x33},     {"К[X]", 0x34},
    {"K[X]", 0x34},    {"[X]", 0x34},     {"KINT", 0x34},    {"INT", 0x34},    {"ЦЕЛ", 0x34},
    {"К{X}", 0x35},    {"K{X}", 0x35},    {"{X}", 0x35},     {"KFRAC", 0x35},  {"FRAC", 0x35},
    {"ДРОБ", 0x35},    {"КMAX", 0x36},    {"KMAX", 0x36},    {"MAX", 0x36},    {"МАКС", 0x36},
    {"К∧", 0x37},      {"K∧", 0x37},      {"∧", 0x37},       {"KAND", 0x37},   {"AND", 0x37},
    {"&", 0x37},       {"К∨", 0x38},      {"K∨", 0x38},      {"∨", 0x38},      {"KOR", 0x38},
    {"OR", 0x38},      {"|", 0x38},       {"К⊕", 0x39},      {"K⊕", 0x39},     {"⊕", 0x39},
    {"KXOR", 0x39},    {"XOR", 0x39},     {"КИНВ", 0x3A},    {"KИНВ", 0x3A},   {"КІНВ", 0x3A},
    {"ИНВ", 0x3A},     {"ІНВ", 0x3A},     {"KNOT", 0x3A},    {"NOT", 0x3A},    {"~", 0x3A},
    {"КСЧ", 0x3B},     {"KСЧ", 0x3B},     {"СЧ", 0x3B},      {"KRND", 0x3B},   {"RND", 0x3B},
    {"RAND", 0x3B},    {"С/П", 0x50},     {"СП", 0x50},      {"C/П", 0x50},    {"C/P", 0x50},
    {"С/P", 0x50},     {"R/S", 0x50},     {"STOP", 0x50},    {"SP", 0x50},     {"В/О", 0x52},
    {"В/0", 0x52},     {"ВО", 0x52},      {"В0", 0x52},      {"B/O", 0x52},    {"B/0", 0x52},
    {"BO", 0x52},      {"RTN", 0x52},     {"RET", 0x52},     {"RETURN", 0x52}, {"КНОП", 0x54},
    {"KНОП", 0x54},    {"НОП", 0x54},     {"KNOP", 0x54},    {"NOP", 0x54},    {"К0", 0x54},
    {"K0", 0x54},      {"К1", 0x55},      {"K1", 0x55},      {"К2", 0x56},     {"K2", 0x56},
    {"ТЬМА", 0x5F},
  };
  const auto it = kMap.find(norm);
  if (it != kMap.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::uint8_t> match_two_byte_jump_mnemonic(const std::string &norm) {
  static const std::map<std::string, std::uint8_t, std::less<>> kJumps = {
    {"БП", 0x51},    {"GOTO", 0x51}, {"JMP", 0x51},   {"ПП", 0x53},    {"GSB", 0x53},
    {"GOSUB", 0x53}, {"CALL", 0x53}, {"FX!=0", 0x57}, {"X!=0", 0x57},  {"FX<>0", 0x57},
    {"X<>0", 0x57},  {"FX#0", 0x57}, {"X#0", 0x57},   {"FL2", 0x58},   {"L2", 0x58},
    {"FX>=0", 0x59}, {"X>=0", 0x59}, {"FX=>0", 0x59}, {"X=>0", 0x59},  {"FL3", 0x5A},
    {"L3", 0x5A},    {"FL1", 0x5B},  {"L1", 0x5B},    {"FX<0", 0x5C},  {"X<0", 0x5C},
    {"FL0", 0x5D},   {"L0", 0x5D},   {"FX=0", 0x5E},  {"FX==0", 0x5E}, {"X=0", 0x5E},
    {"X==0", 0x5E},
  };
  const auto it = kJumps.find(norm);
  if (it != kJumps.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::uint8_t> match_reg_op_prefix(const std::string &norm) {
  static const std::map<std::string, std::uint8_t, std::less<>> kRegBases = {
    {"П", 0x40},     {"XП", 0x40},    {"X->П", 0x40},  {"STO", 0x40},   {"ИП", 0x60},
    {"ІП", 0x60},    {"ПX", 0x60},    {"П->X", 0x60},  {"RCL", 0x60},   {"КX!=0", 0x70},
    {"KX!=0", 0x70}, {"КX<>0", 0x70}, {"KX<>0", 0x70}, {"КX#0", 0x70},  {"KX#0", 0x70},
    {"КБП", 0x80},   {"KБП", 0x80},   {"KGOTO", 0x80}, {"КX>=0", 0x90}, {"KX>=0", 0x90},
    {"КX=>0", 0x90}, {"KX=>0", 0x90}, {"КПП", 0xA0},   {"KПП", 0xA0},   {"KGSB", 0xA0},
    {"КП", 0xB0},    {"KП", 0xB0},    {"КXП", 0xB0},   {"KXП", 0xB0},   {"КX->П", 0xB0},
    {"KX->П", 0xB0}, {"KSTO", 0xB0},  {"КX<0", 0xC0},  {"KX<0", 0xC0},  {"КИП", 0xD0},
    {"КІП", 0xD0},   {"KИП", 0xD0},   {"КПX", 0xD0},   {"КП->X", 0xD0}, {"KП->X", 0xD0},
    {"KRCL", 0xD0},  {"КX=0", 0xE0},  {"KX=0", 0xE0},  {"КX==0", 0xE0}, {"KX==0", 0xE0},
  };
  const auto it = kRegBases.find(norm);
  if (it != kRegBases.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<std::uint8_t> match_fused_reg_op(const std::string &norm) {
  if (norm.size() < 2) {
    return std::nullopt;
  }
  // Останній символ (1 байт для ASCII 0..9, A..E, -, L, C або 2 байти для кирилиці С, Г, Е)
  for (std::size_t suf_len : {std::size_t{1}, std::size_t{2}}) {
    if (norm.size() <= suf_len) {
      continue;
    }
    const std::string_view prefix(norm.data(), norm.size() - suf_len);
    const std::string_view suffix(norm.data() + norm.size() - suf_len, suf_len);
    const auto base = match_reg_op_prefix(std::string(prefix));
    const auto reg = parse_reg_index(suffix);
    if (base.has_value() && reg.has_value()) {
      return static_cast<std::uint8_t>(*base + *reg);
    }
  }
  return std::nullopt;
}

} // namespace

AssembleResult assemble_program(std::string_view source) {
  AssembleResult res;
  res.program.fill(0x00U);

  struct SourceToken {
    std::string text;
    int line_num{0};
  };

  struct UnresolvedJump {
    int addr_step{0};
    std::string target_token;
    int line_num{0};
  };

  // Збираємо єдиний потік токенів із усіх рядків (щоб 2-байтові переходи на межі рядків у
  // таблицях 10x10 на кшталт "x>=0 \n 83" коректно читали адресу з наступного рядка!)
  std::vector<SourceToken> tokens;
  std::istringstream iss{std::string(source)};
  std::string line;
  int line_Index = 0;

  while (std::getline(iss, line)) {
    ++line_Index;
    std::string_view sv(line);
    if (const auto pos = sv.find('#'); pos != std::string_view::npos) {
      sv = sv.substr(0, pos);
    }
    if (const auto pos = sv.find(';'); pos != std::string_view::npos) {
      sv = sv.substr(0, pos);
    }
    if (const auto pos = sv.find("//"); pos != std::string_view::npos) {
      sv = sv.substr(0, pos);
    }
    sv = trim_sv(sv);
    if (sv.empty()) {
      continue;
    }

    std::size_t pos = 0;
    while (pos < sv.size()) {
      while (pos < sv.size() && (sv[pos] == ' ' || sv[pos] == '\t')) {
        ++pos;
      }
      if (pos >= sv.size()) {
        break;
      }
      const std::size_t start = pos;
      while (pos < sv.size() && sv[pos] != ' ' && sv[pos] != '\t') {
        ++pos;
      }
      tokens.push_back({std::string(sv.substr(start, pos - start)), line_Index});
    }
  }

  std::map<std::string, int, std::less<>> labels;
  std::vector<UnresolvedJump> unresolved;

  int current_step = 0;
  int max_step_written = 0;

  auto emit_byte = [&](std::uint8_t b, int line_num) -> bool {
    if (current_step < 0 || current_step >= static_cast<int>(kProgramSize)) {
      res.ok = false;
      res.error_line = line_num;
      res.error_message = std::format("Рядок {}: переповнення пам'яті програм (ліміт {} кроків)",
                                      line_num, kProgramSize);
      return false;
    }
    res.program[static_cast<std::size_t>(current_step)] = b;
    ++current_step;
    if (current_step > max_step_written) {
      max_step_written = current_step;
    }
    return true;
  };

  for (std::size_t i = 0; i < tokens.size(); ++i) {
    const std::string &tok = tokens[i].text;
    const int line_num = tokens[i].line_num;

    // 0. Перевірка на мітку або явну адресу кроку ("00:", "loop:", ":loop", "00.")
    if (tok.size() > 1 && tok.back() == ':') {
      const std::string_view head(tok.data(), tok.size() - 1);
      if (const auto explicit_step = parse_step_address(head); explicit_step.has_value()) {
        current_step = *explicit_step;
      } else {
        labels[normalize_token(head)] = current_step;
      }
      continue;
    }
    if (tok.size() == 3 && tok[0] >= '0' && tok[0] <= '9' && tok[1] >= '0' && tok[1] <= '9' &&
        tok[2] == '.') {
      if (const auto explicit_step = parse_step_address(std::string_view(tok.data(), 2));
          explicit_step.has_value()) {
        current_step = *explicit_step;
        continue;
      }
    }
    if (tok.size() > 1 && tok.front() == ':') {
      const std::string_view tail(tok.data() + 1, tok.size() - 1);
      labels[normalize_token(tail)] = current_step;
      continue;
    }

    const std::string norm = normalize_token(tok);

    // Якщо токен "F" або "К"/"K" стоїть окремо перед наступним токеном, об'єднуємо їх
    if ((norm == "F" || norm == "К" || norm == "K") && i + 1 < tokens.size()) {
      const std::string next_norm = normalize_token(tokens[i + 1].text);
      const std::string prefix = (norm == "F") ? "F" : "К";
      const std::string combined = prefix + next_norm;

      if (const auto single = match_single_mnemonic(combined); single.has_value()) {
        if (!emit_byte(*single, line_num)) {
          return res;
        }
        ++i;
        continue;
      }
      if (const auto jump = match_two_byte_jump_mnemonic(combined); jump.has_value()) {
        if (i + 2 >= tokens.size()) {
          res.ok = false;
          res.error_line = line_num;
          res.error_message = std::format("Рядок {}: очікується адреса переходу після '{} {}'",
                                          line_num, tok, tokens[i + 1].text);
          return res;
        }
        if (!emit_byte(*jump, line_num)) {
          return res;
        }
        unresolved.push_back({current_step, tokens[i + 2].text, tokens[i + 2].line_num});
        if (!emit_byte(0x00U, line_num)) {
          return res;
        }
        i += 2;
        continue;
      }
      if (const auto fused = match_fused_reg_op(combined); fused.has_value()) {
        if (!emit_byte(*fused, line_num)) {
          return res;
        }
        ++i;
        continue;
      }
      if (const auto reg_base = match_reg_op_prefix(combined); reg_base.has_value()) {
        if (i + 2 >= tokens.size()) {
          res.ok = false;
          res.error_line = line_num;
          res.error_message =
            std::format("Рядок {}: очікується номер регістра (0..9, A..E) після '{} {}'", line_num,
                        tok, tokens[i + 1].text);
          return res;
        }
        const auto reg = parse_reg_index(tokens[i + 2].text);
        if (!reg.has_value()) {
          res.ok = false;
          res.error_line = tokens[i + 2].line_num;
          res.error_message = std::format("Рядок {}: невідомий регістр '{}'",
                                          tokens[i + 2].line_num, tokens[i + 2].text);
          return res;
        }
        if (!emit_byte(static_cast<std::uint8_t>(*reg_base + *reg), line_num)) {
          return res;
        }
        i += 2;
        continue;
      }
    }

    // 1. Перевірка однобайтової мнемоніки
    if (const auto single = match_single_mnemonic(norm); single.has_value()) {
      if (!emit_byte(*single, line_num)) {
        return res;
      }
      continue;
    }

    // 2. Перевірка двобайтової команди переходу (БП, ПП, Fx=0, x>=0, FL0...)
    if (const auto jump = match_two_byte_jump_mnemonic(norm); jump.has_value()) {
      if (i + 1 >= tokens.size()) {
        res.ok = false;
        res.error_line = line_num;
        res.error_message =
          std::format("Рядок {}: очікується адреса переходу після '{}'", line_num, tok);
        return res;
      }
      if (!emit_byte(*jump, line_num)) {
        return res;
      }
      unresolved.push_back({current_step, tokens[i + 1].text, tokens[i + 1].line_num});
      if (!emit_byte(0x00U, line_num)) {
        return res;
      }
      ++i;
      continue;
    }

    // 3. Перевірка регістрової операції з окремим номером регістра ("П 0", "Х→П A", "ИП 4")
    if (const auto reg_base = match_reg_op_prefix(norm); reg_base.has_value()) {
      if (i + 1 >= tokens.size()) {
        res.ok = false;
        res.error_line = line_num;
        res.error_message =
          std::format("Рядок {}: очікується номер регістра після '{}'", line_num, tok);
        return res;
      }
      const auto reg = parse_reg_index(tokens[i + 1].text);
      if (!reg.has_value()) {
        res.ok = false;
        res.error_line = tokens[i + 1].line_num;
        res.error_message = std::format("Рядок {}: невідомий регістр '{}'", tokens[i + 1].line_num,
                                        tokens[i + 1].text);
        return res;
      }
      if (!emit_byte(static_cast<std::uint8_t>(*reg_base + *reg), line_num)) {
        return res;
      }
      ++i;
      continue;
    }

    // 4. Перевірка злитої регістрової операції ("П0", "ИП4", "Х→ПA", "КИП4")
    if (const auto fused = match_fused_reg_op(norm); fused.has_value()) {
      if (!emit_byte(*fused, line_num)) {
        return res;
      }
      continue;
    }

    // 5. Перевірка 2-символьного HEX-коду (напр. "0E", "51", "4A")
    if (const auto hex_b = parse_hex_byte_exact(tok); hex_b.has_value()) {
      if (is_two_byte_opcode(*hex_b) && i + 1 < tokens.size()) {
        if (!emit_byte(*hex_b, line_num)) {
          return res;
        }
        unresolved.push_back({current_step, tokens[i + 1].text, tokens[i + 1].line_num});
        if (!emit_byte(0x00U, line_num)) {
          return res;
        }
        ++i;
        continue;
      }
      if (!emit_byte(*hex_b, line_num)) {
        return res;
      }
      continue;
    }

    // 6. Перевірка числового літерала (напр. "3.1415926", "-12.5", "0")
    std::vector<std::uint8_t> num_bytes;
    if (try_parse_number_literal(tok, num_bytes)) {
      for (std::uint8_t b : num_bytes) {
        if (!emit_byte(b, line_num)) {
          return res;
        }
      }
      continue;
    }

    res.ok = false;
    res.error_line = line_num;
    res.error_message = std::format("Рядок {}: невідома команда або токен '{}'", line_num, tok);
    return res;
  }

  // Розв'язуємо адреси переходів та мітки
  for (const auto &u : unresolved) {
    if (const auto step_addr = parse_step_address(u.target_token); step_addr.has_value()) {
      res.program[static_cast<std::size_t>(u.addr_step)] = encode_step_address(*step_addr);
      continue;
    }
    const std::string norm_label = normalize_token(u.target_token);
    if (const auto it = labels.find(norm_label); it != labels.end()) {
      res.program[static_cast<std::size_t>(u.addr_step)] = encode_step_address(it->second);
      continue;
    }
    if (const auto raw_hex = parse_hex_byte_exact(u.target_token); raw_hex.has_value()) {
      res.program[static_cast<std::size_t>(u.addr_step)] = *raw_hex;
      continue;
    }
    res.ok = false;
    res.error_line = u.line_num;
    res.error_message =
      std::format("Рядок {}: невідома мітка або адреса переходу '{}'", u.line_num, u.target_token);
    return res;
  }

  res.steps_used = max_step_written;
  return res;
}

} // namespace mk61::core
