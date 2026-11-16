# mk61emu — Elektronika MK-61 emulator

English | [Українська](README.uk.md)

An emulator of the Soviet programmable calculator **Elektronika MK-61**: the X–Y–Z–T stack, 15 registers, 105 program steps, and the same key codes as the real machine. Two programs ship together — a console (`mk61emu`) and a Qt 6 window (`mk61emu-gui`).

Copyright (C) 2026 Ihor Dolinskyi. Licensed under the GNU GPL v3. See [LICENSE](LICENSE). Qt, the indicator font, and Catch2 (tests only) are listed in [NOTICE](NOTICE).

## Build

CMake 3.25, a C++23 compiler, and Qt 6 Widgets if you want the window.

```sh
cmake --preset gui
cmake --build --preset gui
ctest --preset gui
```

Binaries:

-   `./build/gui/apps/cli/mk61emu`
-   `./build/gui/apps/gui/mk61emu-gui`

Console and tests, no Qt:

```sh
cmake --preset dev -DBUILD_GUI=OFF
cmake --build --preset dev
ctest --preset dev
```

## Packages

`scripts/package.sh` builds a `.deb` and a `.tar.gz` (and an AppImage when `linuxdeploy` is installed). It picks up `~/Qt/<version>/gcc_64` on its own; otherwise set `CMAKE_PREFIX_PATH`. Artifacts land in `dist/`. The text to paste into a GitHub Release is in `packaging/GITHUB_RELEASE_v1.0.0.md`.

```bash
sudo apt install ./mk61emu_1.0.0_amd64.deb
```

## Console

With no arguments you get `МК-61 [АВТ | PC=00]>`. Nothing happens on a bare keypress: type the key names separated by spaces and press Enter. Commands for the program itself start with `:`. `:keys` lists every key and the word to type, `:help` is the longer guide.

```text
2 В↑ 3 +
2 ^ 10 F ↔          # ^ is В↑, F ↔ is x^y
:deg
30 F 7              # sin 30°
:p                  # seven built-in programs
:p 1
2026 В/О С/П        # Easter 2026: X = month, Y = day
```

`:h` is the command list, `:k` is the key map, `:asm` loads a program, `:s` steps, `:r` shows registers, `:save` / `:load` use a `.mk61` file.

```sh
mk61emu --keys "2 В↑ 10 F ↔"
mk61emu --preset easter -x 2026 --run
mk61emu --preset quadratic --reg 1=1 --reg 2=-5 --reg 3=6 --run
```

## Window

The face matches the MK-61. A click presses the key at once, and so does the PC keyboard (Enter is `В↑`, space is `С/П`, F and K are the prefixes), unless the cursor is in the program editor. Hover a key for what it does. `F12` leaves only the calculator, `F10` steps, `F5` runs with a delay, `F9` toggles a breakpoint. The «Інструкція МК-61» tab is the full key guide. The **Про** button by the display is the version and the license notes.
