// Fake Switch platform: see fake_platform.h. Host test code only.
#include "fake_platform.h"

#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <algorithm>
#include <map>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fake {

std::string nandDir, sdRoot;
Faults faults;
Counters counters;
std::deque<Step> script;
std::deque<std::string> keyboardReplies;

static uint32_t g_ticks = 1000;
static std::vector<std::pair<SDL_Texture*, std::string>> g_live;   // textures on screen, in draw order

void press(uint64_t b, int times) { for (int i = 0; i < times; i++) { script.push_back({ b, nullptr }); script.push_back({ 0, nullptr }); } }
void idle(int frames) { for (int i = 0; i < frames; i++) script.push_back({ 0, nullptr }); }
void hold(uint64_t b, int frames) { for (int i = 0; i < frames; i++) script.push_back({ b, nullptr }); script.push_back({ 0, nullptr }); }
void probe(std::function<void()> fn) { script.push_back({ 0, fn }); }

bool screenHas(const std::string& t) {
    for (size_t i = 0; i < g_live.size(); i++) if (g_live[i].second.find(t) != std::string::npos) return true;
    return false;
}
std::string screenText() {
    std::string s;
    for (size_t i = 0; i < g_live.size(); i++) if (!g_live[i].second.empty()) { s += g_live[i].second; s += '\n'; }
    return s;
}

void setFileSizeLimit(long bytes) {
    signal(SIGXFSZ, SIG_IGN);
    struct rlimit rl;
    getrlimit(RLIMIT_FSIZE, &rl);
    rl.rlim_cur = bytes < 0 ? rl.rlim_max : (rlim_t)bytes;
    setrlimit(RLIMIT_FSIZE, &rl);
}

// ------------------------------ file systems -------------------------------
enum { FS_SAVE = 1, FS_SD = 2 };
struct OpenFile { int fs; std::string path; u32 mode; bool open; };
static std::vector<OpenFile> g_files;
static std::map<std::string, std::vector<uint8_t>> g_journal;       // staged savedata writes

static std::string full(FsFileSystem* fs, const char* path) { return (fs->h == FS_SAVE ? nandDir : sdRoot) + path; }
static bool readDisk(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = out.empty() || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}
static bool writeDisk(const std::string& p, const std::vector<uint8_t>& d) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = d.empty() || fwrite(d.data(), 1, d.size(), f) == d.size();
    if (fclose(f) != 0) ok = false;
    return ok;
}
static bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

void reset() {
    faults = Faults(); counters = Counters();
    script.clear(); keyboardReplies.clear();
    g_files.clear(); g_journal.clear(); g_live.clear();
    g_ticks = 1000;
    setFileSizeLimit(-1);
}

} // namespace fake

using namespace fake;

// Replacement for malloc() in main.cpp only (see main_under_test.cpp).
extern "C" void* pp_bench_malloc(size_t size) {
    if (faults.mallocFailSize && size == faults.mallocFailSize && faults.mallocFailCount > 0) {
        if (faults.mallocFailSkip > 0) faults.mallocFailSkip--;
        else { faults.mallocFailCount--; return nullptr; }
    }
    return malloc(size);
}

// --------------------------------- libnx: fs -------------------------------
Result fsOpenSaveDataFileSystem(FsFileSystem* out, FsSaveDataSpaceId, const FsSaveDataAttribute*) {
    if (faults.mountFail) return 0x7D402;
    counters.mounts++;
    out->h = FS_SAVE;
    return 0;
}
Result fsOpenSdCardFileSystem(FsFileSystem* out) {
    if (faults.sdOpenFail > 0) { faults.sdOpenFail--; return 0x2A2; }
    out->h = FS_SD;
    return 0;
}
Result fsFsOpenFile(FsFileSystem* fs, const char* path, u32 mode, FsFile* out) {
    std::string p = full(fs, path);
    if (!exists(p) && !(fs->h == FS_SAVE && g_journal.count(p))) return 0x202;      // PathNotFound
    g_files.push_back({ (int)fs->h, p, mode, true });
    out->h = (u32)g_files.size();
    return 0;
}
Result fsFsCreateFile(FsFileSystem* fs, const char* path, s64 size, u32) {
    std::string p = full(fs, path);
    if (fs->h == FS_SD) counters.sdCreates++;
    if (exists(p)) return 0x402;                                                    // PathAlreadyExists
    if (fs->h == FS_SD && !faults.sdCreateFail.empty() && p.find(faults.sdCreateFail) != std::string::npos) return 0x2EE202;
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return 0x2EE202;
    bool ok = ftruncate(fileno(f), (off_t)size) == 0;                               // fails past RLIMIT_FSIZE ("card full")
    fclose(f);
    if (!ok) { unlink(p.c_str()); return 0x2EE202; }
    return 0;
}
Result fsFsDeleteFile(FsFileSystem* fs, const char* path) {
    return unlink(full(fs, path).c_str()) == 0 ? 0 : 0x202;
}
Result fsFsCommit(FsFileSystem* fs) {
    if (fs->h != FS_SAVE) return 0;
    if (faults.commitFail) return 0x3D4402;
    for (auto& kv : g_journal) writeDisk(kv.first, kv.second);
    g_journal.clear();
    counters.commits++;
    return 0;
}
void fsFsClose(FsFileSystem* fs) {
    if (fs->h == FS_SAVE && !g_journal.empty()) { counters.droppedJournals++; g_journal.clear(); }
    fs->h = 0;
}
static OpenFile* fileOf(FsFile* f) {
    if (f->h == 0 || f->h > g_files.size() || !g_files[f->h - 1].open) return nullptr;
    return &g_files[f->h - 1];
}
static bool current(OpenFile* of, std::vector<uint8_t>& out) {
    if (of->fs == FS_SAVE) { auto it = g_journal.find(of->path); if (it != g_journal.end()) { out = it->second; return true; } }
    return readDisk(of->path, out);
}
Result fsFileGetSize(FsFile* f, s64* out) {
    OpenFile* of = fileOf(f); std::vector<uint8_t> d;
    if (!of || !current(of, d)) return 0x202;
    *out = (s64)d.size();
    return 0;
}
Result fsFileRead(FsFile* f, s64 off, void* buf, u64 size, u32, u64* rd) {
    OpenFile* of = fileOf(f); std::vector<uint8_t> d;
    if (!of || !(of->mode & FsOpenMode_Read) || !current(of, d)) return 0x202;
    u64 n = (u64)off >= d.size() ? 0 : std::min<u64>(size, d.size() - (u64)off);
    if (n) memcpy(buf, d.data() + off, n);
    if (rd) *rd = n;
    return 0;
}
Result fsFileWrite(FsFile* f, s64 off, const void* buf, u64 size, u32) {
    OpenFile* of = fileOf(f); std::vector<uint8_t> d;
    if (!of || !(of->mode & FsOpenMode_Write) || !current(of, d)) return 0x202;
    if (of->fs == FS_SAVE) {
        counters.nandWrites++;
        if (!faults.nandWriteFail.empty() && of->path.find(faults.nandWriteFail) != std::string::npos) { counters.nandWriteFailures++; return 0x2EE202; }
    } else {
        counters.sdWrites++;
        if (!faults.sdWriteFail.empty() && of->path.find(faults.sdWriteFail) != std::string::npos) return 0x2EE202;
    }
    if ((u64)off + size > d.size() && !(of->mode & FsOpenMode_Append)) return 0x1A02;   // no implicit resize
    if ((u64)off + size > d.size()) d.resize((size_t)((u64)off + size));
    memcpy(d.data() + off, buf, size);
    if (of->fs == FS_SAVE) { g_journal[of->path] = d; return 0; }                    // staged until fsFsCommit
    return writeDisk(of->path, d) ? 0 : 0x2EE202;
}
void fsFileClose(FsFile* f) { OpenFile* of = fileOf(f); if (of) of->open = false; f->h = 0; }

// ----------------------------- libnx: everything else ----------------------
Result swkbdCreate(SwkbdConfig*, s32) { return 0; }
void swkbdConfigMakePresetDefault(SwkbdConfig*) {}
void swkbdConfigSetGuideText(SwkbdConfig*, const char*) {}
void swkbdConfigSetInitialText(SwkbdConfig*, const char*) {}
Result swkbdShow(SwkbdConfig*, char* out, size_t size) {
    if (keyboardReplies.empty()) return 0x29F;                                        // cancelled
    snprintf(out, size, "%s", keyboardReplies.front().c_str());
    keyboardReplies.pop_front();
    return 0;
}
void swkbdClose(SwkbdConfig*) {}

PrintConsole* consoleInit(PrintConsole*) { return nullptr; }
void consoleClear(void) {}
void consoleUpdate(PrintConsole*) {}
void consoleExit(PrintConsole*) {}

void padConfigureInput(u32, u32) {}
void padInitializeDefault(PadState* pad) { pad->buttons_cur = pad->buttons_old = 0; }
void padUpdate(PadState* pad) {
    pad->buttons_old = pad->buttons_cur;
    g_ticks += 16;
    counters.frames++;
    if (script.empty()) { pad->buttons_cur = 0; return; }
    Step s = script.front(); script.pop_front();
    if (s.probe) s.probe();
    pad->buttons_cur = s.held;
}
u64 padGetButtonsDown(const PadState* pad) { return pad->buttons_cur & ~pad->buttons_old; }
u64 padGetButtons(const PadState* pad) { return pad->buttons_cur; }

AppletType appletGetAppletType(void) { return faults.appletMode ? AppletType_LibraryApplet : AppletType_Application; }
bool appletMainLoop(void) { return !script.empty(); }
void svcSleepThread(s64) {}

// ----------------------------------- SDL -----------------------------------
struct SDL_Texture { std::string text; };
struct FakeSurface { SDL_Surface s; std::string text; };
static int g_renderer_token, g_window_token;

int SDL_Init(Uint32) { return 0; }
void SDL_Quit(void) {}
SDL_Window* SDL_CreateWindow(const char*, int, int, int, int, Uint32) { return (SDL_Window*)&g_window_token; }
void SDL_DestroyWindow(SDL_Window*) {}
SDL_Renderer* SDL_CreateRenderer(SDL_Window*, int, Uint32) { return (SDL_Renderer*)&g_renderer_token; }
void SDL_DestroyRenderer(SDL_Renderer*) {}
int SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8) { return 0; }
int SDL_SetRenderDrawBlendMode(SDL_Renderer*, SDL_BlendMode) { return 0; }
int SDL_RenderFillRect(SDL_Renderer*, const SDL_Rect*) { return 0; }
int SDL_RenderClear(SDL_Renderer*) { return 0; }
int SDL_RenderCopy(SDL_Renderer*, SDL_Texture*, const SDL_Rect*, const SDL_Rect*) { return 0; }
void SDL_RenderPresent(SDL_Renderer*) {}
SDL_Texture* SDL_CreateTexture(SDL_Renderer*, Uint32, int, int, int) { return new SDL_Texture(); }
SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer*, SDL_Surface* s) {
    SDL_Texture* t = new SDL_Texture();
    t->text = ((FakeSurface*)s)->text;
    g_live.push_back({ t, t->text });
    return t;
}
int SDL_UpdateTexture(SDL_Texture*, const SDL_Rect*, const void*, int) { return 0; }
int SDL_SetTextureBlendMode(SDL_Texture*, SDL_BlendMode) { return 0; }
int SDL_SetTextureColorMod(SDL_Texture*, Uint8, Uint8, Uint8) { return 0; }
int SDL_QueryTexture(SDL_Texture*, Uint32*, int*, int*, int*) { return 0; }
void SDL_DestroyTexture(SDL_Texture* t) {
    for (size_t i = 0; i < g_live.size(); i++) if (g_live[i].first == t) { g_live.erase(g_live.begin() + (long)i); break; }
    delete t;
}
void SDL_FreeSurface(SDL_Surface* s) { delete (FakeSurface*)s; }
Uint32 SDL_GetTicks(void) { return g_ticks; }

struct _TTF_Font { int size; };
int TTF_Init(void) { return 0; }
void TTF_Quit(void) {}
TTF_Font* TTF_OpenFont(const char* file, int ptsize) {
    if (faults.fontOpenFail || !exists(file)) return nullptr;
    TTF_Font* f = new _TTF_Font(); f->size = ptsize; return f;
}
void TTF_CloseFont(TTF_Font* f) { delete f; }
int TTF_FontHeight(const TTF_Font* f) { return f->size; }
// Narrow glyphs on purpose: nothing gets truncated or wrapped mid-sentence,
// so the bench can look for whole strings on the "screen".
int TTF_SizeUTF8(TTF_Font*, const char* text, int* w, int* h) { if (w) *w = (int)strlen(text) * 3; if (h) *h = 20; return 0; }
SDL_Surface* TTF_RenderUTF8_Blended(TTF_Font*, const char* text, SDL_Color) {
    FakeSurface* s = new FakeSurface();
    s->s.w = (int)strlen(text) * 3; s->s.h = 20; s->text = text;
    return &s->s;
}
