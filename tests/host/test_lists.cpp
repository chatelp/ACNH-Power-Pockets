// Unit tests of the catalogue.ac lists (source/pp_lists.cpp), against the real items.txt and
// catalogue_ac.txt. Share links are built with a port of the site's own encoder
// (tools.encoder.js, encodeString), so the reader is checked against the site's format.
#include "../../source/pp_lists.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/time.h>

using namespace pp;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static std::vector<Item> g_items;
static ClothingIds g_cl;
static std::string g_tmp;

static uint16_t idOf(const char* name) { for (size_t i = 0; i < g_items.size(); i++) if (g_items[i].name == name) return (uint16_t)i; printf("  !! no item %s\n", name); return 0xFFFF; }

// --- port of catalogue.ac's Encoder.encodeString and of btoa(unescape(encodeURIComponent(name))) ---
static std::string b64enc(const std::string& s) {
    static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; uint32_t acc = 0; int bits = 0;
    for (unsigned char c : s) { acc = acc << 8 | c; bits += 8; while (bits >= 6) { bits -= 6; o += A[acc >> bits & 63]; } }
    if (bits) o += A[(acc << (6 - bits)) & 63];
    while (o.size() % 4) o += '=';
    return o;
}
static std::string encodeString(const std::string& e) {
    static const char* SYM = "0123456789:;acdr";
    std::string r;
    for (char c : e) { int v = (int)(strchr(SYM, c) - SYM); for (int k = 3; k >= 0; k--) r += (char)('0' + (v >> k & 1)); }
    int l = (int)(r.size() % 6);
    if (l > 0 && 6 - l >= 4) { r += "1011"; l -= 4; }
    if (l > 0) r += std::string((size_t)(6 - l), '0');
    static const char* N = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t t = 0; t < r.size(); t += 6) o += N[strtol(r.substr(t, 6).c_str(), nullptr, 2)];
    return o;
}
static std::string link(const std::string& name, const std::string& packed) { return "https://catalogue.ac/?l=" + b64enc(name) + "-" + encodeString(packed); }

static int qtyOf(const ImportedList& l, uint16_t id) { for (const ListItem& i : l.items) if (i.id == id) return i.qty; return 0; }

static void testClothing() {
    ClothingIds c;
    CHECK(c.applyLine("c652 2542 14A9\n") && c.applyLine("# comment") && c.applyLine(""));
    uint16_t id = 0;
    CHECK(c.lookup(652, 0, id) && id == 0x2542 && c.lookup(652, 1, id) && id == 0x14A9 && !c.lookup(652, 2, id) && !c.lookup(651, 0, id));
    const char* bad[] = { "c", "c652", "c652 ", "x652 1234", "c652 12345", "c652 12G4", "c65x 1234", "652 1234" };
    for (const char* b : bad) CHECK(!c.applyLine(b));
    CHECK(g_cl.size() > 1000);
    CHECK(g_cl.lookup(652, 0, id) && g_items[id].name == "acid-washed jacket (Black)");
    CHECK(g_cl.lookup(652, 1, id) && g_items[id].name == "acid-washed jacket (Blue)");
}

static void testExport() {
    // The shape of catalogue.ac's export (main.js): settings, dashboard, profile, lists.
    const std::string ex = "{\"settings\":{\"language\":\"en\"},\"dashboard\":{},\"profile\":{\"data\":{\"79\":[1]}},\"lists\":["
        "{\"name\":\"Japanese bedroom\",\"items\":{\"79\":1,\"79-3\":2,\"c652\":1,\"c652-1\":3,\"c652-9\":1,\"d632\":1,\"brd09\":1,\"2620\":[1,5],\"99999\":1,\"40000\":1,\"80\":0,\"88\":\"x\"}},"
        "{\"name\":\"Empty\",\"items\":{}},"
        "{\"name\":\"\",\"items\":{\"1374\":12}},"
        "{\"name\":\"Only recipes\",\"items\":{\"d632\":1}}"
        "],\"shared_lists\":[]}";
    std::vector<ImportedList> out; ListStats st;
    CHECK(readListText(ex, g_items, g_cl, out, st) == LR_OK);
    CHECK(out.size() == 2);
    if (out.size() == 2) {
        const ImportedList& j = out[0];
        CHECK(j.name == "Japanese bedroom");
        CHECK(qtyOf(j, 79) == 3);                                  // "79" and "79-3": the same item, base variant
        uint16_t black = 0, blue = 0; g_cl.lookup(652, 0, black); g_cl.lookup(652, 1, blue);
        CHECK(qtyOf(j, black) == 1 && qtyOf(j, blue) == 3);
        CHECK(qtyOf(j, 2620) == 5);                                // [have, need]: what is needed
        CHECK(qtyOf(j, 80) == 1);                                  // 0 read as 1
        CHECK(qtyOf(j, 88) == 0);                                  // not a quantity: skipped
        CHECK(out[1].name == "List 2" && qtyOf(out[1], 1374) == 12);   // no name
        for (const ListItem& i : j.items) CHECK(i.id < g_items.size() && !g_items[i.id].name.empty());
    }
    CHECK(st.variants == 1 && st.recipes == 2 && st.notItems == 1 && st.unknown == 3);   // c652-9, 99999, 40000
    // One list on its own.
    CHECK(readListText("  {\"name\":\"x\",\"items\":{\"79\":2}}\n", g_items, g_cl, out, st) == LR_OK && out.size() == 1 && qtyOf(out[0], 79) == 2);
    CHECK(readListText("{\"lists\":[]}", g_items, g_cl, out, st) == LR_EMPTY);
    CHECK(readListText("{\"lists\":[{\"name\":\"r\",\"items\":{\"d1\":1}}]}", g_items, g_cl, out, st) == LR_EMPTY);
    CHECK(readListText("{\"settings\":{}}", g_items, g_cl, out, st) == LR_NOT_LIST);
    CHECK(readListText("{\"lists\":{}}", g_items, g_cl, out, st) == LR_BAD);
    CHECK(readListText("{\"lists\":[", g_items, g_cl, out, st) == LR_BAD);
    CHECK(readListText("hello", g_items, g_cl, out, st) == LR_NOT_LIST);
    CHECK(readListText("", g_items, g_cl, out, st) == LR_NOT_LIST);
    CHECK(readListText(std::string(LIST_TEXT_MAX + 1, ' '), g_items, g_cl, out, st) == LR_TOO_BIG);
    // Long names are cut on a character boundary; control characters removed.
    CHECK(readListText("{\"name\":\"" + std::string(58, 'a') + "\\u00e9\\u00e9\\n\",\"items\":{\"79\":1}}", g_items, g_cl, out, st) == LR_OK);
    CHECK(out.size() == 1 && out[0].name.size() == 60 && out[0].name.substr(58) == "\xC3\xA9");
    // A huge list stops at LIST_ITEMS_MAX.
    std::string big = "{\"name\":\"big\",\"items\":{";
    int added = 0;
    for (size_t i = 0; i < g_items.size() && added < 3000; i++) if (!g_items[i].name.empty() && validPlaceId((uint16_t)i)) { big += (added ? "," : "") + std::string("\"") + std::to_string(i) + "\":1"; added++; }
    big += "}}";
    CHECK(readListText(big, g_items, g_cl, out, st) == LR_OK && out.size() == 1 && (int)out[0].items.size() == LIST_ITEMS_MAX);
    // Damaged exports: refused or read, never a crash, never an unknown id.
    srand(5);
    for (int i = 0; i < 20000; i++) {
        std::string m = ex;
        for (int k = 0; k < 2; k++) m[(size_t)rand() % m.size()] = (char)(rand() % 128);
        if (readListText(m, g_items, g_cl, out, st) == LR_OK)
            for (const ImportedList& l : out) for (const ListItem& it : l.items) if (!(it.id < g_items.size() && !g_items[it.id].name.empty() && it.qty >= 1)) CHECK(false);
    }
}

static void testLinks() {
    std::vector<ImportedList> out; ListStats st;
    // "79a3" is "79-3" in a link; rr / dd are villagers and special characters.
    const std::string l = link("Japanese bedroom", "79;79a3:2;c652;c652a1:3;d632;2620:1:5;rr4;dd2;99999");
    CHECK(readListText(l, g_items, g_cl, out, st) == LR_OK);
    CHECK(out.size() == 1);
    if (out.size() == 1) {
        uint16_t black = 0, blue = 0; g_cl.lookup(652, 0, black); g_cl.lookup(652, 1, blue);
        CHECK(out[0].name == "Japanese bedroom" && qtyOf(out[0], 79) == 3 && qtyOf(out[0], black) == 1 && qtyOf(out[0], blue) == 3 && qtyOf(out[0], 2620) == 5);
    }
    CHECK(st.variants == 1 && st.recipes == 1 && st.notItems == 2 && st.unknown == 1);
    // Every padding case of the encoder (lengths mod 3), names in any script, the link inside a message.
    const char* packs[] = { "79", "79;", "79;80", "79:12;80:3;1374", "1374:99" };
    for (const char* pk : packs) { CHECK(readListText(link("x", pk), g_items, g_cl, out, st) == LR_OK && out.size() == 1 && qtyOf(out[0], 79) + qtyOf(out[0], 1374) >= 1); }
    CHECK(readListText(link("Caf\xC3\xA9 \xC3\xA9t\xC3\xA9 \xE2\x98\x95", "79"), g_items, g_cl, out, st) == LR_OK && out[0].name == "Caf\xC3\xA9 \xC3\xA9t\xC3\xA9 \xE2\x98\x95");
    CHECK(readListText("  look: " + link("x", "79") + "  thanks!\n", g_items, g_cl, out, st) == LR_OK);
    std::string pct = link("?>?>", "79");                       // base64 with '+' and '/', percent-encoded by a browser
    std::string enc;
    for (char c : pct.substr(24)) { if (c == '+') enc += "%2B"; else if (c == '/') enc += "%2F"; else if (c == '=') enc += "%3D"; else enc += c; }
    CHECK(readListText("https://catalogue.ac/?l=" + enc, g_items, g_cl, out, st) == LR_OK && out[0].name == "?>?>");
    CHECK(readListText("https://catalogue.ac/?x=1&l=" + link("y", "80").substr(24), g_items, g_cl, out, st) == LR_OK && out[0].name == "y");
    CHECK(readListText("https://catalogue.ac/?ld=1AbCdEf", g_items, g_cl, out, st) == LR_NOT_LIST);   // a Drive list: not readable here
    CHECK(readListText("https://example.com/?l=" + link("x", "79").substr(24), g_items, g_cl, out, st) == LR_NOT_LIST);
    CHECK(readListText("https://catalogue.ac/?l=@@@-@@@", g_items, g_cl, out, st) == LR_NOT_LIST);
    CHECK(readListText(link("x", "d1;rr1"), g_items, g_cl, out, st) == LR_EMPTY);
    srand(9);
    const std::string ok = link("Japanese bedroom", "79;79a3:2;c652;c652a1:3;2620:1:5");
    for (int i = 0; i < 20000; i++) {
        std::string m = ok;
        m[24 + (size_t)rand() % (m.size() - 24)] = "ABCxyz019+/-%=;:&"[rand() % 17];
        if (readListText(m, g_items, g_cl, out, st) == LR_OK)
            for (const ImportedList& li : out) for (const ListItem& it : li.items) if (!(it.id < g_items.size() && !g_items[it.id].name.empty())) CHECK(false);
    }
}

static void testStillNeeded() {
    uint8_t buf[ALL_SLOTS_BYTES];
    Pockets p = imageView(buf, 30);
    for (int s = 1; s <= 30; s++) { uint8_t* r = p.rec(s); memset(r, 0, 8); r[0] = 0xFE; r[1] = 0xFF; }
    auto put = [&](int s, uint16_t id, uint16_t raw) { uint8_t* r = p.rec(s); memset(r, 0, 8); r[0] = id & 0xFF; r[1] = id >> 8; r[4] = raw & 0xFF; r[5] = raw >> 8; };
    const uint16_t nmt = idOf("Nook Miles Ticket"), bed = idOf("baby bed"), wood = idOf("wood");
    auto single = [&](uint16_t id) { return id == bed; };
    ImportedList l; l.name = "x";
    l.items = { { nmt, 25 }, { bed, 3 }, { wood, 30 } };
    int already = -1;
    std::vector<ListItem> need = stillNeeded(l, p, single, already);
    CHECK(already == 0 && need.size() == 3 && need[0].qty == 25 && need[1].qty == 3 && need[2].qty == 30);
    put(1, nmt, qtyToRaw(10)); put(25, nmt, qtyToRaw(4));          // 14 tickets, one stack in the bag
    put(2, bed, 0); put(3, bed, 5);                                 // 2 beds, any variant
    put(4, wood, qtyToRaw(30)); put(5, wood, qtyToRaw(30));         // more wood than asked
    need = stillNeeded(l, p, single, already);
    CHECK(already == 14 + 2 + 30);
    CHECK(need.size() == 2 && need[0].id == nmt && need[0].qty == 11 && need[1].id == bed && need[1].qty == 1);
    put(6, bed, 0);
    need = stillNeeded(l, p, single, already);
    CHECK(need.size() == 1 && need[0].id == nmt);
    ImportedList twice; twice.items = { { bed, 2 }, { bed, 2 } };    // the same item twice in a list
    need = stillNeeded(twice, p, single, already);
    CHECK(already == 3 && need.size() == 1 && need[0].qty == 1);
    Pockets p20(buf);                                                // 20 slots: the bag is not counted
    need = stillNeeded(l, p20, single, already);
    CHECK(need.size() == 1 && need[0].id == nmt && need[0].qty == 15);
}

static bool there(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (f) fclose(f); return f != nullptr; }
static int named(const ImportedLists& L, const std::string& n) { int k = 0; for (const ListFile& f : L.files()) for (const ImportedList& l : f.lists) k += l.name == n; return k; }
static const ImportedList* find(const ImportedLists& L, const std::string& n) { for (const ListFile& f : L.files()) for (const ImportedList& l : f.lists) if (l.name == n) return &l; return nullptr; }
static void testFolder() {
    const std::string dir = g_tmp + "/lists";
    std::string cmd = "rm -rf '" + dir + "'";
    if (system(cmd.c_str()) != 0) exit(2);
    ImportedLists L;
    L.load(dir, g_items, g_cl);
    CHECK(L.files().empty() && L.listCount() == 0);
    std::vector<ImportedList> got; ListStats st; int rep = -1;
    // An export with two lists: one file per list, named after it.
    CHECK(L.add(dir, "{\"lists\":[{\"name\":\"My Interior\",\"items\":{\"79\":1,\"c652-1\":2}},{\"name\":\"Island Exterior\",\"items\":{\"80\":1}}]}", g_items, g_cl, got, st, rep) == LR_OK);
    CHECK(got.size() == 2 && rep == 0 && L.listCount() == 2 && there(dir + "/my-interior.json") && there(dir + "/island-exterior.json"));
    uint16_t blue = 0; g_cl.lookup(652, 1, blue);
    const ImportedList* mi = find(L, "My Interior");
    CHECK(mi && mi->items.size() == 2 && qtyOf(*mi, 79) == 1 && qtyOf(*mi, blue) == 2);   // stored with game ids, read back the same
    // The same export again, changed: the lists are replaced, not doubled.
    CHECK(L.add(dir, "{\"lists\":[{\"name\":\"My Interior\",\"items\":{\"80\":5}},{\"name\":\"Island Exterior\",\"items\":{\"80\":2}}]}", g_items, g_cl, got, st, rep) == LR_OK);
    CHECK(rep == 2 && L.listCount() == 2 && named(L, "My Interior") == 1 && named(L, "Island Exterior") == 1);
    mi = find(L, "My Interior");
    CHECK(mi && mi->items.size() == 1 && qtyOf(*mi, 80) == 5);
    // A share link of one of them: only that one is replaced.
    CHECK(L.add(dir, link("Island Exterior", "1374:3"), g_items, g_cl, got, st, rep) == LR_OK && rep == 1 && L.listCount() == 2);
    CHECK(qtyOf(*find(L, "Island Exterior"), 1374) == 3 && qtyOf(*find(L, "My Interior"), 80) == 5);
    // A new name is added; names that make the same file name both stay.
    CHECK(L.add(dir, link("my interior!", "79"), g_items, g_cl, got, st, rep) == LR_OK && rep == 0 && L.listCount() == 3 && there(dir + "/my-interior-2.json"));
    CHECK(L.add(dir, link("Caf\xC3\xA9 \xE2\x98\x95", "79"), g_items, g_cl, got, st, rep) == LR_OK && find(L, "Caf\xC3\xA9 \xE2\x98\x95"));
    // Two lists of the same name in one export.
    CHECK(L.add(dir, "{\"lists\":[{\"name\":\"Twin\",\"items\":{\"79\":1}},{\"name\":\"Twin\",\"items\":{\"80\":1}}]}", g_items, g_cl, got, st, rep) == LR_OK);
    CHECK(named(L, "Twin") == 1 && named(L, "Twin (2)") == 1);
    const int before = L.listCount();
    CHECK(L.add(dir, "not a list", g_items, g_cl, got, st, rep) == LR_NOT_LIST && L.listCount() == before);
    CHECK(L.add(dir, "{\"lists\":[{\"name\":\"r\",\"items\":{\"d1\":1}}]}", g_items, g_cl, got, st, rep) == LR_EMPTY && L.listCount() == before);
    // Files copied by hand: an older export with the same names is hidden by the newer files,
    // and removed once all its lists have been sent again.
    FILE* f = fopen((dir + "/catalogue-2026-11-26-10-00.json").c_str(), "wb");
    fputs("{\"lists\":[{\"name\":\"My Interior\",\"items\":{\"79\":9}},{\"name\":\"Old one\",\"items\":{\"80\":1}}]}", f); fclose(f);
    struct timeval past[2] = { { 1000000000, 0 }, { 1000000000, 0 } };
    utimes((dir + "/catalogue-2026-11-26-10-00.json").c_str(), past);
    f = fopen((dir + "/broken.json").c_str(), "wb"); fputs("{\"lists\":[", f); fclose(f);
    f = fopen((dir + "/notes.md").c_str(), "wb"); fputs("x", f); fclose(f);
    L.load(dir, g_items, g_cl);
    CHECK(named(L, "My Interior") == 1 && qtyOf(*find(L, "My Interior"), 80) == 5 && named(L, "Old one") == 1);
    bool brokenSeen = false;
    for (const ListFile& lf : L.files()) if (lf.file == "broken.json") brokenSeen = lf.result == LR_BAD && lf.lists.empty();
    CHECK(brokenSeen);
    CHECK(L.add(dir, "{\"lists\":[{\"name\":\"Old one\",\"items\":{\"80\":4}}]}", g_items, g_cl, got, st, rep) == LR_OK && rep == 1);
    CHECK(!there(dir + "/catalogue-2026-11-26-10-00.json") && named(L, "Old one") == 1 && qtyOf(*find(L, "Old one"), 80) == 4);
    CHECK(!L.remove(dir, "../lists/my-interior.json", g_items, g_cl));
    CHECK(!L.remove(dir, "notes.md", g_items, g_cl));
    CHECK(L.remove(dir, "my-interior.json", g_items, g_cl) && !find(L, "My Interior"));
    // Nothing left behind by the replacing writes.
    DIR* d = opendir(dir.c_str());
    int tmp = 0;
    while (struct dirent* e = readdir(d)) tmp += strstr(e->d_name, ".tmp") != nullptr;
    closedir(d);
    CHECK(tmp == 0);
}

int main() {
    const char* assets = getenv("PP_ASSETS");
    const char* tmp = getenv("PP_TMP");
    g_tmp = std::string(tmp ? tmp : "/tmp") + "/lists_test";
    std::string a = assets ? assets : "assets";
    std::string cmd = "rm -rf '" + g_tmp + "' && mkdir -p '" + g_tmp + "'";
    if (system(cmd.c_str()) != 0) return 2;
    FILE* f = fopen((a + "/items.txt").c_str(), "rb");
    char line[256]; uint16_t id = 0;
    while (f && fgets(line, sizeof line, f)) { size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0; g_items.push_back({ id++, line }); }
    if (f) fclose(f);
    if (g_items.size() < 10000 || !g_cl.load((a + "/catalogue_ac.txt").c_str())) { printf("assets missing\n"); return 2; }
    CHECK(g_items[79].name == "baby bed" && idOf("baby bed") == 79);
    testClothing(); testExport(); testLinks(); testStillNeeded(); testFolder();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
