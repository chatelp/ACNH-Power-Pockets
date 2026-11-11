#!/bin/sh
# Host checks for ACNH Power Pockets (no devkitPro needed).
#  1. Unit tests of the UI model (source/pp_model.cpp).
#  2. Syntax check of every source file against minimal libnx / SDL2 stubs,
#     with the same language flags as the Switch build (gnu++17, no exceptions, no RTTI).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${PP_TMP:-${TMPDIR:-/tmp}}/pp_host_build"
mkdir -p "$OUT"
CXX="${CXX:-clang++}"
FLAGS="-std=gnu++17 -fno-exceptions -fno-rtti -Wall -Wextra -Wno-unused-parameter"

echo "== unit tests (pp_model) =="
$CXX $FLAGS -O1 -g -fsanitize=address,undefined \
    "$ROOT/source/pp_model.cpp" "$ROOT/tests/host/test_model.cpp" -o "$OUT/test_model"
PP_TMP="$OUT" "$OUT/test_model"

echo
echo "== syntax check against libnx / SDL2 stubs =="
for f in "$ROOT"/source/*.cpp; do
    echo "  $(basename "$f")"
    $CXX $FLAGS -fsyntax-only -D__SWITCH__ -DAPP_VERSION_STR='"host"' \
        -I"$ROOT/tests/host/stubs" -I"$ROOT/source" "$f"
done
echo "OK"
