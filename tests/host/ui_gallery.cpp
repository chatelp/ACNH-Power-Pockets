// README screenshots: renders the REAL UI (source/pp_ui.cpp) on the host with a fake
// engine and a hand-picked pocket, and saves one clean picture per panel.
// Nothing here is a test; the checks live in ui_harness.cpp. Run through
// tests/host/ui_gallery.sh, which writes the PNG files into docs/screenshots/.
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

static SDL_Surface* g_surf = nullptr;
static std::string g_out;

static void shot(const char* name) {
    std::string p = g_out + "/" + name + ".bmp";
    SDL_SaveBMP(g_surf, p.c_str());
    printf("  %s\n", name);
}
static void frame(Ui& ui, uint64_t down, uint64_t held) { ui.frame(down, held); SDL_Delay(1); }
static void press(Ui& ui, uint64_t b, int n = 1) { for (int i = 0; i < n; i++) { frame(ui, b, b); frame(ui, 0, 0); } }
static void idle(Ui& ui, int n = 4) { for (int i = 0; i < n; i++) frame(ui, 0, 0); }
// Lets the 3-second toast expire so it doesn't sit in the picture.
static void settle(Ui& ui) { for (int i = 0; i < 160; i++) { ui.frame(0, 0); SDL_Delay(20); } }

static uint16_t idOf(const std::vector<Item>& items, const char* name) {
    for (size_t i = 0; i < items.size(); i++) if (items[i].name == name) return (uint16_t)i;
    printf("  !! item not found: %s\n", name);
    return ID_EMPTY;
}
static void put(uint8_t* p, int slot, uint16_t id, uint16_t cnt) {
    uint8_t* r = p + (slot - 1) * 8; memset(r, 0, 8);
    r[0] = id & 0xFF; r[1] = id >> 8; r[4] = cnt & 0xFF; r[5] = cnt >> 8;
}
static void clearAll(uint8_t* p) { for (int s = 1; s <= 20; s++) put(p, s, ID_EMPTY, 0); }

static const char* FAV_NAMES[] = {
    "Nook Miles Ticket", "Bell bag", "Bell voucher", "99,000 Bells", "gold roses", "cherry-blossom petal",
    "star fragment", "large star fragment", "star wand", "gold nugget", "iron nugget", "pearl", "gold bars",
    "customization kit", "golden axe", "golden net", "golden rod", "golden shovel", "golden slingshot",
    "golden watering can", "crown (No Variations)", "royal crown (No Variations)",
    "apple", "cherry", "orange", "peach", "pear", "coconut",
    "potato", "carrot", "wheat", "sugarcane", "tomato", "orange pumpkin",
    "wood", "softwood", "hardwood", "stone", "clay", "tree branch", "bamboo piece" };
static const char* LOADOUT_NAMES[] = { "Mining", "Fishing", "Terraforming", "Custom 1", "Custom 2" };

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: ui_gallery <repo-root> <out-dir>\n"); return 2; }
    const std::string root = argv[1];
    g_out = argv[2];
    const std::string sd = g_out + "/sd/";
    const char* assets[] = { "font.ttf", "icon.png", "ui_icons.png", "nonstackable.txt", "item_glyphs.png", "item_glyphs.txt", "item_stacks.txt" };
    for (const char* a : assets) copyFile((root + "/assets/" + a).c_str(), (sd + a).c_str());

    std::vector<Item> items;
    FILE* it = fopen((root + "/assets/items.txt").c_str(), "rb");
    char line[256]; uint16_t id = 0;
    while (it && fgets(line, sizeof line, it)) {
        size_t n = strlen(line);
        while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        items.push_back({ id++, line });
    }
    if (it) fclose(it);
    auto I = [&](const char* name) { return idOf(items, name); };

    Favorites favs; StackRules stack; GlyphMap glyphs;
    remove((sd + "user_favorites.txt").c_str());
    favs.load(items, FAV_NAMES, (int)(sizeof(FAV_NAMES) / sizeof(FAV_NAMES[0])), sd + "user_favorites.txt");
    favs.add(I("red roses")); favs.add(I("sea bass")); favs.add(I("wood"));
    glyphs.build(items, (sd + "item_glyphs.txt").c_str());
    StackLimits limits; limits.load((sd + "item_stacks.txt").c_str(), items.size());
    stack.build(items, (sd + "nonstackable.txt").c_str(), &glyphs, &limits);

    // The "console" pockets: a bit of everything, five free slots.
    uint8_t pockets[POCKETS_BYTES]; clearAll(pockets);
    put(pockets, 1, I("golden axe"), 0);          put(pockets, 2, I("fishing rod"), 0);
    put(pockets, 3, I("shovel"), 0);              put(pockets, 4, I("red roses"), 9);
    put(pockets, 5, I("white tulips"), 9);       put(pockets, 6, I("blue hyacinths"), 4);
    put(pockets, 7, I("cherry"), 9);             put(pockets, 8, I("peach"), 9);
    put(pockets, 9, I("sea bass"), 0);            put(pockets, 10, I("wood"), 29);
    put(pockets, 11, I("iron nugget"), 29);       put(pockets, 12, I("star fragment"), 4);
    put(pockets, 13, I("straw hat (Red)"), 0);    put(pockets, 14, I("denim jacket (Blue)"), 0);
    put(pockets, 15, I("rain boots (Yellow)"), 0);
    put(pockets, 16, I("100 turnips"), 3);                 // 4 bundles of 100
    uint32_t vals[4] = { 12345, 4800000, 230500, 98000 };

    uint8_t lo[POCKETS_BYTES];
    clearAll(lo);
    put(lo, 1, I("golden axe"), 0); put(lo, 2, I("shovel"), 0); put(lo, 3, I("iron nugget"), 29); put(lo, 4, I("wood"), 29);
    put(lo, 5, I("star fragment"), 4); put(lo, 6, I("cherry"), 9); put(lo, 11, I("pearl"), 2);
    loadoutWrite((sd + "l_0.dat").c_str(), lo);
    clearAll(lo);
    put(lo, 1, I("fishing rod"), 0); put(lo, 2, I("sea bass"), 0); put(lo, 3, I("peach"), 9);
    loadoutWrite((sd + "l_1.dat").c_str(), lo);
    for (int i = 2; i < 5; i++) remove((sd + "l_" + std::to_string(i) + ".dat").c_str());

    bool preEdit = false, applied = false;
    std::deque<std::string> keyboard;
    EngineHooks h;
    h.pockets = pockets; h.vals = vals; h.items = &items; h.favorites = &favs; h.stack = &stack; h.limits = &limits; h.glyphs = &glyphs;
    h.saveReady = true; h.editSupported = true; h.loadoutNames = LOADOUT_NAMES; h.loadoutCount = 5;
    h.sdDir = sd; h.version = "1.1.1";
    h.apply = [&]() { applied = true; return ER_OK; };
    h.restore = [&]() { return ER_OK; };
    TurnipWeek week; week.present = true; week.buy = 98; week.pattern = 1;
    week.saved.year = 2026; week.saved.month = 11; week.saved.day = 11; week.saved.hour = 9; week.saved.valid = true;
    { const uint32_t pr[14] = { 0, 0, 85, 80, 76, 72, 110, 160, 532, 201, 140, 90, 60, 50 }; memcpy(week.price, pr, sizeof pr); }
    h.turnips = [&](TurnipWeek& w) { w = week; return true; };
    h.clock = [&](GameDate& g, int& wd) { g = week.saved; g.hour = 14; wd = 3; return true; };
    h.doctor = [&]() { return ER_OK; };
    h.checkBackup = [&](bool) { return BC_OK; };
    h.writeSessionBackup = [&]() { preEdit = true; return true; };
    h.sessionToBackup = [&]() { return true; };
    h.info = [&](InfoData& d) {
        d.saveSizeOk = true; d.personalSize = 0x74A40; d.mainSize = 0x9B0E90;
        d.pRegOk = applied ? 1 : 2; d.pRegCount = 2; d.mRegOk = 19; d.mRegCount = 19; d.poisonAllZero = true;
        d.backupPersonal = d.backupMain = applied ? 1 : -1;
        d.sessionPersonal = d.sessionMain = preEdit ? 1 : -1;
    };
    h.keyboard = [&](const char*, const char* init) {
        if (keyboard.empty()) return std::string(init ? init : "");
        std::string s = keyboard.front(); keyboard.pop_front(); return s;
    };
    h.reloadLists = [&]() {};

    TTF_Init();
    g_surf = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Renderer* ren = SDL_CreateSoftwareRenderer(g_surf);
    Ui ui;
    if (!ui.init(ren, sd)) { printf("font.ttf could not be loaded\n"); return 1; }
    ui.start(h);
    idle(ui);

    const uint64_t A = HidNpadButton_A, B = HidNpadButton_B, Y = HidNpadButton_Y, R = HidNpadButton_R, L = HidNpadButton_L,
                   UP = HidNpadButton_Up, DOWN = HidNpadButton_Down, LEFT = HidNpadButton_Left, RIGHT = HidNpadButton_Right,
                   PLUS = HidNpadButton_Plus;

    // Favorites: YOURS then BUILT-IN.
    press(ui, R); idle(ui);
    shot("favorites");

    // Take Nook Miles Ticket x10 (grouped order: the user's 3 favorites come first).
    int nmt = 3;
    { std::vector<std::string> builtin;
      for (size_t i = 0; i < favs.list().size(); i++) if (favs.list()[i].builtin) builtin.push_back(favs.list()[i].label);
      for (size_t i = 0; i < builtin.size(); i++) if (builtin[i] == "Nook Miles Ticket") nmt = 3 + (int)i; }
    press(ui, DOWN, nmt); press(ui, A); press(ui, DOWN); press(ui, A);     // "Take with quantity..."
    press(ui, UP); press(ui, A); idle(ui);                                 // 1 -> 10 (tickets stack to 10), back in Pockets
    press(ui, Y, 3);                                                       // three empty slots filled
    settle(ui);
    shot("pockets");

    // Action menu of an occupied slot (slot 4, red roses).
    press(ui, LEFT); press(ui, UP, 9); press(ui, DOWN, 3); idle(ui);
    press(ui, A); idle(ui);
    shot("slot-menu");
    press(ui, B); idle(ui);

    // Catalog search: one picture per kind of item, tinted with the item's color.
    press(ui, R, 2); idle(ui);
    keyboard.push_back("sneakers");
    press(ui, A); idle(ui);
    press(ui, DOWN, 2); idle(ui);
    shot("catalog");

    // Bells & Miles: one value already changed, the number editor open on another.
    press(ui, R); idle(ui);
    press(ui, Y); press(ui, RIGHT, 5); press(ui, A); idle(ui);             // wallet + 5,000
    press(ui, DOWN, 2); press(ui, Y); press(ui, UP, 2); idle(ui);          // Nook Miles + 200,000, editor open
    shot("bells-miles");
    press(ui, A); idle(ui);

    // Turnips: the week's prices read from the save (fake clock: Wednesday afternoon).
    press(ui, R); idle(ui);
    shot("turnips");

    // Loadouts with their preview.
    press(ui, R); idle(ui);
    shot("loadouts");

    // Review: everything that would be written, before » after.
    press(ui, PLUS); idle(ui);
    shot("review");

    // Apply (fake engine), then the System panel and its hold-to-confirm dialog.
    press(ui, A); idle(ui, 8);
    settle(ui);
    press(ui, R); idle(ui);
    shot("system");
    press(ui, A); idle(ui);
    press(ui, RIGHT); idle(ui);
    for (int i = 0; i < 45; i++) { frame(ui, i == 0 ? A : 0, A); SDL_Delay(19); }   // ~0.9 s of the 1.5 s hold
    shot("danger-dialog");
    frame(ui, 0, 0); press(ui, B); idle(ui);

    (void)L;
    ui.shutdown();
    return 0;
}
