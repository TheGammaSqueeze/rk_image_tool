#!/usr/bin/env bash
# Build static rk_image_tool binaries for all supported platforms.
# Output: dist/rk_image_tool-<version>-<platform>.<ext>
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

VERSION="$(awk '/^[[:space:]]*VERSION[[:space:]]+[0-9]/{print $2; exit}' CMakeLists.txt)"
DIST="$ROOT/dist"
mkdir -p "$DIST"

build_one() {
    local label="$1"
    local builddir="$2"
    local ext="$3"
    local strip_cmd="$4"
    shift 4
    echo ""
    echo "=== building $label ==="
    rm -rf "$builddir"
    mkdir -p "$builddir"
    cmake -S "$ROOT" -B "$builddir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DRK_STATIC=ON \
        -DRK_TUI=ON \
        "$@"
    cmake --build "$builddir" --parallel
    local exe="$builddir/rk_image_tool$ext"
    local out="$DIST/rk_image_tool-${VERSION}-${label}${ext}"
    cp "$exe" "$out"
    if command -v "$strip_cmd" >/dev/null 2>&1; then
        "$strip_cmd" "$out"
    fi
    ( cd "$DIST" && sha256sum "$(basename "$out")" > "$(basename "$out").sha256" )
    ls -lh "$out"
}

build_one linux-x86_64 build-linux-x86_64 "" strip \
    -DCMAKE_C_FLAGS="-O2"

build_one linux-aarch64 build-linux-aarch64 "" aarch64-linux-gnu-strip \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-linux-aarch64.cmake" \
    -DCMAKE_C_FLAGS="-O2"

build_one linux-armhf build-linux-armhf "" arm-linux-gnueabihf-strip \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-linux-armhf.cmake" \
    -DCMAKE_C_FLAGS="-O2"

build_one windows-x86_64 build-windows-x86_64 ".exe" x86_64-w64-mingw32-strip \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-mingw64.cmake" \
    -DCMAKE_C_FLAGS="-O2"

echo ""
echo "=== dist artifacts ==="
ls -lh "$DIST"
