// ============================================================================
// Engine bench: runs the REAL source/main.cpp (upstream save engine + Power
// Pockets hooks + panel UI) on a desktop OS, against the fake Switch platform
// of fake_platform.cpp, on a synthetic 3.0.x save (personal.dat 0x74A40 bytes,
// main.dat 0x9B0E90 bytes, valid hashes and EncryptedInt32 fields).
//
// Each scenario drives the app with scripted button presses, then checks byte
// for byte, after decrypting with an independent oracle, what reached the
// simulated console save and the simulated SD card.
//
// Build + run: sh tests/host/engine_bench.sh
// ============================================================================
#include "fake_platform.h"
#include "pp_model.h"

#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int pp_app_main(int argc, char** argv);                       // source/main.cpp, built with -Dmain=pp_app_main
void CryptACNH(const uint8_t* header, uint8_t* data, size_t dataLen);   // engine (symmetric AES-CTR)

using namespace fake;
typedef std::vector<uint8_t> Bytes;

// ------------------------------ save layout 3.0.x --------------------------
static const size_t   P_SZ = 0x74A40, M_SZ = 0x9B0E90, HDR_SZ = 0x300;
static const uint32_t WALLET = 0x37D60, BANK = 0x6527C, MILES = 0x12718, LOAN = 0x33CBF0, POCKETS = 0x37CA8;
static const uint32_t HEAL0 = 0x5B3D50, HEAL_STEP = 0x74A40;
struct Reg { uint32_t off, len; };
static const Reg P_REGS[] = { { 0x00110, 0x37acc }, { 0x37be0, 0x3ce5c } };      // NHSE FileHashRevision REV_300
static const Reg M_REGS[] = {
    { 0x000110, 0x1e339c }, { 0x1e34b0, 0x3d089c }, { 0x5b3e60, 0x037acc }, { 0x5eb930, 0x03ce5c },
    { 0x6288a0, 0x037acc }, { 0x660370, 0x03ce5c }, { 0x69d2e0, 0x037acc }, { 0x6d4db0, 0x03ce5c },
    { 0x711d20, 0x037acc }, { 0x7497f0, 0x03ce5c }, { 0x786760, 0x037acc }, { 0x7be230, 0x03ce5c },
    { 0x7fb1a0, 0x037acc }, { 0x832c70, 0x03ce5c }, { 0x86fbe0, 0x037acc }, { 0x8a76b0, 0x03ce5c },
    { 0x8e4620, 0x037acc }, { 0x91c0f0, 0x03ce5c }, { 0x958f50, 0x057f3c } };
static const uint16_t ID_NMT = 0x16DB, ID_WOOD = 0x0AD0, ID_APPLE = 0x08A5, ID_GAXE = 0x2591;   // line index in items.txt

// ------------------------------- test plumbing -----------------------------
static int g_fail = 0, g_pass = 0;
static int g_sysRow = 0;                                        // System panel cursor (row 0 at launch, kept between visits)
static bool g_rawStart = false;                                 // true: the script starts on the very first frame
static const char* g_scn = "";
static std::string g_repo, g_tmp;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("    FAIL [%s] %s:%d  %s\n", g_scn, __FILE__, __LINE__, #c); } } while (0)

static bool readFile(const std::string& p, Bytes& out) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) { out.clear(); return false; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    bool ok = out.empty() || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}
static Bytes slurp(const std::string& p) { Bytes b; readFile(p, b); return b; }
static void writeFile(const std::string& p, const Bytes& d) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) { printf("cannot write %s\n", p.c_str()); exit(2); }
    if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    fclose(f);
}
static bool fileThere(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

// --------------------------- independent oracles ---------------------------
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t rotl(uint32_t x, unsigned r) { r &= 31; return r ? (x << r) | (x >> (32 - r)) : x; }
static uint32_t rotr(uint32_t x, unsigned r) { r &= 31; return r ? (x >> r) | (x << (32 - r)) : x; }
// Murmur3 x86_32 over whole 32-bit words, seed 0 (NHSE Murmur3.GetMurmur3Hash).
static uint32_t murmur(const uint8_t* d, size_t len) {
    uint32_t h = 0;
    for (size_t i = 0; i + 4 <= len; i += 4) {
        uint32_t k = rd32(d + i);
        k *= 0xCC9E2D51u; k = rotl(k, 15); k *= 0x1B873593u;
        h ^= k; h = rotl(h, 13); h = h * 5 + 0xE6546B64u;
    }
    h ^= (uint32_t)len; h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
// EncryptedInt32 (NHSE): u32 enc, u16 adjust, u8 shift, u8 checksum.
static uint32_t encDecode(const uint8_t* f) { return rotr(rd32(f), f[6] + 3u) + 0x80E32B11u - (uint32_t)(f[4] | (f[5] << 8)); }
static uint8_t  encChecksum(uint32_t enc) { return (uint8_t)((enc & 0xFF) + ((enc >> 8) & 0xFF) + ((enc >> 16) & 0xFF) + (enc >> 24) - 0x2D); }
static void encStore(uint8_t* f, uint32_t value) {                      // keeps adjust and shift
    uint32_t enc = rotl(value + (uint32_t)(f[4] | (f[5] << 8)) - 0x80E32B11u, f[6] + 3u);
    wr32(f, enc); f[7] = encChecksum(enc);
}
static bool encValid(const uint8_t* f) { return f[7] == encChecksum(rd32(f)); }

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static void fillRandom(Bytes& b) { for (size_t i = 0; i + 8 <= b.size(); i += 8) { uint64_t v = rnd(); memcpy(&b[i], &v, 8); } }
static void fixHashes(Bytes& b, const Reg* r, size_t n) { for (size_t i = 0; i < n; i++) wr32(&b[r[i].off], murmur(&b[r[i].off + 4], r[i].len)); }
static int  validHashes(const Bytes& b, const Reg* r, size_t n) {
    int ok = 0;
    for (size_t i = 0; i < n; i++) if (r[i].off + 4 + r[i].len <= b.size() && rd32(&b[r[i].off]) == murmur(&b[r[i].off + 4], r[i].len)) ok++;
    return ok;
}

// What the app is expected to hold / write: 20 pocket records + 4 values.
struct Model { uint8_t pockets[pp::POCKETS_BYTES]; uint32_t vals[4]; };
static void rec(uint8_t* pockets, int slot, uint16_t id, uint8_t sys, uint8_t wrap, uint16_t raw, uint16_t wear) {
    uint8_t* r = pockets + (slot - 1) * 8;
    r[0] = (uint8_t)id; r[1] = (uint8_t)(id >> 8); r[2] = sys; r[3] = wrap; r[4] = (uint8_t)raw; r[5] = (uint8_t)(raw >> 8); r[6] = (uint8_t)wear; r[7] = (uint8_t)(wear >> 8);
}

struct Save {
    Bytes pHdr, mHdr;            // *Header.dat (never written by the app)
    Bytes pPlain, mPlain;        // decrypted
    Bytes pEnc, mEnc;            // as stored on the console
    Model model;
};
static void encrypt(Save& s) {
    s.pEnc = s.pPlain; s.mEnc = s.mPlain;
    CryptACNH(s.pHdr.data(), s.pEnc.data(), s.pEnc.size());
    CryptACNH(s.mHdr.data(), s.mEnc.data(), s.mEnc.size());
}
static Save makeSave(size_t pSz = P_SZ, size_t mSz = M_SZ) {
    Save s;
    s.pHdr.resize(HDR_SZ); s.mHdr.resize(HDR_SZ); fillRandom(s.pHdr); fillRandom(s.mHdr);
    s.pPlain.resize(pSz); s.mPlain.resize(mSz); fillRandom(s.pPlain); fillRandom(s.mPlain);
    Model& m = s.model;
    for (int i = 1; i <= 20; i++) rec(m.pockets, i, pp::ID_EMPTY, 0, 0, 0, 0);
    rec(m.pockets, 1, ID_GAXE, 0x20, 0, 0, 7);               // tool with wear and a flag byte
    rec(m.pockets, 2, ID_NMT, 0, 0, 4, 0);
    rec(m.pockets, 3, ID_WOOD, 0, 0x41, 29, 0);              // wrapped stack
    rec(m.pockets, 4, ID_APPLE, 0, 0, 9, 0);
    rec(m.pockets, 19, pp::ID_EMPTY, 0, 0, 5, 0);            // "dirty" empty record: must be left alone
    memcpy(&s.pPlain[POCKETS], m.pockets, sizeof m.pockets);
    wr32(&s.pPlain[POCKETS + 160], 20);                       // slot count that follows the 20 records
    m.vals[0] = 12345; m.vals[1] = 4800000; m.vals[2] = 230500; m.vals[3] = 98000;
    encStore(&s.pPlain[WALLET], m.vals[0]); encStore(&s.pPlain[BANK], m.vals[1]);
    encStore(&s.pPlain[MILES], m.vals[2]);  encStore(&s.mPlain[LOAN], m.vals[3]);
    for (int i = 0; i < 8; i++) wr32(&s.mPlain[HEAL0 + i * HEAL_STEP], i == 2 ? 0xDEADBEEFu : 0);   // one "poisoned" field
    if (pSz >= P_SZ) fixHashes(s.pPlain, P_REGS, 2);          // valid hashes keep the engine's region scan fast
    if (mSz >= M_SZ) fixHashes(s.mPlain, M_REGS, 19);
    encrypt(s);
    return s;
}

// The state Apply must produce from `before` and the model: nothing else may change.
static void expectApply(Bytes& p, Bytes& m, const Model& mo) {
    memcpy(&p[POCKETS], mo.pockets, sizeof mo.pockets);
    encStore(&p[WALLET], mo.vals[0]); encStore(&p[BANK], mo.vals[1]); encStore(&p[MILES], mo.vals[2]);
    encStore(&m[LOAN], mo.vals[3]);
    for (int i = 0; i < 8; i++) wr32(&m[HEAL0 + i * HEAL_STEP], 0);
}
static size_t firstDiff(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return 0;
    for (size_t i = 0; i < a.size(); i++) if (a[i] != b[i]) return i;
    return (size_t)-1;
}
#define CHECK_SAME(a, b) do { size_t d_ = firstDiff(a, b); if (d_ != (size_t)-1) printf("    first difference at 0x%zX (sizes %zu / %zu)\n", d_, (a).size(), (b).size()); CHECK(d_ == (size_t)-1); } while (0)

// ------------------------------- test world --------------------------------
// Paths are relative: each app run happens in a forked child that chdir()s into the world.
static const char* NAND_P = "nand/Villager0/personal.dat";
static const char* NAND_M = "nand/main.dat";
static std::string sd(const char* name) { return std::string("sdmc:/switch/acnh_editor/") + name; }

struct World { std::string root; };
static void putSave(const World& w, const Save& s) {
    writeFile(w.root + "/nand/Villager0/personal.dat", s.pEnc);
    writeFile(w.root + "/nand/Villager0/personalHeader.dat", s.pHdr);
    writeFile(w.root + "/nand/main.dat", s.mEnc);
    writeFile(w.root + "/nand/mainHeader.dat", s.mHdr);
}
static World makeWorld(const char* name, const Save& s, bool withItems = true, bool withFont = true) {
    World w; w.root = g_tmp + "/" + name;
    std::string cmd = "rm -rf '" + w.root + "' && mkdir -p '" + w.root + "/nand/Villager0' '" + w.root + "/sdmc:/switch/acnh_editor'";
    if (system(cmd.c_str()) != 0) { printf("cannot create %s\n", w.root.c_str()); exit(2); }
    putSave(w, s);
    std::string dst = w.root + "/sdmc:/switch/acnh_editor/";
    if (withFont) writeFile(dst + "font.ttf", slurp(g_repo + "/assets/font.ttf"));
    if (withItems) writeFile(dst + "items.txt", slurp(g_repo + "/assets/items.txt"));
    writeFile(dst + "nonstackable.txt", slurp(g_repo + "/assets/nonstackable.txt"));
    return w;
}
// Decrypted committed console state (child side, relative paths).
static Bytes nandPlainP(const Save& s) { Bytes b = slurp(NAND_P); if (!b.empty()) CryptACNH(s.pHdr.data(), b.data(), b.size()); return b; }
static Bytes nandPlainM(const Save& s) { Bytes b = slurp(NAND_M); if (!b.empty()) CryptACNH(s.mHdr.data(), b.data(), b.size()); return b; }

// One app launch in a child process (main.cpp keeps static state: one run per process).
// `build` fills the controller script; `after` runs once the app has returned.
template <class Build, class After>
static void launch(const World& w, const char* label, Build build, After after) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        if (chdir(w.root.c_str()) != 0) _exit(120);
        reset(); nandDir = "nand"; sdRoot = "sdmc:";
        g_fail = 0; g_pass = 0; g_sysRow = 0;
        if (!g_rawStart) idle(2);                             // a real player never presses on the first frame
        build();
        char arg0[] = "app"; char* argv[] = { arg0, nullptr };
        pp_app_main(1, argv);
        after();
        CHECK(script.empty());                                // the app consumed the whole script
        FILE* f = fopen((g_tmp + "/passes.txt").c_str(), "ab");
        if (f) { fprintf(f, "%d\n", g_pass); fclose(f); }
        fflush(stdout);
        _exit(g_fail > 100 ? 100 : g_fail);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) <= 100) g_fail += WEXITSTATUS(st);
    else { g_fail++; printf("    FAIL [%s] launch \"%s\" crashed or was killed (status 0x%X)\n", g_scn, label, st); }
}
template <class Build> static void launch(const World& w, const char* label, Build build) { launch(w, label, build, []() {}); }

// ------------------------------ script helpers -----------------------------
static const uint64_t A = HidNpadButton_A, B = HidNpadButton_B, Y = HidNpadButton_Y, L = HidNpadButton_L, R = HidNpadButton_R,
                      UP = HidNpadButton_Up, DOWN = HidNpadButton_Down, LEFT = HidNpadButton_Left, RIGHT = HidNpadButton_Right,
                      PLUS = HidNpadButton_Plus, MINUS = HidNpadButton_Minus;
enum { P_POCKETS, P_FAVS, P_CATALOG, P_MONEY, P_LOADOUTS, P_SYSTEM };
enum { ROW_RESTORE, ROW_SESSION, ROW_DOCTOR };
static const int HOLD_FRAMES = 110;                             // 1.76 s of virtual time > 1.5 s

static void see_(const char* text, bool want, int line) {
    std::string t = text;
    probe([t, want, line]() {
        bool has = screenHas(t);
        if (has == want) { g_pass++; return; }
        g_fail++;
        printf("    FAIL [%s] line %d: screen %s \"%s\". Screen:\n", g_scn, line, want ? "does not show" : "shows", t.c_str());
        std::string s = screenText(); size_t a = 0;
        while (a < s.size()) { size_t e = s.find('\n', a); printf("      | %s\n", s.substr(a, e - a).c_str()); a = e + 1; }
    });
}
#define SEE(t) see_(t, true, __LINE__)
#define NOT_SEE(t) see_(t, false, __LINE__)

static void panel(int from, int to) { if (to > from) press(R, to - from); else press(L, from - to); idle(2); }
// Pockets, hand empty, cursor on an occupied slot: A > "Enter item ID (hex)..." is the 5th entry.
static void enterIdFromOccupied(const char* hex, bool handValid) {
    keyboardReplies.push_back(hex);
    press(A); press(DOWN, handValid ? 5 : 4); press(A); idle(2);
}
static void applyFromPanel() { press(PLUS); idle(2); SEE("nothing is written until you apply"); press(A); idle(6); }
static void quitFromPanel() { press(PLUS); idle(2); script.push_back({ A, nullptr }); }   // no pending change: "Quit" is focused
// System panel, cursor on `row` (0 restore, 1 pre-edit backup, 2 Doctor): A, move to the hold button, hold A.
static void systemRow(int row) {
    if (row > g_sysRow) press(DOWN, row - g_sysRow); else press(UP, g_sysRow - row);
    g_sysRow = row; idle(1); press(A); idle(2);
}
static void confirmHold() { press(RIGHT); idle(1); hold(A, HOLD_FRAMES); idle(6); }

// The first edit used by most scenarios: Nook Miles Ticket into the next empty slot (5).
static void placeTicket(Model& m, int slot) {
    enterIdFromOccupied("16DB", false);
    SEE("Holding: Nook Miles Ticket");
    press(Y); idle(2);
    rec(m.pockets, slot, ID_NMT, 0, 0, pp::qtyToRaw(1), 0);
}

// ================================ scenarios =================================

// Apply twice, restore the SD backup, restore the pre-edit backup: exact bytes everywhere.
static void scnApplyRestoreCycle(const Save& s0) {
    g_scn = "apply_restore_cycle";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        static Bytes p1, m1, enc1P, enc1M;
        idle(2); SEE("20 slots");
        // --- edits: place x2, clear a wrapped stack, quantity, 1/slot item, 4 money values ---
        placeTicket(m, 5);
        press(Y); idle(2); rec(m.pockets, 6, ID_NMT, 0, 0, pp::qtyToRaw(1), 0);          // cursor now on slot 7
        press(UP, 4); press(A); press(UP); press(A); idle(2);                           // slot 3 > Clear slot (last entry)
        rec(m.pockets, 3, pp::ID_EMPTY, 0, 0, 0, 0);
        SEE("Slot 3 cleared");
        press(UP); press(A); press(DOWN, 3); press(A); idle(2);                         // slot 2 > Quantity...
        press(RIGHT, 5); press(A); idle(2);                                             // 4 -> 9
        rec(m.pockets, 2, ID_NMT, 0, 0, pp::qtyToRaw(9), 0);
        enterIdFromOccupied("2591", true);                                              // golden axe: 1/slot
        SEE("1/slot");
        press(Y); idle(2); rec(m.pockets, 3, ID_GAXE, 0, 0, 0, 0);                       // next empty from slot 2 is slot 3: clean record, raw 0
        panel(P_POCKETS, P_MONEY);
        keyboardReplies.push_back("54321"); press(A); press(DOWN); press(A); idle(2); m.vals[0] = 54321;
        press(DOWN); keyboardReplies.push_back("1,000,000"); press(A); press(DOWN); press(A); idle(2); m.vals[1] = 1000000;
        press(DOWN); press(Y); press(RIGHT, 2); press(A); idle(2); m.vals[2] += 2000;   // stepper +2,000
        press(DOWN); press(A); press(DOWN, 3); press(A); idle(2); m.vals[3] = 0;        // Pay off loan
        SEE("8 changes");
        probe([&]() {                                         // nothing may be written before Apply
            CHECK(counters.nandWrites == 0 && counters.commits == 0 && counters.sdCreates == 0);
            CHECK(!fileThere(sd("backup_main.dat")) && !fileThere(sd("session_main.dat")));
        });
        applyFromPanel();
        SEE("Changes applied"); SEE("Up to date");
        probe([&, mo = m]() {                                  // `mo`: the model as it is at this point of the script
            Bytes eP = s0.pPlain, eM = s0.mPlain; expectApply(eP, eM, mo);
            p1 = nandPlainP(s0); m1 = nandPlainM(s0);
            CHECK_SAME(p1, eP); CHECK_SAME(m1, eM);            // only pockets, 4 values, 8 heal fields changed
            CHECK(encDecode(&p1[WALLET]) == 54321 && encValid(&p1[WALLET]) && encValid(&p1[BANK]) && encValid(&p1[MILES]) && encValid(&m1[LOAN]));
            CHECK(rd32(&p1[POCKETS + 160]) == 20);            // slot counter after the 20 records untouched
            CHECK(validHashes(m1, M_REGS, 19) == 18);         // only the loan region hash is stale (engine never rehashes)
            CHECK_SAME(slurp(sd("backup_personal.dat")), s0.pEnc); CHECK_SAME(slurp(sd("backup_main.dat")), s0.mEnc);
            CHECK_SAME(slurp(sd("session_personal.dat")), s0.pEnc); CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc);
            CHECK_SAME(slurp("nand/Villager0/personalHeader.dat"), s0.pHdr); CHECK_SAME(slurp("nand/mainHeader.dat"), s0.mHdr);
            CHECK(counters.commits == 1 && counters.nandWrites == 2);
            enc1P = slurp(NAND_P); enc1M = slurp(NAND_M);
        });
        // --- second Apply in the same session ---
        panel(P_MONEY, P_POCKETS);
        press(Y); idle(2); rec(m.pockets, 7, ID_GAXE, 0, 0, 0, 0);
        SEE("1 change");
        applyFromPanel();
        SEE("Changes applied");
        probe([&, mo = m]() {
            Bytes eP = p1, eM = m1; expectApply(eP, eM, mo);
            CHECK_SAME(nandPlainP(s0), eP); CHECK_SAME(nandPlainM(s0), eM);
            CHECK_SAME(slurp(sd("backup_personal.dat")), enc1P); CHECK_SAME(slurp(sd("backup_main.dat")), enc1M);   // state before this Apply
            CHECK_SAME(slurp(sd("session_personal.dat")), s0.pEnc); CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc); // still the pre-edit state
            CHECK(counters.commits == 2);
        });
        // --- restore the SD backup: console back to the state after the first Apply ---
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Restore the SD backup?");
        confirmHold();
        SEE("Save restored");
        probe([&]() {
            CHECK_SAME(slurp(NAND_P), enc1P); CHECK_SAME(slurp(NAND_M), enc1M);
            CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc);
            CHECK(counters.commits == 3);
        });
        // --- restore the pre-edit backup: console back to the very first state ---
        systemRow(ROW_SESSION);
        confirmHold();
        SEE("Save restored");
        probe([&]() {
            CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc);
            CHECK(counters.commits == 4);
        });
        panel(P_SYSTEM, P_POCKETS);
        SEE("Up to date");
        quitFromPanel();
    }, []() { CHECK(counters.droppedJournals == 0); });
}

// The pristine state must survive a relaunch and stay restorable (two Applies, quit,
// the game refuses the save, relaunch: the user needs the state before the FIRST Apply).
static void scnRelaunchKeepsPreEditBackup(const Save& s0) {
    g_scn = "relaunch_keeps_pre_edit_backup";
    World w = makeWorld(g_scn, s0);
    launch(w, "launch 1: two applies", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5); applyFromPanel(); SEE("Changes applied");
        press(Y); idle(2); applyFromPanel(); SEE("Changes applied");
        quitFromPanel();
    }, [&]() { CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc); CHECK(slurp(sd("backup_personal.dat")) != s0.pEnc); });
    launch(w, "launch 2: restore the SD backup first", [&]() {
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        confirmHold();
        SEE("Save restored");
        probe([&]() {                                         // the pre-edit pair must not have been overwritten
            CHECK_SAME(slurp(sd("session_personal.dat")), s0.pEnc); CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc);
        });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
    launch(w, "launch 3: restore the pre-edit backup", [&]() {
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_SESSION);
        NOT_SEE("Blocked");
        confirmHold();
        SEE("Save restored");
        probe([&]() { CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
}

// The game saved between two launches (new header, new content): the old backups are
// dead and must be refused; the next write must take a fresh pre-edit backup.
static void scnGameSavedInBetween(const Save& s0) {
    g_scn = "game_saved_in_between";
    World w = makeWorld(g_scn, s0);
    launch(w, "launch 1: apply", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5); applyFromPanel(); SEE("Changes applied"); quitFromPanel();
    });
    Save g = s0;                                               // "the game saved": content changes, hashes fixed, new key material
    g.pPlain[0x200] ^= 0x5A; g.mPlain[0x300] ^= 0x5A;
    fixHashes(g.pPlain, P_REGS, 2); fixHashes(g.mPlain, M_REGS, 19);
    fillRandom(g.pHdr); fillRandom(g.mHdr);
    encrypt(g);
    putSave(w, g);
    launch(w, "launch 2: old backups refused, fresh pre-edit backup", [&]() {
        static Model m; m = g.model;
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Not available right now"); NOT_SEE("Restore the SD backup?");
        press(A); idle(2);
        systemRow(ROW_SESSION);
        SEE("Not available right now");
        press(A); idle(2);
        probe([&]() { CHECK(counters.nandWrites == 0 && counters.commits == 0); });
        panel(P_SYSTEM, P_POCKETS);
        placeTicket(m, 5); applyFromPanel(); SEE("Changes applied");
        probe([&]() {
            CHECK_SAME(slurp(sd("session_personal.dat")), g.pEnc); CHECK_SAME(slurp(sd("session_main.dat")), g.mEnc);
            CHECK_SAME(slurp(sd("backup_main.dat")), g.mEnc);
        });
        quitFromPanel();
    });
}

// Audit 3.f, critical case: backup_main.dat created at the right size but never filled.
static void scnBackupMainEmpty(const Save& s0) {
    g_scn = "backup_main_left_empty";
    World w = makeWorld(g_scn, s0);
    launch(w, "launch 1", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5);
        probe([]() { faults.sdWriteFail = "backup_main.dat"; });
        applyFromPanel();
        SEE("The SD backup failed"); SEE("Nothing was written");
        probe([&]() {
            CHECK(counters.nandWrites == 0 && counters.commits == 0);
            CHECK(slurp(sd("backup_main.dat")).size() == M_SZ);                         // right size, all zeroes: the trap
            CHECK_SAME(slurp(NAND_M), s0.mEnc);
            faults.sdWriteFail.clear();
        });
        press(A); idle(2); press(B); idle(2);                  // close the error, leave Review
        SEE("1 change");                                       // the change is still pending
        press(MINUS); press(RIGHT); press(A); idle(2);         // discard, so that only the backup guard is in the way
        SEE("Up to date");
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Not available right now"); NOT_SEE("Restore the SD backup?");
        press(A); idle(2);
        probe([&]() { CHECK(counters.nandWrites == 0); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
    // Next launch: the in-memory "untrusted" flag is gone; only the content check protects.
    launch(w, "launch 2: content check alone", [&]() {
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Not available right now"); NOT_SEE("Restore the SD backup?");
        press(A); idle(2);
        probe([&]() { CHECK(counters.nandWrites == 0 && counters.commits == 0); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    }, [&]() { CHECK_SAME(slurp(NAND_M), s0.mEnc); });
}

// A backup_main.dat damaged outside the 4 EncryptedInt32 fields (bad SD sector,
// interrupted copy on a PC...) must be refused too.
static void scnBackupDamaged(const Save& s0) {
    g_scn = "backup_main_damaged";
    World w = makeWorld(g_scn, s0);
    launch(w, "launch 1: apply", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5); applyFromPanel(); SEE("Changes applied");
        probe([&]() {
            Bytes b = slurp(sd("backup_main.dat"));
            for (size_t i = 0x700000; i < 0x700200; i++) b[i] ^= 0xFF;                  // 512 bytes in villager block data
            writeFile(sd("backup_main.dat"), b);
        });
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Not available right now"); NOT_SEE("Restore the SD backup?");
        press(A); idle(2);
        probe([&]() { CHECK(counters.commits == 1); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
}

// personal.dat written, main.dat fails: no commit, console unchanged, changes kept, retry works.
static void scnNandWriteFails(const Save& s0) {
    g_scn = "nand_write_fails";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5);
        probe([]() { faults.nandWriteFail = "main.dat"; });
        applyFromPanel();
        SEE("Writing the save failed");
        probe([&]() {
            CHECK(counters.nandWrites == 2 && counters.nandWriteFailures == 1 && counters.commits == 0);
            CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc);     // committed state untouched
            CHECK_SAME(slurp(sd("backup_main.dat")), s0.mEnc);
            faults.nandWriteFail.clear();
        });
        press(A); idle(2);                                     // close the error: still on Review
        SEE("1 change");
        press(A); idle(6);                                     // Apply again
        SEE("Changes applied");
        probe([&, mo = m]() {
            Bytes eP = s0.pPlain, eM = s0.mPlain; expectApply(eP, eM, mo);
            CHECK_SAME(nandPlainP(s0), eP); CHECK_SAME(nandPlainM(s0), eM);
            CHECK(counters.commits == 1);
        });
        quitFromPanel();
    }, []() { CHECK(counters.droppedJournals == 0); });
}

// doSave cannot allocate its 10 MB work copy.
static void scnMallocFails(const Save& s0) {
    g_scn = "malloc_fails";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5);
        probe([]() { faults.mallocFailSize = M_SZ; faults.mallocFailCount = 1; });
        applyFromPanel();
        SEE("Not enough memory");
        probe([&]() {
            CHECK(counters.nandWrites == 0 && counters.commits == 0);
            CHECK_SAME(slurp(sd("backup_main.dat")), s0.mEnc); CHECK_SAME(slurp(sd("backup_personal.dat")), s0.pEnc);
        });
        press(A); idle(2); SEE("1 change");
        press(A); idle(6); SEE("Changes applied");
        // The backup check also needs 10 MB: a failure there must refuse, not restore blindly.
        probe([]() { faults.mallocFailSize = M_SZ; faults.mallocFailCount = 1; });
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Not enough memory to check the backup");
        press(A); idle(2);
        probe([&]() { CHECK(counters.commits == 1); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
}

// SD card full: the pre-edit backup cannot be written, then the engine backup fails too.
static void scnSdFull(const Save& s0) {
    g_scn = "sd_card_full";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5);
        probe([]() { setFileSizeLimit(4 * 1024 * 1024); });   // personal (0.5 MB) fits, main (10 MB) does not
        applyFromPanel();
        SEE("backup failed");                                  // pre-edit backup dialog: Cancel / Apply anyway
        probe([&]() { CHECK(counters.nandWrites == 0); CHECK(!fileThere(sd("session_main.dat")) && !fileThere(sd("session_personal.dat"))); });
        press(RIGHT); press(A); idle(6);                       // Apply anyway
        SEE("The SD backup failed");                           // engine backup fails as well: nothing written
        probe([&]() { CHECK(counters.nandWrites == 0 && counters.commits == 0); CHECK_SAME(slurp(NAND_M), s0.mEnc); setFileSizeLimit(-1); });
        press(A); idle(2); SEE("1 change");
        press(A); idle(6);                                     // room again: Apply
        SEE("Changes applied");
        probe([&]() {
            CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc); CHECK_SAME(slurp(sd("backup_main.dat")), s0.mEnc);
            CHECK(counters.commits == 1);
        });
        quitFromPanel();
    });
}

// A rescue must stay possible when the SD card has no room for one more copy: the
// restore asks, then goes on with the backup that is already on the card.
static void scnRestoreWhenSdFull(const Save& s0) {
    g_scn = "restore_when_sd_full";
    World w = makeWorld(g_scn, s0);
    launch(w, "launch 1: apply", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5); applyFromPanel(); SEE("Changes applied"); quitFromPanel();
    });
    remove((w.root + "/sdmc:/switch/acnh_editor/session_main.dat").c_str());       // no pre-edit pair for this state
    remove((w.root + "/sdmc:/switch/acnh_editor/session_personal.dat").c_str());
    launch(w, "launch 2: card full, restore", [&]() {
        probe([]() { setFileSizeLimit(4 * 1024 * 1024); });
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        confirmHold();
        SEE("Pre-edit backup failed"); SEE("not kept anywhere");
        probe([&]() { CHECK(counters.nandWrites == 0); setFileSizeLimit(-1); });   // (the fake console shares the limit)
        press(RIGHT); press(A); idle(6);                       // Restore anyway
        SEE("Save restored");
        probe([&]() { CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc); CHECK(counters.commits == 1); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
}

// Copying the pre-edit backup over the regular backup fails half-way (backup_main.dat
// read-only): nothing is restored, and the now inconsistent pair is not offered either.
static void scnSessionCopyFails(const Save& s0) {
    g_scn = "pre_edit_copy_fails";
    if (geteuid() == 0) { printf("    skipped: running as root, a read-only file cannot make the copy fail\n"); return; }
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        static Bytes enc2P, enc2M;
        placeTicket(m, 5); applyFromPanel(); SEE("Changes applied");
        press(Y); idle(2); applyFromPanel(); SEE("Changes applied");                    // backup = state 1, pre-edit = state 0
        probe([&]() { enc2P = slurp(NAND_P); enc2M = slurp(NAND_M); chmod(sd("backup_main.dat").c_str(), 0444); });
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_SESSION);
        confirmHold();
        SEE("couldn't be copied over the regular backup");
        probe([&]() { CHECK(counters.commits == 2); chmod(sd("backup_main.dat").c_str(), 0644); });
        press(A); idle(2);
        // backup_personal = state 0, backup_main = state 1: both valid on their own. Must be blocked.
        systemRow(ROW_RESTORE);
        SEE("Not available right now"); NOT_SEE("Restore the SD backup?");
        press(A); idle(2);
        probe([&]() { CHECK(counters.commits == 2); CHECK_SAME(slurp(NAND_P), enc2P); CHECK_SAME(slurp(NAND_M), enc2M); });
        // The pre-edit backup itself is intact: retrying works.
        systemRow(ROW_SESSION);
        confirmHold();
        SEE("Save restored");
        probe([&]() { CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc); });
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });
}

// Save Doctor: a clean run rewrites the same content; a failed run locks the session.
static void scnDoctor(const Save& s0) {
    g_scn = "save_doctor";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_DOCTOR);
        SEE("Run the Save Doctor?");
        confirmHold();
        SEE("Repair finished");
        probe([&]() {
            CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc);     // no pending change: identical bytes
            CHECK_SAME(slurp(sd("damaged_main.dat")), s0.mEnc); CHECK_SAME(slurp(sd("session_main.dat")), s0.mEnc);
            CHECK(counters.commits == 1);
            faults.nandWriteFail = "personal.dat";
        });
        press(A); idle(2);
        systemRow(ROW_DOCTOR);
        confirmHold();
        SEE("Repair failed");
        probe([&]() { CHECK(counters.commits == 1); faults.nandWriteFail.clear(); });
        press(A); idle(2);
        // Tainted session: no restore, no Doctor, no edit, no Apply.
        systemRow(ROW_RESTORE); SEE("Not available right now"); press(A); idle(2);
        systemRow(ROW_DOCTOR); SEE("Not available right now"); press(A); idle(2);
        panel(P_SYSTEM, P_POCKETS);
        enterIdFromOccupied("16DB", false);
        press(Y); idle(2);
        SEE("A repair failed earlier"); press(A); idle(2);
        SEE("Up to date");
        probe([&]() { CHECK(counters.commits == 1 && counters.nandWrites == 3); });
        quitFromPanel();
    }, [&]() { CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc); });
}

// Unexpected file size (another game version): read-only, no mutation, no write of any kind.
static void scnReadOnly() {
    g_scn = "read_only_save";
    Save s = makeSave(P_SZ + 0x10, M_SZ);
    World w = makeWorld(g_scn, s);
    launch(w, "session", [&]() {
        idle(2); SEE("Unsupported save version: read-only");
        enterIdFromOccupied("16DB", false);
        press(Y); idle(2); SEE("isn't supported"); press(A); idle(2);                  // place refused
        press(A); press(UP); press(A); idle(2); SEE("isn't supported"); press(A); idle(2);   // Clear slot refused
        panel(P_POCKETS, P_MONEY);
        press(Y); idle(2); SEE("isn't supported"); press(A); idle(2);
        press(A); press(DOWN, 2); press(A); idle(2); SEE("isn't supported"); press(A); idle(2);   // Set to max refused
        panel(P_MONEY, P_SYSTEM);
        for (int row = 0; row < 3; row++) { systemRow(row); SEE("unsupported save version"); press(A); idle(2); }
        panel(P_SYSTEM, P_POCKETS);
        SEE("Up to date");
        quitFromPanel();
    }, [&]() {
        CHECK(counters.nandWrites == 0 && counters.commits == 0 && counters.sdCreates == 0);
        CHECK(!fileThere(sd("backup_main.dat")) && !fileThere(sd("session_main.dat")) && !fileThere(sd("damaged_main.dat")));
        CHECK_SAME(slurp(NAND_P), s.pEnc); CHECK_SAME(slurp(NAND_M), s.mEnc);
    });
}

// Fatal screens and launch guards: nothing is read or written, A held at launch does not skip the message.
static void scnGuards(const Save& s0) {
    g_scn = "launch_guards";
    World w = makeWorld(g_scn, s0);
    g_rawStart = true;
    launch(w, "game still running, A held since the Homebrew Menu", [&]() {
        faults.mountFail = true;
        hold(A, 5);
        SEE("The save can't be opened"); SEE("Mount failed");
        idle(2);
        script.push_back({ A, nullptr });                      // a real press, after the release: quits
    }, []() { CHECK(counters.frames >= 9); CHECK(counters.mounts == 0 && counters.nandWrites == 0); });
    g_rawStart = false;
    World w2 = makeWorld("launch_guards_nofont", s0, true, false);
    launch(w2, "font.ttf missing", [&]() { idle(3); press(A); idle(50); },
           []() { CHECK(counters.mounts == 0 && counters.frames < 20); script.clear(); });
    World w3 = makeWorld("launch_guards_badfont", s0);
    launch(w3, "font.ttf unreadable", [&]() { faults.fontOpenFail = true; idle(3); press(A); idle(50); },
           []() { CHECK(counters.mounts == 0 && counters.frames < 20); script.clear(); });
    World w4 = makeWorld("launch_guards_applet", s0);
    launch(w4, "applet mode", [&]() { faults.appletMode = true; idle(3); press(A); idle(50); },
           []() { CHECK(counters.mounts == 0 && counters.frames < 20); script.clear(); });
    World w5 = makeWorld("launch_guards_noitems", s0, false, true);
    launch(w5, "items.txt missing", [&]() {
        idle(2); SEE("items.txt is missing");
        SEE("ID 16DB");                                        // names fall back to IDs
        quitFromPanel();
    }, []() { CHECK(counters.nandWrites == 0); });
}

// Review screen: after reverting one row, the focus must not land on "Apply".
static void scnReviewRevertFocus(const Save& s0) {
    g_scn = "review_revert_focus";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5); press(Y); idle(2);                  // 2 pending changes
        press(PLUS); idle(2);
        press(UP); press(A); press(A); idle(2);                // last row > Revert this change
        SEE("1 change");
        press(A); idle(8);                                     // a second A by reflex must not apply
        probe([]() { CHECK(counters.nandWrites == 0 && counters.commits == 0 && counters.sdCreates == 0); });
        NOT_SEE("Changes applied");
        press(B); idle(2); press(B); idle(2);
        press(MINUS); press(RIGHT); press(A); idle(2);
        SEE("Up to date");
        quitFromPanel();
    });
}

// Known engine limitation, kept visible: fsFsCommit's result is ignored, so a failed
// commit is reported as a success. Documented in docs/review/BUG_HUNT.md (engine, not fixed).
static void scnCommitFailsKnown(const Save& s0) {
    g_scn = "commit_fails (known engine limitation)";
    World w = makeWorld("commit_fails", s0);
    launch(w, "session", [&]() {
        static Model m; m = s0.model;
        placeTicket(m, 5);
        probe([]() { faults.commitFail = true; });
        applyFromPanel();
        probe([&]() {
            bool lied = screenHas("Changes applied") && firstDiff(slurp(NAND_M), s0.mEnc) == (size_t)-1;
            printf("    note: commit failure %s\n", lied ? "is NOT detected: the UI says \"Changes applied\" but the console save is unchanged (engine)" : "was detected");
            faults.commitFail = false;
        });
        quitFromPanel();
    }, []() {});
}

// Not part of the default run (slow on purpose): how long the engine's region scan takes
// at launch when a hash is stale, i.e. after an edit and before the game saved again.
//   sh tests/host/engine_bench.sh slow_scan      (PP_NOSAN=1 for realistic timings)
static void scnSlowScan(const Save& s0) {
    struct { const char* what; int file; uint32_t off; } cases[] = {
        { "all hashes valid (fresh game save)", 0, 0 },
        { "pockets / wallet / bank edited (personal region 2 stale)", 1, 0x37be0 },
        { "Nook Miles edited (personal region 1 stale)", 1, 0x110 },
        { "house loan edited (main region 2 stale)", 2, 0x1e34b0 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        g_scn = "slow_scan";
        Save s = s0;
        if (cases[i].file == 1) s.pPlain[cases[i].off] ^= 1;
        if (cases[i].file == 2) s.mPlain[cases[i].off] ^= 1;
        encrypt(s);
        World w = makeWorld("slow_scan", s);
        struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
        launch(w, cases[i].what, [&]() { idle(3); });
        clock_gettime(CLOCK_MONOTONIC, &t1);
        printf("    %-58s launch took %.1f s\n", cases[i].what, (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9);
        fflush(stdout);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: engine_bench <repo-root> <tmp-dir> [scenario-substring]\n"); return 2; }
    g_repo = argv[1]; g_tmp = argv[2];
    const char* only = argc > 3 ? argv[3] : "";
    remove((g_tmp + "/passes.txt").c_str());
    printf("building the synthetic 3.0.x save...\n");
    Save s0 = makeSave();
    if (validHashes(s0.pPlain, P_REGS, 2) != 2 || validHashes(s0.mPlain, M_REGS, 19) != 19) { printf("bad synthetic save\n"); return 2; }

    struct { const char* name; void (*fn)(const Save&); } all[] = {
        { "apply_restore_cycle", scnApplyRestoreCycle },
        { "relaunch_keeps_pre_edit_backup", scnRelaunchKeepsPreEditBackup },
        { "game_saved_in_between", scnGameSavedInBetween },
        { "backup_main_left_empty", scnBackupMainEmpty },
        { "backup_main_damaged", scnBackupDamaged },
        { "nand_write_fails", scnNandWriteFails },
        { "malloc_fails", scnMallocFails },
        { "sd_card_full", scnSdFull },
        { "restore_when_sd_full", scnRestoreWhenSdFull },
        { "pre_edit_copy_fails", scnSessionCopyFails },
        { "save_doctor", scnDoctor },
        { "read_only_save", [](const Save&) { scnReadOnly(); } },
        { "launch_guards", scnGuards },
        { "review_revert_focus", scnReviewRevertFocus },
        { "commit_fails", scnCommitFailsKnown },
    };
    if (!strcmp(only, "slow_scan")) { scnSlowScan(s0); return 0; }
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        if (*only && !strstr(all[i].name, only)) continue;
        int before = g_fail;
        printf("== %s\n", all[i].name);
        all[i].fn(s0);
        printf("   %s\n", g_fail == before ? "ok" : "FAILED");
    }
    int passes = 0, n = 0;
    FILE* f = fopen((g_tmp + "/passes.txt").c_str(), "rb");
    if (f) { while (fscanf(f, "%d", &n) == 1) passes += n; fclose(f); }
    printf("\n%d passed, %d failed\n", passes, g_fail);
    return g_fail ? 1 : 0;
}
