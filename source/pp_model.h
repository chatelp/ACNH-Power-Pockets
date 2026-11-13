// ============================================================================
// ACNH Power Pockets - UI model (pure C++, no libnx / SDL dependency)
//
// Everything in this file is UI-side state and helpers. It NEVER touches the
// save engine in main.cpp (crypto, hashes, doSave, backups, restore, Doctor).
// The only save bytes it is allowed to write are the 20 pocket records
// (160 bytes at personal + POCKETS_BASE) and the 4 money values (g_vals),
// exactly like the upstream "clear slot" and "load loadout" features did.
// See docs/engine/AUDIT_ECRITURE.md (invariants I1..I13).
//
// Built for devkitPro (gnu++17, -fno-exceptions, -fno-rtti) and for the host
// unit tests in tests/host/.
// ============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace pp {

// ---------------------------------------------------------------------------
// Pocket record layout (8 bytes per slot, NHSE "Item" structure)
//   +0 u16 ItemId   (0xFFFE = empty, 0xFFFD = multi-tile extension: never valid)
//   +2 u8  SystemParam   +3 u8 AdditionalParam (wrapping)
//   +4 u16 Count    (stack count, or variant / recipe id for non-stackables)
//   +6 u16 UseCount (tool wear)
// ---------------------------------------------------------------------------
static const int      SLOT_COUNT     = 20;                      // slots 1..20: the pocket block
static const int      SLOT_MAX       = 40;                      // with the bag fully unlocked
static const int      REC_SIZE       = 8;
static const int      POCKETS_BYTES  = SLOT_COUNT * REC_SIZE;   // 160: one block (pocket or bag)
static const int      ALL_SLOTS_BYTES = SLOT_MAX * REC_SIZE;    // 320: slots 1..40 back to back
static const uint16_t ID_EMPTY       = 0xFFFE;
static const uint16_t ID_MAX_VALID   = 0xFFFC;                  // 0xFFFD is EXTENSION
static const int      QTY_MIN        = 1;                       // upstream bounds, unchanged
static const int      QTY_MAX        = 99;
static const int      FAV_LIMIT      = 100;
static const uint32_t WALLET_GAME_MAX = 99999;                  // warning only, bound unchanged

// Quantity convention of the +4 field.
// false = the number shown is the raw value written (upstream behaviour).
// true  = raw = quantity - 1 (0 means one item).
// The game stores quantity - 1: NHSE fills a stack with Count = max - 1, and the
// console test of 2026-11-12 (ACNH 3.0.3) placed raw 9 and got 10 Nook Miles Tickets.
// v1.0.0 shipped with false and gave one item too many. Keep true.
static const bool kCountZeroBased = true;

uint16_t qtyToRaw(int qty);          // qty clamped to QTY_MIN..QTY_MAX first
int      rawToQty(uint16_t raw);     // display value for a stackable item
// Raw value written when placing an item. Non-stackables always get 0: for them
// +4 is a variant / recipe id and 0 is the neutral value (NHSE default item).
uint16_t placeRaw(bool single, int qty);

struct Item {
    uint16_t id;
    std::string name;
};

// --------------------------------- slots -----------------------------------
// The game keeps the inventory in two blocks of 20 records in personal.dat
// (3.0.x, NHSE PersonalOffsets30):
//   0x37CA8  slots 1..20  ("Pocket"), followed by a u32 = 20
//   0x37BF0  slots 21..40 ("Bag"),    followed by a u32 = how many of them are
//            unlocked: 0, 10 or 20 (pocket upgrades). Read, never written.
// A Pockets view covers the slots the player really has. Slot numbers are
// 1..count; functions refuse anything else (returns false / does nothing), so a
// bad index can never write outside the two 160-byte blocks, and never into a
// locked bag slot.
static const uint32_t POCKETS_OFF_30   = 0x37CA8;
static const uint32_t BAG_OFF_30       = 0x37BF0;
static const uint32_t BAG_COUNT_OFF_30 = 0x37C90;
// Usable slots from the bag counter: 20, 30 or 40; 20 for any other value.
int slotsFromBagCount(uint32_t bagCount);

struct Pockets {
    uint8_t* main = nullptr;           // slots 1..20
    uint8_t* bag = nullptr;            // slots 21..40 (nullptr when count is 20)
    int      count = SLOT_COUNT;       // 20, 30 or 40
    Pockets() {}
    Pockets(uint8_t* m) : main(m) {}   // a plain 160-byte block: 20 slots
    Pockets(uint8_t* m, uint8_t* b, int n) : main(m), bag(b), count(b && (n == 30 || n == 40) ? n : SLOT_COUNT) {}
    bool valid(int slot) const { return main && slot >= 1 && slot <= count; }
    uint8_t* rec(int slot) const { return slot <= SLOT_COUNT ? main + (slot - 1) * REC_SIZE : bag + (slot - SLOT_COUNT - 1) * REC_SIZE; }
};
// A contiguous 320-byte image (slots 1..40 back to back) seen as Pockets.
inline Pockets imageView(uint8_t* img, int count) { return count == SLOT_COUNT ? Pockets(img) : Pockets(img, img + POCKETS_BYTES, count); }
struct SlotRec { uint16_t id; uint16_t raw; };

bool     validSlot(int slot);                             // 1..20: the engine's g_slot range
bool     validPlaceId(uint16_t id);                       // 0x0000..0xFFFC
SlotRec  readSlot(const Pockets& p, int slot);
bool     slotEmpty(const Pockets& p, int slot);
int      countEmpty(const Pockets& p);
// First empty slot at or after `from`, wrapping around; -1 if the pockets are full.
int      nextEmptySlot(const Pockets& p, int from);
// Writes the clean record {id, 0, 0, raw, 0} - ONLY into an empty slot.
bool     placeInEmpty(const Pockets& p, int slot, uint16_t id, uint16_t raw);
// Writes the NHSE empty record FE FF 00 00 00 00 00 00. Returns false if already empty.
bool     clearSlot(const Pockets& p, int slot);
// Changes only the +4 field of an occupied slot.
bool     setSlotRaw(const Pockets& p, int slot, uint16_t raw);
int      countItems(const Pockets& p);                    // non-empty slots
// Copies the usable slots into a 320-byte image (slots beyond count: empty records).
void     toImage(const Pockets& p, uint8_t img[ALL_SLOTS_BYTES]);

// ----------------------------- base / pending ------------------------------
// The "Base" is the decrypted state last read from / written to the console.
// Pending changes = Working state (pocket buffer + g_vals) != Base.
struct Base {
    uint8_t  pockets[ALL_SLOTS_BYTES];   // image: slots 1..40 back to back
    int      count;
    uint32_t vals[4];
    bool     valid;
};

void    snapshotBase(Base& b, const Pockets& p, const uint32_t* vals);
SlotRec baseSlot(const Base& b, int slot);
bool    slotModified(const Base& b, const Pockets& p, int slot);
bool    valModified(const Base& b, const uint32_t* vals, int idx);
int     pendingPockets(const Base& b, const Pockets& p);
int     pendingVals(const Base& b, const uint32_t* vals);
int     pendingCount(const Base& b, const Pockets& p, const uint32_t* vals);
void    revertSlot(const Base& b, const Pockets& p, int slot);
void    revertVal(const Base& b, uint32_t* vals, int idx);
void    discardAll(const Base& b, const Pockets& p, uint32_t* vals);

// Invariant I2: called immediately before doSave(). doSave() writes
// g_newId/g_newCount into slot g_slot on top of the buffer copy; after this
// sync that write is a no-op and can never overwrite a slot.
void syncSaveSlotVars(const uint8_t* pockets, int& gSlot, int32_t& gNewId, int32_t& gNewCount);

// --------------------------------- values ----------------------------------
// 0 Wallet, 1 Bank, 2 Nook Miles, 3 House loan. Bounds identical to upstream.
uint32_t valMax(int idx);
uint32_t clampVal(int idx, int64_t v);

// ------------------------------ engine status ------------------------------
// The engine reports its outcome only through g_status_buf. These parsers map
// the exact upstream strings to a result without changing the engine.
enum EngineResult {
    ER_OK = 0,
    ER_OK_NO_FORENSIC,     // Doctor succeeded but could not write damaged_*.dat
    ER_BACKUP_FAILED,      // "BAK ..." : nothing written to NAND, backup untrusted
    ER_NAND_FAILED,
    ER_MALLOC_FAILED,
    ER_HASH,               // integrity-hash self-check failed before writing: nothing written
    ER_RESTORE_MISSING,
    ER_RESTORE_SIZE,
    ER_RESTORE_READ,
    ER_OTHER
};
EngineResult parseSaveStatus(const char* s);
EngineResult parseRestoreStatus(const char* s);
EngineResult parseDoctorStatus(const char* s);

// ------------------------------ item helpers -------------------------------
std::string lower(const std::string& s);
bool        parseHexId(const std::string& s, uint16_t& out);   // "16A2", "0x16a2"
// Case-insensitive substring search; a 4-digit hex query also puts that ID first.
std::vector<int> searchItems(const std::vector<Item>& items, const std::string& query);
std::string formatThousands(uint64_t v);                       // 99999 -> "99,999"

// ----------------------------- non-stackables ------------------------------
// Rule syntax (built-in rules and nonstackable.txt), one per line:
//   golden axe     exact item name (case-insensitive)
//   0x0A1B         item id
//   ~wand          substring of the name (case-insensitive)
//   -<rule>        removes matches of <rule> (applied in order)
//   # comment / blank lines ignored
// ------------------------------ stack limits -------------------------------
// Largest stack the game allows for each item, from item_stacks.txt on the SD card
// (generated by tools/make_item_stacks.py from NHSE's item kinds). Lines:
//   0ACF-0AD1 30     an inclusive id range (hex)
//   16DB 10          one id
//   # comment
// 0 = unknown (id not listed, or file missing).
class StackLimits {
public:
    bool load(const char* path, size_t itemCount);   // false if the file is missing
    int  maxOf(uint16_t id) const { return id < max_.size() ? max_[id] : 0; }
    bool loaded() const       { return loaded_; }
    int  known() const;                               // ids with a known limit
    int  ignoredLines() const { return ignored_; }
    // Exposed for tests: applies one line, returns false if it is invalid.
    bool applyLine(const std::string& line);
private:
    std::vector<uint8_t> max_;
    bool loaded_ = false;
    int  ignored_ = 0;
};

class GlyphMap;
class StackRules {
public:
    // Builds the flags from the built-in rules (tools), then from the item kinds that
    // never stack ("single" glyphs of item_glyphs.txt, when `glyphs` is loaded), then
    // from the game's stack limits (`limits`: 1 = never stacks, more = stacks, this
    // overrides the glyph guess), then from `userPath` if it exists: its rules apply
    // last and can add or remove items.
    void build(const std::vector<Item>& items, const char* userPath, const GlyphMap* glyphs = nullptr,
               const StackLimits* limits = nullptr);
    bool isSingle(uint16_t id) const;
    // Largest quantity the UI offers for this item: 1 for a 1/slot item, the game's
    // limit when known, QTY_MAX otherwise.
    int  maxQty(uint16_t id) const;
    // True when a stackable slot holds more than the game allows (shown, flagged, never fixed).
    bool overMax(uint16_t id, uint16_t raw) const;
    int  fileRules() const   { return fileRules_; }
    int  fileIgnored() const { return fileIgnored_; }
    bool fileFound() const   { return fileFound_; }
    int  singleCount() const;
    // Exposed for tests: applies one rule line, returns false if it is invalid.
    bool applyRule(const std::vector<Item>& items, const std::string& line);
private:
    std::vector<uint8_t> flags_;
    std::vector<uint8_t> limit_;         // copy of StackLimits, 0 = unknown
    int  fileRules_ = 0, fileIgnored_ = 0;
    bool fileFound_ = false;
};

// ------------------------------- item glyphs -------------------------------
// Which small picture (a cell of item_glyphs.png) goes with each item, and in
// which color. Everything comes from item_glyphs.txt on the SD card (format and
// rule syntax are documented at the top of that file and in
// docs/design/GLYPHES.md); tools/make_item_glyphs.py is the reference
// implementation and tests/host/run.sh checks both agree on every item.
// Purely cosmetic: a missing or broken file only means "no pictures".
class GlyphMap {
public:
    void build(const std::vector<Item>& items, const char* path);
    bool loaded() const      { return loaded_; }      // file found, with a glyph table
    int  glyphOf(uint16_t id) const;                  // cell index, 0 = unknown ("?")
    // True when the item's kind never stacks in the game ("single" flag of its glyph).
    bool singleKind(uint16_t id) const;
    // True when the item's glyph can be tinted AND a color word was found in its name.
    bool colorOf(uint16_t id, uint32_t& rgb) const;
    int  glyphCount() const  { return glyphCount_; }  // highest index + 1
    int  ruleCount() const   { return rules_; }
    int  ignoredLines() const { return ignored_; }
    int  recognized() const  { return recognized_; }  // named items with a glyph other than "?"
    int  colored() const     { return colored_; }
private:
    std::vector<uint8_t>  glyph_;
    std::vector<uint32_t> color_;                     // 0 = none, else 0x01RRGGBB
    std::vector<uint8_t>  singleGlyph_;               // per cell index: kind never stacks
    bool loaded_ = false;
    int  glyphCount_ = 0, rules_ = 0, ignored_ = 0, recognized_ = 0, colored_ = 0;
};

// -------------------------------- favorites --------------------------------
// Built-in favorites (FAV_NAMES in main.cpp, resolved by name like upstream)
// plus a user overlay file on the SD card. The overlay is plain text:
//   +16DB Nook Miles Ticket     added by the user  (the ID is authoritative,
//   -0A4E gold roses            built-in hidden     the name is informative)
// Written atomically: <file>.tmp, then remove <file>, then rename. The tmp file
// ends with "# end" so a half-written tmp is never trusted on load.
struct Fav {
    uint16_t    id;
    std::string label;
    bool        builtin;
};

class Favorites {
public:
    enum Result { ADDED, REMOVED, ALREADY, NOT_FAV, FULL, INVALID, IO_ERROR, READ_ONLY };

    void load(const std::vector<Item>& items, const char* const* builtinNames, int builtinCount,
              const std::string& path);
    const std::vector<Fav>& list() const { return list_; }
    bool   isFav(uint16_t id) const;
    bool   isUserAdded(uint16_t id) const;
    Result add(uint16_t id);
    Result remove(uint16_t id);
    // Renames the overlay to <file>.bak, back to the built-ins only.
    bool   resetToDefaults();

    int  ignoredLines() const    { return ignored_; }
    int  missingBuiltins() const { return missing_; }
    bool loadedFromTmp() const   { return fromTmp_; }
    // Without items.txt nothing can be resolved: never rewrite the user's file then.
    bool readOnly() const        { return items_ == nullptr || items_->empty(); }

private:
    void rebuild();
    bool save();
    bool parseFile(const std::string& p, bool requireEnd,
                   std::vector<uint16_t>& added, std::vector<uint16_t>& hidden, int& ignored);

    const std::vector<Item>* items_ = nullptr;
    std::string path_;
    std::vector<Fav> builtins_;          // resolved built-ins (missing ones dropped)
    std::vector<uint16_t> added_;        // user additions
    std::vector<uint16_t> hidden_;       // hidden built-ins
    std::vector<Fav> list_;              // effective list, sorted A-Z
    int  ignored_ = 0, missing_ = 0;
    bool fromTmp_ = false, bakDone_ = false;
};

// --------------------------------- loadouts --------------------------------
// A loadout file is a pocket image: 160 bytes (slots 1..20, the format of
// upstream and of versions before 1.2.0) or 320 bytes (slots 1..40).
// True when every record holds a placeable id or the empty marker: a loadout
// file with 0xFFFD (EXTENSION) or 0xFFFF in a slot is not a pocket snapshot.
bool pocketsPlausible(const uint8_t* img, int slots);
// Reads a 160- or 320-byte file into a 320-byte image (slots 21..40 empty for a
// 160-byte file) and sets `slots` to 20 or 40. Returns false (and leaves `out`
// untouched) on a missing, truncated or implausible file (invariant I12).
bool loadoutRead(const char* path, uint8_t out[ALL_SLOTS_BYTES], int& slots);
// Highest occupied slot of an image (0 when empty).
int  lastUsedSlot(const uint8_t img[ALL_SLOTS_BYTES]);
// Writes 160 bytes when the pockets have 20 slots (readable by older versions),
// else the 320-byte image.
bool loadoutWrite(const char* path, const Pockets& p);

// ------------------------------- SD file utils -----------------------------
bool fileExists(const char* path);
long fileSize(const char* path);                                  // -1 if missing
bool writeFileAll(const char* path, const uint8_t* data, size_t size);  // SD only
bool copyFile(const char* src, const char* dst);                  // SD only

// ------------------------------ backup checking ----------------------------
// A real backup of the loaded save can only differ from it where this app
// writes (pockets, the 4 values, the 8 "heal" fields). Returns the number of
// differing bytes OUTSIDE the allowed ranges; 0 means the backup belongs to
// this save. Read-only.
struct ByteRange { uint32_t off; uint32_t len; };
size_t diffOutside(const uint8_t* a, const uint8_t* b, size_t size, const ByteRange* allowed, int n);

// ---------------------------- pre-edit backup mark --------------------------
// Signature of a console state (two encrypted files). The app stores the
// signature of the state it last wrote; finding the same state at the next
// launch means the game has not saved in between, so the pre-edit backup on
// the SD card is still the state before the first edit and must be kept.
std::string stateSignature(const uint8_t* a, size_t na, const uint8_t* b, size_t nb);
// Mark file content: "<signature> ok" (pre-edit pair present) or "<signature> nopair".
bool writeStateMark(const char* path, const std::string& signature, bool pairPresent);
bool stateMarkMatches(const char* path, const std::string& signature);   // true only for "<signature> ok"

// ------------------------------ turnip market ------------------------------
// Read-only view of the Stalk Market. The game computes the whole week's prices
// when the week starts and keeps them in main.dat; nothing here predicts anything,
// and nothing here writes to the save.
// main.dat 3.0.x layout (NHSE MainSaveOffsets30: ShopKabu = GSaveShop + 0x2D40):
//   +0x00 u32 Daisy Mae's buy price
//   +0x04 u32[14] sell prices: Sunday AM, Sunday PM, Monday AM ... Saturday PM
//   +0x3C u32 pattern (0 Fluctuating, 1 Large spike, 2 Decreasing, 3 Small spike)
//   +0x40 u32 (spike start, unused here)
// The date of the last save comes from the offset used by the Turnips homebrew (averne).
static const uint32_t TURNIP_OFF_30    = 0x490770;
static const uint32_t SAVE_DATE_OFF_30 = 0x97D670;    // u16 year, u8 month, day, hour, minute, second
static const size_t   MAIN_SIZE_30     = 0x9B0E90;
static const int      TURNIP_SLOTS     = 14;          // index 2*day + (PM ? 1 : 0), day 0 = Sunday
static const int      TURNIP_FIRST_SELL = 2;          // Monday AM

struct GameDate { int year = 0, month = 0, day = 0, hour = 0, minute = 0; bool valid = false; };
struct TurnipWeek {
    bool     present = false;          // read from a 3.0.x main.dat
    uint32_t buy = 0;
    uint32_t price[TURNIP_SLOTS] = {};
    uint32_t pattern = 0;
    GameDate saved;                    // last save, game clock
    bool     unusual = false;          // a value outside what the game produces (shown, never fixed)
};
// `mainPlain` is the decrypted main.dat. Returns false (week.present = false) when the
// size is not the 3.0.x one.
bool readTurnipWeek(const uint8_t* mainPlain, size_t size, TurnipWeek& week);
const char* turnipPatternName(uint32_t pattern);      // nullptr when unknown
struct TurnipStats { uint32_t min = 0, max = 0; int minSlot = -1, maxSlot = -1; double avg = 0; };
TurnipStats turnipStats(const TurnipWeek& week);     // over Monday AM .. Saturday PM
// Price slot in effect at this local time: 0 on Sunday (Daisy Mae), else 2..13. The game
// day starts at 5 AM, the PM price at noon. `weekday`: 0 = Sunday.
int turnipSlotAt(int weekday, int hour);
const char* turnipSlotName(int slot);                 // "Mon AM" ... "Sat PM", "Sun"
// Days since 1970-01-01 of a civil date (proleptic Gregorian).
long daysFromCivil(int year, int month, int day);
enum WeekStatus { WEEK_CURRENT, WEEK_PAST, WEEK_FUTURE, WEEK_UNKNOWN };
// Compares the game week (Sunday 5 AM to Sunday 5 AM) of the last save with now.
WeekStatus turnipWeekStatus(const GameDate& saved, const GameDate& now);
// Day number of the Sunday that starts the game week of `d` (for "week of ..." texts).
long gameWeekStart(const GameDate& d);
void civilFromDays(long days, int& year, int& month, int& day);

// Turnips carried in the pockets: items 0x0A48..0x0A51 are bundles of 10..100 turnips,
// each slot holds up to 10 bundles. 0x0A52 = spoiled turnips (worth nothing).
static const uint16_t ID_TURNIPS_10 = 0x0A48, ID_TURNIPS_100 = 0x0A51, ID_TURNIPS_SPOILED = 0x0A52;
struct TurnipHold { long turnips = 0; int slots = 0; int spoiledSlots = 0; };
TurnipHold turnipsInPockets(const Pockets& p);

// Read-only diagnostics.
struct RegionRef { uint32_t off; uint32_t len; };
typedef uint32_t (*HashFn)(const uint8_t*, size_t);
int  regionsHashOk(const uint8_t* plain, size_t size, const RegionRef* r, int n, HashFn fn);

} // namespace pp
