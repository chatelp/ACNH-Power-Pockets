// Builds the REAL source/main.cpp for the engine bench, unchanged:
//   - main()   becomes pp_app_main()      (called once per scenario launch)
//   - malloc() becomes pp_bench_malloc()  (fault injector, main.cpp only)
//
// The two renames are done here, AFTER every system and project header has been
// included once, instead of with -Dmalloc on the command line: redefining malloc
// before <cstdlib> breaks libstdc++ (std::malloc) and the SSE headers on Linux.
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cstdarg>
#include <climits>
#include <cmath>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <functional>
// The C spellings too: libstdc++'s <stdlib.h> wrapper has its own include guard and
// does "using std::malloc;", which must not see the rename below (stb_image includes it).
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <math.h>
#include <assert.h>
#include "pp_model.h"
#include "pp_ui.h"

extern "C" void* pp_bench_malloc(size_t size);
#define malloc pp_bench_malloc
#define main pp_app_main

// stb_image is compiled inside main.cpp; without SIMD it pulls in no extra system header.
#define STBI_NO_SIMD
#include "main.cpp"
