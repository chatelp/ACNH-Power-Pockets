// Host unit tests for source/pp_model.cpp (run with tests/host/run.sh).
#include "../../source/pp_model.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

using namespace pp;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string g_dir;
static std::string path(const char* name) { return g_dir + "/" + name; }
static std::string readAll(const std::string& p) {
    std::string s; FILE* f = fopen(p.c_str(), "rb"); if (!f) return s;
    char b[512]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n); fclose(f); return s;
}
static void writeText(const std::string& p, const char* t) { FILE* f = fopen(p.c_str(), "wb"); fputs(t, f); fclose(f); }

static std::vector<Item> makeItems() {
    std::vector<Item> v;
    const char* names[] = { "", "golden axe", "Nook Miles Ticket", "star wand", "gold roses", "pearl",
                            "cherry-blossom petal", "wood", "Wandering", "tomato start", "dup", "dup",
                            "medium tomato sprout", "Bell bag" };
    for (uint16_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) v.push_back({ i, names[i] });
    return v;
}

static void emptyPockets(uint8_t* p) {
    for (int s = 0; s < SLOT_COUNT; s++) { uint8_t* r = p + s * 8; r[0] = 0xFE; r[1] = 0xFF; memset(r + 2, 0, 6); }
}
static void putRaw(uint8_t* p, int slot, uint16_t id, uint16_t cnt, uint8_t wrap, uint16_t wear) {
    uint8_t* r = p + (slot - 1) * 8;
    r[0] = id & 0xFF; r[1] = id >> 8; r[2] = 0x20; r[3] = wrap; r[4] = cnt & 0xFF; r[5] = cnt >> 8; r[6] = wear & 0xFF; r[7] = wear >> 8;
}

static void testSlots() {
    printf("slots\n");
    uint8_t p[POCKETS_BYTES]; emptyPockets(p);
    putRaw(p, 3, 0x0001, 1, 0x41, 77);                     // wrapped, worn golden axe
    uint8_t before[8]; memcpy(before, p + 16, 8);
    CHECK(!placeInEmpty(p, 3, 0x0002, 10));                // occupied: refused
    CHECK(memcmp(before, p + 16, 8) == 0);                 // and untouched
    CHECK(!placeInEmpty(p, 0, 0x0002, 10));
    CHECK(!placeInEmpty(p, 21, 0x0002, 10));
    CHECK(!placeInEmpty(p, 4, 0xFFFD, 1));                 // EXTENSION never placeable
    CHECK(placeInEmpty(p, 4, 0x0002, 10));
    const uint8_t clean[8] = { 0x02, 0x00, 0, 0, 10, 0, 0, 0 };
    CHECK(memcmp(p + 24, clean, 8) == 0);                  // full clean record
    CHECK(clearSlot(p, 3));
    const uint8_t empty[8] = { 0xFE, 0xFF, 0, 0, 0, 0, 0, 0 };
    CHECK(memcmp(p + 16, empty, 8) == 0);                  // NHSE empty record
    CHECK(!clearSlot(p, 3));                               // already empty
    CHECK(!setSlotRaw(p, 3, 5));                           // empty: refused
    CHECK(setSlotRaw(p, 4, 7) && readSlot(p, 4).raw == 7 && readSlot(p, 4).id == 2);
    CHECK(countItems(p) == 1 && countEmpty(p) == 19);
}

static void testFillNoOverwrite() {
    printf("10 x 'place in next empty slot' never overwrites\n");
    uint8_t p[POCKETS_BYTES]; emptyPockets(p);
    int occupied[] = { 2, 5, 6, 13, 20 };
    for (int s : occupied) putRaw(p, s, 0x0005, 3, 0, 0);
    uint8_t orig[POCKETS_BYTES]; memcpy(orig, p, POCKETS_BYTES);
    int focus = 4, placed = 0;
    for (int press = 0; press < 10; press++) {
        int s = nextEmptySlot(p, focus);
        CHECK(s > 0);
        CHECK(placeInEmpty(p, s, 0x0002, 10)); placed++;
        int n = nextEmptySlot(p, s);
        if (n > 0) focus = n;
    }
    CHECK(placed == 10);
    for (int s : occupied) CHECK(memcmp(p + (s - 1) * 8, orig + (s - 1) * 8, 8) == 0);
    CHECK(countEmpty(p) == 20 - 5 - 10);
    // Slots 4, 7..12, 14..16 should be filled (starting at focus 4, skipping 5, 6, 13).
    int expect[] = { 4, 7, 8, 9, 10, 11, 12, 14, 15, 16 };
    for (int s : expect) CHECK(readSlot(p, s).id == 0x0002);
    // Wrap-around and full pockets.
    for (int s = 1; s <= 20; s++) placeInEmpty(p, s, 0x0007, 1);
    CHECK(nextEmptySlot(p, 1) == -1);
}

static void testBaseAndSync() {
    printf("base, pending, discard, I2 sync\n");
    uint8_t p[POCKETS_BYTES]; emptyPockets(p);
    putRaw(p, 1, 0x0001, 1, 0, 0);
    uint32_t vals[4] = { 1000, 2000, 300, 400 };
    Base b; snapshotBase(b, p, vals);
    CHECK(pendingCount(b, p, vals) == 0);
    placeInEmpty(p, 20, 0x0002, 10);
    clearSlot(p, 1);
    vals[0] = 99999;
    CHECK(pendingPockets(b, p) == 2 && pendingVals(b, vals) == 1 && pendingCount(b, p, vals) == 3);
    CHECK(slotModified(b, p, 20) && slotModified(b, p, 1) && !slotModified(b, p, 2));

    // I2: slot 20 modified while g_slot = 1. Simulate doSave(): copy the buffer,
    // then write g_newId/g_newCount into g_slot. The result must equal the buffer.
    int gSlot = 1; int32_t gId = 0x0003, gCnt = 42;          // stale upstream values
    syncSaveSlotVars(p, gSlot, gId, gCnt);
    uint8_t out[POCKETS_BYTES]; memcpy(out, p, POCKETS_BYTES);
    uint8_t* r = out + (gSlot - 1) * 8;
    r[0] = gId & 0xFF; r[1] = (gId >> 8) & 0xFF; r[4] = gCnt & 0xFF; r[5] = (gCnt >> 8) & 0xFF;
    CHECK(memcmp(out, p, POCKETS_BYTES) == 0);
    int badSlot = 0; syncSaveSlotVars(p, badSlot, gId, gCnt);
    CHECK(badSlot == 1);
    badSlot = 21; syncSaveSlotVars(p, badSlot, gId, gCnt);
    CHECK(badSlot == 1);

    revertSlot(b, p, 20);
    CHECK(!slotModified(b, p, 20) && pendingCount(b, p, vals) == 2);
    discardAll(b, p, vals);
    CHECK(pendingCount(b, p, vals) == 0 && vals[0] == 1000 && readSlot(p, 1).id == 1);
}

static void testValuesStatus() {
    printf("values, quantities, engine status\n");
    CHECK(clampVal(0, -5) == 0 && clampVal(0, 2000000000LL) == 999999999u);
    CHECK(clampVal(2, 99999999) == 9999999u && clampVal(3, 123) == 123u);
    CHECK(placeRaw(true, 50) == 0);
    CHECK(qtyToRaw(0) == (kCountZeroBased ? 0 : 1) && qtyToRaw(500) == (kCountZeroBased ? 98 : 99));
    // Exact upstream strings (main.cpp).
    CHECK(parseSaveStatus("SAVED personal + main to NAND!") == ER_OK);
    CHECK(parseSaveStatus("BAK sd open 0x2EE202") == ER_BACKUP_FAILED);
    CHECK(parseSaveStatus("BAK write failed!") == ER_BACKUP_FAILED);
    CHECK(parseSaveStatus("NAND write failed!") == ER_NAND_FAILED);
    CHECK(parseSaveStatus("malloc failed!") == ER_MALLOC_FAILED);
    CHECK(parseSaveStatus("") == ER_OTHER);
    CHECK(parseRestoreStatus("RESTORED from SD backup!") == ER_OK);
    CHECK(parseRestoreStatus("Restore failed: backup files missing!") == ER_RESTORE_MISSING);
    CHECK(parseRestoreStatus("Restore failed: backup size mismatch!") == ER_RESTORE_SIZE);
    CHECK(parseRestoreStatus("Restore failed: backup read error!") == ER_RESTORE_READ);
    CHECK(parseRestoreStatus("Restore failed: NAND write failed!") == ER_NAND_FAILED);
    CHECK(parseDoctorStatus("REPAIRED! Quit fully, then try the game.") == ER_OK);
    CHECK(parseDoctorStatus("REPAIRED (no forensic copy) - quit & try game") == ER_OK_NO_FORENSIC);
    CHECK(parseDoctorStatus("Repair NAND write failed!") == ER_NAND_FAILED);
}

static void testItems() {
    printf("item helpers\n");
    std::vector<Item> items = makeItems();
    uint16_t id = 0;
    CHECK(parseHexId("16A2", id) && id == 0x16A2);
    CHECK(parseHexId(" 0x00ff ", id) && id == 0x00FF);
    CHECK(!parseHexId("12345", id) && !parseHexId("zzzz", id));
    std::vector<int> r = searchItems(items, "WAND");
    CHECK(r.size() == 2 && r[0] == 3 && r[1] == 8);
    r = searchItems(items, "0005");                          // hex id first, then name matches
    CHECK(!r.empty() && r[0] == 5);
    CHECK(searchItems(items, "   ").empty());
    CHECK(formatThousands(0) == "0" && formatThousands(99999) == "99,999" && formatThousands(999999999) == "999,999,999");
}

static void testStackRules() {
    printf("non-stackable rules\n");
    std::vector<Item> items = makeItems();
    StackRules sr;
    sr.build(items, path("missing.txt").c_str());
    CHECK(sr.isSingle(1) && !sr.isSingle(2) && !sr.fileFound());
    writeText(path("ns.txt"),
              "# comment\n~wand\n0x0007\nNOOK MILES TICKET\n-0x0007\nnot an item\n~\n0xZZZZ\n\n-~wand\n~sprout\n");
    sr.build(items, path("ns.txt").c_str());
    CHECK(sr.fileFound());
    CHECK(sr.isSingle(2));                                   // exact name, case-insensitive
    CHECK(!sr.isSingle(7));                                  // added then removed
    CHECK(!sr.isSingle(3) && !sr.isSingle(8));               // ~wand added then removed
    CHECK(sr.isSingle(12));                                  // ~sprout
    CHECK(sr.fileIgnored() == 3);                            // "not an item", "~", "0xZZZZ"
    CHECK(!sr.isSingle(0xFFFE));
}

static const char* BUILTIN[] = { "gold roses", "pearl", "golden axe", "does not exist" };

static void testFavorites() {
    printf("favorites overlay\n");
    std::vector<Item> items = makeItems();
    std::string fp = path("user_favorites.txt");
    Favorites f;
    f.load(items, BUILTIN, 4, fp);
    CHECK(f.list().size() == 3 && f.missingBuiltins() == 1);
    CHECK(f.list()[0].label == "gold roses" && f.list()[2].label == "pearl");   // A-Z
    CHECK(f.add(2) == Favorites::ADDED && f.isFav(2) && f.isUserAdded(2));
    CHECK(f.add(2) == Favorites::ALREADY);
    CHECK(f.add(0) == Favorites::INVALID && f.add(500) == Favorites::INVALID);
    CHECK(f.remove(4) == Favorites::REMOVED && !f.isFav(4));                  // hide a built-in
    std::string txt = readAll(fp);
    CHECK(txt.find("+0002 Nook Miles Ticket") != std::string::npos);
    CHECK(txt.find("-0004 gold roses") != std::string::npos);
    CHECK(txt.find("# end") != std::string::npos);
    CHECK(!fileExists((fp + ".tmp").c_str()));

    Favorites g; g.load(items, BUILTIN, 4, fp);                               // persistence
    CHECK(g.isFav(2) && !g.isFav(4) && g.isFav(5) && g.ignoredLines() == 0);
    CHECK(g.add(4) == Favorites::ADDED && g.isFav(4));                        // un-hide
    CHECK(readAll(fp).find("-0004") == std::string::npos);

    // Corrupted file: bad lines ignored, good ones kept, built-ins always there.
    writeText(fp, "garbage\n+0003 star wand\n+00ZZ x\n+9999 out of range\n-0002 not a builtin\n+0003 dup\n\x01\x02\n");
    Favorites h; h.load(items, BUILTIN, 4, fp);
    CHECK(h.isFav(3) && h.isFav(4) && h.isFav(5) && h.ignoredLines() == 6);
    // The first rewrite keeps a .bak of the user's original file.
    CHECK(h.add(6) == Favorites::ADDED && readAll(fp + ".bak").find("garbage") != std::string::npos);

    // Interrupted rewrite: a complete .tmp wins, an incomplete one is ignored.
    writeText(fp, "+0002 Nook Miles Ticket\n");
    writeText(fp + ".tmp", "+0005 pearl\n+0006 cherry-blossom petal\n# end\n");
    Favorites t; t.load(items, BUILTIN, 4, fp);
    CHECK(t.loadedFromTmp() && t.isFav(6) && !t.isFav(2));
    writeText(fp + ".tmp", "+0006 cherry-blossom petal\n");
    Favorites u; u.load(items, BUILTIN, 4, fp);
    CHECK(!u.loadedFromTmp() && u.isFav(2) && !u.isFav(6));
    ::remove((fp + ".tmp").c_str());

    // Limit.
    std::vector<Item> big;
    for (uint16_t i = 0; i < 200; i++) { char n[16]; snprintf(n, sizeof n, "item %03u", i); big.push_back({ i, n }); }
    Favorites L; L.load(big, BUILTIN, 0, path("big_favs.txt"));
    int added = 0; for (uint16_t i = 0; i < 100; i++) if (L.add(i) == Favorites::ADDED) added++;
    CHECK(added == FAV_LIMIT && L.add(150) == Favorites::FULL);

    // No items.txt: read-only, the user's file is never rewritten.
    std::vector<Item> none;
    writeText(fp, "+0002 Nook Miles Ticket\n");
    Favorites ro; ro.load(none, BUILTIN, 4, fp);
    CHECK(ro.readOnly() && ro.list().empty() && ro.add(2) == Favorites::READ_ONLY);
    CHECK(readAll(fp) == "+0002 Nook Miles Ticket\n");

    // I/O error: change reverted in memory.
    Favorites io; io.load(items, BUILTIN, 4, g_dir + "/no/such/dir/favs.txt");
    CHECK(io.add(2) == Favorites::IO_ERROR && !io.isFav(2));

    // Reset to defaults.
    Favorites rs; rs.load(items, BUILTIN, 4, fp);
    CHECK(rs.isFav(2) && rs.resetToDefaults() && !rs.isFav(2) && rs.list().size() == 3);
    CHECK(!fileExists(fp.c_str()) && fileExists((fp + ".bak").c_str()));
}

static void testLoadoutsFiles() {
    printf("loadouts and SD helpers\n");
    uint8_t p[POCKETS_BYTES]; emptyPockets(p); putRaw(p, 7, 0x0002, 10, 0, 0);
    CHECK(loadoutWrite(path("l_0.dat").c_str(), p));
    uint8_t q[POCKETS_BYTES]; memset(q, 0xAA, sizeof q);
    CHECK(loadoutRead(path("l_0.dat").c_str(), q) && memcmp(p, q, POCKETS_BYTES) == 0);
    writeText(path("l_1.dat"), "short");
    memset(q, 0xAA, sizeof q);
    CHECK(!loadoutRead(path("l_1.dat").c_str(), q));
    bool untouched = true; for (int i = 0; i < POCKETS_BYTES; i++) if (q[i] != 0xAA) untouched = false;
    CHECK(untouched);                                        // I12
    CHECK(!loadoutRead(path("nope.dat").c_str(), q));
    // A 160-byte file that is not a pocket snapshot (EXTENSION / 0xFFFF ids) is refused too.
    uint8_t bad[POCKETS_BYTES]; emptyPockets(bad); putRaw(bad, 5, 0xFFFD, 1, 0, 0);
    CHECK(!pocketsPlausible(bad) && pocketsPlausible(p));
    CHECK(loadoutWrite(path("l_2.dat").c_str(), bad));
    memset(q, 0xAA, sizeof q);
    CHECK(!loadoutRead(path("l_2.dat").c_str(), q));
    untouched = true; for (int i = 0; i < POCKETS_BYTES; i++) if (q[i] != 0xAA) untouched = false;
    CHECK(untouched);
    memset(bad, 0xFF, sizeof bad);
    CHECK(!pocketsPlausible(bad));
    CHECK(copyFile(path("l_0.dat").c_str(), path("copy.dat").c_str()) && fileSize(path("copy.dat").c_str()) == 160);
    CHECK(fileSize(path("nope.dat").c_str()) == -1);
}

static uint32_t fakeHash(const uint8_t* d, size_t n) { uint32_t h = 0; for (size_t i = 0; i < n; i++) h = h * 31 + d[i]; return h; }

static void testRegions() {
    printf("region hash diagnostic\n");
    uint8_t buf[64]; for (int i = 0; i < 64; i++) buf[i] = (uint8_t)i;
    uint32_t h = fakeHash(buf + 4, 20);
    buf[0] = h & 0xFF; buf[1] = (h >> 8) & 0xFF; buf[2] = (h >> 16) & 0xFF; buf[3] = h >> 24;
    RegionRef r[3] = { { 0, 20 }, { 30, 10 }, { 60, 100 } };
    CHECK(regionsHashOk(buf, sizeof buf, r, 3, fakeHash) == 1);
}

static void testBackupCheck() {
    printf("backup check and pre-edit mark\n");
    uint8_t a[256], b[256];
    for (int i = 0; i < 256; i++) a[i] = b[i] = (uint8_t)(i * 7);
    const ByteRange ok[2] = { { 16, 8 }, { 100, 4 } };
    CHECK(diffOutside(a, b, 256, ok, 2) == 0);
    for (int i = 16; i < 24; i++) b[i] ^= 0xFF;              // inside an allowed range
    b[103] ^= 1;
    CHECK(diffOutside(a, b, 256, ok, 2) == 0);
    b[15] ^= 1; b[24] ^= 1; b[104] ^= 1; b[255] ^= 1;        // just outside, and the last byte
    CHECK(diffOutside(a, b, 256, ok, 2) == 4);
    CHECK(diffOutside(a, b, 256, nullptr, 0) == 13);
    CHECK(diffOutside(a, nullptr, 256, ok, 2) != 0);
    CHECK(diffOutside(a, a, 0, ok, 2) == 0);

    std::string s1 = stateSignature(a, 256, b, 256);
    CHECK(s1.size() == 16 && s1 == stateSignature(a, 256, b, 256));
    b[200] ^= 1;
    CHECK(stateSignature(a, 256, b, 256) != s1);              // one bit anywhere changes it
    b[200] ^= 1;
    CHECK(stateSignature(a, 255, b, 256) != s1 && stateSignature(b, 256, a, 256) != s1);
    std::string mp = path("session_mark.txt");
    CHECK(!stateMarkMatches(mp.c_str(), s1));                 // no file
    CHECK(writeStateMark(mp.c_str(), s1, true) && stateMarkMatches(mp.c_str(), s1));
    CHECK(!stateMarkMatches(mp.c_str(), stateSignature(a, 255, b, 256)));
    CHECK(writeStateMark(mp.c_str(), s1, false) && !stateMarkMatches(mp.c_str(), s1));   // "nopair" never matches
    writeText(mp, "");
    CHECK(!stateMarkMatches(mp.c_str(), s1) && !stateMarkMatches(mp.c_str(), ""));
    writeText(mp, " ok");
    CHECK(!stateMarkMatches(mp.c_str(), ""));
}

// Robustness of every parser that reads a user-editable SD file: random and mutated
// inputs must never crash, read out of bounds (ASan/UBSan) or break the invariants.
static uint32_t g_seed = 12345;
static uint32_t rnd() { g_seed ^= g_seed << 13; g_seed ^= g_seed >> 17; g_seed ^= g_seed << 5; return g_seed; }
static void writeBytes(const std::string& p, const std::string& d) { FILE* f = fopen(p.c_str(), "wb"); if (!d.empty()) fwrite(d.data(), 1, d.size(), f); fclose(f); }

static void testFuzzParsers() {
    printf("fuzz: user_favorites.txt, nonstackable.txt, loadouts, mark\n");
    std::vector<Item> items = makeItems();
    const std::string fp = path("fuzz_favs.txt"), np = path("fuzz_ns.txt"), lp = path("fuzz_lo.dat"), mp = path("fuzz_mark.txt");
    const char* seeds[] = { "+0002 Nook Miles Ticket\n-0004 gold roses\n# end\n", "~wand\n-Wandering\n0x0001\ngolden axe\n# c\n",
                            "+", "-", "+000", "+0002", "+0002x", "0x", "0xZZZZ", "~", "-~", "--", "# end", "\r\n\r\n", "+FFFF a\n+FFFE b\n+FFFD c\n" };
    const int nSeeds = (int)(sizeof(seeds) / sizeof(seeds[0]));
    bool ok = true;
    for (int it = 0; it < 3000 && ok; it++) {
        std::string d;
        int mode = (int)(rnd() % 3);
        if (mode == 0) {                                       // pure noise, sometimes very long lines
            size_t n = rnd() % 2000;
            for (size_t i = 0; i < n; i++) d += (char)(rnd() % 256);
        } else if (mode == 1) {                                // text-like noise built from the grammar's alphabet
            static const char alpha[] = "+-~#0xX123456789abcdefABCDEF \t\r\n\ngoldenaxewand";
            size_t n = rnd() % 600;
            for (size_t i = 0; i < n; i++) d += alpha[rnd() % (sizeof(alpha) - 1)];
        } else {                                               // mutated valid file
            d = seeds[rnd() % nSeeds];
            for (int k = (int)(rnd() % 4); k > 0 && !d.empty(); k--) {
                size_t at = rnd() % d.size();
                switch (rnd() % 3) { case 0: d[at] = (char)(rnd() % 256); break; case 1: d.erase(at, 1); break; default: d.insert(at, 1, (char)(rnd() % 256)); }
            }
            for (int k = (int)(rnd() % 3); k > 0; k--) d += d;
        }
        writeBytes(fp, d); writeBytes(np, d); writeBytes(lp, d); writeBytes(mp, d);
        if (rnd() % 4 == 0) writeBytes(fp + ".tmp", d); else ::remove((fp + ".tmp").c_str());

        Favorites f; f.load(items, BUILTIN, 4, fp);
        if ((int)f.list().size() > FAV_LIMIT) ok = false;
        for (size_t i = 0; i < f.list().size(); i++) {         // every favorite is a real, named item
            uint16_t id = f.list()[i].id;
            if (id >= items.size() || items[id].name.empty()) ok = false;
            if (i > 0 && f.list()[i - 1].id == id) ok = false;
        }
        StackRules r; r.build(items, np.c_str());
        if (r.singleCount() > (int)items.size() || r.isSingle(0xFFFF) || r.isSingle((uint16_t)items.size())) ok = false;
        uint8_t q[POCKETS_BYTES]; memset(q, 0xAA, sizeof q);
        if (loadoutRead(lp.c_str(), q)) { if (!pocketsPlausible(q)) ok = false; }
        else for (int i = 0; i < POCKETS_BYTES; i++) if (q[i] != 0xAA) ok = false;
        stateMarkMatches(mp.c_str(), "0123456789abcdef");
        uint16_t id = 0; parseHexId(d, id); searchItems(items, d.substr(0, 40));
        if (!ok) printf("  fuzz failure at iteration %d\n", it);
    }
    CHECK(ok);
    // The app's own rewrite after loading garbage must give a file it reads back identically.
    writeBytes(fp, "garbage\n+0003 x\n\xff\xfe\n-0004\n"); ::remove((fp + ".tmp").c_str());
    Favorites f; f.load(items, BUILTIN, 4, fp);
    CHECK(f.add(2) == Favorites::ADDED);
    Favorites g; g.load(items, BUILTIN, 4, fp);
    CHECK(g.ignoredLines() == 0 && g.list().size() == f.list().size() && g.isFav(2) && g.isFav(3) && !g.isFav(4));
}

int main() {
    char tmpl[] = "/tmp/pp_test_XXXXXX";
    const char* t = getenv("PP_TMP");
    std::string base = t ? std::string(t) + "/pp_test_XXXXXX" : tmpl;
    std::vector<char> b(base.begin(), base.end()); b.push_back(0);
    if (!mkdtemp(b.data())) { perror("mkdtemp"); return 2; }
    g_dir = b.data();

    testSlots();
    testFillNoOverwrite();
    testBaseAndSync();
    testValuesStatus();
    testItems();
    testStackRules();
    testFavorites();
    testLoadoutsFiles();
    testRegions();
    testBackupCheck();
    testFuzzParsers();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
