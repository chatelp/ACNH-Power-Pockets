#!/bin/sh
# Regenerates the README screenshots in docs/screenshots/ from the real UI code
# (tests/host/ui_gallery.cpp: fake engine, hand-picked pockets, no stale toast).
# Needs SDL2 + SDL2_ttf (macOS: brew install sdl2 sdl2_ttf) and Python Pillow.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${PP_TMP:-${TMPDIR:-/tmp}}/pp_ui_gallery"
rm -rf "$OUT"; mkdir -p "$OUT/sd"
CXX="${CXX:-clang++}"
SDL_CFLAGS="$(sdl2-config --cflags)"
SDL_LIBS="$(sdl2-config --libs) -lSDL2_ttf"
$CXX -std=gnu++17 -fno-exceptions -fno-rtti -O1 -g \
    $SDL_CFLAGS -I"$(dirname "$(sdl2-config --prefix)/include/SDL2/SDL.h")/.." -I"$ROOT/tests/host/stubs" \
    "$ROOT/source/pp_model.cpp" "$ROOT/source/pp_ui.cpp" "$ROOT/tests/host/ui_gallery.cpp" \
    $SDL_LIBS -o "$OUT/ui_gallery"
# Homebrew's sdl2 is sdl2-compat and dlopen()s SDL3 from outside the default search path.
DYLD_FALLBACK_LIBRARY_PATH="$(brew --prefix 2>/dev/null || echo /usr/local)/lib:${DYLD_FALLBACK_LIBRARY_PATH:-}" \
    "$OUT/ui_gallery" "$ROOT" "$OUT"
python3 - "$OUT" "$ROOT/docs/screenshots" <<'EOF'
import sys, glob, os
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for p in sorted(glob.glob(src + "/*.bmp")):
    out = os.path.join(dst, os.path.basename(p)[:-4] + ".png")
    Image.open(p).convert("RGB").save(out, optimize=True)
    print("wrote", out)
EOF
