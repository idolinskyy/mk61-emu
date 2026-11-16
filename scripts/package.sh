#!/usr/bin/env bash
# Збирає пакети mk61emu без сирців: .deb, .tar.gz і, якщо є linuxdeploy, AppImage.
# Скрипт нічого не викладає: тег, файли і GitHub Release лишаються ручними.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${MK61_BUILD_DIR:-$ROOT/build-package}"
DIST="$ROOT/dist"

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTS=OFF)
if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH")
else
  # Офіційний інсталятор: ~/Qt/6.10.2/gcc_64, ~/Qt/6.12.0/gcc_64, …
  qt_prefix="$(ls -1d "$HOME"/Qt/[0-9]*/gcc_64 2>/dev/null | sort -V | tail -1 || true)"
  if [[ -n "$qt_prefix" ]]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$qt_prefix")
    echo "Qt prefix: $qt_prefix"
  fi
fi

cmake -S "$ROOT" -B "$BUILD" "${CMAKE_ARGS[@]}"
cmake --build "$BUILD" -j"$(nproc)"

rm -rf "$DIST"
mkdir -p "$DIST/AppDir"
cmake --install "$BUILD" --prefix "$DIST/AppDir/usr"

cmake --build "$BUILD" --target package
find "$BUILD" -maxdepth 1 \( -name 'mk61emu*.deb' -o -name 'mk61emu*.tar.gz' \) \
  -exec cp -a {} "$DIST/" \;

if command -v linuxdeploy >/dev/null 2>&1; then
  linuxdeploy \
    --appdir "$DIST/AppDir" \
    --desktop-file "$ROOT/packaging/mk61emu.desktop" \
    --icon-file "$ROOT/packaging/icons/hicolor/256x256/apps/mk61emu.png" \
    --output appimage
  find "$ROOT" "$DIST" "$BUILD" -maxdepth 1 -name '*.AppImage' \
    -exec cp -a {} "$DIST/" \;
  echo "AppImage: $DIST"
else
  echo "linuxdeploy не знайдено — AppImage пропущено (є .deb і .tar.gz у $DIST)"
fi

echo "Готово. Каталог артефактів: $DIST"
ls -la "$DIST"
