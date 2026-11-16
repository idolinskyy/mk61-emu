# mk61emu — Elektronika MK-61 Programmable Calculator Emulator

English | [Українська](README.uk.md)

A complete C++23 emulator of the classic Soviet RPN programmable scientific calculator **Elektronika MK-61**, featuring both an interactive console application (`mk61emu`) and a Qt 6 Widgets desktop GUI (`mk61emu-gui`).

## Build

```sh
# Build with Qt 6 GUI, CLI, and unit tests
cmake --preset gui-local
cmake --build --preset gui-local
ctest --preset gui-local

# Build CLI and unit tests only (without Qt)
cmake --preset dev -DBUILD_GUI=OFF
cmake --build --preset dev
ctest --preset dev
```

## Console Application (`mk61emu`) Quick Guide

Run `./build/gui/apps/cli/mk61emu` with no arguments to enter the interactive REPL, or pass CLI flags for batch execution.

### Interactive REPL Commands

- **Direct RPN Calculations**:
  - `2 В↑ 3 +` (or `2 ^ 3 +`) → `5.`
  - `2 ^ 10 F ↔` → `1024.` ($2^{10}$)
  - `:deg` then `30 F 7` → `0.5` ($\sin 30^\circ$)
- **Built-in Preset Programs (`1..7`)**:
  - `:p` (or `:presets`) — list all 7 built-in MK-61 programs (*Easter Date / Пасхалія*, *Factorial*, *Quadratic Solver*, *Hypotenuse*, *Indirect Array Sum*, *Simpson Integration*, *Moon Lander / Лунолёт-1*).
  - `:p 1` (or `:preset easter`) — load preset #1 into program memory.
  - `2026 В/О С/П` (or `2026 В/О :run`) — enter `2026`, reset `PC=00`, and run (`X = 4` April, `Y = 12`).
- **Entering Custom Programs (Mnemonics or `10×10` Table)**:
  - `:asm` — enter multi-line program input (supports both line-by-line mnemonics with labels and `10×10` tabular PMK grids; finish with an empty line or `END`).
  - `:l` (or `:list`) — detailed disassembled listing.
  - `:g` (or `:grid`) — compact `10×10` tabular listing.
- **Step-by-Step Debugging & Registers**:
  - `:s` (or `:step`) — execute 1 program step with trace output.
  - `:s 5` — execute 5 steps with trace output.
  - `:r` (or `:regs`) — inspect all 15 memory registers (`R0..RE`) and the subroutine return stack.
  - `:r 1=25` (or `:reg 1=25`) — set register `R1` to `25`.
  - `:save file.mk61` / `:load file.mk61` — save or load calculator state / program files.
  - `:keys` (`:k`) — display the 30-key MK-61 keyboard map.
  - `:help` (`:h`, `?`) — display the full interactive guide.

### Command-Line Batch Examples

```sh
./build/gui/apps/cli/mk61emu --keys "2 В↑ 10 F ↔"
./build/gui/apps/cli/mk61emu --preset easter -x 2026 --run
./build/gui/apps/cli/mk61emu --preset factorial -x 7 --list --grid --run
./build/gui/apps/cli/mk61emu --preset quadratic --reg 1=1 --reg 2=-5 --reg 3=6 --run
```

## Qt 6 GUI Application (`mk61emu-gui`)

Run `./build/gui/apps/gui/mk61emu-gui`:
- **`F12`** (or **`◀ Тільки МК-61`** button) — toggle between compact calculator-only mode and the full inspector/debugger view.
- **`F10`** (`⏭ Крок`) — single-step debugger execution.
- **`F5`** (`▶ Автокрок`) — animated step execution with adjustable speed.
- **`F9`** (or double-click a step row) — toggle a breakpoint (`●`).
