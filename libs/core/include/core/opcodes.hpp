#pragma once

#include "core/types.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mk61::core {

/// Опис одного коду операції (0x00..0xFF) у пам'яті програм МК-61
struct OpcodeInfo {
  std::uint8_t code{0};
  std::uint8_t byte_length{1}; // 1 або 2 байти (2 байти для БП, ПП, F x.., F L..)
  std::string display_code;    // Двозначний код на індикаторі МК-61 (напр. "0E", "5-", "4Г")
  std::string mnemonic;        // Мнемоніка команди (напр. "В↑", "F sin", "Х→П 0", "БП")
  std::string description;     // Короткий опис українською мовою
};

/// Один розібраний крок програми під час дизасемблювання
struct DisassembledStep {
  int step{0};                          // Адреса кроку (0..104)
  std::string step_str;                 // Адреса на індикаторі ("00".."99", "-0".."-4")
  std::uint8_t opcode{0};               // Перший байт команди
  std::string opcode_str;               // Код на індикаторі ("0E", "51" тощо)
  std::optional<std::uint8_t> arg_byte; // Другий байт (для двобайтових переходів)
  std::string arg_str;                  // Відформатована адреса переходу (якщо двобайтова)
  std::string mnemonic;                 // Повна мнемоніка (напр. "БП 00", "Х→П 4", "F sin")
  std::string description;              // Пояснення дії
};

/// Результат асемблювання тексту програми у 105-крокову пам'ять МК-61
struct AssembleResult {
  bool ok{true};
  std::array<std::uint8_t, kProgramSize> program{};
  int steps_used{0};
  std::string error_message;
  int error_line{0};
};

/// Повертає таблицю всіх 30 клавіш МК-61 у порядку рядків і стовпців (6x5)
[[nodiscard]] const std::array<KeyInfo, kKeyCount> &all_keys() noexcept;

/// Повертає метадані конкретної клавіші
[[nodiscard]] const KeyInfo &key_info(Key key) noexcept;

/// Шукає клавішу за текстовим позначенням або скороченням
[[nodiscard]] std::optional<Key> find_key_by_token(std::string_view token) noexcept;

/// Перетворює 4-бітну тетраду (0..15) на символ 7-сегментного індикатора МК-61
/// (0..9 -> '0'..'9', 10 -> '-', 11 -> 'L', 12 -> 'C', 13 -> 'Г', 14 -> 'E', 15 -> ' ')
[[nodiscard]] std::string_view nibble_to_mk61_glyph(std::uint8_t nibble) noexcept;

/// Перетворює індекс регістра (0..14) на позначення регістра ('0'..'9', 'A'..'E')
[[nodiscard]] char reg_index_to_char(std::uint8_t reg) noexcept;

/// Перетворює індекс регістра (0..14) на символ індикатора МК-61 ('0'..'9', '-', 'L', 'C', 'Г',
/// 'E')
[[nodiscard]] std::string_view reg_index_to_mk61_glyph(std::uint8_t reg) noexcept;

/// Розпізнає індекс регістра (0..14) із символу ('0'..'9', 'a'..'e', 'A'..'E', '-', 'L', 'C', 'Г')
[[nodiscard]] std::optional<std::uint8_t> parse_reg_index(std::string_view text) noexcept;

/// Форматує байт коду команди (0x00..0xFF) у 2-символьний код індикатора МК-61
[[nodiscard]] std::string format_mk61_hex(std::uint8_t code);

/// Форматує номер кроку програми (0..104) у вигляд індикатора МК-61 ("00".."99", "-0".."-4")
[[nodiscard]] std::string format_step_address(int step);

/// Кодує номер кроку (0..104) у BCD-байт адреси переходу МК-61 (0x00..0x99, 0xA0..0xA4)
[[nodiscard]] std::uint8_t encode_step_address(int step) noexcept;

/// Декодує BCD-байт адреси переходу МК-61 у номер кроку (0..104)
[[nodiscard]] int decode_step_address(std::uint8_t addr_byte) noexcept;

/// Розпізнає номер кроку (0..104) із рядка ("00".."104", "-0".."-4", "A0".."A4")
[[nodiscard]] std::optional<int> parse_step_address(std::string_view text) noexcept;

/// Повертає повну інформацію про код операції МК-61 (0x00..0xFF)
[[nodiscard]] OpcodeInfo opcode_info(std::uint8_t code);

/// Перевіряє, чи є код операції двобайтовим переходом (БП, ПП, F x.., F L..)
[[nodiscard]] bool is_two_byte_opcode(std::uint8_t code) noexcept;

/// Дизасемблює пам'ять програм МК-61 у список кроків
[[nodiscard]] std::vector<DisassembledStep>
disassemble_program(std::span<const std::uint8_t, kProgramSize> memory,
                    bool include_trailing_zeros = false);

/// Форматує програму з пам'яті МК-61 у компактну таблицю по 10 кроків у рядку (формат ПМК)
[[nodiscard]] std::string
format_program_compact_grid(std::span<const std::uint8_t, kProgramSize> memory);

/// Асемблює текстовий лістинг програми (мнемоніки, таблиця 10x10, мітки, числа або HEX-коди)
[[nodiscard]] AssembleResult assemble_program(std::string_view source);

} // namespace mk61::core
