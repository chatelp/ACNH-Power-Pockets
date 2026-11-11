#!/bin/sh
# Renders the real panel UI on the host with a fake engine and checks the main flows.
# Needs SDL2 + SDL2_ttf (macOS: brew install sdl2 sdl2_ttf) and Pillow for PNG output.
# Screenshots land in $OUT (default: $TMPDIR/pp_ui_shots).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-${TMPDIR:-/tmp}/pp_ui_shots}"
rm -rf "$OUT"; mkdir -p "$OUT/sd"
CXX="${CXX:-clang++}"
SDL_CFLAGS="$(sdl2-config --cflags)"
SDL_LIBS="$(sdl2-config --libs) -lSDL2_ttf"
# Real SDL headers first; the stubs dir only supplies <switch.h> button constants.
$CXX -std=gnu++17 -fno-exceptions -fno-rtti -O1 -g -fsanitize=address,undefined \
    $SDL_CFLAGS -I"$(dirname "$(sdl2-config --prefix)/include/SDL2/SDL.h")/.." -I"$ROOT/tests/host/stubs" \
    "$ROOT/source/pp_model.cpp" "$ROOT/source/pp_ui.cpp" "$ROOT/tests/host/ui_harness.cpp" \
    $SDL_LIBS -o "$OUT/ui_harness"
# Homebrew's sdl2 is sdl2-compat: it dlopen()s SDL3, which lives outside the default
# search path on Apple Silicon (otherwise SDL shows a blocking alert and waits).
DYLD_FALLBACK_LIBRARY_PATH="$(brew --prefix 2>/dev/null || echo /usr/local)/lib:${DYLD_FALLBACK_LIBRARY_PATH:-}" \
    "$OUT/ui_harness" "$ROOT" "$OUT"
python3 - "$OUT" <<'EOF'
import sys, glob
from PIL import Image
for p in sorted(glob.glob(sys.argv[1] + "/*.bmp")):
    Image.open(p).save(p[:-4] + ".png")
print("PNG screenshots in", sys.argv[1])
EOF
