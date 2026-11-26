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
#include "pp_update.h"

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
#include <zlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

int pp_app_main(int argc, char** argv);                       // source/main.cpp, renamed in main_under_test.cpp
void CryptACNH(const uint8_t* header, uint8_t* data, size_t dataLen);   // engine (symmetric AES-CTR)

using namespace fake;
typedef std::vector<uint8_t> Bytes;

// ------------------------------ save layout 3.0.x --------------------------
static const size_t   P_SZ = 0x74A40, M_SZ = 0x9B0E90, HDR_SZ = 0x300;
static const uint32_t WALLET = 0x37D60, BANK = 0x6527C, MILES = 0x12718, LOAN = 0x33CBF0, POCKETS = 0x37CA8;
static const uint32_t BAG = 0x37BF0, BAG_COUNT = 0x37C90;   // slots 21..40 and how many are unlocked
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
static std::string g_arg0 = "app";                             // argv[0], the address of the .nro
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
struct Model { uint8_t pockets[pp::POCKETS_BYTES]; uint8_t bag[pp::POCKETS_BYTES]; uint32_t vals[4]; };
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
    rec(m.pockets, 2, ID_NMT, 0, 0, 4, 0);                   // raw 4 = 5 tickets
    rec(m.pockets, 3, ID_WOOD, 0, 0x41, 29, 0);              // wrapped stack
    rec(m.pockets, 4, ID_APPLE, 0, 0, 9, 0);
    rec(m.pockets, 19, pp::ID_EMPTY, 0, 0, 5, 0);            // "dirty" empty record: must be left alone
    memcpy(&s.pPlain[POCKETS], m.pockets, sizeof m.pockets);
    wr32(&s.pPlain[POCKETS + 160], 20);                       // slot count that follows the 20 records
    for (int i = 1; i <= 20; i++) rec(m.bag, i, pp::ID_EMPTY, 0, 0, 0, 0);
    memcpy(&s.pPlain[BAG], m.bag, sizeof m.bag);
    wr32(&s.pPlain[BAG_COUNT], 0);                            // no pocket upgrade: 20 slots
    m.vals[0] = 12345; m.vals[1] = 4800000; m.vals[2] = 230500; m.vals[3] = 98000;
    encStore(&s.pPlain[WALLET], m.vals[0]); encStore(&s.pPlain[BANK], m.vals[1]);
    encStore(&s.pPlain[MILES], m.vals[2]);  encStore(&s.mPlain[LOAN], m.vals[3]);
    for (int i = 0; i < 8; i++) wr32(&s.mPlain[HEAL0 + i * HEAL_STEP], i == 2 ? 0xDEADBEEFu : 0);   // one "poisoned" field
    if (mSz >= M_SZ) {                                        // Stalk Market: a large-spike week, saved Wed 11 Nov 2026
        static const uint32_t prices[14] = { 0, 0, 85, 80, 76, 72, 110, 160, 532, 201, 140, 90, 60, 50 };
        wr32(&s.mPlain[pp::TURNIP_OFF_30], 98);
        for (int i = 0; i < 14; i++) wr32(&s.mPlain[pp::TURNIP_OFF_30 + 4 + i * 4], prices[i]);
        wr32(&s.mPlain[pp::TURNIP_OFF_30 + 0x3C], 1);
        uint8_t* d = &s.mPlain[pp::SAVE_DATE_OFF_30];
        d[0] = 2026 & 0xFF; d[1] = 2026 >> 8; d[2] = 11; d[3] = 11; d[4] = 9; d[5] = 41; d[6] = 3;
    }
    if (pSz >= P_SZ) fixHashes(s.pPlain, P_REGS, 2);          // valid hashes keep the engine's region scan fast
    if (mSz >= M_SZ) fixHashes(s.mPlain, M_REGS, 19);
    encrypt(s);
    return s;
}

// The state Apply must produce from `before` and the model: nothing else may change.
static void expectApply(Bytes& p, Bytes& m, const Model& mo) {
    memcpy(&p[POCKETS], mo.pockets, sizeof mo.pockets);
    memcpy(&p[BAG], mo.bag, sizeof mo.bag);
    encStore(&p[WALLET], mo.vals[0]); encStore(&p[BANK], mo.vals[1]); encStore(&p[MILES], mo.vals[2]);
    encStore(&m[LOAN], mo.vals[3]);
    for (int i = 0; i < 8; i++) wr32(&m[HEAL0 + i * HEAL_STEP], 0);
    // The app must leave every region hash consistent with the data it writes
    // (independent Murmur3 above), or the game refuses the save.
    if (p.size() >= P_SZ) fixHashes(p, P_REGS, 2);
    if (m.size() >= M_SZ) fixHashes(m, M_REGS, 19);
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
static std::string sd(const char* name) { return std::string("sdmc:/switch/acnh_power_pockets/") + name; }

struct World { std::string root; };
static void putSave(const World& w, const Save& s) {
    writeFile(w.root + "/nand/Villager0/personal.dat", s.pEnc);
    writeFile(w.root + "/nand/Villager0/personalHeader.dat", s.pHdr);
    writeFile(w.root + "/nand/main.dat", s.mEnc);
    writeFile(w.root + "/nand/mainHeader.dat", s.mHdr);
}
static World makeWorld(const char* name, const Save& s, bool withItems = true, bool withFont = true) {
    World w; w.root = g_tmp + "/" + name;
    std::string cmd = "rm -rf '" + w.root + "' && mkdir -p '" + w.root + "/nand/Villager0' '" + w.root + "/sdmc:/switch/acnh_power_pockets'";
    if (system(cmd.c_str()) != 0) { printf("cannot create %s\n", w.root.c_str()); exit(2); }
    putSave(w, s);
    std::string dst = w.root + "/sdmc:/switch/acnh_power_pockets/";
    if (withFont) writeFile(dst + "font.ttf", slurp(g_repo + "/assets/font.ttf"));
    if (withItems) writeFile(dst + "items.txt", slurp(g_repo + "/assets/items.txt"));
    writeFile(dst + "nonstackable.txt", slurp(g_repo + "/assets/nonstackable.txt"));
    writeFile(dst + "item_stacks.txt", slurp(g_repo + "/assets/item_stacks.txt"));
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
        std::string arg0 = g_arg0; char* argv[] = { &arg0[0], nullptr };
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
enum { P_POCKETS, P_FAVS, P_CATALOG, P_MONEY, P_TURNIPS, P_LOADOUTS, P_SYSTEM };
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
        press(RIGHT, 8); press(A); idle(2);                                             // 5 -> 10: capped, tickets stack to 10
        rec(m.pockets, 2, ID_NMT, 0, 0, pp::qtyToRaw(10), 0);
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
            CHECK(validHashes(p1, P_REGS, 2) == 2 && validHashes(m1, M_REGS, 19) == 19);   // every hash matches what was written
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
    remove((w.root + "/sdmc:/switch/acnh_power_pockets/session_main.dat").c_str());       // no pre-edit pair for this state
    remove((w.root + "/sdmc:/switch/acnh_power_pockets/session_personal.dat").c_str());
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

// A save with a stale region hash, as written by a build that didn't rehash (the state a
// real console ended up in: pockets edited, hash of personal region 2 left as it was, the
// game then refuses the save). The app must say so, refuse to restore a backup in that
// state, and both the Save Doctor and Apply must leave every hash valid.
static Save staleSave(const Save& s0) {
    Save s = s0;
    rec(s.model.pockets, 5, ID_NMT, 0, 0, pp::qtyToRaw(1), 0);
    memcpy(&s.pPlain[POCKETS], s.model.pockets, sizeof s.model.pockets);     // data changed, hash not updated
    encrypt(s);
    return s;
}
static void scnStaleHash(const Save& s0) {
    g_scn = "stale_hash_repaired";
    const Save s = staleSave(s0);
    CHECK(validHashes(s.pPlain, P_REGS, 2) == 1 && validHashes(s.mPlain, M_REGS, 19) == 19);

    World w = makeWorld(g_scn, s);
    launch(w, "launch 1: Save Doctor", [&]() {
        idle(2);
        SEE("Stale integrity hash");
        probe([&]() { writeFile(sd("backup_personal.dat"), s.pEnc); writeFile(sd("backup_main.dat"), s.mEnc); });
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);                                  // a backup the game would refuse is not offered
        SEE("stale integrity hash"); NOT_SEE("Restore the SD backup?");
        press(A); idle(2);
        systemRow(ROW_DOCTOR);
        SEE("Run the Save Doctor?");
        confirmHold();
        SEE("Repair finished");
        probe([&]() {
            Bytes eP = s.pPlain, eM = s.mPlain;                  // same data, hashes made consistent, nothing else
            fixHashes(eP, P_REGS, 2); fixHashes(eM, M_REGS, 19);
            Bytes p1 = nandPlainP(s), m1 = nandPlainM(s);
            CHECK_SAME(p1, eP); CHECK_SAME(m1, eM);
            CHECK(validHashes(p1, P_REGS, 2) == 2 && validHashes(m1, M_REGS, 19) == 19);
            CHECK(counters.commits == 1);
        });
        press(A); idle(2);
        panel(P_SYSTEM, P_POCKETS);
        NOT_SEE("Stale integrity hash");
        quitFromPanel();
    });

    World w2 = makeWorld("stale_hash_apply", s);
    launch(w2, "other console: Apply", [&]() {
        static Model m; m = s.model;
        idle(2);
        SEE("Stale integrity hash");
        placeTicket(m, 6);
        applyFromPanel();
        SEE("Changes applied");
        NOT_SEE("Stale integrity hash");
        probe([&, mo = m]() {
            Bytes eP = s.pPlain, eM = s.mPlain; expectApply(eP, eM, mo);
            Bytes p1 = nandPlainP(s), m1 = nandPlainM(s);
            CHECK_SAME(p1, eP); CHECK_SAME(m1, eM);
            CHECK(validHashes(p1, P_REGS, 2) == 2 && validHashes(m1, M_REGS, 19) == 19);
        });
        quitFromPanel();
    });
}

// Optional: the same checks on a REAL save dump (never part of the repository).
//   PP_REAL_SAVE=<dir with Villager0/personal.dat, Villager0/personalHeader.dat, main.dat,
//                 mainHeader.dat, e.g. an unzipped JKSV backup>  sh tests/host/engine_bench.sh real_save
// The Save Doctor must leave the data untouched and every region hash valid; when slot 1 is
// occupied and a slot is free, an Apply must change exactly what is expected.
static void scnRealSave(const Save&) {
    g_scn = "real_save";
    const char* dir = getenv("PP_REAL_SAVE");
    if (!dir) { printf("    skipped (PP_REAL_SAVE not set)\n"); return; }
    Save s; std::string d = dir;
    readFile(d + "/Villager0/personalHeader.dat", s.pHdr); readFile(d + "/mainHeader.dat", s.mHdr);
    readFile(d + "/Villager0/personal.dat", s.pEnc); readFile(d + "/main.dat", s.mEnc);
    CHECK(s.pHdr.size() == HDR_SZ && s.mHdr.size() == HDR_SZ && s.pEnc.size() == P_SZ && s.mEnc.size() == M_SZ);
    if (g_fail) return;
    s.pPlain = s.pEnc; s.mPlain = s.mEnc;
    CryptACNH(s.pHdr.data(), s.pPlain.data(), s.pPlain.size()); CryptACNH(s.mHdr.data(), s.mPlain.data(), s.mPlain.size());
    memcpy(s.model.pockets, &s.pPlain[POCKETS], sizeof s.model.pockets);
    s.model.vals[0] = encDecode(&s.pPlain[WALLET]); s.model.vals[1] = encDecode(&s.pPlain[BANK]);
    s.model.vals[2] = encDecode(&s.pPlain[MILES]);  s.model.vals[3] = encDecode(&s.mPlain[LOAN]);
    CHECK(encValid(&s.pPlain[WALLET]) && encValid(&s.pPlain[BANK]) && encValid(&s.pPlain[MILES]) && encValid(&s.mPlain[LOAN]));
    const int hp = validHashes(s.pPlain, P_REGS, 2), hm = validHashes(s.mPlain, M_REGS, 19);
    printf("    as dumped: personal %d/2 hashes valid, main %d/19\n", hp, hm);
    const bool stale = hp != 2 || hm != 19;
    {                                                                        // pocket upgrades, as the app sees them
        const uint32_t bc = rd32(&s.pPlain[BAG_COUNT]);
        const int slots = pp::slotsFromBagCount(bc);
        pp::Pockets pk(&s.pPlain[POCKETS], &s.pPlain[BAG], slots);
        printf("    pockets: bag counter %u -> %d slots, %d used; pocket block counter %u\n", (unsigned)bc, pk.count, pp::countItems(pk),
               (unsigned)rd32(&s.pPlain[POCKETS + 160]));
        for (int i = 21; i <= pk.count; i++) { pp::SlotRec r = pp::readSlot(pk, i); if (r.id != pp::ID_EMPTY) printf("      slot %d: 0x%04X raw %u\n", i, r.id, r.raw); }
        CHECK(pp::pocketsPlausible(&s.pPlain[BAG], 20));
    }
    pp::TurnipWeek tw;
    if (pp::readTurnipWeek(s.mPlain.data(), s.mPlain.size(), tw)) {          // compare with the game / Turnips
        printf("    turnips: Daisy Mae %u, pattern %u, saved %04d-%02d-%02d %02d:%02d%s\n      ", (unsigned)tw.buy, (unsigned)tw.pattern,
               tw.saved.year, tw.saved.month, tw.saved.day, tw.saved.hour, tw.saved.minute, tw.unusual ? " (unusual values)" : "");
        for (int i = 2; i < 14; i++) printf("%s %u%s", pp::turnipSlotName(i), (unsigned)tw.price[i], i < 13 ? " \xC2\xB7 " : "\n");
    }

    World w = makeWorld(g_scn, s);
    launch(w, "Save Doctor", [&]() {
        idle(2);
        if (stale) SEE("Stale integrity hash"); else NOT_SEE("Stale integrity hash");
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_DOCTOR);
        SEE("Run the Save Doctor?");
        confirmHold();
        SEE("Repair finished");
        probe([&]() {
            Bytes eP = s.pPlain, eM = s.mPlain;
            fixHashes(eP, P_REGS, 2); fixHashes(eM, M_REGS, 19);
            Bytes p1 = nandPlainP(s), m1 = nandPlainM(s);
            CHECK_SAME(p1, eP); CHECK_SAME(m1, eM);              // data untouched, hashes consistent
            CHECK(validHashes(p1, P_REGS, 2) == 2 && validHashes(m1, M_REGS, 19) == 19);
            if (!stale) { CHECK_SAME(slurp(NAND_P), s.pEnc); CHECK_SAME(slurp(NAND_M), s.mEnc); }   // healthy save: no-op
            printf("    after the Save Doctor: personal 2/2, main 19/19\n");
        });
        press(A); idle(2);
        panel(P_SYSTEM, P_POCKETS); quitFromPanel();
    });

    const int freeSlot = pp::nextEmptySlot(s.model.pockets, 1);
    if (pp::slotEmpty(s.model.pockets, 1) || freeSlot < 0) { printf("    Apply part skipped (needs slot 1 occupied and a free slot)\n"); return; }
    World w2 = makeWorld("real_save_apply", s);
    launch(w2, "Apply one ticket", [&]() {
        static Model m; m = s.model;
        idle(2);
        placeTicket(m, freeSlot);
        applyFromPanel();
        SEE("Changes applied");
        probe([&, mo = m]() {
            Bytes eP = s.pPlain, eM = s.mPlain; expectApply(eP, eM, mo);
            Bytes p1 = nandPlainP(s), m1 = nandPlainM(s);
            CHECK_SAME(p1, eP); CHECK_SAME(m1, eM);              // one pocket record + consistent hashes, nothing else
            CHECK(validHashes(p1, P_REGS, 2) == 2 && validHashes(m1, M_REGS, 19) == 19);
            printf("    after Apply (ticket in slot %d): personal 2/2, main 19/19, nothing else changed\n", freeSlot);
        });
        quitFromPanel();
    });
}

// Turnips panel: the real main.cpp hook reads the Stalk Market from the decrypted main.dat
// (fake clock: Wednesday 11 Nov 2026, 14:30). Read-only: no action, no write of any kind.
static void scnTurnips(const Save& s0) {
    g_scn = "turnips_read_only";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        idle(2);
        panel(P_POCKETS, P_TURNIPS);
        SEE("Daisy Mae (Sunday)"); SEE("98"); SEE("Large spike"); SEE("532");
        SEE("Week of Sun 8 Nov 2026"); SEE("now Wed PM: 160");
        SEE("No turnips in your pockets");
        NOT_SEE("These prices are from the week of");
        press(A); idle(2); press(Y); idle(2); press(HidNpadButton_X); idle(2);          // nothing to do here
        SEE("Up to date"); SEE("Daisy Mae (Sunday)");
        probe([&]() { CHECK(counters.nandWrites == 0 && counters.commits == 0 && counters.sdCreates == 0); });
        panel(P_TURNIPS, P_POCKETS); quitFromPanel();
    });
}

// Pocket upgrades: the bag counter says 10 or 20 more slots. Items go into the bag block
// (slots 21..40), locked slots are never touched, the counter is never written, and the
// SD backup of a save with bag edits is still accepted for restoring.
static Save bagSave(const Save& s0, uint32_t bagCount) {
    Save s = s0;
    for (int i = 1; i <= 20; i++) rec(s.model.bag, i, pp::ID_EMPTY, 0, 0, 0, 0);
    rec(s.model.bag, 3, ID_WOOD, 0, 0, 9, 0);                // slot 23: 10 wood
    rec(s.model.bag, 15, ID_APPLE, 0, 0, 4, 0);              // slot 35: locked with 30 slots
    memcpy(&s.pPlain[BAG], s.model.bag, sizeof s.model.bag);
    wr32(&s.pPlain[BAG_COUNT], bagCount);
    fixHashes(s.pPlain, P_REGS, 2);
    encrypt(s);
    return s;
}
static void scnBag(const Save& s0) {
    g_scn = "bag_40_slots";
    static Save s; s = bagSave(s0, 20);
    World w = makeWorld(g_scn, s);
    launch(w, "session", [&]() {
        static Model m; m = s.model;
        idle(2); SEE("40 slots");
        enterIdFromOccupied("16DB", false);                  // cursor on slot 1, take a ticket
        press(RIGHT, 2); idle(1); press(Y); idle(2);          // column 3, row 1 = slot 21
        rec(m.bag, 1, ID_NMT, 0, 0, pp::qtyToRaw(1), 0);
        SEE("in slot 21");
        press(RIGHT); press(DOWN, 9); idle(1); press(Y); idle(2);   // column 4, row 10 = slot 40
        rec(m.bag, 20, ID_NMT, 0, 0, pp::qtyToRaw(1), 0);
        SEE("in slot 40");
        // After a place the cursor goes to the next empty slot: slot 5 (slots 1..4 are full).
        press(RIGHT, 2); press(UP, 2); idle(1); press(A); press(UP); press(A); idle(2);   // slot 23 (wood) > Clear slot
        rec(m.bag, 3, pp::ID_EMPTY, 0, 0, 0, 0);
        SEE("Slot 23 cleared");
        applyFromPanel();
        SEE("Changes applied");
        probe([&, mo = m]() {
            Bytes eP = s.pPlain, eM = s.mPlain; expectApply(eP, eM, mo);
            CHECK_SAME(nandPlainP(s), eP); CHECK_SAME(nandPlainM(s), eM);
            { Bytes p1 = nandPlainP(s); CHECK(rd32(&p1[BAG_COUNT]) == 20); }   // counter untouched
            CHECK(validHashes(nandPlainP(s), P_REGS, 2) == 2);
        });
        panel(P_POCKETS, P_SYSTEM);
        systemRow(ROW_RESTORE);
        SEE("Restore the SD backup?");                         // a backup with bag edits is accepted
        confirmHold();
        SEE("Save restored");
        probe([&]() { CHECK_SAME(slurp(NAND_P), s.pEnc); CHECK_SAME(slurp(NAND_M), s.mEnc); });
        panel(P_SYSTEM, P_POCKETS);
        quitFromPanel();
    });
    // 30 slots: slots 31..40 are not shown and never written, even when they hold items.
    static Save s30; s30 = bagSave(s0, 10);
    World w30 = makeWorld("bag_30_slots", s30);
    launch(w30, "session", [&]() {
        static Model m; m = s30.model;
        idle(2); SEE("30 slots");                             // slot 35 (an apple) is locked: not shown
        enterIdFromOccupied("16DB", false);
        press(LEFT); press(RIGHT, 4); idle(1);                // only 3 columns: the cursor stops on slot 21
        press(Y); idle(2); rec(m.bag, 1, ID_NMT, 0, 0, pp::qtyToRaw(1), 0);
        SEE("in slot 21");
        applyFromPanel();
        SEE("Changes applied");
        probe([&, mo = m]() {
            Bytes eP = s30.pPlain, eM = s30.mPlain; expectApply(eP, eM, mo);
            CHECK_SAME(nandPlainP(s30), eP);
            { Bytes p1 = nandPlainP(s30); CHECK((p1[BAG + 14 * 8] | (p1[BAG + 14 * 8 + 1] << 8)) == ID_APPLE); }   // locked slot 35 kept as it was
        });
        quitFromPanel();
    });
}

// ---- updates ----
static Bytes zipOf(const std::vector<std::pair<std::string, Bytes>>& files) {      // stored entries
    Bytes o, cd;
    auto p16 = [](Bytes& b, unsigned v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); };
    auto p32 = [&](Bytes& b, uint32_t v) { p16(b, v & 0xFFFF); p16(b, v >> 16); };
    for (const auto& f : files) {
        uint32_t crc = (uint32_t)crc32(0, f.second.data(), (uInt)f.second.size()), off = (uint32_t)o.size(), n = (uint32_t)f.second.size();
        p32(o, 0x04034b50); p16(o, 20); p16(o, 0); p16(o, 0); p32(o, 0); p32(o, crc); p32(o, n); p32(o, n); p16(o, (unsigned)f.first.size()); p16(o, 0);
        o.insert(o.end(), f.first.begin(), f.first.end()); o.insert(o.end(), f.second.begin(), f.second.end());
        p32(cd, 0x02014b50); p16(cd, 20); p16(cd, 20); p16(cd, 0); p16(cd, 0); p32(cd, 0); p32(cd, crc); p32(cd, n); p32(cd, n); p16(cd, (unsigned)f.first.size());
        p16(cd, 0); p16(cd, 0); p16(cd, 0); p16(cd, 0); p32(cd, 0); p32(cd, off); cd.insert(cd.end(), f.first.begin(), f.first.end());
    }
    uint32_t at = (uint32_t)o.size();
    o.insert(o.end(), cd.begin(), cd.end());
    p32(o, 0x06054b50); p16(o, 0); p16(o, 0); p16(o, (unsigned)files.size()); p16(o, (unsigned)files.size()); p32(o, (uint32_t)cd.size()); p32(o, at); p16(o, 0);
    return o;
}
static Bytes nroOf(const char* tag) { Bytes b(0x10, 1); const char* m = "NRO0"; b.insert(b.end(), m, m + 4); b.insert(b.end(), tag, tag + strlen(tag)); b.resize(70000, 7); return b; }
static std::string hexOf(const Bytes& b) { pp::Sha256 s; s.update(b.data(), b.size()); return s.hex(); }
static void putRelease(const char* version, const Bytes& zip, const std::string& digest) {
    std::string tag = std::string("v") + version, name = "ACNH-Power-Pockets-" + tag + ".zip", url = "https://github.com/chatelp/ACNH-Power-Pockets/releases/download/" + tag + "/" + name;
    net.pages[pp::UPDATE_API_URL] = "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false,\"body\":\"## What's new\\n- A brand new thing.\",\"assets\":[{\"name\":\"" + name +
        "\",\"size\":" + std::to_string(zip.size()) + ",\"digest\":\"sha256:" + digest + "\",\"browser_download_url\":\"" + url + "\"}]}";
    net.pages[url] = std::string(zip.begin(), zip.end());
}
static const char* NRO = "sdmc:/switch/acnh_power_pockets/acnh_power_pockets.nro";
static const char* NRO_PREV = "sdmc:/switch/acnh_power_pockets/acnh_power_pockets_previous.nro";
enum { ROW_ABOUT = 4 };
static void openCheck() { panel(P_POCKETS, P_SYSTEM); systemRow(ROW_ABOUT); SEE("github.com/chatelp/ACNH-Power-Pockets"); SEE("cbreezy210"); press(LEFT); press(A); idle(2); SEE("Connect to GitHub?"); }
static void connect() { press(RIGHT); press(A); idle(3); }

// The whole update: check, download, install, restart, confirmation, going back; and what must not happen.
static void scnUpdate(const Save& s0) {
    g_scn = "update";
    const Bytes oldNro = nroOf("1.4.0"), newNro = nroOf("1.4.1");
    const std::string P = "switch/acnh_power_pockets/";
    World w = makeWorld(g_scn, s0);
    const std::string dst = w.root + "/sdmc:/switch/acnh_power_pockets/";
    writeFile(dst + "acnh_power_pockets.nro", oldNro);
    const Bytes fav = { '+', '1', '6', 'D', 'B', '\n' }, load(160, 0x11), font = slurp(g_repo + "/assets/font.ttf"), items = slurp(g_repo + "/assets/items.txt"), rules = slurp(g_repo + "/assets/nonstackable.txt");
    writeFile(dst + "user_favorites.txt", fav); writeFile(dst + "l_0.dat", load);
    const Bytes extra = { 'n', 'e', 'w' };
    const Bytes zip = zipOf({ { P + "acnh_power_pockets.nro", newNro }, { P + "font.ttf", font }, { P + "items.txt", items }, { P + "nonstackable.txt", extra }, { P + "licenses/x.txt", extra }, { P + "extra.txt", extra } });
    auto untouched = [&]() {
        CHECK(counters.nandWrites == 0 && counters.commits == 0);
        CHECK_SAME(slurp(NAND_P), s0.pEnc); CHECK_SAME(slurp(NAND_M), s0.mEnc);
        CHECK_SAME(slurp(sd("user_favorites.txt")), fav); CHECK_SAME(slurp(sd("l_0.dat")), load); CHECK_SAME(slurp(sd("nonstackable.txt")), rules);
        CHECK(!net.unsafe && net.socketInits == net.socketExits);
    };
    auto unchanged = [&]() { untouched(); CHECK_SAME(slurp(sd("acnh_power_pockets.nro")), oldNro); CHECK(!fileThere(sd("extra.txt")) && !fileThere(sd("update/new")) && net.nextLoad.empty()); };
    g_arg0 = NRO;
    // Nothing happens on its own: a normal session never asks for the network.
    launch(w, "no network unless asked", [&]() { idle(2); panel(P_POCKETS, P_SYSTEM); systemRow(ROW_ABOUT); SEE("ACNH Power Pockets 1.4.0"); press(B); idle(2); NOT_SEE("cbreezy210");
        systemRow(ROW_ABOUT); press(LEFT); press(A); idle(2); press(B); idle(2);              // Cancel in the question
        panel(P_SYSTEM, P_POCKETS); quitFromPanel(); },
        [&]() { unchanged(); CHECK(net.urls.empty() && net.onlineChecks == 0 && net.socketInits == 0); });
    launch(w, "offline", [&]() { net.connected = false; idle(2); openCheck(); connect(); SEE("isn't connected to the internet"); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); },
        [&]() { unchanged(); CHECK(net.urls.empty() && net.socketInits == 0); });
    launch(w, "up to date", [&]() { putRelease("1.4.0", zip, hexOf(zip)); idle(2); openCheck(); connect(); SEE("You have the latest version (1.4.0)"); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); },
        [&]() { unchanged(); CHECK(net.urls.size() == 1); });
    struct { const char* label; int curl; bool sock, thr; const char* see; } errs[] = {
        { "bad certificate", 60, false, false, "date and time" }, { "no answer", 28, false, false, "connection was lost" }, { "sockets refused", 0, true, false, "connection was lost" }, { "thread refused", 0, false, true, "connection was lost" } };
    for (const auto& x : errs)
        launch(w, x.label, [&]() { net.curlResult = x.curl; net.socketFail = x.sock; net.threadFail = x.thr; idle(2); openCheck(); connect(); SEE(x.see); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); }, unchanged);
    launch(w, "not now", [&]() { putRelease("1.4.1", zip, hexOf(zip)); idle(2); openCheck(); connect(); SEE("Version 1.4.1 is available"); SEE("A brand new thing."); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); },
        [&]() { unchanged(); CHECK(net.urls.size() == 1); });
    launch(w, "pending changes block it", [&]() { putRelease("1.4.1", zip, hexOf(zip)); idle(2); static Model m; m = s0.model; placeTicket(m, 5); openCheck(); connect(); press(RIGHT); press(A); idle(2); SEE("Apply or discard your pending changes");
        press(A); idle(2); press(MINUS); idle(2); press(RIGHT); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); }, [&]() { unchanged(); CHECK(net.urls.size() == 1); });
    launch(w, "wrong checksum", [&]() { putRelease("1.4.1", zip, std::string(64, 'a')); idle(2); openCheck(); connect(); press(RIGHT); press(A); idle(3); SEE("wrong checksum"); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); }, unchanged);
    launch(w, "downloaded, not installed", [&]() { putRelease("1.4.1", zip, hexOf(zip)); idle(2); openCheck(); connect(); press(RIGHT); press(A); idle(3); SEE("Install version 1.4.1?"); press(A); idle(2); SEE("nothing was installed"); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); }, unchanged);
    g_arg0 = "sdmc:/switch/somewhere_else.nro";
    launch(w, "another copy of the app", [&]() { putRelease("1.4.1", zip, hexOf(zip)); idle(2); openCheck(); connect(); press(RIGHT); press(A); idle(2); SEE("can't update itself"); press(A); idle(2); panel(P_SYSTEM, P_POCKETS); quitFromPanel(); }, unchanged);
    g_arg0 = NRO;
    auto install = [&]() { putRelease("1.4.1", zip, hexOf(zip)); idle(2); openCheck(); connect(); press(RIGHT); press(A); idle(3); SEE("Install version 1.4.1?"); press(RIGHT); script.push_back({ A, nullptr }); };
    auto installed = [&]() { untouched(); CHECK_SAME(slurp(sd("acnh_power_pockets.nro")), newNro); CHECK_SAME(slurp(sd("extra.txt")), extra); CHECK_SAME(slurp(sd("licenses/x.txt")), extra); };
    launch(w, "install", install, [&]() { installed(); CHECK(net.nextLoad == NRO && net.nextArgs == std::string("\"") + NRO + "\""); CHECK_SAME(slurp(NRO_PREV), oldNro); });
    launch(w, "first launch after", [&]() { SEE("Updated to version"); quitFromPanel(); }, [&]() { installed(); CHECK(!fileThere(NRO_PREV)); CHECK_SAME(slurp(sd("update/previous.nro")), oldNro); CHECK(net.urls.empty()); });
    launch(w, "second launch", [&]() { NOT_SEE("Updated to version"); quitFromPanel(); }, installed);
    // Back to 1.4.0, install again, and this time the user starts the previous version.
    writeFile(dst + "acnh_power_pockets.nro", oldNro); remove((dst + "extra.txt").c_str()); remove((dst + "licenses/x.txt").c_str());
    launch(w, "install again", install, installed);
    g_arg0 = NRO_PREV;
    launch(w, "previous version started", [&]() { SEE("previous version was put back"); quitFromPanel(); }, [&]() { unchanged(); CHECK(!fileThere(NRO_PREV) && !fileThere(sd("update/journal.txt"))); });
    g_arg0 = "app";
}

// ---- lists from catalogue.ac ----
static int g_rxSock = -1;
static void rxSend(const std::string& code, const std::string& body) {            // probe side: the phone
    probe([code, body]() {
        std::string t = screenText();
        size_t at = t.find("http://127.0.0.1:");
        CHECK(at != std::string::npos);
        int port = at == std::string::npos ? 0 : atoi(t.c_str() + at + 17);
        g_rxSock = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(connect(g_rxSock, (sockaddr*)&a, sizeof a) == 0);
        std::string req = "POST /send HTTP/1.1\r\nHost: x\r\nX-Code: " + code + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        CHECK(send(g_rxSock, req.data(), req.size(), 0) == (ssize_t)req.size());
        usleep(30000);                                   // let the loopback deliver it before the next frames
    });
    idle(12);
}
static void rxExpect(const char* text) {
    std::string want = text;
    probe([want]() {
        struct timeval tv = { 1, 0 };
        setsockopt(g_rxSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        std::string out; char buf[2048]; ssize_t n;
        while ((n = recv(g_rxSock, buf, sizeof buf, 0)) > 0) out.append(buf, (size_t)n);
        close(g_rxSock); g_rxSock = -1;
        if (out.find(want) == std::string::npos) { printf("    want [%s] reply: %s\n", want.c_str(), out.c_str()); CHECK(false); } else CHECK(true);
    });
}
// Catalog > Browse collections, from the Pockets panel: Receive entry first, then the lists.
static void openBrowse() { panel(P_POCKETS, P_CATALOG); press(RIGHT); press(A); idle(2); SEE("YOUR LISTS"); }

static void scnLists(const Save& s0) {
    g_scn = "catalogue_lists";
    World w = makeWorld(g_scn, s0);
    const std::string dst = w.root + "/sdmc:/switch/acnh_power_pockets/";
    writeFile(dst + "catalogue_ac.txt", slurp(g_repo + "/assets/catalogue_ac.txt"));
    std::string cmd = "mkdir -p '" + dst + "lists'";
    if (system(cmd.c_str()) != 0) exit(2);
    const std::string ex = "{\"lists\":[{\"name\":\"Japanese bedroom\",\"items\":{\"5851\":25,\"2768\":30,\"9617\":2,\"d632\":1}}]}";
    writeFile(dst + "lists/catalogue-2026-11-26.json", Bytes(ex.begin(), ex.end()));
    // Fill pockets with a list, then Apply: exactly those slots, nothing else.
    launch(w, "fill from a list", [&]() {
        idle(2); openBrowse(); SEE("Receive a list from catalogue.ac"); SEE("Japanese bedroom");
        press(DOWN); press(A); idle(2); SEE("Fill pockets with this list");
        press(DOWN); press(A); idle(2); SEE("Fill pockets with \"Japanese bedroom\"?"); SEE("base color");
        press(RIGHT); press(A); idle(2); SEE("6 slots filled from the list");
        press(B); idle(2); panel(P_CATALOG, P_POCKETS);
        applyFromPanel(); SEE("Changes applied");
        probe([&]() {
            Bytes p = nandPlainP(s0);
            int nmt = 0, nmtStacks = 0, wood = 0, axes = 0, changedOld = 0;
            for (int i = 1; i <= 20; i++) {
                const uint8_t* r = &p[POCKETS + (i - 1) * 8];
                const uint8_t* o = &s0.model.pockets[(i - 1) * 8];
                const uint16_t id = (uint16_t)(r[0] | r[1] << 8), raw = (uint16_t)(r[4] | r[5] << 8);
                if ((o[0] | o[1] << 8) != pp::ID_EMPTY) { if (memcmp(r, o, 8)) changedOld++; continue; }
                if (id == ID_NMT) { nmt += pp::rawToQty(raw); nmtStacks++; }
                if (id == ID_WOOD) wood += pp::rawToQty(raw);
                if (id == ID_GAXE) { axes++; CHECK(raw == 0); }
            }
            CHECK(changedOld == 0); CHECK(nmt == 25 && nmtStacks == 3); CHECK(wood == 30); CHECK(axes == 2);
            CHECK(validHashes(p, P_REGS, 2) == 2);
        });
        quitFromPanel();
    });
    // Receive over the local network: wrong code, a list, then five wrong codes close it.
    launch(w, "receive", [&]() {
        idle(2); openBrowse(); press(A); idle(2); SEE("Code:"); SEE("4821"); SEE("http://127.0.0.1:");
        rxSend("1234", "{\"name\":\"x\",\"items\":{\"5851\":1}}"); rxExpect("Wrong code");
        rxSend("4821", "hello"); rxExpect("isn't a catalogue.ac export");
        rxSend("4821", "{\"name\":\"From phone\",\"items\":{\"2768\":5,\"rr1\":1}}"); rxExpect("From phone\\\" is on the console: 1 item. Open it in Catalog > Browse collections. 1 entry skipped");
        SEE("Received \"From phone\": 1 item");
        for (int i = 0; i < 4; i++) { rxSend("0000", "x"); rxExpect("Wrong code"); }
        SEE("Too many wrong codes"); press(A); idle(2);
        SEE("From phone");
        press(A); idle(2); SEE("Code:");                             // open again: a working server
        rxSend("4821", "{\"name\":\"Second\",\"items\":{\"5851\":2}}"); rxExpect("\"ok\":true");
        press(B); idle(2); NOT_SEE("Code:");
        quitFromPanel();
    }, [&]() {
        CHECK(fileThere(sd("lists/list_001.json")) && fileThere(sd("lists/list_002.json")) && !fileThere(sd("lists/list_003.json")));
        CHECK(counters.nandWrites == 0 && counters.commits == 0);
        CHECK(net.socketInits == net.socketExits && net.socketInits == 2 && net.urls.empty());
    });
    launch(w, "offline", [&]() { net.connected = false; idle(2); openBrowse(); press(A); idle(2); SEE("isn't connected to a network"); press(A); idle(2); quitFromPanel(); },
        [&]() { CHECK(net.socketInits == 0); });
    // Delete a list file; a damaged file shows up and can be deleted too.
    writeFile(dst + "lists/broken.json", Bytes(5, '{'));
    launch(w, "delete", [&]() {
        idle(2); openBrowse(); SEE("broken.json"); SEE("can't be read");
        press(DOWN); press(A); idle(2); SEE("Delete broken.json?"); press(RIGHT); press(A); idle(2); SEE("List file deleted");
        NOT_SEE("broken.json");
        press(A); idle(2); press(UP); press(A); idle(2);              // the first list's menu, last entry: delete
        SEE("Delete catalogue-2026-11-26.json?"); press(RIGHT); press(A); idle(2);
        NOT_SEE("Japanese bedroom");
        quitFromPanel();
    }, [&]() {
        CHECK(!fileThere(sd("lists/broken.json")) && !fileThere(sd("lists/catalogue-2026-11-26.json")) && fileThere(sd("lists/list_001.json")));
        CHECK(counters.nandWrites == 0);
    });
}

// L / R wrap around the panels: L on Pockets opens System, R on System comes back.
static void scnPanelsWrap(const Save& s0) {
    g_scn = "panels_wrap";
    World w = makeWorld(g_scn, s0);
    launch(w, "session", [&]() {
        idle(2); SEE("20 slots");
        press(L); idle(2); SEE("Restore SD backup to console"); NOT_SEE("20 slots");
        NOT_SEE("built-in not found");                                  // every FAV_NAMES entry exists in items.txt
        press(R); idle(2); SEE("20 slots");
        press(R, P_SYSTEM + 1); idle(2); SEE("20 slots");              // a full turn to the right
        quitFromPanel();
    });
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
        { "stale_hash_repaired", scnStaleHash },
        { "real_save", scnRealSave },
        { "read_only_save", [](const Save&) { scnReadOnly(); } },
        { "turnips_read_only", scnTurnips },
        { "panels_wrap", scnPanelsWrap },
        { "update", scnUpdate },
        { "catalogue_lists", scnLists },
        { "bag_slots", scnBag },
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
