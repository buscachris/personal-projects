#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
OUTPUT="$BUILD_DIR/libdevkity_native.so"
ALIAS_OUTPUT="$BUILD_DIR/libsonickity_native.so"

mkdir -p "$BUILD_DIR"

# Core scaffold: strict warnings
gcc -std=c89 -O2 -Wall -Wextra -Werror -pedantic -fPIC -c \
  "$ROOT_DIR/devkity_native.c" -o "$BUILD_DIR/devkity_native.o"

# Subsystems: C89 but lenient on declaration-after-statement and unused
gcc -std=c89 -O2 -Wall -Wextra -Wno-error=declaration-after-statement -Wno-error=unused-function -fPIC -c \
  "$ROOT_DIR/vdp.c" -o "$BUILD_DIR/vdp.o"

gcc -std=c89 -O2 -Wall -Wextra -Wno-error=declaration-after-statement -Wno-error=unused-function -fPIC -c \
  "$ROOT_DIR/render.c" -o "$BUILD_DIR/render.o"

gcc -std=c89 -O2 -Wall -Wextra -Wno-error=declaration-after-statement -Wno-error=unused-function -fPIC -c \
  "$ROOT_DIR/z80.c" -o "$BUILD_DIR/z80.o"

gcc -std=c89 -O2 -Wall -Wextra -Wno-error=declaration-after-statement -Wno-error=unused-function -fPIC -c \
  "$ROOT_DIR/ym2612.c" -o "$BUILD_DIR/ym2612.o"

gcc -std=c89 -O2 -Wall -Wextra -Werror -pedantic -fPIC -c \
  "$ROOT_DIR/sandbox.c" -o "$BUILD_DIR/sandbox.o"

# Link
gcc -shared -o "$OUTPUT" \
  "$BUILD_DIR/devkity_native.o" \
  "$BUILD_DIR/vdp.o" \
  "$BUILD_DIR/render.o" \
  "$BUILD_DIR/z80.o" \
  "$BUILD_DIR/ym2612.o" \
  "$BUILD_DIR/sandbox.o"

cp "$OUTPUT" "$ALIAS_OUTPUT"

echo "built $OUTPUT"
echo "built $ALIAS_OUTPUT"
