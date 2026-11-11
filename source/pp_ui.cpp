// ============================================================================
// ACNH Power Pockets - panel UI. Layout, colors and components follow
// docs/design/CHARTE.md; panels, buttons and flows follow docs/ux/UX_SPEC.md.
//
// Safety model ("BIOS"): every edit happens in memory (the 160-byte pocket
// buffer and g_vals). Nothing reaches the console save until Review > Apply,
// which calls the unchanged doSave() once through EngineHooks::apply. Restore
// and Save Doctor (direct NAND writes) live in the System panel behind a
// hold-to-confirm danger dialog and are blocked while changes are pending.
// ============================================================================
#include "pp_ui.h"
#include "pp_strings.h"

#include <switch.h>
#include "stb_image.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <algorithm>

namespace pp {

// ------------------------------- theme tokens ------------------------------
namespace C {
static const SDL_Color bgLeaf      {0x7A,0xC1,0x43,255};
static const SDL_Color bgLeafDots  {0x6F,0xB7,0x39,255};
static const SDL_Color panel       {0xF8,0xF5,0xEC,255};
static const SDL_Color panelAlt    {0xF1,0xEB,0xDD,255};
static const SDL_Color sunken      {0xE6,0xDC,0xC3,255};
static const SDL_Color lineSoft    {0xDD,0xD0,0xB4,255};
static const SDL_Color wood        {0x7A,0x5C,0x3E,255};
static const SDL_Color woodDark    {0x5C,0x44,0x30,255};
static const SDL_Color woodInk     {0xEA,0xDF,0xC8,255};
static const SDL_Color woodMute    {0xA8,0x8C,0x6C,255};
static const SDL_Color textPrimary {0x5A,0x40,0x29,255};
static const SDL_Color textSecond  {0x7A,0x5C,0x3E,255};
static const SDL_Color inkStrong   {0x3B,0x2A,0x1A,255};
static const SDL_Color selectBg    {0x3F,0x7A,0x2E,255};
static const SDL_Color selectInk   {0xF8,0xF5,0xEC,255};
static const SDL_Color tabIdle     {0x8B,0x6B,0x47,255};
static const SDL_Color modified    {0xE8,0xA3,0x3D,255};
static const SDL_Color modifiedInk {0xA6,0x5F,0x00,255};
static const SDL_Color warn        {0xF2,0xC1,0x4E,255};
static const SDL_Color warnBg      {0xFC,0xEF,0xC7,255};
static const SDL_Color warnInk     {0x7A,0x4B,0x00,255};
static const SDL_Color error       {0xC0,0x39,0x2B,255};
static const SDL_Color errorBg     {0xF9,0xE1,0xDD,255};
static const SDL_Color errorInk    {0xA3,0x2F,0x23,255};
static const SDL_Color ok          {0x2E,0x7D,0x32,255};
static const SDL_Color disabledInk {0xB8,0xAC,0x98,255};
static const SDL_Color singleBg    {0xFC,0xE3,0xC2,255};
static const SDL_Color singleInk   {0x8C,0x3A,0x10,255};
static const SDL_Color veil        {0x2A,0x1E,0x12,153};
static const SDL_Color veilLight   {0x2A,0x1E,0x12, 80};
}

static const int LIST_X = 56, LIST_Y = 192, LIST_W = 1164, ROW_H = 40, ROW_PITCH = 44, ROWS = 8;
static const int GRID_Y = 188, CELL_H = 36, CELL_PITCH = 40, CELL_W = 576;
static const int DROW_H = 64, DROW_PITCH = 68;
static const uint32_t TOAST_MS = 3000, HOLD_CONFIRM_MS = 1500;
static const int ACCEL_FRAMES = 120;                    // ~2 s at 60 fps

enum Panel { P_POCKETS, P_FAVS, P_CATALOG, P_MONEY, P_LOADOUTS, P_SYSTEM, P_COUNT };
enum ToastKind { T_OK, T_INFO, T_WARN, T_ERR };
enum Icon { IC_STAR = 0, IC_CHECK, IC_WARN, IC_LOCK, IC_LOCK_LIGHT, IC_CROSS };

static std::string fmt(const char* f, ...) {
    char buf[512];
    va_list ap; va_start(ap, f); vsnprintf(buf, sizeof buf, f, ap); va_end(ap);
    return buf;
}
static const char* plural(int n) { return n == 1 ? "" : "s"; }

// ------------------------------ modal objects ------------------------------
struct MenuEntry {
    std::string label, detail, reason;
    bool enabled = true, destructive = false, isDefault = false;
    std::function<void()> run;
};
struct DialogButton {
    std::string label;
    bool destructive = false, hold = false;
    std::function<void()> run;
};
struct ReviewRow {
    int kind = 0;           // 0 header, 1 slot, 2 value
    int slot = 0, idx = 0;
    uint16_t beforeId = ID_EMPTY, afterId = ID_EMPTY;
    std::string target, before, after;
};
struct Modal {
    enum Kind { MENU, STEPPER, DIALOG, REVIEW, WAIT } kind = MENU;
    std::string title, subtitle;
    // menu
    std::vector<MenuEntry> entries;
    int focus = 0;
    SDL_Rect anchor { 0, 0, 0, 0 };
    // stepper
    int64_t value = 0, minV = 0, maxV = 0, small = 1, big = 10, original = 0;
    bool money = false;
    std::function<void(int64_t)> onOk;
    // dialog
    std::vector<std::string> body;
    std::vector<DialogButton> buttons;
    int safe = 0;
    bool danger = false;
    uint32_t holdStart = 0;
    bool holdArmed = false;
    float holdProgress = 0.f;
    // review
    std::vector<ReviewRow> rows;
    int rowFocus = -1;      // -1: in the button row
    int top = 0;
};

struct Hand { bool valid = false; uint16_t id = ID_EMPTY; int qty = 1; };

struct Op {
    enum Kind { RECT, TEX } kind;
    SDL_Rect r;
    SDL_Color c;
    SDL_Texture* t;
    SDL_Rect src;
    bool hasSrc;
    bool mod;               // TEX only: multiply the texture by `c` (tinted glyph mask)
};

struct LoadoutInfo { bool exists = false, bad = false; int count = 0; uint8_t data[POCKETS_BYTES]; };

// =============================== implementation ============================
struct Ui::Impl {
    SDL_Renderer* ren = nullptr;
    TTF_Font *fDisplay = nullptr, *fTitle = nullptr, *fBody = nullptr, *fSmall = nullptr, *fMicro = nullptr;
    SDL_Texture *appIcon = nullptr, *icons = nullptr, *bgTex = nullptr;
    SDL_Texture *glyphTex = nullptr;      // item_glyphs.png: 24x24 cells, rows = full color / tint mask / outlines
    int glyphCols = 0;
    bool glyphTintRows = false;
    std::string sdDir;
    EngineHooks* e = nullptr;

    Base base {};
    int panel = P_POCKETS;
    int focus[P_COUNT] = { 0 };
    Hand hand;
    bool backupTrusted = true, tainted = false, sessionWritten = false, quit = false;
    bool armed = false;                   // false until the buttons held at launch are released
    std::string query;
    std::vector<int> results;
    LoadoutInfo lo[8];
    InfoData info;
    bool infoValid = false;

    std::string toast;
    int toastKind = T_INFO;
    uint32_t toastUntil = 0;

    std::vector<Modal> modals;
    std::function<void()> pendingOp;
    int waitFrames = 0;

    int rep[4] = { 0, 0, 0, 0 };
    int holdDirFrames = 0;

    std::vector<Op> ops;
    std::vector<SDL_Texture*> owned;
    bool dirty = true;
    bool gaugeOn = false;
    SDL_Rect gaugeRect { 0, 0, 0, 0 };

    // ----------------------------- data helpers ----------------------------
    bool ready() const { return e && e->saveReady && e->pockets; }
    const std::vector<Item>& items() const { static std::vector<Item> none; return (e && e->items) ? *e->items : none; }
    bool itemsLoaded() const { return !items().empty(); }
    bool single(uint16_t id) const { return e && e->stack && e->stack->isSingle(id); }
    int pending() const { return ready() ? pendingCount(base, e->pockets, e->vals) : 0; }

    std::string itemName(uint16_t id) const {
        if (id == ID_EMPTY) return S::EMPTY;
        const std::vector<Item>& it = items();
        if (id < it.size() && !it[id].name.empty()) return it[id].name;
        return fmt("ID %04X", (unsigned)id);
    }
    std::string qtyText(uint16_t id, uint16_t raw) const {
        if (single(id)) return rawToQty(raw) > 1 ? fmt("%s%d", S::TIMES, rawToQty(raw)) : std::string();
        return fmt("%s%d", S::TIMES, rawToQty(raw));
    }
    std::string slotText(const uint8_t* pockets, int slot) const {
        SlotRec r = readSlot(pockets, slot);
        if (r.id == ID_EMPTY) return S::EMPTY;
        std::string q = qtyText(r.id, r.raw);
        return q.empty() ? itemName(r.id) : itemName(r.id) + " " + q;
    }
    std::string handText() const {
        if (!hand.valid) return std::string();
        if (single(hand.id)) return itemName(hand.id) + " (" + S::BADGE_SINGLE + ")";
        return fmt("%s %s%d", itemName(hand.id).c_str(), S::TIMES, hand.qty);
    }
    bool canEdit() {
        if (!ready()) return false;
        if (!e->editSupported) { showError(S::E_RO); return false; }
        if (tainted) { showError(S::E_TAINTED); return false; }
        return true;
    }

    void setToast(int kind, const std::string& s) {
        toast = s; toastKind = kind; toastUntil = SDL_GetTicks() + TOAST_MS; dirty = true;
    }
    void showError(const std::string& msg, const char* title = S::D_ERR_TITLE) {
        Modal m; m.kind = Modal::DIALOG; m.title = title; m.body.push_back(msg);
        DialogButton b; b.label = S::B_OK; m.buttons.push_back(b); m.safe = 0; m.focus = 0;
        push(m);
    }
    void push(const Modal& m) { modals.push_back(m); dirty = true; }
    void popModal() { if (!modals.empty()) modals.pop_back(); dirty = true; }

    // ------------------------------- drawing -------------------------------
    void clearOps() {
        for (size_t i = 0; i < owned.size(); i++) SDL_DestroyTexture(owned[i]);
        owned.clear(); ops.clear();
        gaugeOn = false;
    }
    void fill(SDL_Rect r, SDL_Color c) { ops.push_back({ Op::RECT, r, c, nullptr, { 0, 0, 0, 0 }, false, false }); }
    // Chamfered corners: 2 rects (k <= 4) or a 3-step stair (k > 4). CHARTE 6.1.
    void fillRound(SDL_Rect a, int k, SDL_Color c) {
        if (k <= 0) { fill(a, c); return; }
        if (k <= 4) {
            fill({ a.x + k, a.y, a.w - 2 * k, a.h }, c);
            fill({ a.x, a.y + k, a.w, a.h - 2 * k }, c);
            return;
        }
        fill({ a.x + k, a.y, a.w - 2 * k, a.h }, c);
        fill({ a.x + k / 3, a.y + k / 3, a.w - 2 * (k / 3), a.h - 2 * (k / 3) }, c);
        fill({ a.x, a.y + k, a.w, a.h - 2 * k }, c);
    }
    void frameRect(SDL_Rect a, int t, SDL_Color c) {
        fill({ a.x, a.y, a.w, t }, c); fill({ a.x, a.y + a.h - t, a.w, t }, c);
        fill({ a.x, a.y, t, a.h }, c); fill({ a.x + a.w - t, a.y, t, a.h }, c);
    }
    int textWidth(TTF_Font* f, const std::string& s) {
        int w = 0, h = 0;
        if (f && !s.empty()) TTF_SizeUTF8(f, s.c_str(), &w, &h);
        return w;
    }
    int fontH(TTF_Font* f) { return f ? TTF_FontHeight(f) : 0; }
    // Measured truncation with an ellipsis (CHARTE 3.3), UTF-8 safe.
    std::string fit(TTF_Font* f, const std::string& s, int maxW) {
        if (textWidth(f, s) <= maxW) return s;
        std::string t = s;
        while (!t.empty()) {
            size_t n = t.size() - 1;
            while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80) n--;
            t.erase(n);
            if (textWidth(f, t + S::ELLIPSIS) <= maxW) return t + S::ELLIPSIS;
        }
        return S::ELLIPSIS;
    }
    // Returns the drawn width.
    int text(TTF_Font* f, const std::string& s, SDL_Color c, int x, int y) {
        if (!f || s.empty()) return 0;
        SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
        if (!surf) return 0;
        SDL_Texture* t = SDL_CreateTextureFromSurface(ren, surf);
        int w = surf->w, h = surf->h;
        SDL_FreeSurface(surf);
        if (!t) return 0;
        owned.push_back(t);
        ops.push_back({ Op::TEX, { x, y, w, h }, c, t, { 0, 0, 0, 0 }, false, false });
        return w;
    }
    int textR(TTF_Font* f, const std::string& s, SDL_Color c, int right, int y) {
        int w = textWidth(f, s);
        text(f, s, c, right - w, y);
        return w;
    }
    void textC(TTF_Font* f, const std::string& s, SDL_Color c, SDL_Rect r) {
        int w = textWidth(f, s);
        text(f, s, c, r.x + (r.w - w) / 2, r.y + (r.h - fontH(f)) / 2);
    }
    int textMid(TTF_Font* f, const std::string& s, SDL_Color c, int x, SDL_Rect row) {
        return text(f, s, c, x, row.y + (row.h - fontH(f)) / 2);
    }
    // Item pictures are optional: without item_glyphs.png / item_glyphs.txt the layout is
    // the plain one (names start where the picture would be).
    bool glyphsOn() const { return glyphTex && glyphCols > 0 && e && e->glyphs && e->glyphs->loaded(); }
    int nameShift() const { return glyphsOn() ? 28 : 0; }
    void glyph(uint16_t id, int x, int y) {
        if (!glyphsOn() || id == ID_EMPTY) return;
        int g = e->glyphs->glyphOf(id);
        if (g < 0 || g >= glyphCols) g = 0;
        uint32_t rgb = 0;
        if (glyphTintRows && e->glyphs->colorOf(id, rgb)) {
            SDL_Color tint = { (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255 };
            ops.push_back({ Op::TEX, { x, y, 24, 24 }, tint, glyphTex, { g * 24, 24, 24, 24 }, true, true });
            ops.push_back({ Op::TEX, { x, y, 24, 24 }, C::panel, glyphTex, { g * 24, 48, 24, 24 }, true, false });
        } else {
            ops.push_back({ Op::TEX, { x, y, 24, 24 }, C::panel, glyphTex, { g * 24, 0, 24, 24 }, true, false });
        }
    }
    void icon(int idx, int x, int y, int size = 20) {
        if (!icons) return;
        ops.push_back({ Op::TEX, { x, y, size, size }, C::panel, icons, { idx * 24, 0, 24, 24 }, true, false });
    }
    // Badge pill (CHARTE 6.6). Returns its width.
    int badge(const std::string& s, SDL_Color bg, SDL_Color ink, int x, int y) {
        int w = textWidth(fMicro, s) + 16;
        fillRound({ x, y, w, 24 }, 2, bg);
        textC(fMicro, s, ink, { x, y, w, 24 });
        return w;
    }
    int badgeWidth(const std::string& s) { return textWidth(fMicro, s) + 16; }
    int favMark(int x, int y) {          // star icon, or the FAV pill when ui_icons.png is missing
        if (icons) { icon(IC_STAR, x, y); return 20; }
        return badge(S::BADGE_FAV, C::sunken, C::textPrimary, x, y - 2);
    }
    void keycap(const char* label, int x, int y, bool round, bool off) {
        SDL_Rect r = { x, y, round ? 32 : 40, 32 };
        fillRound(r, round ? 8 : 4, off ? C::woodDark : C::panel);
        textC(fSmall, label, off ? C::woodInk : C::woodDark, r);
    }

    // --------------------------------- frame -------------------------------
    void rebuild() {
        clearOps();
        // Background + dots
        fill({ 0, 0, 1280, 720 }, C::bgLeaf);
        if (!bgTex) for (int y = 20; y < 720; y += 48) for (int x = 20; x < 1280; x += 48) fill({ x, y, 8, 8 }, C::bgLeafDots);
        fill({ 32, 24, 1216, 672 }, C::panel);
        drawHeader();
        if (!ready()) { drawFatal(); drawHelp(); drawModals(); return; }
        bool review = !modals.empty() && modals[0].kind == Modal::REVIEW;
        if (review) drawReviewScreen(modals[0]);
        else {
            drawTabs();
            switch (panel) {
                case P_POCKETS: drawPockets(); break;
                case P_FAVS: drawFavorites(); break;
                case P_CATALOG: drawCatalog(); break;
                case P_MONEY: drawMoney(); break;
                case P_LOADOUTS: drawLoadouts(); break;
                default: drawSystem(); break;
            }
        }
        drawStatus();
        drawModals();
        drawHelp();
    }

    void drawHeader() {
        fill({ 32, 24, 1216, 64 }, C::wood);
        if (appIcon) ops.push_back({ Op::TEX, { 48, 32, 48, 48 }, C::panel, appIcon, { 0, 0, 0, 0 }, false, false });
        text(fDisplay, S::APP_TITLE, C::selectInk, 112, 24 + (64 - fontH(fDisplay)) / 2);
        if (!ready()) return;
        int n = pending();
        int right = 1232;
        // Pending chip
        std::string chip = n ? fmt(S::CHIP_CHANGES_FMT, n, plural(n)) : std::string(S::CHIP_UPTODATE);
        int cw = textWidth(fSmall, chip) + (n ? 48 : (icons ? 52 : 24));
        SDL_Rect cr = { right - cw, 36, cw, 40 };
        fillRound(cr, 4, n ? C::modified : C::ok);
        if (n) {
            fill({ cr.x + 16, cr.y + 16, 8, 8 }, C::inkStrong);
            text(fSmall, chip, C::inkStrong, cr.x + 32, cr.y + (40 - fontH(fSmall)) / 2);
        } else {
            int tx = cr.x + 12;
            if (icons) { icon(IC_CHECK, tx, cr.y + 10); tx += 28; }
            text(fSmall, chip, C::selectInk, tx, cr.y + (40 - fontH(fSmall)) / 2);
        }
        // Holding
        std::string h = hand.valid ? fmt(S::HOLDING_FMT, fit(fSmall, handText(), 460).c_str()) : std::string(S::HOLDING_NONE);
        int hw = textR(fSmall, h, hand.valid ? C::selectInk : C::woodInk, cr.x - 24, 24 + (64 - fontH(fSmall)) / 2);
        if (hand.valid) glyph(hand.id, cr.x - 24 - hw - 32, 24 + 20);
    }

    bool panelHasPending(int p) const {
        if (!ready()) return false;
        if (p == P_POCKETS) return pendingPockets(base, e->pockets) > 0;
        if (p == P_MONEY) return pendingVals(base, e->vals) > 0;
        return false;
    }

    void drawTabs() {
        fill({ 32, 88, 1216, 56 }, C::woodDark);
        keycap("L", 48, 100, false, panel == 0 || !modals.empty());
        keycap("R", 1192, 100, false, panel == P_COUNT - 1 || !modals.empty());
        for (int i = 0; i < P_COUNT; i++) {
            bool act = i == panel;
            SDL_Rect r = act ? SDL_Rect{ 104 + i * 180, 92, 176, 52 } : SDL_Rect{ 104 + i * 180, 96, 176, 48 };
            fill({ r.x + 4, r.y, r.w - 8, r.h }, act ? C::panel : C::tabIdle);
            fill({ r.x, r.y + 4, r.w, r.h - 4 }, act ? C::panel : C::tabIdle);
            int tw = textWidth(fSmall, S::TAB[i]);
            int tx = r.x + (r.w - tw) / 2 - (panelHasPending(i) ? 10 : 0);
            text(fSmall, S::TAB[i], act ? C::woodDark : C::selectInk, tx, r.y + (r.h - fontH(fSmall)) / 2);
            if (panelHasPending(i)) fill({ tx + tw + 12, r.y + r.h / 2 - 4, 8, 8 }, C::modified);
        }
    }

    void contextBanner(const std::string& left, const std::string& right = std::string(), bool warnStyle = false) {
        if (warnStyle) {
            fillRound({ 56, 150, 1168, 32 }, 4, C::warnBg);
            int x = 68;
            if (icons) { icon(IC_WARN, x, 156); x += 28; }
            text(fSmall, left, C::warnInk, x, 150 + (32 - fontH(fSmall)) / 2);
            return;
        }
        text(fSmall, left, C::textSecond, 56, 152);
        if (!right.empty()) textR(fSmall, right, C::textSecond, 1224, 152);
    }
    // Degraded-mode banner (items.txt missing / read-only) takes precedence.
    bool degradedBanner() {
        if (!e->editSupported) { contextBanner(S::RO_BANNER, "", true); return true; }
        if (hashStale()) { contextBanner(S::STALE_BANNER, "", true); return true; }
        if (!itemsLoaded()) { contextBanner(S::NO_ITEMS_BANNER, "", true); return true; }
        return false;
    }

    void pager(int page, int pages) {
        if (pages <= 1) return;
        int total = pages * 16 - 8;
        int x0 = 640 - total / 2;
        for (int i = 0; i < pages; i++) fill({ x0 + i * 16, 564, 8, 8 }, i == page ? C::wood : C::lineSoft);
        textR(fSmall, fmt("Page %d/%d", page + 1, pages), C::textSecond, 1220, 556);
    }

    // List row background (CHARTE 6.3). Returns the row rect.
    SDL_Rect listRow(int i, bool focused, bool zebra, bool modifiedTab) {
        SDL_Rect r = { LIST_X, LIST_Y + i * ROW_PITCH, LIST_W, ROW_H };
        if (focused) fillRound(r, 4, C::selectBg);
        else if (zebra) fillRound(r, 4, C::panelAlt);
        if (modifiedTab) fill({ r.x, r.y, 8, r.h }, C::modified);
        return r;
    }
    SDL_Rect doubleRow(int i, int y0, int w, bool focused, bool modifiedTab) {
        SDL_Rect r = { LIST_X, y0 + i * DROW_PITCH, w, DROW_H };
        fillRound(r, 4, focused ? C::selectBg : (i % 2 ? C::panelAlt : C::panel));
        if (modifiedTab) fill({ r.x, r.y, 8, r.h }, C::modified);
        return r;
    }

    // ------------------------------- Pockets -------------------------------
    void drawPockets() {
        const uint8_t* p = e->pockets;
        if (!degradedBanner()) {
            int changed = pendingPockets(base, p);
            contextBanner(fmt("20 slots \xC2\xB7 %d used \xC2\xB7 %d changed", countItems(p), changed),
                          hand.valid ? "Y places in the next empty slot" : "Take an item in Favorites or Catalog (R)");
        }
        int f = focus[P_POCKETS];
        for (int s = 1; s <= SLOT_COUNT; s++) {
            int col = (s - 1) / 10, row = (s - 1) % 10;
            SDL_Rect r = { col ? 648 : 56, GRID_Y + row * CELL_PITCH, CELL_W, CELL_H };
            bool foc = (s - 1) == f;
            bool mod = slotModified(base, p, s);
            SlotRec rec = readSlot(p, s);
            bool empty = rec.id == ID_EMPTY;
            SDL_Color ink = foc ? C::selectInk : C::textPrimary;
            if (foc) fillRound(r, 4, C::selectBg);
            else if (empty) { fillRound(r, 4, C::panel); frameRect(r, 2, C::lineSoft); }
            else fillRound(r, 4, C::panelAlt);
            SDL_Rect pill = { r.x, r.y, 44, CELL_H };
            fillRound(pill, 4, mod ? C::modified : (foc ? C::selectBg : C::sunken));
            textC(fSmall, fmt("%d", s), mod ? C::inkStrong : (foc ? C::selectInk : C::textSecond), pill);
            if (empty) { textMid(fBody, S::EMPTY, foc ? C::selectInk : C::disabledInk, r.x + 56, r); continue; }
            // Right side: quantity, then badges to its left.
            int rx = r.x + r.w - 16;
            std::string q = qtyText(rec.id, rec.raw);
            if (!q.empty()) {
                rx -= textR(fSmall, q, foc ? C::selectInk : C::textSecond, rx, r.y + (CELL_H - fontH(fSmall)) / 2) + 8;
                if (single(rec.id)) {                       // quantity > 1 on a non-stackable: shown as-is, flagged
                    if (icons) { icon(IC_WARN, rx - 20, r.y + 8); rx -= 28; }
                }
            }
            if (single(rec.id)) { int bw = badgeWidth(S::BADGE_SINGLE); badge(S::BADGE_SINGLE, C::singleBg, C::singleInk, rx - bw, r.y + 6); rx -= bw + 8; }
            if (e->favorites && e->favorites->isFav(rec.id)) { rx -= 20; favMark(rx, r.y + 8); rx -= 8; }
            glyph(rec.id, r.x + 52, r.y + 6);
            const int nx = r.x + 56 + nameShift();
            textMid(fBody, fit(fBody, itemName(rec.id), rx - nx - 8), ink, nx, r);
        }
    }
    std::string pocketsDetail() {
        int s = focus[P_POCKETS] + 1;
        SlotRec r = readSlot(e->pockets, s);
        std::string d = fmt(S::SLOT_DETAIL_FMT, s);
        if (r.id != ID_EMPTY) {
            d += S::DOT + itemName(r.id) + S::DOT + fmt("0x%04X", (unsigned)r.id);
            std::string q = qtyText(r.id, r.raw);
            if (!q.empty()) d += S::DOT + q;
            if (single(r.id)) d += S::DOT + std::string(S::DOESNT_STACK);
        } else d += S::DOT + std::string(S::EMPTY);
        if (slotModified(base, e->pockets, s)) d += S::DOT + std::string("was ") + slotText(base.pockets, s);
        return d;
    }
    void pocketsInput(uint64_t down, uint64_t nav) {
        int& f = focus[P_POCKETS];
        int col = f / 10, row = f % 10;
        if ((nav & HidNpadButton_AnyUp) && row > 0) { f--; dirty = true; }
        if ((nav & HidNpadButton_AnyDown) && row < 9) { f++; dirty = true; }
        if ((nav & HidNpadButton_AnyLeft) && col == 1) { f -= 10; dirty = true; }
        if ((nav & HidNpadButton_AnyRight) && col == 0) { f += 10; dirty = true; }
        if (down & HidNpadButton_Y) pocketsQuick(f + 1);
        else if (down & HidNpadButton_A) pocketsMenu(f + 1);
    }
    void placeNext(int fromSlot) {
        if (!canEdit()) return;
        if (!hand.valid) { setToast(T_INFO, S::T_NOTHING_HELD); return; }
        int s = nextEmptySlot(e->pockets, fromSlot);
        if (s < 0) { setToast(T_WARN, S::T_FULL); return; }
        if (!placeInEmpty(e->pockets, s, hand.id, placeRaw(single(hand.id), hand.qty))) { setToast(T_ERR, S::T_BAD_ID); return; }
        int n = nextEmptySlot(e->pockets, s);
        focus[P_POCKETS] = (n > 0 ? n : s) - 1;
        setToast(T_INFO, fmt(S::T_PLACED_FMT, handText().c_str(), s));
    }
    void takeFromSlot(int slot) {
        SlotRec r = readSlot(e->pockets, slot);
        if (r.id == ID_EMPTY) return;
        int q = single(r.id) ? 1 : std::max(QTY_MIN, std::min(QTY_MAX, rawToQty(r.raw)));
        setHand(r.id, q, false);
    }
    void setHand(uint16_t id, int qty, bool goPockets) {
        bool clamped = single(id) && qty > 1;
        hand.valid = true; hand.id = id; hand.qty = single(id) ? 1 : std::max(QTY_MIN, std::min(QTY_MAX, qty));
        if (goPockets) panel = P_POCKETS;
        if (clamped) setToast(T_WARN, S::T_QTY_ONE);
        else setToast(T_INFO, fmt(S::T_HOLDING_FMT, handText().c_str()));
    }
    void pocketsQuick(int slot) {
        if (hand.valid) { placeNext(slot); return; }
        if (!slotEmpty(e->pockets, slot)) { takeFromSlot(slot); return; }
        setToast(T_INFO, S::T_NOTHING_HELD);
    }
    void openQtyStepper(const std::string& title, int initial, std::function<void(int64_t)> ok) {
        Modal m; m.kind = Modal::STEPPER; m.title = title;
        m.value = m.original = initial; m.minV = QTY_MIN; m.maxV = QTY_MAX; m.small = 1; m.big = 10; m.money = false;
        m.onOk = ok;
        push(m);
    }
    void pocketsMenu(int slot) {
        Modal m; m.kind = Modal::MENU;
        SlotRec r = readSlot(e->pockets, slot);
        bool occ = r.id != ID_EMPTY;
        m.title = fmt(S::SLOT_DETAIL_FMT, slot) + S::DOT + slotText(e->pockets, slot);
        int empties = countEmpty(e->pockets);
        MenuEntry pl; pl.label = S::M_PLACE_NEXT; pl.detail = hand.valid ? handText() : std::string();
        pl.enabled = hand.valid && empties > 0; pl.reason = !hand.valid ? S::R_NOTHING_HELD : S::R_NO_EMPTY;
        pl.run = [this, slot]() { placeNext(slot); };
        MenuEntry tk; tk.label = S::M_TAKE; tk.enabled = occ; tk.reason = S::R_SLOT_EMPTY;
        tk.run = [this, slot]() { takeFromSlot(slot); };
        // The default entry (first, Y keycap) is always what Y does here.
        if (hand.valid || !occ) { pl.isDefault = true; m.entries.push_back(pl); }
        else { tk.isDefault = true; m.entries.push_back(tk); }
        MenuEntry fe; fe.label = fmt(S::M_FILL_EMPTY_FMT, empties); fe.enabled = hand.valid && empties > 0;
        fe.reason = !hand.valid ? S::R_NOTHING_HELD : S::R_NO_EMPTY;
        fe.run = [this]() { confirmFill(); };
        m.entries.push_back(fe);
        if (hand.valid || !occ) m.entries.push_back(tk);
        MenuEntry qt; qt.label = S::M_QTY; qt.enabled = occ && !single(r.id);
        qt.reason = !occ ? S::R_SLOT_EMPTY : S::R_SINGLE;
        if (occ && !single(r.id)) qt.detail = qtyText(r.id, r.raw);
        qt.run = [this, slot, r]() {
            if (!canEdit()) return;
            const int shown = std::max(QTY_MIN, std::min(QTY_MAX, rawToQty(r.raw)));
            // Confirming without moving is not a change, even when the stored count is
            // outside 1..99 and was clamped for display.
            openQtyStepper(std::string(S::M_QTY) + " " + itemName(r.id), shown,
                           [this, slot, shown](int64_t v) { if ((int)v != shown) setSlotRaw(e->pockets, slot, qtyToRaw((int)v)); dirty = true; });
        };
        m.entries.push_back(qt);
        MenuEntry hq; hq.label = S::M_HELD_QTY; hq.enabled = hand.valid && !single(hand.id);
        hq.reason = !hand.valid ? S::R_NOTHING_HELD : S::R_SINGLE;
        if (hq.enabled) hq.detail = fmt("%s%d", S::TIMES, hand.qty);
        hq.run = [this]() {
            openQtyStepper(S::M_HELD_QTY, hand.qty, [this](int64_t v) { hand.qty = (int)v; dirty = true; });
        };
        m.entries.push_back(hq);
        MenuEntry id; id.label = S::M_ENTER_ID;
        id.run = [this]() {
            std::string s = e->keyboard ? e->keyboard(S::KB_ENTER_ID, "") : std::string();
            uint16_t v = 0;
            if (s.empty()) return;
            if (!parseHexId(s, v) || !validPlaceId(v)) { setToast(T_WARN, S::T_BAD_ID); return; }
            setHand(v, hand.valid ? hand.qty : 1, false);
        };
        m.entries.push_back(id);
        bool fav = occ && e->favorites && e->favorites->isFav(r.id);
        MenuEntry fv; fv.label = fav ? favRemoveLabel(r.id) : S::M_ADD_FAV;
        fv.enabled = occ && itemsLoaded(); fv.reason = !occ ? S::R_SLOT_EMPTY : S::R_NO_ITEMS;
        fv.run = [this, r, fav]() { if (fav) confirmUnfav(r.id); else addFav(r.id); };
        m.entries.push_back(fv);
        MenuEntry rv; rv.label = S::M_REVERT; rv.enabled = slotModified(base, e->pockets, slot); rv.reason = S::R_NOT_MODIFIED;
        if (rv.enabled) rv.detail = "was " + slotText(base.pockets, slot);
        rv.run = [this, slot]() { revertSlot(base, e->pockets, slot); setToast(T_INFO, S::T_REVERTED); };
        m.entries.push_back(rv);
        MenuEntry cl; cl.label = S::M_CLEAR; cl.destructive = true; cl.enabled = occ; cl.reason = S::R_SLOT_EMPTY;
        cl.run = [this, slot]() {
            if (!canEdit()) return;
            if (clearSlot(e->pockets, slot)) setToast(T_INFO, fmt(S::T_CLEARED_FMT, slot));
        };
        m.entries.push_back(cl);
        m.anchor = { (slot <= 10 ? 56 : 648), GRID_Y + ((slot - 1) % 10) * CELL_PITCH, CELL_W, CELL_H };
        push(m);
    }
    void confirmFill() {
        if (!canEdit() || !hand.valid) return;
        int n = countEmpty(e->pockets);
        if (n == 0) { setToast(T_WARN, S::T_FULL); return; }
        Modal m; m.kind = Modal::DIALOG; m.title = S::D_FILL_TITLE;
        m.body.push_back(fmt(S::D_FILL_BODY_FMT, n, plural(n), handText().c_str()));
        DialogButton no; no.label = S::B_NO;
        DialogButton yes; yes.label = S::B_FILL;
        yes.run = [this]() {
            int filled = 0;
            uint16_t raw = placeRaw(single(hand.id), hand.qty);
            for (int s = 1; s <= SLOT_COUNT; s++) if (placeInEmpty(e->pockets, s, hand.id, raw)) filled++;
            setToast(T_INFO, fmt(S::T_FILLED_FMT, filled, plural(filled)));
        };
        m.buttons.push_back(no); m.buttons.push_back(yes); m.safe = 0; m.focus = 0;
        push(m);
    }

    // ------------------------------- Favorites -----------------------------
    void addFav(uint16_t id) {
        if (!e->favorites) return;
        switch (e->favorites->add(id)) {
            case Favorites::ADDED: setToast(T_OK, S::T_FAV_ADDED); break;
            case Favorites::FULL: showError(S::T_FAV_FULL, S::D_BLOCKED_TITLE); break;
            case Favorites::IO_ERROR: setToast(T_ERR, S::T_FAV_IO); break;
            case Favorites::READ_ONLY: setToast(T_WARN, S::T_FAV_RO); break;
            default: break;
        }
    }
    void confirmUnfav(uint16_t id) {
        Modal m; m.kind = Modal::DIALOG; m.title = fmt(S::D_UNFAV_TITLE_FMT, fit(fTitle, itemName(id), 300).c_str());
        if (!e->favorites->isUserAdded(id)) m.body.push_back(S::D_HIDE_BODY);
        DialogButton no; no.label = S::B_NO;
        DialogButton yes; yes.label = S::B_REMOVE; yes.destructive = true;
        yes.run = [this, id]() {
            Favorites::Result r = e->favorites->remove(id);
            if (r == Favorites::REMOVED) {
                setToast(T_OK, S::T_FAV_REMOVED);
                int n = (int)e->favorites->list().size();
                if (focus[P_FAVS] >= n) focus[P_FAVS] = n > 0 ? n - 1 : 0;
                dirty = true;
            } else if (r == Favorites::IO_ERROR) setToast(T_ERR, S::T_FAV_IO);
            else if (r == Favorites::READ_ONLY) setToast(T_WARN, S::T_FAV_RO);
        };
        m.buttons.push_back(no); m.buttons.push_back(yes);
        push(m);
    }
    void confirmResetFavs() {
        Modal m; m.kind = Modal::DIALOG; m.title = S::D_RESET_TITLE; m.body.push_back(S::D_RESET_BODY);
        DialogButton no; no.label = S::B_NO;
        DialogButton yes; yes.label = S::B_RESTORE_DEF; yes.destructive = true;
        yes.run = [this]() {
            if (e->favorites->resetToDefaults()) { focus[P_FAVS] = 0; setToast(T_OK, S::T_FAV_RESET); }
            else setToast(T_ERR, S::T_FAV_IO);
        };
        m.buttons.push_back(no); m.buttons.push_back(yes);
        push(m);
    }
    // One row of an item list (favorites / catalog): star column, name, badges, ID.
    void itemRow(int i, uint16_t id, bool foc) {
        SDL_Rect r = listRow(i, foc, i % 2 == 1, false);
        SDL_Color ink = foc ? C::selectInk : C::textPrimary;
        bool fav = e->favorites && e->favorites->isFav(id);
        if (fav && panel == P_CATALOG) favMark(r.x + 24, r.y + 10);
        int rx = r.x + r.w - 20;
        rx -= textR(fSmall, fmt("0x%04X", (unsigned)id), foc ? C::selectInk : C::textSecond, rx, r.y + (ROW_H - fontH(fSmall)) / 2) + 16;
        if (single(id)) { int bw = badgeWidth(S::BADGE_SINGLE); badge(S::BADGE_SINGLE, C::singleBg, C::singleInk, rx - bw, r.y + 8); rx -= bw + 8; }
        glyph(id, r.x + 52, r.y + 8);
        int x = r.x + 56 + nameShift();
        textMid(fBody, fit(fBody, itemName(id), rx - x - 8), ink, x, r);
    }
    void emptyState(const std::string& a, const std::string& b) {
        textC(fBody, a, C::textSecond, { 56, 300, 1168, 40 });
        textC(fSmall, b, C::textSecond, { 56, 344, 1168, 30 });
    }
    // Favorites are shown in two groups, each A-Z: the user's own first, then the built-in
    // ones (CHARTE 6.15). The focus index runs over this order. A group header takes one
    // row, is not focusable, and is repeated with "(cont.)" when a page starts mid-group.
    std::vector<Fav> favOrder() const {
        std::vector<Fav> out;
        if (!e || !e->favorites) return out;
        const std::vector<Fav>& L = e->favorites->list();           // already A-Z
        for (size_t i = 0; i < L.size(); i++) if (!L[i].builtin) out.push_back(L[i]);
        for (size_t i = 0; i < L.size(); i++) if (L[i].builtin) out.push_back(L[i]);
        return out;
    }
    struct FavRow { int item; bool cont; };                         // item < 0: header of the next item's group
    std::vector<std::vector<FavRow> > favPages(const std::vector<Fav>& L) const {
        std::vector<std::vector<FavRow> > pages(1);
        for (int i = 0; i < (int)L.size(); i++) {
            const bool groupStart = i == 0 || L[i].builtin != L[i - 1].builtin;
            // A header and its first item stay together: both need room on the page.
            if ((int)pages.back().size() + (groupStart ? 2 : 1) > ROWS) pages.push_back(std::vector<FavRow>());
            if (groupStart || pages.back().empty()) pages.back().push_back({ -1, !groupStart });
            pages.back().push_back({ i, false });
        }
        return pages;
    }
    // Page and row of a favorite in that layout.
    static bool favLocate(const std::vector<std::vector<FavRow> >& pages, int item, int& page, int& row) {
        for (size_t p = 0; p < pages.size(); p++)
            for (size_t r = 0; r < pages[p].size(); r++)
                if (pages[p][r].item == item) { page = (int)p; row = (int)r; return true; }
        page = row = 0;
        return false;
    }
    const char* favRemoveLabel(uint16_t id) const {
        return (e->favorites && e->favorites->isUserAdded(id)) ? S::M_REMOVE_FAV : S::M_HIDE_FAV;
    }
    void drawFavorites() {
        const std::vector<Fav> L = favOrder();
        int yours = 0;
        for (size_t i = 0; i < L.size(); i++) if (!L[i].builtin) yours++;
        if (!degradedBanner())
            contextBanner(fmt("%d favorite%s \xC2\xB7 A\xE2\x80\x93Z", (int)L.size(), plural((int)L.size())), "Add your own from the Catalog");
        if (L.empty()) { emptyState(S::FAVS_EMPTY, itemsLoaded() ? S::FAVS_EMPTY_HINT : S::FAVS_RO_HINT); return; }
        const int f = std::min(focus[P_FAVS], (int)L.size() - 1);
        const std::vector<std::vector<FavRow> > pages = favPages(L);
        int page = 0, row = 0;
        favLocate(pages, f, page, row);
        for (size_t r = 0; r < pages[page].size(); r++) {
            const FavRow& fr = pages[page][r];
            if (fr.item >= 0) { itemRow((int)r, L[fr.item].id, fr.item == f); continue; }
            const bool builtin = L[pages[page][r + 1].item].builtin;   // a header is always followed by an item
            const int y = LIST_Y + (int)r * ROW_PITCH;
            fillRound({ LIST_X, y + 4, LIST_W, 32 }, 4, C::panelAlt);
            std::string label = fmt("%s \xC2\xB7 %d", builtin ? S::FAV_GROUP_BUILTIN : S::FAV_GROUP_YOURS, builtin ? (int)L.size() - yours : yours);
            if (fr.cont) label += S::FAV_GROUP_CONT;
            text(fSmall, label, C::textSecond, LIST_X + 16, y + 4 + (32 - fontH(fSmall)) / 2);
        }
        pager(page, (int)pages.size());
    }
    void takeMenu(uint16_t id, bool fromFavs) {
        Modal m; m.kind = Modal::MENU; m.title = itemName(id) + S::DOT + fmt("0x%04X", (unsigned)id);
        MenuEntry tk; tk.label = S::M_TAKE_FAV; tk.isDefault = true;
        tk.detail = single(id) ? std::string(S::BADGE_SINGLE) : fmt("%s%d", S::TIMES, hand.valid ? hand.qty : 1);
        tk.run = [this, id]() { setHand(id, hand.valid ? hand.qty : 1, true); };
        m.entries.push_back(tk);
        MenuEntry tq; tq.label = S::M_TAKE_QTY; tq.enabled = !single(id); tq.reason = S::R_SINGLE;
        tq.run = [this, id]() {
            openQtyStepper(std::string(S::M_TAKE_QTY), hand.valid ? hand.qty : 1, [this, id](int64_t v) { setHand(id, (int)v, true); });
        };
        m.entries.push_back(tq);
        bool fav = e->favorites && e->favorites->isFav(id);
        MenuEntry fv; fv.label = fav ? favRemoveLabel(id) : S::M_ADD_FAV; fv.destructive = fav;
        fv.enabled = itemsLoaded(); fv.reason = S::R_NO_ITEMS;
        fv.run = [this, id, fav]() { if (fav) confirmUnfav(id); else addFav(id); };
        m.entries.push_back(fv);
        if (fromFavs) {
            MenuEntry rs; rs.label = S::M_RESET_FAVS; rs.enabled = itemsLoaded(); rs.reason = S::R_NO_ITEMS;
            rs.run = [this]() { confirmResetFavs(); };
            m.entries.push_back(rs);
        }
        int row = focus[P_CATALOG] == 0 ? 0 : 1 + (focus[P_CATALOG] - 1) % CAT_ROWS;
        if (fromFavs) { int page = 0; favLocate(favPages(favOrder()), focus[P_FAVS], page, row); }
        m.anchor = { 56, LIST_Y + row * ROW_PITCH, 600, ROW_H };
        push(m);
    }
    void favsInput(uint64_t down, uint64_t nav) {
        const std::vector<Fav> L = favOrder();
        int n = (int)L.size();
        if (n == 0) return;                                    // no modulo by zero (upstream bug)
        int& f = focus[P_FAVS];
        if (f >= n) f = n - 1;
        if (nav & HidNpadButton_AnyUp) { f = (f + n - 1) % n; dirty = true; }
        if (nav & HidNpadButton_AnyDown) { f = (f + 1) % n; dirty = true; }
        if (nav & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {   // previous / next page
            const std::vector<std::vector<FavRow> > pages = favPages(L);
            int page = 0, row = 0;
            favLocate(pages, f, page, row);
            const int to = page + ((nav & HidNpadButton_AnyRight) ? 1 : -1);
            if (to >= 0 && to < (int)pages.size())
                for (size_t r = 0; r < pages[to].size(); r++) if (pages[to][r].item >= 0) { f = pages[to][r].item; break; }
            dirty = true;
        }
        if (down & HidNpadButton_Y) setHand(L[f].id, hand.valid ? hand.qty : 1, true);
        else if (down & HidNpadButton_A) takeMenu(L[f].id, true);
    }

    // -------------------------------- Catalog ------------------------------
    static const int CAT_ROWS = ROWS - 1;                      // row 0 is the search field
    void drawCatalog() {
        if (!degradedBanner()) {
            if (query.empty()) contextBanner(S::SEARCH_EMPTY);
            else contextBanner(fmt(S::CTX_RESULTS_FMT, (int)results.size(), plural((int)results.size()), query.c_str()));
        }
        int f = focus[P_CATALOG];
        SDL_Rect sr = { LIST_X, LIST_Y, LIST_W, ROW_H };
        fillRound(sr, 4, C::sunken);
        frameRect(sr, f == 0 ? 3 : 2, f == 0 ? C::selectBg : C::wood);
        textMid(fSmall, S::SEARCH_LABEL, C::textSecond, sr.x + 24, sr);
        textMid(fBody, fit(fBody, query, 800), C::textPrimary, sr.x + 104, sr);
        textR(fSmall, S::SEARCH_HINT, C::textSecond, sr.x + sr.w - 20, sr.y + (ROW_H - fontH(fSmall)) / 2);
        if (!itemsLoaded()) return;
        if (!query.empty() && results.empty()) { emptyState(fmt(S::SEARCH_NONE_FMT, query.c_str()), S::SEARCH_EMPTY); return; }
        int sel = f - 1;
        int page = sel >= 0 ? sel / CAT_ROWS : 0;
        int pages = ((int)results.size() + CAT_ROWS - 1) / CAT_ROWS;
        for (int i = page * CAT_ROWS; i < std::min((int)results.size(), page * CAT_ROWS + CAT_ROWS); i++)
            itemRow(1 + i - page * CAT_ROWS, (uint16_t)results[i], i == sel);
        pager(page, pages);
    }
    void runSearch() {
        if (!e->keyboard) return;
        std::string q = e->keyboard(S::KB_SEARCH, query.c_str());
        if (q == query) return;                                  // cancelled or unchanged: keep the list and the cursor
        query = q;
        results = searchItems(items(), query);
        focus[P_CATALOG] = results.empty() ? 0 : 1;
        dirty = true;
    }
    void catalogInput(uint64_t down, uint64_t nav) {
        int& f = focus[P_CATALOG];
        int n = 1 + (int)results.size();
        if (nav & HidNpadButton_AnyUp) { f = (f + n - 1) % n; dirty = true; }
        if (nav & HidNpadButton_AnyDown) { f = (f + 1) % n; dirty = true; }
        if ((nav & HidNpadButton_AnyRight) && f > 0) { f = std::min(n - 1, f + CAT_ROWS); dirty = true; }
        if ((nav & HidNpadButton_AnyLeft) && f > 0) { f = std::max(1, f - CAT_ROWS); dirty = true; }
        if (!(down & (HidNpadButton_A | HidNpadButton_Y))) return;
        if (f == 0) { if (itemsLoaded()) runSearch(); return; }
        uint16_t id = (uint16_t)results[f - 1];
        if (down & HidNpadButton_Y) setHand(id, hand.valid ? hand.qty : 1, true);
        else takeMenu(id, false);
    }

    // ----------------------------- Bells & Miles ---------------------------
    void drawMoney() {
        if (!degradedBanner()) contextBanner("Bounds as in upstream \xC2\xB7 nothing is written until Apply");
        int f = focus[P_MONEY];
        for (int i = 0; i < 4; i++) {
            bool foc = i == f, mod = valModified(base, e->vals, i);
            SDL_Rect r = doubleRow(i, LIST_Y, LIST_W, foc, mod);
            SDL_Color ink = foc ? C::selectInk : C::textPrimary;
            text(fBody, S::VAL_NAME[i], ink, r.x + 32, r.y + 6);
            bool warnW = i == 0 && e->vals[0] > WALLET_GAME_MAX;
            std::string sub = fmt("0 \xE2\x80\x93 %s", formatThousands(valMax(i)).c_str());
            int sx = r.x + 32;
            if (warnW) {
                if (icons) { icon(IC_WARN, sx, r.y + 38); sx += 28; }
                text(fSmall, S::WALLET_WARN, foc ? C::selectInk : C::warnInk, sx, r.y + 36);
            } else text(fSmall, sub, foc ? C::selectInk : C::textSecond, sx, r.y + 36);
            std::string v = formatThousands(e->vals[i]);
            if (mod) v = formatThousands(base.vals[i]) + S::ARROW + v;
            textR(fBody, v, foc ? C::selectInk : (mod ? C::modifiedInk : C::textPrimary), r.x + r.w - 28, r.y + (DROW_H - fontH(fBody)) / 2);
        }
    }
    void openMoneyStepper(int i) {
        if (!canEdit()) return;
        Modal m; m.kind = Modal::STEPPER; m.title = S::VAL_NAME[i];
        m.value = e->vals[i]; m.original = base.vals[i]; m.minV = 0; m.maxV = valMax(i);
        m.small = 1000; m.big = 100000; m.money = true;
        m.onOk = [this, i](int64_t v) { e->vals[i] = clampVal(i, v); dirty = true; };
        push(m);
    }
    void moneyMenu(int i) {
        Modal m; m.kind = Modal::MENU; m.title = std::string(S::VAL_NAME[i]) + S::DOT + formatThousands(e->vals[i]);
        MenuEntry md; md.label = S::M_MODIFY; md.isDefault = true; md.run = [this, i]() { openMoneyStepper(i); };
        m.entries.push_back(md);
        MenuEntry ty; ty.label = S::M_TYPE_VALUE;
        ty.run = [this, i]() {
            if (!canEdit() || !e->keyboard) return;
            std::string s = e->keyboard(S::VAL_NAME[i], formatThousands(e->vals[i]).c_str());
            std::string d;
            for (size_t k = 0; k < s.size(); k++) {
                if (s[k] >= '0' && s[k] <= '9') d += s[k];
                else if (s[k] != ',' && s[k] != ' ' && s[k] != '.') { setToast(T_WARN, S::T_BAD_VALUE); return; }
            }
            if (d.empty() || d.size() > 12) { setToast(T_WARN, S::T_BAD_VALUE); return; }
            e->vals[i] = clampVal(i, (int64_t)strtoll(d.c_str(), nullptr, 10));
            dirty = true;
        };
        m.entries.push_back(ty);
        MenuEntry mx; mx.label = S::M_SET_MAX; mx.detail = formatThousands(valMax(i));
        mx.run = [this, i]() { if (canEdit()) { e->vals[i] = valMax(i); dirty = true; } };
        m.entries.push_back(mx);
        MenuEntry z; z.label = i == 3 ? S::M_PAY_LOAN : S::M_SET_ZERO;
        z.run = [this, i]() { if (canEdit()) { e->vals[i] = 0; dirty = true; } };
        m.entries.push_back(z);
        MenuEntry rv; rv.label = S::M_REVERT; rv.enabled = valModified(base, e->vals, i); rv.reason = S::R_NOT_MODIFIED;
        if (rv.enabled) rv.detail = "was " + formatThousands(base.vals[i]);
        rv.run = [this, i]() { revertVal(base, e->vals, i); setToast(T_INFO, S::T_REVERTED); };
        m.entries.push_back(rv);
        m.anchor = { 56, LIST_Y + i * DROW_PITCH, 600, DROW_H };
        push(m);
    }
    void moneyInput(uint64_t down, uint64_t nav) {
        int& f = focus[P_MONEY];
        if (nav & HidNpadButton_AnyUp) { f = (f + 3) % 4; dirty = true; }
        if (nav & HidNpadButton_AnyDown) { f = (f + 1) % 4; dirty = true; }
        if (down & HidNpadButton_Y) openMoneyStepper(f);
        else if (down & HidNpadButton_A) moneyMenu(f);
    }

    // ------------------------------- Loadouts ------------------------------
    std::string loPath(int i) const { return e->sdDir + fmt("l_%d.dat", i); }
    void refreshLoadouts() {
        for (int i = 0; i < e->loadoutCount && i < 8; i++) {
            lo[i].exists = fileExists(loPath(i).c_str());
            lo[i].count = 0;
            lo[i].bad = false;
            if (lo[i].exists && loadoutRead(loPath(i).c_str(), lo[i].data)) lo[i].count = countItems(lo[i].data);
            else if (lo[i].exists) lo[i].bad = true;             // truncated or not a pocket snapshot
        }
    }
    void drawLoadouts() {
        if (!degradedBanner()) contextBanner("5 pocket setups on the SD card \xC2\xB7 loading one is a pending change");
        int f = focus[P_LOADOUTS];
        for (int i = 0; i < e->loadoutCount && i < 6; i++) {
            bool foc = i == f;
            SDL_Rect r = doubleRow(i, LIST_Y, 560, foc, false);
            SDL_Color ink = foc ? C::selectInk : C::textPrimary;
            text(fBody, e->loadoutNames[i], ink, r.x + 24, r.y + 6);
            text(fSmall, lo[i].bad ? std::string(S::LO_BAD) : lo[i].exists ? fmt(S::LO_SAVED_FMT, lo[i].count) : std::string(S::LO_EMPTY),
                 foc ? C::selectInk : (lo[i].bad ? C::warnInk : lo[i].exists ? C::textSecond : C::disabledInk), r.x + 24, r.y + 36);
        }
        SDL_Rect pv = { 640, LIST_Y, 584, 392 };
        fillRound(pv, 8, C::sunken);
        if (f < 0 || f >= e->loadoutCount) return;
        text(fTitle, e->loadoutNames[f], C::textPrimary, pv.x + 20, pv.y + 12);
        if (!lo[f].exists) { text(fSmall, S::LO_EMPTY, C::disabledInk, pv.x + 20, pv.y + 56); return; }
        if (lo[f].bad) { text(fSmall, S::LO_BAD, C::warnInk, pv.x + 20, pv.y + 56); return; }
        for (int s = 1; s <= SLOT_COUNT; s++) {
            int col = (s - 1) / 10, row = (s - 1) % 10;
            int x = pv.x + 20 + col * 284, y = pv.y + 56 + row * 32;
            SlotRec r = readSlot(lo[f].data, s);
            SDL_Color c = r.id == ID_EMPTY ? C::disabledInk : C::textPrimary;
            text(fSmall, fmt("%2d", s), C::textSecond, x, y);
            glyph(r.id, x + 30, y);
            text(fSmall, fit(fSmall, slotText(lo[f].data, s), 236 - nameShift()), c, x + 32 + nameShift(), y);
        }
    }
    void loadLoadout(int i) {
        if (!canEdit()) return;
        uint8_t tmp[POCKETS_BYTES];
        if (!loadoutRead(loPath(i).c_str(), tmp)) { setToast(T_ERR, S::T_LO_BAD); return; }   // I12
        memcpy(e->pockets, tmp, POCKETS_BYTES);
        panel = P_POCKETS;
        setToast(T_INFO, fmt(S::T_LO_LOADED_FMT, e->loadoutNames[i]));
    }
    void loadoutsMenu(int i) {
        Modal m; m.kind = Modal::MENU; m.title = e->loadoutNames[i];
        MenuEntry ld; ld.label = S::M_LOAD_LO; ld.isDefault = true; ld.enabled = lo[i].exists && !lo[i].bad;
        ld.reason = lo[i].bad ? S::LO_BAD : S::LO_EMPTY;
        ld.run = [this, i]() { askLoadLoadout(i); };
        m.entries.push_back(ld);
        MenuEntry sv; sv.label = S::M_SAVE_LO;
        sv.run = [this, i]() {
            auto doSaveLo = [this, i]() {
                if (loadoutWrite(loPath(i).c_str(), e->pockets)) setToast(T_OK, fmt(S::T_LO_SAVED_FMT, e->loadoutNames[i]));
                else setToast(T_ERR, S::T_LO_WRITE);
                refreshLoadouts();
            };
            if (!lo[i].exists) { doSaveLo(); return; }
            Modal d; d.kind = Modal::DIALOG; d.title = fmt(S::D_LO_REPLACE_FMT, e->loadoutNames[i]);
            d.body.push_back(S::D_LO_REPLACE_BODY);
            DialogButton no; no.label = S::B_NO;
            DialogButton yes; yes.label = S::B_REPLACE; yes.run = doSaveLo;
            d.buttons.push_back(no); d.buttons.push_back(yes);
            push(d);
        };
        m.entries.push_back(sv);
        MenuEntry dl; dl.label = S::M_DELETE_LO; dl.destructive = true; dl.enabled = lo[i].exists; dl.reason = S::LO_EMPTY;
        dl.run = [this, i]() {
            Modal d; d.kind = Modal::DIALOG; d.title = fmt(S::D_LO_DEL_FMT, e->loadoutNames[i]);
            DialogButton no; no.label = S::B_NO;
            DialogButton yes; yes.label = S::B_DELETE; yes.destructive = true;
            yes.run = [this, i]() { ::remove(loPath(i).c_str()); refreshLoadouts(); setToast(T_INFO, S::T_LO_DELETED); };
            d.buttons.push_back(no); d.buttons.push_back(yes);
            push(d);
        };
        m.entries.push_back(dl);
        m.anchor = { 56, LIST_Y + i * DROW_PITCH, 560, DROW_H };
        push(m);
    }
    void askLoadLoadout(int i) {
        if (!canEdit()) return;
        if (pendingPockets(base, e->pockets) == 0) { loadLoadout(i); return; }
        Modal d; d.kind = Modal::DIALOG; d.title = fmt(S::D_LO_LOAD_FMT, e->loadoutNames[i]);
        d.body.push_back(S::D_LO_LOAD_BODY);
        DialogButton no; no.label = S::B_CANCEL;
        DialogButton yes; yes.label = S::B_LOAD; yes.run = [this, i]() { loadLoadout(i); };
        d.buttons.push_back(no); d.buttons.push_back(yes);
        push(d);
    }
    void loadoutsInput(uint64_t down, uint64_t nav) {
        int n = std::min(e->loadoutCount, 6);
        if (n <= 0) return;
        int& f = focus[P_LOADOUTS];
        if (nav & HidNpadButton_AnyUp) { f = (f + n - 1) % n; dirty = true; }
        if (nav & HidNpadButton_AnyDown) { f = (f + 1) % n; dirty = true; }
        if (down & HidNpadButton_Y) { if (lo[f].exists) askLoadLoadout(f); }
        else if (down & HidNpadButton_A) loadoutsMenu(f);
    }

    // -------------------------------- System -------------------------------
    enum SysRow { SR_RESTORE, SR_SESSION, SR_DOCTOR, SR_RELOAD, SR_QUIT, SR_COUNT };
    // Why a NAND action is blocked right now (empty = allowed). Invariants I5-I7, I9.
    std::string blockedReason(int row) {
        int n = pending();
        if (!e->editSupported) return S::BLOCKED_RO;
        if (tainted) return S::BLOCKED_TAINT;
        if (n > 0) return fmt(S::BLOCKED_FMT, n, plural(n));
        if (row == SR_RESTORE && !backupTrusted) return S::BLOCKED_TRUST;
        if (row == SR_SESSION && !sessionWritten) return S::BLOCKED_SESSION;
        return std::string();
    }
    void refreshInfo() { if (e->info) { e->info(info); infoValid = true; } }
    // True when the save on the console has a region hash that doesn't match its data: the
    // game refuses such a save. Apply and the Save Doctor both write consistent hashes.
    bool hashStale() {
        if (!ready() || !e->editSupported || !e->info) return false;
        if (!infoValid) refreshInfo();
        return info.pRegOk < info.pRegCount || info.mRegOk < info.mRegCount;
    }
    void drawSystem() {
        fillRound({ 56, 150, 1168, 40 }, 4, C::warnBg);
        int bx = 68;
        if (icons) { icon(IC_WARN, bx, 160); bx += 28; }
        text(fSmall, S::SYS_BANNER, C::warnInk, bx, 150 + (40 - fontH(fSmall)) / 2);
        const char* titles[SR_COUNT] = { S::SYS_RESTORE, S::SYS_SESSION, S::SYS_DOCTOR, S::SYS_RELOAD, S::SYS_QUIT };
        const char* subs[SR_COUNT] = { S::SYS_RESTORE_SUB, S::SYS_SESSION_SUB, S::SYS_DOCTOR_SUB, S::SYS_RELOAD_SUB, S::SYS_QUIT_SUB };
        int f = focus[P_SYSTEM];
        for (int i = 0; i < SR_COUNT; i++) {
            bool foc = i == f, danger = i <= SR_DOCTOR;
            SDL_Rect r = doubleRow(i, 204, 600, foc, false);
            std::string why = danger ? blockedReason(i) : std::string();
            SDL_Color ink = foc ? C::selectInk : (danger ? C::error : C::textPrimary);
            int w = text(fBody, titles[i], ink, r.x + 24, r.y + 6);
            if (danger) badge(S::BADGE_WRITES, C::errorBg, C::errorInk, r.x + 24 + w + 12, r.y + 8);
            if (!why.empty()) {
                text(fSmall, why, foc ? C::selectInk : C::warnInk, r.x + 24, r.y + 36);
                if (icons) icon(foc ? IC_LOCK_LIGHT : IC_LOCK, r.x + r.w - 36, r.y + 22);
            } else text(fSmall, subs[i], foc ? C::selectInk : C::textSecond, r.x + 24, r.y + 36);
        }
        // Info pane (read-only diagnostics)
        SDL_Rect pv = { 680, 204, 544, 384 };
        fillRound(pv, 8, C::sunken);
        text(fTitle, S::INFO_TITLE, C::textPrimary, pv.x + 20, pv.y + 12);
        if (!infoValid) refreshInfo();
        std::vector<std::pair<std::string, std::string>> L;
        L.push_back({ "Version", e->version });
        L.push_back({ "Save files", info.saveSizeOk ? "3.0.x layout (supported)" : "unexpected size (read-only)" });
        L.push_back({ "Region hashes", fmt("personal %d/%d \xC2\xB7 main %d/%d", info.pRegOk, info.pRegCount, info.mRegOk, info.mRegCount) });
        L.push_back({ "Heal fields", info.poisonAllZero ? "already 0 (no-op)" : "non-zero (zeroed on Apply)" });
        L.push_back({ "SD backup", (info.backupPersonal >= 0 && info.backupMain >= 0) ? std::string(backupTrusted ? "present" : "present, NOT trusted") : std::string("none") });
        L.push_back({ "Pre-edit backup", (info.sessionPersonal >= 0 && info.sessionMain >= 0) ? std::string(sessionWritten ? "ready (before this app's first write)" : "from an older save state") : std::string("none") });
        std::string names = fmt("%d names", (int)items().size());
        if (glyphsOn()) {
            names += fmt(" \xC2\xB7 %d pictures", e->glyphs->glyphCount());
            if (e->glyphs->ignoredLines()) names += fmt(" (%d rule%s ignored)", e->glyphs->ignoredLines(), plural(e->glyphs->ignoredLines()));
        } else names += " \xC2\xB7 no pictures";
        L.push_back({ "Items", names });
        int nf = e->favorites ? (int)e->favorites->list().size() : 0;
        std::string favs = fmt("%d", nf);
        if (e->favorites && e->favorites->missingBuiltins()) favs += fmt(" (%d built-in not found)", e->favorites->missingBuiltins());
        if (e->favorites && e->favorites->ignoredLines()) favs += fmt(" \xC2\xB7 %d line%s ignored", e->favorites->ignoredLines(), plural(e->favorites->ignoredLines()));
        L.push_back({ "Favorites", favs });
        std::string ns = e->stack ? fmt("%d items", e->stack->singleCount()) : std::string("-");
        if (e->stack && e->stack->fileIgnored()) ns += fmt(" \xC2\xB7 %d rule%s ignored", e->stack->fileIgnored(), plural(e->stack->fileIgnored()));
        L.push_back({ "1/slot", ns });
        for (size_t i = 0; i < L.size(); i++) {
            int y = pv.y + 56 + (int)i * 34;
            text(fSmall, L[i].first, C::textSecond, pv.x + 20, y);
            text(fSmall, fit(fSmall, L[i].second, 340), C::textPrimary, pv.x + 184, y);
        }
    }
    void systemInput(uint64_t down, uint64_t nav) {
        int& f = focus[P_SYSTEM];
        if (nav & HidNpadButton_AnyUp) { f = (f + SR_COUNT - 1) % SR_COUNT; dirty = true; }
        if (nav & HidNpadButton_AnyDown) { f = (f + 1) % SR_COUNT; dirty = true; }
        if (!(down & HidNpadButton_A)) return;                   // Y does nothing here on purpose
        switch (f) {
            case SR_RESTORE: askRestore(false); break;
            case SR_SESSION: askRestore(true); break;
            case SR_DOCTOR: askDoctor(); break;
            case SR_RELOAD:
                if (e->reloadLists) e->reloadLists();
                infoValid = false;
                setToast(T_OK, S::T_RELOADED);
                if (e->favorites && e->favorites->ignoredLines())
                    setToast(T_WARN, fmt(S::T_FAV_IGNORED_FMT, e->favorites->ignoredLines(), plural(e->favorites->ignoredLines())));
                break;
            default: requestQuit(); break;
        }
    }
    void blockedDialog(const std::string& why) { showError(why, S::D_BLOCKED_TITLE); }
    static const char* backupText(BackupCheck c) {
        switch (c) {
            case BC_MISSING: return S::BK_MISSING;
            case BC_SIZE: return S::BK_SIZE;
            case BC_READ: return S::BK_READ;
            case BC_CORRUPT: return S::BK_CORRUPT;
            case BC_NOMEM: return S::BK_NOMEM;
            case BC_STALE_HASH: return S::BK_STALE_HASH;
            default: return "";
        }
    }
    void dangerDialog(const char* subtitle, const char* b1, const char* b2, const char* holdLabel, std::function<void()> run) {
        Modal m; m.kind = Modal::DIALOG; m.danger = true; m.title = S::DANGER_TITLE; m.subtitle = subtitle;
        m.body.push_back(b1); m.body.push_back(b2);
        DialogButton c; c.label = S::B_CANCEL;
        DialogButton h; h.label = holdLabel; h.destructive = true; h.hold = true; h.run = run;
        m.buttons.push_back(c); m.buttons.push_back(h); m.safe = 0; m.focus = 0;
        push(m);
    }
    void askRestore(bool session) {
        std::string why = blockedReason(session ? SR_SESSION : SR_RESTORE);
        if (!why.empty()) { blockedDialog(why); return; }
        BackupCheck c = e->checkBackup ? e->checkBackup(session) : BC_MISSING;           // I8
        if (c != BC_OK) { blockedDialog(backupText(c)); return; }
        if (session) dangerDialog(S::D_SESSION_SUB, S::D_SESSION_B1, S::D_SESSION_B2, S::B_HOLD_RESTORE, [this]() { runRestore(true); });
        else dangerDialog(S::D_RESTORE_SUB, S::D_RESTORE_B1, S::D_RESTORE_B2, S::B_HOLD_RESTORE, [this]() { runRestore(false); });
    }
    bool ensureSessionBackup() {
        if (!e->writeSessionBackup) return false;
        bool ok = e->writeSessionBackup();
        if (ok) { sessionWritten = true; infoValid = false; }
        return ok;
    }
    // Runs `go` once the pre-edit backup is on the SD card. If it can't be written (card
    // full...), the user decides: a rescue must not be refused for lack of SD space.
    void afterPreEditBackup(const char* body, const char* anywayLabel, std::function<void()> go) {
        if (ensureSessionBackup()) { go(); return; }
        Modal d; d.kind = Modal::DIALOG; d.title = S::D_SESSION_FAIL_TITLE; d.body.push_back(body);
        DialogButton c; c.label = S::B_CANCEL;
        DialogButton a; a.label = anywayLabel; a.destructive = true;
        a.run = [this, go]() { withWait(go); };
        d.buttons.push_back(c); d.buttons.push_back(a);
        push(d);
    }
    void restoreNow() {
        EngineResult r = e->restore();
        infoValid = false;
        if (r == ER_OK) { snapshotBase(base, e->pockets, e->vals); setToast(T_OK, S::T_RESTORED); }
        else if (r == ER_RESTORE_MISSING) showError(S::BK_MISSING);
        else if (r == ER_RESTORE_SIZE) showError(S::BK_SIZE);
        else if (r == ER_RESTORE_READ) showError(S::BK_READ);
        else showError(S::E_RESTORE_FAILED);
    }
    void runRestore(bool session) {
        withWait([this, session]() {
            if (session) {
                // The copy overwrites backup_*.dat: if it stops half-way the regular backup is a
                // mix of two states (each file valid on its own), so it must not be offered (I6).
                infoValid = false;
                if (!e->sessionToBackup || !e->sessionToBackup()) { backupTrusted = false; showError(S::E_SESSION_TO_BACKUP); return; }
                BackupCheck c = e->checkBackup(false);
                if (c != BC_OK) { backupTrusted = false; showError(backupText(c)); return; }
                backupTrusted = true;
                restoreNow();
                return;
            }
            afterPreEditBackup(S::D_SESSION_FAIL_DIRECT, S::B_RESTORE_ANYWAY, [this]() { restoreNow(); });
        });
    }
    void askDoctor() {
        std::string why = blockedReason(SR_DOCTOR);
        if (!why.empty()) { blockedDialog(why); return; }
        dangerDialog(S::D_DOCTOR_SUB, S::D_DOCTOR_B1, S::D_DOCTOR_B2, S::B_HOLD_REPAIR, [this]() {
            withWait([this]() {
                afterPreEditBackup(S::D_SESSION_FAIL_DIRECT, S::B_REPAIR_ANYWAY, [this]() {
                    EngineResult r = e->doctor();
                    infoValid = false;
                    if (r == ER_OK || r == ER_OK_NO_FORENSIC) {
                        snapshotBase(base, e->pockets, e->vals);
                        showError(r == ER_OK ? S::D_REPAIRED_BODY : S::D_REPAIRED_NOFOR, S::D_REPAIRED_TITLE);
                    } else if (r == ER_HASH) {
                        showError(S::E_HASH);                        // stopped before the repair pass: nothing written
                    } else {
                        tainted = true;                              // I7: origP no longer matches the NAND
                        showError(S::E_DOCTOR_FAILED, S::D_DOCTOR_FAILED_TITLE);
                    }
                });
            });
        });
    }

    // ---------------------------- Review / Apply ---------------------------
    void buildReviewRows(Modal& m) {
        m.rows.clear();
        int np = pendingPockets(base, e->pockets), nv = pendingVals(base, e->vals);
        if (np) {
            ReviewRow h; h.kind = 0; h.target = fmt("%s \xC2\xB7 %d", S::REVIEW_GROUP_POCKETS, np); m.rows.push_back(h);
            for (int s = 1; s <= SLOT_COUNT; s++) if (slotModified(base, e->pockets, s)) {
                ReviewRow r; r.kind = 1; r.slot = s; r.target = fmt(S::SLOT_DETAIL_FMT, s);
                r.before = slotText(base.pockets, s); r.after = slotText(e->pockets, s);
                r.beforeId = readSlot(base.pockets, s).id; r.afterId = readSlot(e->pockets, s).id;
                if (r.before == r.after) r.after += " (attributes)";
                m.rows.push_back(r);
            }
        }
        if (nv) {
            ReviewRow h; h.kind = 0; h.target = fmt("%s \xC2\xB7 %d", S::REVIEW_GROUP_MONEY, nv); m.rows.push_back(h);
            for (int i = 0; i < 4; i++) if (valModified(base, e->vals, i)) {
                ReviewRow r; r.kind = 2; r.idx = i; r.target = S::VAL_NAME[i];
                r.before = formatThousands(base.vals[i]); r.after = formatThousands(e->vals[i]);
                m.rows.push_back(r);
            }
        }
        m.buttons.clear();
        if (np + nv > 0) {
            DialogButton a; a.label = S::B_APPLY; a.run = [this]() { startApply(false); };
            DialogButton aq; aq.label = S::B_APPLY_QUIT; aq.run = [this]() { startApply(true); };
            DialogButton d; d.label = S::B_DISCARD_ALL; d.destructive = true; d.run = [this]() { askDiscard(); };
            DialogButton q; q.label = S::B_QUIT_NOAPPLY; q.run = [this]() { requestQuit(); };
            DialogButton b; b.label = S::B_BACK;
            m.buttons = { a, aq, d, q, b };
            m.safe = 4;
        } else {
            DialogButton q; q.label = S::B_QUIT; q.run = [this]() { quit = true; };
            DialogButton b; b.label = S::B_BACK;
            m.buttons = { q, b };
            m.safe = 1;
        }
        m.focus = 0;
        m.rowFocus = -1;
        m.top = 0;
    }
    void openReview() {
        Modal m; m.kind = Modal::REVIEW;
        buildReviewRows(m);
        modals.clear();
        push(m);
    }
    void drawReviewScreen(Modal& m) {
        int n = pending();
        fill({ 32, 88, 1216, 56 }, C::woodDark);
        text(fSmall, fmt(S::REVIEW_BANNER_FMT, n, plural(n)), C::woodInk, 56, 88 + (56 - fontH(fSmall)) / 2);
        if (m.rows.empty()) {
            emptyState(S::REVIEW_NONE, "");
        } else {
            const int VIS = 7;
            if (m.rowFocus >= 0) {
                if (m.rowFocus < m.top) m.top = m.rowFocus;
                if (m.rowFocus >= m.top + VIS) m.top = m.rowFocus - VIS + 1;
            }
            for (int i = m.top; i < std::min((int)m.rows.size(), m.top + VIS); i++) {
                const ReviewRow& r = m.rows[i];
                int y = 156 + (i - m.top) * ROW_PITCH;
                SDL_Rect rr = { LIST_X, y, LIST_W, ROW_H };
                if (r.kind == 0) {
                    fillRound({ LIST_X, y + 4, LIST_W, 32 }, 4, C::panelAlt);
                    text(fSmall, r.target, C::textSecond, LIST_X + 16, y + 4 + (32 - fontH(fSmall)) / 2);
                    continue;
                }
                bool foc = i == m.rowFocus;
                if (foc) fillRound(rr, 4, C::selectBg);
                fill({ rr.x, rr.y, 8, rr.h }, C::modified);
                SDL_Color ink = foc ? C::selectInk : C::textPrimary;
                textMid(fBody, r.target, ink, 80, rr);
                const int sh = r.kind == 1 ? nameShift() + (glyphsOn() ? 4 : 0) : 0;   // pocket rows: room for the picture
                glyph(r.beforeId, 260, rr.y + 8);
                glyph(r.afterId, 680, rr.y + 8);
                textMid(fBody, fit(fBody, r.before, 360 - sh), foc ? C::selectInk : C::textSecond, 260 + sh, rr);
                textMid(fBody, "\xC2\xBB", ink, 640, rr);
                textMid(fBody, fit(fBody, r.after, 520 - sh), foc ? C::selectInk : C::modifiedInk, 680 + sh, rr);
            }
            if ((int)m.rows.size() > VIS) textR(fSmall, fmt("%d-%d of %d", m.top + 1, std::min((int)m.rows.size(), m.top + VIS), (int)m.rows.size()), C::textSecond, 1220, 466);
            text(fSmall, S::REVIEW_NOTE1, C::textPrimary, 56, 466);
            text(fSmall, S::REVIEW_NOTE2, C::textPrimary, 56, 490);
        }
        int x = 56;
        for (size_t i = 0; i < m.buttons.size(); i++) {
            const DialogButton& b = m.buttons[i];
            int w = textWidth(fBody, b.label) + 48;
            bool foc = m.rowFocus < 0 && (int)i == m.focus;
            dialogButton({ x, 528, w, 48 }, b, foc);
            x += w + 16;
        }
    }
    void reviewInput(Modal& m, uint64_t down, uint64_t nav) {
        auto selectable = [&](int i) { return i >= 0 && i < (int)m.rows.size() && m.rows[i].kind != 0; };
        if (nav & HidNpadButton_AnyUp) {
            if (m.rowFocus < 0) { int i = (int)m.rows.size() - 1; while (i >= 0 && !selectable(i)) i--; if (i >= 0) m.rowFocus = i; }
            else { int i = m.rowFocus - 1; while (i >= 0 && !selectable(i)) i--; if (i >= 0) m.rowFocus = i; }
            dirty = true;
        }
        if (nav & HidNpadButton_AnyDown) {
            if (m.rowFocus >= 0) { int i = m.rowFocus + 1; while (i < (int)m.rows.size() && !selectable(i)) i++; m.rowFocus = i < (int)m.rows.size() ? i : -1; }
            dirty = true;
        }
        if (m.rowFocus < 0) {
            int nb = (int)m.buttons.size();
            if (nav & HidNpadButton_AnyLeft) { m.focus = (m.focus + nb - 1) % nb; dirty = true; }
            if (nav & HidNpadButton_AnyRight) { m.focus = (m.focus + 1) % nb; dirty = true; }
        }
        if (down & HidNpadButton_B) { popModal(); return; }
        if (!(down & HidNpadButton_A)) return;
        if (m.rowFocus >= 0) {
            ReviewRow r = m.rows[m.rowFocus];
            const int at = m.rowFocus;
            Modal mm; mm.kind = Modal::MENU; mm.title = r.target;
            MenuEntry rv; rv.label = S::M_REVERT_CHANGE; rv.isDefault = true;
            rv.run = [this, r, at]() {
                if (r.kind == 1) revertSlot(base, e->pockets, r.slot); else revertVal(base, e->vals, r.idx);
                if (modals.empty() || modals.back().kind != Modal::REVIEW) return;
                // Rebuilding puts the cursor on the first button, which is Apply. Keep it in
                // the list instead (or on the safe button): a repeated A must never write.
                Modal& rm = modals.back();
                buildReviewRows(rm);
                const int n = (int)rm.rows.size();
                int i = std::min(at, n - 1);
                while (i < n && i >= 0 && rm.rows[i].kind == 0) i++;
                if (i >= n) { i = std::min(at, n - 1); while (i >= 0 && rm.rows[i].kind == 0) i--; }
                if (i >= 0 && i < n) rm.rowFocus = i; else rm.focus = rm.safe;
            };
            MenuEntry go; go.label = S::M_GO_TO;
            go.run = [this, r]() {
                modals.clear();
                if (r.kind == 1) { panel = P_POCKETS; focus[P_POCKETS] = r.slot - 1; }
                else { panel = P_MONEY; focus[P_MONEY] = r.idx; }
                dirty = true;
            };
            mm.entries.push_back(rv); mm.entries.push_back(go);
            mm.anchor = { 56, 156 + (m.rowFocus - m.top) * ROW_PITCH, 600, ROW_H };
            push(mm);
            return;
        }
        std::function<void()> run = m.buttons[m.focus].run;
        if (!run) { popModal(); return; }
        run();
    }
    void startApply(bool quitAfter) {
        if (!e->editSupported) { showError(S::E_RO); return; }
        if (tainted) { showError(S::E_TAINTED); return; }
        if (pending() == 0) return;
        withWait([this, quitAfter]() {
            // The pre-edit backup (~10 MB on the SD card) runs behind the "Writing..." screen.
            afterPreEditBackup(S::D_SESSION_FAIL_BODY, S::B_APPLY_ANYWAY, [this, quitAfter]() { applyNow(quitAfter); });
        });
    }
    void applyNow(bool quitAfter) {
        EngineResult r = e->apply();                                 // one doSave() (I3)
        infoValid = false;
        switch (r) {
            case ER_OK:
                snapshotBase(base, e->pockets, e->vals);              // I4: only after success
                backupTrusted = true;
                modals.clear();
                setToast(T_OK, S::T_APPLIED);
                if (quitAfter) quit = true;
                break;
            case ER_BACKUP_FAILED: backupTrusted = false; showError(S::E_BACKUP); break;   // I6
            case ER_NAND_FAILED: showError(S::E_NAND); break;
            case ER_MALLOC_FAILED: showError(S::E_MALLOC); break;
            case ER_HASH: showError(S::E_HASH); break;
            default: showError(S::E_OTHER); break;
        }
        if (!modals.empty() && modals[0].kind == Modal::REVIEW) buildReviewRows(modals[0]);
    }
    void askDiscard() {
        int n = pending();
        if (n == 0) { setToast(T_INFO, S::T_NO_CHANGES); return; }
        Modal d; d.kind = Modal::DIALOG; d.title = fmt(S::D_DISCARD_TITLE_FMT, n, plural(n)); d.body.push_back(S::D_DISCARD_BODY);
        DialogButton no; no.label = S::B_NO;
        DialogButton yes; yes.label = S::B_YES_DISCARD; yes.destructive = true;
        yes.run = [this]() {
            discardAll(base, e->pockets, e->vals);                   // RAM only, no engine call
            modals.clear();
            setToast(T_INFO, S::T_DISCARDED);
        };
        d.buttons.push_back(no); d.buttons.push_back(yes);
        push(d);
    }
    void requestQuit() {
        int n = pending();
        if (n == 0) { quit = true; return; }
        Modal d; d.kind = Modal::DIALOG; d.title = fmt(S::D_QUIT_TITLE_FMT, n, plural(n)); d.body.push_back(S::D_QUIT_BODY);
        DialogButton stay; stay.label = S::B_STAY;
        DialogButton aq; aq.label = S::B_APPLY_QUIT; aq.run = [this]() { startApply(true); };
        DialogButton q; q.label = S::B_QUIT_NOAPPLY; q.destructive = true; q.run = [this]() { quit = true; };
        d.buttons.push_back(stay); d.buttons.push_back(aq); d.buttons.push_back(q);
        push(d);
    }
    void withWait(std::function<void()> op) {
        Modal w; w.kind = Modal::WAIT; w.title = S::WAIT_TITLE; w.body.push_back(S::WAIT_BODY);
        push(w);
        pendingOp = op;
        waitFrames = 0;
    }

    // -------------------------------- modals -------------------------------
    void dialogButton(SDL_Rect r, const DialogButton& b, bool foc) {
        SDL_Color bg, ink;
        if (b.destructive) {
            if (foc) { bg = C::error; ink = C::selectInk; fillRound(r, 4, bg); }
            else { fillRound(r, 4, C::error); fillRound({ r.x + 2, r.y + 2, r.w - 4, r.h - 4 }, 3, C::panel); ink = C::error; }
        } else {
            bg = foc ? C::selectBg : C::sunken; ink = foc ? C::selectInk : C::textPrimary;
            fillRound(r, 4, bg);
        }
        textC(fBody, b.label, ink, r);
    }
    void boxFrame(SDL_Rect r, SDL_Color frameC) {
        fill({ r.x + 6, r.y + 6, r.w, r.h }, C::woodDark);
        fillRound(r, 8, frameC);
        fillRound({ r.x + 3, r.y + 3, r.w - 6, r.h - 6 }, 6, C::panel);
    }
    void drawMenu(const Modal& m) {
        fill({ 0, 0, 1280, 640 }, C::veilLight);
        int n = (int)m.entries.size();
        int w = 480, h = 40 + n * 40 + 12;
        int x = (m.anchor.x + m.anchor.w / 2 < 640) ? std::min(m.anchor.x + m.anchor.w + 12, 1224 - w) : std::max(56, m.anchor.x - w - 12);
        if (m.anchor.w >= 560) x = std::min(m.anchor.x + 120, 1224 - w);
        int y = std::max(152, std::min(m.anchor.y, 592 - h));
        SDL_Rect r = { x, y, w, h };
        boxFrame(r, C::wood);
        text(fSmall, fit(fSmall, m.title, w - 32), C::textSecond, x + 16, y + (40 - fontH(fSmall)) / 2);
        fill({ x + 8, y + 38, w - 16, 2 }, C::lineSoft);
        for (int i = 0; i < n; i++) {
            const MenuEntry& en = m.entries[i];
            SDL_Rect er = { x + 8, y + 44 + i * 40, w - 16, 36 };
            bool foc = i == m.focus;
            if (foc) fillRound(er, 4, en.enabled ? C::selectBg : C::sunken);
            SDL_Color ink = !en.enabled ? C::disabledInk : (foc ? C::selectInk : (en.destructive ? C::error : C::textPrimary));
            int lw = textMid(fBody, en.label, ink, er.x + 12, er);
            int rx = er.x + er.w - 12;
            if (en.isDefault && en.enabled) { keycap("Y", rx - 32, er.y + 2, true, false); rx -= 40; }
            std::string right = en.enabled ? en.detail : en.reason;
            if (!right.empty()) {
                int maxW = rx - (er.x + 12 + lw + 16);
                if (maxW > 40) textR(fSmall, fit(fSmall, right, maxW), foc && en.enabled ? C::selectInk : (en.enabled ? C::textSecond : C::disabledInk), rx, er.y + (36 - fontH(fSmall)) / 2);
            }
        }
    }
    void drawStepper(const Modal& m) {
        fill({ 0, 0, 1280, 640 }, C::veilLight);
        SDL_Rect r = { 376, 200, 528, 256 };
        boxFrame(r, C::wood);
        fillRound({ r.x + 3, r.y + 3, r.w - 6, 50 }, 6, C::wood);
        text(fTitle, fit(fTitle, m.title, r.w - 48), C::selectInk, r.x + 24, r.y + 3 + (50 - fontH(fTitle)) / 2);
        SDL_Rect fr = { r.x + 24, r.y + 76, r.w - 48, 64 };
        fillRound(fr, 4, C::sunken);
        text(fTitle, "\xE2\x80\xB9", m.value <= m.minV ? C::disabledInk : C::textSecond, fr.x + 16, fr.y + (64 - fontH(fTitle)) / 2);
        textR(fTitle, "\xE2\x80\xBA", m.value >= m.maxV ? C::disabledInk : C::textSecond, fr.x + fr.w - 16, fr.y + (64 - fontH(fTitle)) / 2);
        std::string v = m.money ? formatThousands((uint64_t)m.value) : fmt("%s%lld", S::TIMES, (long long)m.value);
        textC(fTitle, v, m.value != m.original ? C::modifiedInk : C::textPrimary, fr);
        std::string was = m.money ? formatThousands((uint64_t)m.original) : fmt("%lld", (long long)m.original);
        std::string range = m.money ? formatThousands((uint64_t)m.minV) + " \xE2\x80\x93 " + formatThousands((uint64_t)m.maxV)
                                    : fmt("%lld \xE2\x80\x93 %lld", (long long)m.minV, (long long)m.maxV);
        text(fSmall, "was " + was + S::DOT + range, C::textSecond, r.x + 24, r.y + 156);
        text(fSmall, fit(fSmall, m.money ? S::STEP_MONEY : S::STEP_QTY, r.w - 48), C::textSecond, r.x + 24, r.y + 184);
        text(fSmall, S::STEP_ACCEL, C::textSecond, r.x + 24, r.y + 212);
    }
    void drawDialog(Modal& m) {
        fill({ 0, 0, 1280, 640 }, C::veil);
        int w = 720;
        int bodyLines = 0;
        std::vector<std::string> lines;
        for (size_t i = 0; i < m.body.size(); i++) wrap(fBody, m.body[i], w - 48, lines);
        bodyLines = (int)lines.size();
        bool hasButtons = !m.buttons.empty();
        int h = 56 + 20 + (m.subtitle.empty() ? 0 : 40) + bodyLines * 32 + 20 + (hasButtons ? 48 + 28 : 0) + (m.danger ? 14 : 0);
        SDL_Rect r = { 640 - w / 2, std::max(120, 320 - h / 2), w, h };
        SDL_Color fc = m.danger ? C::error : C::wood;
        boxFrame(r, fc);
        fillRound({ r.x + 3, r.y + 3, r.w - 6, 53 }, 6, fc);
        text(fTitle, fit(fTitle, m.title, w - 48), C::selectInk, r.x + 24, r.y + 3 + (53 - fontH(fTitle)) / 2);
        int y = r.y + 56 + 16;
        if (!m.subtitle.empty()) { text(fTitle, m.subtitle, C::textPrimary, r.x + 24, y); y += 40; }
        for (size_t i = 0; i < lines.size(); i++) { text(fBody, lines[i], C::textPrimary, r.x + 24, y); y += 32; }
        if (!hasButtons) return;
        int x = r.x + 24, by = r.y + r.h - 48 - 24 - (m.danger ? 14 : 0);
        for (size_t i = 0; i < m.buttons.size(); i++) {
            int bw = textWidth(fBody, m.buttons[i].label) + 48;
            SDL_Rect br = { x, by, bw, 48 };
            dialogButton(br, m.buttons[i], (int)i == m.focus);
            if (m.buttons[i].hold && (int)i == m.focus) {
                gaugeOn = true; gaugeRect = { br.x, br.y + br.h + 6, br.w, 8 };
            }
            x += bw + 16;
        }
    }
    void drawWait(const Modal& m) {
        fill({ 0, 0, 1280, 640 }, C::veil);
        SDL_Rect r = { 400, 250, 480, 150 };
        boxFrame(r, C::wood);
        fillRound({ r.x + 3, r.y + 3, r.w - 6, 53 }, 6, C::wood);
        text(fTitle, m.title, C::selectInk, r.x + 24, r.y + 3 + (53 - fontH(fTitle)) / 2);
        if (!m.body.empty()) text(fBody, m.body[0], C::textPrimary, r.x + 24, r.y + 80);
    }
    void wrap(TTF_Font* f, const std::string& s, int maxW, std::vector<std::string>& out) {
        std::string line, word;
        for (size_t i = 0; i <= s.size(); i++) {
            char c = i < s.size() ? s[i] : ' ';
            if (c == ' ') {
                std::string cand = line.empty() ? word : line + " " + word;
                if (!line.empty() && textWidth(f, cand) > maxW) { out.push_back(line); line = word; }
                else line = cand;
                word.clear();
            } else word += c;
        }
        if (!line.empty()) out.push_back(line);
    }
    void drawModals() {
        for (size_t i = 0; i < modals.size(); i++) {
            Modal& m = modals[i];
            switch (m.kind) {
                case Modal::MENU: drawMenu(m); break;
                case Modal::STEPPER: drawStepper(m); break;
                case Modal::DIALOG: drawDialog(m); break;
                case Modal::WAIT: drawWait(m); break;
                default: break;                                      // REVIEW is a full screen, drawn first
            }
        }
    }

    // --------------------------- status + help bar -------------------------
    void drawStatus() {
        fill({ 32, 600, 1216, 2 }, C::lineSoft);
        if (!toast.empty()) {
            SDL_Color bg = toastKind == T_OK ? C::ok : toastKind == T_WARN ? C::warn : toastKind == T_ERR ? C::error : C::wood;
            SDL_Color ink = toastKind == T_WARN ? C::inkStrong : C::selectInk;
            std::string t = fit(fSmall, toast, 1150);
            int w = textWidth(fSmall, t) + 24;
            fillRound({ 48, 604, w, 32 }, 4, bg);
            text(fSmall, t, ink, 60, 604 + (32 - fontH(fSmall)) / 2);
            return;
        }
        if (!ready() || (!modals.empty() && modals[0].kind == Modal::REVIEW)) return;
        std::string d;
        switch (panel) {
            case P_POCKETS: d = pocketsDetail(); break;
            case P_FAVS: {
                const std::vector<Fav> L = favOrder();
                if (!L.empty()) { const Fav& f = L[std::min(focus[P_FAVS], (int)L.size() - 1)];
                    d = itemName(f.id) + S::DOT + fmt("0x%04X", (unsigned)f.id) + S::DOT + (f.builtin ? "built-in favorite" : "added by you (user_favorites.txt)"); }
                break;
            }
            case P_CATALOG:
                if (focus[P_CATALOG] > 0 && focus[P_CATALOG] <= (int)results.size()) {
                    uint16_t id = (uint16_t)results[focus[P_CATALOG] - 1];
                    d = itemName(id) + S::DOT + fmt("0x%04X", (unsigned)id);
                    if (single(id)) d += S::DOT + std::string(S::DOESNT_STACK);
                }
                break;
            case P_MONEY: {
                int i = focus[P_MONEY];
                d = std::string(S::VAL_NAME[i]) + S::DOT + formatThousands(e->vals[i]);
                if (valModified(base, e->vals, i)) d += S::DOT + std::string("was ") + formatThousands(base.vals[i]);
                break;
            }
            default: break;
        }
        text(fSmall, fit(fSmall, d, 1160), C::textSecond, 56, 600 + (40 - fontH(fSmall)) / 2);
    }
    struct HelpKey { const char* cap; bool round; std::string label; bool off; bool hot; };
    void drawHelp() {
        fill({ 32, 640, 1216, 56 }, C::wood);
        std::vector<HelpKey> left, right;
        const Modal* top = modals.empty() ? nullptr : &modals.back();
        if (!ready()) {
            left.push_back({ "A", true, S::H_QUIT, false, false });
        } else if (top && top->kind == Modal::WAIT) {
            // nothing: the write is running
        } else if (top && top->kind == Modal::MENU) {
            left.push_back({ "A", true, S::H_CHOOSE, false, false });
            left.push_back({ "B", true, S::H_CLOSE, false, false });
        } else if (top && top->kind == Modal::STEPPER) {
            left.push_back({ "A", true, S::H_CONFIRM, false, false });
            left.push_back({ "B", true, S::H_CANCEL, false, false });
        } else if (top && top->kind == Modal::DIALOG) {
            bool hold = top->focus < (int)top->buttons.size() && top->buttons[top->focus].hold;
            left.push_back({ "A", true, hold ? S::H_HOLD_A : S::H_CHOOSE, false, false });
            left.push_back({ "B", true, S::H_CANCEL, false, false });
        } else if (top && top->kind == Modal::REVIEW) {
            left.push_back({ "A", true, S::H_CHOOSE, false, false });
            left.push_back({ "B", true, S::H_BACK, false, false });
        } else {
            std::string y;
            bool yOff = false;
            switch (panel) {
                case P_POCKETS: y = hand.valid ? S::H_PLACE : S::H_TAKE; yOff = !hand.valid && slotEmpty(e->pockets, focus[P_POCKETS] + 1); break;
                case P_FAVS: y = S::H_TAKE; yOff = e->favorites->list().empty(); break;
                case P_CATALOG: y = focus[P_CATALOG] == 0 ? S::H_TYPE : S::H_TAKE; yOff = !itemsLoaded(); break;
                case P_MONEY: y = S::H_MODIFY; break;
                case P_LOADOUTS: y = S::H_LOAD; yOff = !lo[focus[P_LOADOUTS]].exists; break;
                default: y = "\xE2\x80\x94"; yOff = true; break;
            }
            left.push_back({ "A", true, S::H_ACTIONS, false, false });
            left.push_back({ "Y", true, y, yOff, false });
            left.push_back({ "B", true, S::H_BACK, true, false });
            int n = pending();
            right.push_back({ "L", false, "", panel == 0, false });
            right.push_back({ "R", false, S::H_PANEL, panel == P_COUNT - 1, false });
            right.push_back({ "+", false, n ? fmt("%s (%d)", S::H_REVIEW, n) : std::string(S::H_REVIEW), false, n > 0 });
            right.push_back({ "-", false, S::H_DISCARD, n == 0, false });
        }
        int x = 56;
        for (size_t i = 0; i < left.size(); i++) x = helpKey(left[i], x) + 28;
        int total = 0;
        for (size_t i = 0; i < right.size(); i++) total += helpKeyWidth(right[i]) + (i + 1 < right.size() ? (right[i].label.empty() ? 6 : 28) : 0);
        x = 1232 - total;
        for (size_t i = 0; i < right.size(); i++) x = helpKey(right[i], x) + (right[i].label.empty() ? 6 : 28);
    }
    int helpKeyWidth(const HelpKey& k) {
        int w = k.round ? 32 : 40;
        if (!k.label.empty()) w += 8 + textWidth(fSmall, k.label);
        return w;
    }
    int helpKey(const HelpKey& k, int x) {
        int w = helpKeyWidth(k);
        if (k.hot) fillRound({ x - 8, 648, w + 16, 40 }, 4, C::modified);
        keycap(k.cap, x, 652, k.round, k.off);
        if (!k.label.empty())
            text(fSmall, k.label, k.hot ? C::inkStrong : (k.off ? C::woodMute : C::selectInk), x + (k.round ? 32 : 40) + 8, 640 + (56 - fontH(fSmall)) / 2);
        return x + w;
    }
    void drawFatal() {
        fill({ 32, 88, 1216, 56 }, C::woodDark);
        textC(fTitle, S::FATAL_TITLE, C::textPrimary, { 56, 230, 1168, 40 });
        if (e && e->fatalError) textC(fBody, e->fatalError, C::error, { 56, 290, 1168, 34 });
        textC(fSmall, S::FATAL_HINT1, C::textSecond, { 56, 350, 1168, 28 });
        textC(fSmall, S::FATAL_HINT2, C::textSecond, { 56, 380, 1168, 28 });
    }

    // --------------------------------- input -------------------------------
    uint64_t navMask(uint64_t down, uint64_t held) {
        static const uint64_t dirs[4] = { HidNpadButton_AnyUp, HidNpadButton_AnyDown, HidNpadButton_AnyLeft, HidNpadButton_AnyRight };
        uint64_t nav = 0;
        bool any = false;
        for (int i = 0; i < 4; i++) {
            if (held & dirs[i]) {
                any = true;
                rep[i]++;
                if ((down & dirs[i]) || (rep[i] > 20 && rep[i] % 5 == 0)) nav |= dirs[i];  // upstream cadence
            } else rep[i] = 0;
        }
        holdDirFrames = any ? holdDirFrames + 1 : 0;
        return nav;
    }
    void modalInput(uint64_t down, uint64_t held, uint64_t nav) {
        Modal& m = modals.back();
        switch (m.kind) {
            case Modal::WAIT: return;
            case Modal::REVIEW: reviewInput(m, down, nav); return;
            case Modal::MENU: {
                int n = (int)m.entries.size();
                if (nav & HidNpadButton_AnyUp) { m.focus = (m.focus + n - 1) % n; dirty = true; }
                if (nav & HidNpadButton_AnyDown) { m.focus = (m.focus + 1) % n; dirty = true; }
                if (down & HidNpadButton_B) { popModal(); return; }
                if (down & HidNpadButton_A) {
                    const MenuEntry& en = m.entries[m.focus];
                    if (!en.enabled) { setToast(T_INFO, en.label + ": " + en.reason); return; }
                    std::function<void()> run = en.run;
                    popModal();
                    if (run) run();
                }
                return;
            }
            case Modal::STEPPER: {
                int64_t mult = holdDirFrames > ACCEL_FRAMES ? 10 : 1;
                int64_t v = m.value;
                if (nav & HidNpadButton_AnyLeft) v -= m.small * mult;
                if (nav & HidNpadButton_AnyRight) v += m.small * mult;
                if (nav & HidNpadButton_AnyDown) v -= m.big * mult;
                if (nav & HidNpadButton_AnyUp) v += m.big * mult;
                v = std::max(m.minV, std::min(m.maxV, v));
                if (v != m.value) { m.value = v; dirty = true; }
                if (down & HidNpadButton_B) { popModal(); return; }
                if (down & HidNpadButton_A) {
                    std::function<void(int64_t)> ok = m.onOk; int64_t val = m.value;
                    popModal();
                    if (ok) ok(val);
                }
                return;
            }
            case Modal::DIALOG: {
                int n = (int)m.buttons.size();
                if (n == 0) return;
                if (nav & (HidNpadButton_AnyLeft | HidNpadButton_AnyUp)) { m.focus = (m.focus + n - 1) % n; m.holdStart = 0; m.holdProgress = 0; dirty = true; }
                if (nav & (HidNpadButton_AnyRight | HidNpadButton_AnyDown)) { m.focus = (m.focus + 1) % n; m.holdStart = 0; m.holdProgress = 0; dirty = true; }
                if (down & HidNpadButton_B) {
                    std::function<void()> run = m.buttons[m.safe].run;
                    popModal();
                    if (run) run();
                    return;
                }
                const DialogButton& b = m.buttons[m.focus];
                if (b.hold) {
                    // Hold-to-confirm (replaces upstream "ZR armed, ZR again"). A must be
                    // released once after the dialog opened, then held for 1.5 s.
                    if (!(held & HidNpadButton_A)) { m.holdArmed = true; if (m.holdStart) { m.holdStart = 0; m.holdProgress = 0; } return; }
                    if (!m.holdArmed) return;
                    uint32_t now = SDL_GetTicks();
                    if (!m.holdStart) m.holdStart = now;
                    m.holdProgress = (float)(now - m.holdStart) / (float)HOLD_CONFIRM_MS;
                    if (m.holdProgress >= 1.f) {
                        std::function<void()> run = b.run;
                        popModal();
                        if (run) run();
                    }
                    return;
                }
                if (down & HidNpadButton_A) {
                    std::function<void()> run = b.run;
                    popModal();
                    if (run) run();
                }
                return;
            }
        }
    }
    void input(uint64_t down, uint64_t held) {
        uint64_t nav = navMask(down, held);
        if (!ready()) { if (down & (HidNpadButton_A | HidNpadButton_Plus)) quit = true; return; }
        if (!modals.empty()) { modalInput(down, held, nav); return; }
        if ((down & HidNpadButton_L) && panel > 0) { panel--; onPanelEnter(); return; }
        if ((down & HidNpadButton_R) && panel < P_COUNT - 1) { panel++; onPanelEnter(); return; }
        if (down & HidNpadButton_Plus) { openReview(); return; }
        if (down & HidNpadButton_Minus) { askDiscard(); return; }
        // ZL / ZR / X: intentionally no action (upstream ZL = instant restore, ZR = Doctor).
        switch (panel) {
            case P_POCKETS: pocketsInput(down, nav); break;
            case P_FAVS: favsInput(down, nav); break;
            case P_CATALOG: catalogInput(down, nav); break;
            case P_MONEY: moneyInput(down, nav); break;
            case P_LOADOUTS: loadoutsInput(down, nav); break;
            default: systemInput(down, nav); break;
        }
    }
    void onPanelEnter() {
        if (panel == P_LOADOUTS) refreshLoadouts();
        if (panel == P_SYSTEM) refreshInfo();
        dirty = true;
    }

    void render() {
        for (size_t i = 0; i < ops.size(); i++) {
            const Op& o = ops[i];
            if (o.kind == Op::RECT) {
                SDL_SetRenderDrawBlendMode(ren, o.c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
                SDL_SetRenderDrawColor(ren, o.c.r, o.c.g, o.c.b, o.c.a);
                SDL_RenderFillRect(ren, &o.r);
            } else if (o.mod) {
                SDL_SetTextureColorMod(o.t, o.c.r, o.c.g, o.c.b);
                SDL_RenderCopy(ren, o.t, o.hasSrc ? &o.src : nullptr, &o.r);
                SDL_SetTextureColorMod(o.t, 255, 255, 255);
            } else {
                SDL_RenderCopy(ren, o.t, o.hasSrc ? &o.src : nullptr, &o.r);
            }
        }
        // Hold-to-confirm gauge: the only element redrawn every frame (CHARTE 6.8).
        if (gaugeOn && !modals.empty() && modals.back().kind == Modal::DIALOG) {
            SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(ren, C::errorBg.r, C::errorBg.g, C::errorBg.b, 255);
            SDL_RenderFillRect(ren, &gaugeRect);
            float p = std::min(1.f, modals.back().holdProgress);
            SDL_Rect g = { gaugeRect.x, gaugeRect.y, (int)(gaugeRect.w * p), gaugeRect.h };
            SDL_SetRenderDrawColor(ren, C::error.r, C::error.g, C::error.b, 255);
            if (g.w > 0) SDL_RenderFillRect(ren, &g);
        }
        SDL_RenderPresent(ren);
    }
};

// ================================== public =================================
static SDL_Texture* loadPng(SDL_Renderer* ren, const std::string& path, int* outW = nullptr, int* outH = nullptr) {
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!px) return nullptr;
    if (outW) *outW = w;
    if (outH) *outH = h;
    SDL_Texture* t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (t) { SDL_UpdateTexture(t, nullptr, px, w * 4); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); }
    stbi_image_free(px);
    return t;
}

bool Ui::init(SDL_Renderer* ren, const std::string& sdDir) {
    d_ = new Impl();
    d_->ren = ren;
    d_->sdDir = sdDir;
    const std::string font = sdDir + "font.ttf";
    d_->fDisplay = TTF_OpenFont(font.c_str(), 32);
    d_->fTitle   = TTF_OpenFont(font.c_str(), 28);
    d_->fBody    = TTF_OpenFont(font.c_str(), 24);
    d_->fSmall   = TTF_OpenFont(font.c_str(), 20);
    d_->fMicro   = TTF_OpenFont(font.c_str(), 16);
    d_->appIcon  = loadPng(ren, sdDir + "icon.png");
    d_->icons    = loadPng(ren, sdDir + "ui_icons.png");      // optional, text fallbacks otherwise
    int gw = 0, gh = 0;
    d_->glyphTex = loadPng(ren, sdDir + "item_glyphs.png", &gw, &gh);   // optional item pictures
    d_->glyphCols = d_->glyphTex ? gw / 24 : 0;
    d_->glyphTintRows = gh >= 72;
    return d_->fBody != nullptr;
}

void Ui::splash() {
    if (!d_) return;
    Impl& d = *d_;
    d.clearOps();
    d.fill({ 0, 0, 1280, 720 }, C::bgLeaf);
    d.fill({ 32, 24, 1216, 672 }, C::panel);
    d.fill({ 32, 24, 1216, 64 }, C::wood);
    if (d.appIcon) d.ops.push_back({ Op::TEX, { 48, 32, 48, 48 }, C::panel, d.appIcon, { 0, 0, 0, 0 }, false, false });
    d.text(d.fDisplay, S::APP_TITLE, C::selectInk, 112, 24 + (64 - d.fontH(d.fDisplay)) / 2);
    d.textC(d.fTitle, S::SPLASH_TITLE, C::textPrimary, { 56, 260, 1168, 40 });
    d.textC(d.fSmall, S::SPLASH_HINT1, C::textSecond, { 56, 320, 1168, 28 });
    d.textC(d.fSmall, S::SPLASH_HINT2, C::textSecond, { 56, 350, 1168, 28 });
    d.fill({ 32, 640, 1216, 56 }, C::wood);
    d.render();
    d.render();                                  // double-buffered display: fill both buffers
    d.dirty = true;
}

void Ui::start(EngineHooks& e) {
    d_->e = &e;
    d_->sessionWritten = e.sessionBackupKept;
    if (d_->ready()) snapshotBase(d_->base, e.pockets, e.vals);
    if (e.favorites && e.favorites->ignoredLines())
        d_->setToast(T_WARN, fmt(S::T_FAV_IGNORED_FMT, e.favorites->ignoredLines(), plural(e.favorites->ignoredLines())));
    d_->dirty = true;
}

bool Ui::frame(uint64_t down, uint64_t held) {
    Impl& d = *d_;
    // A button still held from the Homebrew Menu counts as "just pressed" on the first
    // frame: ignore the action buttons until they have all been released once.
    static const uint64_t ACTION_BUTTONS = HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y |
                                           HidNpadButton_L | HidNpadButton_R | HidNpadButton_Plus | HidNpadButton_Minus;
    if (!d.armed) {
        if (held & ACTION_BUTTONS) down &= ~ACTION_BUTTONS; else d.armed = true;
    }
    if (d.pendingOp) {
        // The "Writing..." screen was presented on the previous frame; now run the blocking call.
        if (d.waitFrames++ >= 1) {
            std::function<void()> op = d.pendingOp;
            d.pendingOp = nullptr;
            if (!d.modals.empty() && d.modals.back().kind == Modal::WAIT) d.popModal();
            op();
            d.dirty = true;
        }
    } else {
        d.input(down, held);
    }
    if (!d.toast.empty() && SDL_GetTicks() > d.toastUntil) { d.toast.clear(); d.dirty = true; }
    if (d.dirty) { d.dirty = false; d.rebuild(); }
    d.render();
    return !d.quit;
}

void Ui::shutdown() {
    if (!d_) return;
    d_->clearOps();
    if (d_->appIcon) SDL_DestroyTexture(d_->appIcon);
    if (d_->icons) SDL_DestroyTexture(d_->icons);
    if (d_->glyphTex) SDL_DestroyTexture(d_->glyphTex);
    TTF_Font* fonts[5] = { d_->fDisplay, d_->fTitle, d_->fBody, d_->fSmall, d_->fMicro };
    for (int i = 0; i < 5; i++) if (fonts[i]) TTF_CloseFont(fonts[i]);
    delete d_;
    d_ = nullptr;
}

} // namespace pp
