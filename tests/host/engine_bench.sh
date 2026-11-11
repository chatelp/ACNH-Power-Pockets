#!/bin/sh
# Engine bench: builds the REAL source/main.cpp against a fake Switch platform
# (tests/host/engine/fake_platform.cpp) and runs it on a synthetic 3.0.x save.
# Checks byte for byte what reaches the simulated console save and SD card after
# Apply, restore and Save Doctor, including injected failures. No devkitPro, no SDL.
#   sh tests/host/engine_bench.sh [scenario-substring]
#   PP_SRC=<dir>  builds another copy of source/ (e.g. to compare with an older revision)
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="${PP_SRC:-$ROOT/source}"
OUT="${PP_TMP:-${TMPDIR:-/tmp}}/pp_engine_bench"
mkdir -p "$OUT/worlds"
CXX="${CXX:-clang++}"
FLAGS="-std=gnu++17 -fno-exceptions -fno-rtti -O2 -g"
# PP_NOSAN=1 drops the sanitizers (only useful for the slow_scan timing scenario).
[ -n "$PP_NOSAN" ] || FLAGS="$FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined"

cc() {  # cc <source> <object> [extra flags]
    src="$1"; obj="$2"; shift 2
    $CXX $FLAGS -D__SWITCH__ -DAPP_VERSION_STR='"bench"' \
        -I"$ROOT/tests/host/stubs" -I"$SRC" -I"$ROOT/tests/host/engine" "$@" -c "$src" -o "$obj"
}

echo "== engine bench (real main.cpp, fake Switch platform) =="
# main() becomes pp_app_main(); malloc() in main.cpp only goes through the fault injector.
cc "$SRC/main.cpp" "$OUT/main.o" -Dmain=pp_app_main -Dmalloc=pp_bench_malloc
cc "$SRC/pp_model.cpp" "$OUT/pp_model.o"
cc "$SRC/pp_ui.cpp" "$OUT/pp_ui.o"
cc "$ROOT/tests/host/engine/fake_platform.cpp" "$OUT/fake_platform.o"
cc "$ROOT/tests/host/engine/engine_bench.cpp" "$OUT/engine_bench.o"
$CXX $FLAGS "$OUT/main.o" "$OUT/pp_model.o" "$OUT/pp_ui.o" "$OUT/fake_platform.o" "$OUT/engine_bench.o" -o "$OUT/engine_bench"
"$OUT/engine_bench" "$ROOT" "$OUT/worlds" "$@"
