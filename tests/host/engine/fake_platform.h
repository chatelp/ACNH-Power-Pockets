// Fake Switch platform for tests/host/engine/engine_bench.cpp.
//
// Implements, on a desktop OS, everything source/main.cpp needs from libnx,
// SDL2 and SDL_ttf (declarations: tests/host/stubs/):
//   - the ACNH savedata as a directory with a commit journal (writes are staged
//     in memory and only reach the directory on fsFsCommit, like Horizon's
//     journaled savedata; closing without a commit drops them),
//   - the SD card as a directory (the app's fopen("sdmc:/...") calls resolve to
//     the same files because the bench chdir()s next to a folder named "sdmc:"),
//   - a scripted controller, a virtual clock (16 ms per padUpdate),
//   - a text-only "screen": every string the UI renders is recorded,
//   - fault injection for each engine step.
// Nothing here is linked into the Switch build.
#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>

namespace fake {

// ---- directories (set before running the app) ----
extern std::string nandDir;      // holds main.dat, mainHeader.dat, Villager0/...
extern std::string sdRoot;       // SD card root (".../sdmc:")

// ---- fault injection ----
struct Faults {
    bool        appletMode = false;      // launched from the Album
    bool        mountFail = false;       // fsOpenSaveDataFileSystem fails (game running)
    int         sdOpenFail = 0;          // next N fsOpenSdCardFileSystem calls fail
    std::string sdCreateFail;            // fsFsCreateFile on the SD fails when the path contains this
    std::string sdWriteFail;             // fsFileWrite on the SD fails (file stays created, zero-filled)
    std::string nandWriteFail;           // fsFileWrite on the savedata fails when the path contains this
    bool        commitFail = false;      // fsFsCommit returns an error and commits nothing
    size_t      mallocFailSize = 0;      // main.cpp malloc(size) fails for this exact size...
    int         mallocFailSkip = 0;      // ...after letting this many matching calls succeed
    int         mallocFailCount = 0;     // ...this many times (then succeeds again)
    bool        fontOpenFail = false;    // TTF_OpenFont returns nullptr (corrupted font)
};
extern Faults faults;

// ---- counters ----
struct Counters {
    int mounts = 0, commits = 0, nandWrites = 0, nandWriteFailures = 0;
    int sdCreates = 0, sdWrites = 0, droppedJournals = 0, frames = 0;
};
extern Counters counters;

// ---- scripted controller ----
// Each step is one main-loop iteration: `probe` runs first (it sees the screen
// and the files as the previous frame left them), then `held` becomes the pad state.
struct Step { uint64_t held; std::function<void()> probe; };
extern std::deque<Step> script;
extern std::deque<std::string> keyboardReplies;      // empty queue = keyboard cancelled
void press(uint64_t button, int times = 1);          // 1 frame down, 1 frame up
void idle(int frames = 3);
void hold(uint64_t button, int frames);
void probe(std::function<void()> fn);

// ---- text screen ----
bool screenHas(const std::string& text);
std::string screenText();

// RLIMIT_FSIZE as a "SD card full" model for every file write of the process.
void setFileSizeLimit(long bytes);                   // < 0 removes the limit

void reset();

} // namespace fake
