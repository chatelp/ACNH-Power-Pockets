// Visual + flow harness for the panel UI (source/pp_ui.cpp) on macOS / Linux.
// Renders the REAL UI with a software SDL renderer into 1280x720 BMP screenshots,
// driven by scripted button presses, against a FAKE engine (no save involved).
// Run through tests/host/ui_shots.sh.
#define STB_IMAGE_IMPLEMENTATION
#include "../../source/stb_image.h"
#include "../../source/pp_ui.h"
#include <switch.h>

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <deque>

using namespace pp;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static SDL_Surface* g_surf = nullptr;
static std::string g_out;
static int g_shot = 0;

static void shot(const char* name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%02d-%s.bmp", g_out.c_str(), ++g_shot, name);
    SDL_SaveBMP(g_surf, p);
    printf("  shot %s\n", p);
}
static void frame(Ui& ui, uint64_t down, uint64_t held) { ui.frame(down, held); SDL_Delay(1); }
static void press(Ui& ui, uint64_t b, int n = 1) { for (int i = 0; i < n; i++) { frame(ui, b, b); frame(ui, 0, 0); } }
static void idle(Ui& ui, int n = 3) { for (int i = 0; i < n; i++) frame(ui, 0, 0); }
// Hold-to-confirm: A held for ~1.7 s of real time (the dialog needs 1.5 s), then released.
static void holdA(Ui& ui) {
    for (int i = 0; i < 110; i++) { ui.frame(i == 0 ? HidNpadButton_A : 0, HidNpadButton_A); SDL_Delay(16); }
    idle(ui, 5);
}

static uint16_t idOf(const std::vector<Item>& items, const char* name) {
    for (size_t i = 0; i < items.size(); i++) if (items[i].name == name) return (uint16_t)i;
    return ID_EMPTY;
}
static void put(uint8_t* p, int slot, uint16_t id, uint16_t cnt) {
    uint8_t* r = p + (slot - 1) * 8; memset(r, 0, 8);
    r[0] = id & 0xFF; r[1] = id >> 8; r[4] = cnt & 0xFF; r[5] = cnt >> 8;
}
static bool copy(const std::string& a, const std::string& b) { return copyFile(a.c_str(), b.c_str()); }

static const char* FAV_NAMES[] = {
    "Nook Miles Ticket", "Bell bag", "Bell voucher", "99,000 Bells", "gold roses", "cherry-blossom petal",
    "star fragment", "large star fragment", "star wand", "gold nugget", "iron nugget", "pearl", "gold bars",
    "customization kit", "golden axe", "golden net", "golden rod", "golden shovel", "golden slingshot",
    "golden watering can", "crown (No Variations)", "royal crown (No Variations)" };
static const char* LOADOUT_NAMES[] = { "Mining", "Fishing", "Terraforming", "Custom 1", "Custom 2" };

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: ui_harness <repo-root> <out-dir>\n"); return 2; }
    std::string root = argv[1];
    g_out = argv[2];
    std::string sd = g_out + "/sd/";
    copy(root + "/assets/font.ttf", sd + "font.ttf");
    copy(root + "/assets/icon.png", sd + "icon.png");
    copy(root + "/assets/ui_icons.png", sd + "ui_icons.png");
    copy(root + "/assets/nonstackable.txt", sd + "nonstackable.txt");
    copy(root + "/assets/item_glyphs.png", sd + "item_glyphs.png");
    copy(root + "/assets/item_glyphs.txt", sd + "item_glyphs.txt");

    // items.txt, exactly like main.cpp loads it
    std::vector<Item> items;
    FILE* it = fopen((root + "/assets/items.txt").c_str(), "rb");
    char line[256]; uint16_t id = 0;
    while (it && fgets(line, sizeof line, it)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        items.push_back({ id++, line });
    }
    if (it) fclose(it);
    CHECK(items.size() > 10000);

    Favorites favs; StackRules stack;
    favs.load(items, FAV_NAMES, 22, sd + "user_favorites.txt");
    GlyphMap glyphs;
    glyphs.build(items, (sd + "item_glyphs.txt").c_str());
    stack.build(items, (sd + "nonstackable.txt").c_str(), &glyphs);
    CHECK(glyphs.loaded() && glyphs.glyphOf(idOf(items, "golden axe")) != 0);
    CHECK(stack.isSingle(idOf(items, "golden axe")) && stack.isSingle(idOf(items, "star wand")));
    CHECK(!stack.isSingle(idOf(items, "Nook Miles Ticket")));

    // Fake save: pockets + money
    uint8_t pockets[POCKETS_BYTES];
    for (int s = 1; s <= 20; s++) put(pockets, s, ID_EMPTY, 0);
    put(pockets, 1, idOf(items, "golden axe"), 0);
    put(pockets, 2, idOf(items, "Nook Miles Ticket"), 3);
    put(pockets, 3, idOf(items, "wood"), 30);
    put(pockets, 6, idOf(items, "golden shovel"), 0);
    put(pockets, 11, idOf(items, "apple"), 10);
    put(pockets, 12, idOf(items, "star wand"), 5);          // non-stackable with an odd count
    uint32_t vals[4] = { 12345, 4800000, 230500, 98000 };
    uint8_t loadout[POCKETS_BYTES]; memcpy(loadout, pockets, sizeof loadout);
    put(loadout, 4, idOf(items, "iron nugget"), 30);
    loadoutWrite((sd + "l_0.dat").c_str(), loadout);

    // Fake engine. apply() reproduces what main.cpp + doSave() do with the slot vars (I2).
    int applyCalls = 0, restoreCalls = 0, doctorCalls = 0, sessionCalls = 0;
    int gSlot = 7; int32_t gNewId = 0x1234, gNewCount = 55;     // deliberately stale
    uint8_t written[POCKETS_BYTES]; uint32_t writtenVals[4] = { 0, 0, 0, 0 };
    std::deque<std::string> keyboard;

    EngineHooks h;
    h.pockets = pockets; h.vals = vals; h.items = &items; h.favorites = &favs; h.stack = &stack; h.glyphs = &glyphs;
    h.saveReady = true; h.editSupported = true; h.loadoutNames = LOADOUT_NAMES; h.loadoutCount = 5;
    h.sdDir = sd; h.version = "1.0.0 (harness)";
    h.apply = [&]() {
        applyCalls++;
        syncSaveSlotVars(pockets, gSlot, gNewId, gNewCount);
        uint8_t out[POCKETS_BYTES]; memcpy(out, pockets, sizeof out);
        uint8_t* r = out + (gSlot - 1) * 8;                      // doSave's extra write
        r[0] = gNewId & 0xFF; r[1] = (gNewId >> 8) & 0xFF; r[4] = gNewCount & 0xFF; r[5] = (gNewCount >> 8) & 0xFF;
        memcpy(written, out, sizeof written); memcpy(writtenVals, vals, sizeof writtenVals);
        return ER_OK;
    };
    h.restore = [&]() { restoreCalls++; return ER_OK; };
    h.doctor = [&]() { doctorCalls++; return ER_OK; };
    h.checkBackup = [&](bool) { return BC_OK; };
    h.writeSessionBackup = [&]() { sessionCalls++; return true; };
    h.sessionToBackup = [&]() { return true; };
    h.info = [&](InfoData& d) {
        d.saveSizeOk = true; d.personalSize = 0x74A40; d.mainSize = 0x9B0E90;
        d.pRegOk = 2; d.pRegCount = 2; d.mRegOk = 19; d.mRegCount = 19; d.poisonAllZero = true;
        d.backupPersonal = 0x74A40; d.backupMain = 0x9B0E90; d.sessionPersonal = -1; d.sessionMain = -1;
    };
    h.keyboard = [&](const char*, const char* init) {
        if (keyboard.empty()) return std::string(init ? init : "");
        std::string s = keyboard.front(); keyboard.pop_front(); return s;
    };
    h.reloadLists = [&]() { favs.load(items, FAV_NAMES, 22, sd + "user_favorites.txt"); glyphs.build(items, (sd + "item_glyphs.txt").c_str()); stack.build(items, (sd + "nonstackable.txt").c_str(), &glyphs); };

    TTF_Init();
    g_surf = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(g_surf);
    Ui ui;
    CHECK(ui.init(ren, sd));
    ui.splash();
    shot("splash");
    ui.start(h);
    idle(ui);
    shot("pockets-start");

    const uint64_t A = HidNpadButton_A, B = HidNpadButton_B, Y = HidNpadButton_Y, L = HidNpadButton_L, R = HidNpadButton_R,
                   UP = HidNpadButton_Up, DOWN = HidNpadButton_Down, LEFT = HidNpadButton_Left, RIGHT = HidNpadButton_Right,
                   PLUS = HidNpadButton_Plus, MINUS = HidNpadButton_Minus, ZL = HidNpadButton_ZL, ZR = HidNpadButton_ZR;

    // ZL / ZR must do nothing anymore (upstream: instant restore / Doctor).
    press(ui, ZL); press(ui, ZR); press(ui, ZR);
    CHECK(restoreCalls == 0 && doctorCalls == 0);

    // Favorites: take Nook Miles Ticket with quantity 10, back to Pockets automatically.
    press(ui, R); idle(ui);
    shot("favorites");
    int nmt = -1;
    for (size_t i = 0; i < favs.list().size(); i++) if (favs.list()[i].label == "Nook Miles Ticket") nmt = (int)i;
    CHECK(nmt >= 0);
    press(ui, DOWN, nmt);
    press(ui, A); idle(ui);
    shot("favorites-menu");
    press(ui, DOWN); press(ui, A); idle(ui);                // "Take with quantity..."
    press(ui, UP); press(ui, LEFT); idle(ui);               // 1 -> 11 -> 10
    shot("quantity-stepper");
    press(ui, A); idle(ui);
    shot("pockets-holding");

    // 10 x Y from slot 1: only empty slots get filled, occupied ones untouched.
    uint8_t before[POCKETS_BYTES]; memcpy(before, pockets, sizeof before);
    press(ui, Y, 10); idle(ui);
    shot("pockets-after-10-Y");
    int filled = 0; bool occupiedKept = true;
    for (int s = 1; s <= 20; s++) {
        SlotRec b0 = readSlot(before, s), a0 = readSlot(pockets, s);
        if (b0.id != ID_EMPTY && memcmp(before + (s - 1) * 8, pockets + (s - 1) * 8, 8) != 0) occupiedKept = false;
        if (b0.id == ID_EMPTY && a0.id == idOf(items, "Nook Miles Ticket")) filled++;
    }
    CHECK(filled == 10 && occupiedKept);
    CHECK(readSlot(pockets, 4).raw == qtyToRaw(10));

    // Slot menu on an occupied slot (slot 1, golden axe).
    press(ui, UP, 12); press(ui, LEFT); idle(ui);
    press(ui, A); idle(ui);
    shot("slot-menu");
    press(ui, B); idle(ui);

    // Catalog: search "golden", take golden axe (1/slot) -> quantity clamped.
    press(ui, R, 2); press(ui, A); keyboard.push_back("golden");
    // The first A already ran the keyboard with an empty queue; type again.
    press(ui, A); idle(ui);
    shot("catalog-results");
    press(ui, DOWN); press(ui, A); idle(ui);
    shot("catalog-menu");
    press(ui, B);

    // Bells & Miles: wallet +3,000 via the stepper.
    press(ui, R); idle(ui);
    press(ui, Y); press(ui, RIGHT, 3); idle(ui);
    shot("money-stepper");
    press(ui, A); idle(ui);
    CHECK(vals[0] == 15345);
    vals[0] = 150000; press(ui, DOWN); press(ui, UP); idle(ui);   // above the game's wallet max
    shot("money-wallet-warning");

    // Loadouts
    press(ui, R); idle(ui);
    shot("loadouts");

    // System: restore is blocked while changes are pending.
    press(ui, R); idle(ui);
    shot("system-blocked");
    press(ui, A); idle(ui);
    shot("system-blocked-dialog");
    press(ui, B); idle(ui);
    CHECK(restoreCalls == 0);

    // Review and Apply (exactly one engine call; doSave's extra write is a no-op).
    press(ui, PLUS); idle(ui);
    shot("review");
    uint8_t expect[POCKETS_BYTES]; memcpy(expect, pockets, sizeof expect);
    uint32_t expectVals[4]; memcpy(expectVals, vals, sizeof expectVals);
    press(ui, A); idle(ui, 2);                              // "Apply" is focused
    shot("writing");
    idle(ui, 4);
    shot("applied-toast");
    CHECK(applyCalls == 1 && sessionCalls >= 1);
    CHECK(memcmp(written, expect, sizeof expect) == 0);
    CHECK(memcmp(writtenVals, expectVals, sizeof expectVals) == 0);

    // Now restore is allowed: danger dialog + hold A 1.5 s.
    press(ui, A); idle(ui);
    shot("danger-dialog");
    press(ui, RIGHT); idle(ui);
    for (int i = 0; i < 50; i++) { frame(ui, i == 0 ? A : 0, A); SDL_Delay(14); }
    shot("danger-hold-gauge");
    CHECK(restoreCalls == 0);                               // not yet: < 1.5 s
    for (int i = 0; i < 80 && restoreCalls == 0; i++) { frame(ui, 0, A); SDL_Delay(16); }
    frame(ui, 0, 0); idle(ui, 4);
    CHECK(restoreCalls == 1);
    shot("restored");

    // Discard: change, then minus.
    press(ui, L, 5); idle(ui);
    press(ui, DOWN, 2); press(ui, A); idle(ui);
    int clearIdx = 7;                                        // "Clear slot" is the last entry (9 entries)
    press(ui, DOWN, clearIdx + 1); press(ui, A); idle(ui);
    CHECK(slotEmpty(pockets, 3));
    press(ui, MINUS); idle(ui);
    shot("discard-dialog");
    press(ui, RIGHT); press(ui, A); idle(ui);
    CHECK(!slotEmpty(pockets, 3));

    // Quit with pending changes asks first.
    press(ui, Y); idle(ui);
    press(ui, PLUS); press(ui, LEFT, 2); idle(ui);          // Apply -> (wrap) Back -> Quit without applying
    press(ui, A); idle(ui);
    shot("quit-dialog");
    press(ui, B); idle(ui);                                  // "Stay": nothing quits, nothing is written
    CHECK(applyCalls == 1);
    CHECK(ui.frame(0, 0));

    // Favorites in two groups: the user's own first, then the built-in ones.
    press(ui, B); idle(ui);                                  // close Review
    CHECK(favs.add(idOf(items, "apple")) == Favorites::ADDED);
    CHECK(favs.add(idOf(items, "wood")) == Favorites::ADDED);
    CHECK(favs.add(idOf(items, "sea bass")) == Favorites::ADDED);
    press(ui, R); idle(ui);
    press(ui, LEFT, 6); idle(ui);                            // the panel remembers its cursor: back to page 1
    shot("favorites-groups");
    press(ui, RIGHT); idle(ui);                              // next page starts mid-group: header repeated
    shot("favorites-groups-page2");
    press(ui, LEFT); idle(ui);                               // first row of page 1 = first of YOURS, A-Z: apple
    press(ui, Y); idle(ui);
    CHECK(readSlot(pockets, 1).id == idOf(items, "golden axe"));   // taking never writes
    press(ui, Y); idle(ui);                                  // place it in the next empty slot
    {
        bool placed = false;
        for (int s = 1; s <= 20; s++) if (readSlot(pockets, s).id == idOf(items, "apple") && s != 11) placed = true;
        CHECK(placed);
    }
    shot("pockets-apple-from-yours");

    // Fatal screen (save not mounted)
    EngineHooks f; f.saveReady = false; f.fatalError = "Mount failed! Close ACNH fully first.";
    Ui ui2; ui2.init(ren, sd); ui2.start(f); idle(ui2);
    shot("fatal");
    ui2.shutdown();

    // A still held from the Homebrew Menu must not dismiss the fatal screen; a real press does.
    {
        Ui u; u.init(ren, sd); u.start(f);
        bool stays = true;
        for (int i = 0; i < 5; i++) stays = stays && u.frame(i == 0 ? A : 0, A);
        CHECK(stays);
        CHECK(u.frame(0, 0) && !u.frame(A, A));
        u.shutdown();
    }

    // ---------------- Fault injection: every engine hook can fail ----------------
    {
        uint8_t pk[POCKETS_BYTES]; memcpy(pk, loadout, sizeof pk);
        uint32_t vv[4] = { 100, 200, 300, 400 };
        int applyN = 0, restoreN = 0, doctorN = 0, checkN = 0, copyN = 0;
        EngineResult applyRes = ER_BACKUP_FAILED, doctorRes = ER_NAND_FAILED;
        BackupCheck checkRes = BC_OK;
        bool sessionOk = false, copyOk = false;
        EngineHooks x = h;
        x.pockets = pk; x.vals = vv;
        x.apply = [&]() { applyN++; return applyRes; };
        x.restore = [&]() { restoreN++; return ER_OK; };
        x.doctor = [&]() { doctorN++; return doctorRes; };
        x.checkBackup = [&](bool) { checkN++; return checkRes; };
        x.writeSessionBackup = [&]() { return sessionOk; };
        x.sessionToBackup = [&]() { copyN++; return copyOk; };
        Ui u; u.init(ren, sd); u.start(x); idle(u);
        auto clearSlot1 = [&]() { press(u, A); press(u, UP); press(u, A); idle(u); };   // cursor on slot 1: "Clear slot" is the last entry

        // Pre-edit backup fails: Cancel writes nothing; "Apply anyway" calls the engine once.
        clearSlot1();
        CHECK(slotEmpty(pk, 1));
        press(u, PLUS); press(u, A); idle(u, 4);
        shot("fault-pre-edit-backup-failed");
        press(u, B); idle(u);
        CHECK(applyN == 0);
        press(u, A); idle(u, 4); press(u, RIGHT); press(u, A); idle(u, 4);
        CHECK(applyN == 1);
        // The engine reported "BAK...": changes stay pending, Base untouched, restore blocked (I6).
        shot("fault-sd-backup-failed");
        press(u, A); press(u, B); idle(u);
        CHECK(slotEmpty(pk, 1));
        press(u, MINUS); press(u, RIGHT); press(u, A); idle(u);
        CHECK(!slotEmpty(pk, 1));                               // discard goes back to the real console state
        press(u, R, 5); press(u, A); idle(u);
        shot("fault-restore-blocked-untrusted");
        CHECK(restoreN == 0 && checkN == 0);
        press(u, A); press(u, L, 5); idle(u);

        // NAND write failed: pending kept, nothing re-based. Then a good Apply re-bases.
        sessionOk = true; applyRes = ER_NAND_FAILED;
        clearSlot1();
        press(u, PLUS); press(u, A); idle(u, 4);
        CHECK(applyN == 2);
        press(u, A); idle(u);
        applyRes = ER_OK;
        press(u, A); idle(u, 5);
        CHECK(applyN == 3 && slotEmpty(pk, 1));
        press(u, MINUS); idle(u);                               // nothing left to discard
        CHECK(slotEmpty(pk, 1));

        // Backup check fails: the restore block is never reached (I8).
        checkRes = BC_CORRUPT;
        press(u, R, 5); press(u, A); idle(u);
        CHECK(checkN == 1 && restoreN == 0);
        press(u, A); idle(u);
        // Pre-edit restore: the copy over backup_*.dat fails => nothing restored, regular restore blocked.
        checkRes = BC_OK;
        press(u, DOWN); press(u, A); idle(u); press(u, RIGHT); idle(u);
        holdA(u);
        CHECK(copyN == 1 && restoreN == 0);
        press(u, A); idle(u);
        int checksBefore = checkN;
        press(u, UP); press(u, A); idle(u);
        CHECK(restoreN == 0 && checkN == checksBefore);
        press(u, A); idle(u);
        // The copy works on a second try: restored, and the regular backup is trusted again.
        copyOk = true;
        press(u, DOWN); press(u, A); idle(u); press(u, RIGHT); idle(u);
        holdA(u);
        CHECK(copyN == 2 && restoreN == 1);
        press(u, UP); press(u, A); idle(u); press(u, RIGHT); idle(u);
        holdA(u);
        CHECK(restoreN == 2);

        // Save Doctor fails: the session is locked (I7). No edit, no Apply, no restore, no second Doctor.
        press(u, DOWN, 2); press(u, A); idle(u); press(u, RIGHT); idle(u);
        holdA(u);
        CHECK(doctorN == 1);
        shot("fault-doctor-failed");
        press(u, A); idle(u);
        press(u, A); idle(u); press(u, A); idle(u);              // Doctor again: blocked dialog, closed
        press(u, UP, 2); press(u, A); idle(u); press(u, A); idle(u);   // restore: blocked
        CHECK(doctorN == 1 && restoreN == 2);
        press(u, L, 5); press(u, DOWN); idle(u);
        uint8_t keep[POCKETS_BYTES]; memcpy(keep, pk, sizeof keep);
        press(u, A); press(u, UP); press(u, A); idle(u);         // Clear slot 2: refused
        press(u, A); idle(u);
        press(u, R, 3); press(u, Y); idle(u); press(u, A); idle(u);     // money stepper: refused
        CHECK(memcmp(keep, pk, sizeof pk) == 0 && vv[0] == 100 && applyN == 3);
        u.shutdown();
    }

    // ---------------- Read-only save (unexpected file sizes, I9) ----------------
    {
        uint8_t pk[POCKETS_BYTES]; memcpy(pk, loadout, sizeof pk);
        uint8_t keep[POCKETS_BYTES]; memcpy(keep, pk, sizeof keep);
        uint32_t vv[4] = { 100, 200, 300, 400 };
        int calls = 0;
        EngineHooks x = h;
        x.pockets = pk; x.vals = vv; x.editSupported = false;
        x.apply = [&]() { calls++; return ER_OK; };
        x.restore = [&]() { calls++; return ER_OK; };
        x.doctor = [&]() { calls++; return ER_OK; };
        x.checkBackup = [&](bool) { calls++; return BC_OK; };
        x.writeSessionBackup = [&]() { calls++; return true; };
        x.sessionToBackup = [&]() { calls++; return true; };
        Ui u; u.init(ren, sd); u.start(x); idle(u);
        shot("read-only");
        press(u, A); press(u, UP); press(u, A); idle(u); press(u, A);                    // Clear slot
        press(u, A); press(u, DOWN, 2); press(u, A); idle(u); press(u, A);               // Quantity
        press(u, R); press(u, Y); press(u, Y); idle(u); press(u, A);                     // take a favorite, place it
        press(u, R, 3); press(u, Y); idle(u); press(u, A);                               // load a loadout
        press(u, L); press(u, Y); idle(u); press(u, A);                                  // money stepper
        press(u, A); press(u, DOWN, 2); press(u, A); idle(u); press(u, A);               // Set to max
        press(u, R, 2);
        for (int row = 0; row < 3; row++) { press(u, A); idle(u); press(u, A); press(u, DOWN); }   // restore, pre-edit, Doctor
        press(u, PLUS); idle(u);
        CHECK(memcmp(keep, pk, sizeof pk) == 0 && vv[0] == 100 && vv[1] == 200 && vv[2] == 300 && vv[3] == 400);
        CHECK(calls == 0);
        u.shutdown();
    }

    ui.shutdown();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
