#!/bin/sh
# Regenerates the README screenshots in docs/screenshots/ from the real UI code
# (tests/host/ui_gallery.cpp: fake engine, hand-picked pockets, no stale toast).
# Needs SDL2 + SDL2_ttf (macOS: brew install sdl2 sdl2_ttf) and Python Pillow.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${PP_TMP:-${TMPDIR:-/tmp}}/pp_ui_gallery"
rm -rf "$OUT"; mkdir -p "$OUT/sd" "$OUT/demo"
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
# Demo animation: frames named NNN_<milliseconds>.bmp -> docs/screenshots/demo.gif (half size)
# and, when ffmpeg is installed, docs/screenshots/demo.mp4 (full size).
python3 - "$OUT/demo" "$ROOT/docs/screenshots" <<'PYDEMO'
import sys, glob, os, subprocess, shutil
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
frames = sorted(glob.glob(src + "/*.bmp"))
if not frames: sys.exit(0)
ims, durs = [], []
for p in frames:
    durs.append(int(os.path.basename(p)[:-4].split("_")[1]))
    ims.append(Image.open(p).convert("RGB"))
small = [im.resize((640, 360), Image.LANCZOS).quantize(colors=128, method=Image.MEDIANCUT) for im in ims]
small[0].save(dst + "/demo.gif", save_all=True, append_images=small[1:], duration=durs, loop=0, optimize=True)
print("wrote", dst + "/demo.gif", len(ims), "frames")
if shutil.which("ffmpeg"):
    lst = src + "/frames.txt"
    with open(lst, "w") as f:
        for p, d in zip(frames, durs): f.write("file '%s'\nduration %.3f\n" % (p, d / 1000.0))
        f.write("file '%s'\n" % frames[-1])
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "concat", "-safe", "0", "-i", lst, "-vf", "fps=30,format=yuv420p",
                    "-c:v", "libx264", "-crf", "20", "-movflags", "+faststart", dst + "/demo.mp4"], check=True)
    print("wrote", dst + "/demo.mp4")
PYDEMO
