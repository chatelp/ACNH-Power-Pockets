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
static const int      SLOT_COUNT     = 20;
static const int      REC_SIZE       = 8;
static const int      POCKETS_BYTES  = SLOT_COUNT * REC_SIZE;   // 160
static const uint16_t ID_EMPTY       = 0xFFFE;
static const uint16_t ID_MAX_VALID   = 0xFFFC;                  // 0xFFFD is EXTENSION
static const int      QTY_MIN        = 1;                       // upstream bounds, unchanged
static const int      QTY_MAX        = 99;
static const int      FAV_LIMIT      = 64;
static const uint32_t WALLET_GAME_MAX = 99999;                  // warning only, bound unchanged

// Quantity convention of the +4 field.
// false = upstream behaviour: the number shown is the raw value written.
// true  = NHSE convention: raw = quantity - 1 (0 means one item).
// Decide after the in-game "Phase 0" test (see README / plan) and flip here only.
static const bool kCountZeroBased = false;

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
// All slot numbers are 1..20. Functions refuse anything else (returns false /
// does nothing), so a bad index can never write outside the 160-byte buffer.
struct SlotRec { uint16_t id; uint16_t raw; };

bool     validSlot(int slot);
bool     validPlaceId(uint16_t id);                       // 0x0000..0xFFFC
SlotRec  readSlot(const uint8_t* pockets, int slot);
bool     slotEmpty(const uint8_t* pockets, int slot);
int      countEmpty(const uint8_t* pockets);
// First empty slot at or after `from`, wrapping around; -1 if the pockets are full.
int      nextEmptySlot(const uint8_t* pockets, int from);
// Writes the clean record {id, 0, 0, raw, 0} - ONLY into an empty slot.
bool     placeInEmpty(uint8_t* pockets, int slot, uint16_t id, uint16_t raw);
// Writes the NHSE empty record FE FF 00 00 00 00 00 00. Returns false if already empty.
bool     clearSlot(uint8_t* pockets, int slot);
// Changes only the +4 field of an occupied slot.
bool     setSlotRaw(uint8_t* pockets, int slot, uint16_t raw);
int      countItems(const uint8_t* pockets);              // non-empty slots

// ----------------------------- base / pending ------------------------------
// The "Base" is the decrypted state last read from / written to the console.
// Pending changes = Working state (pocket buffer + g_vals) != Base.
struct Base {
    uint8_t  pockets[POCKETS_BYTES];
    uint32_t vals[4];
    bool     valid;
};

void snapshotBase(Base& b, const uint8_t* pockets, const uint32_t* vals);
bool slotModified(const Base& b, const uint8_t* pockets, int slot);
bool valModified(const Base& b, const uint32_t* vals, int idx);
int  pendingPockets(const Base& b, const uint8_t* pockets);
int  pendingVals(const Base& b, const uint32_t* vals);
int  pendingCount(const Base& b, const uint8_t* pockets, const uint32_t* vals);
void revertSlot(const Base& b, uint8_t* pockets, int slot);
void revertVal(const Base& b, uint32_t* vals, int idx);
void discardAll(const Base& b, uint8_t* pockets, uint32_t* vals);

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
class StackRules {
public:
    // Builds the flags from the built-in rules, then from `userPath` if it exists.
    void build(const std::vector<Item>& items, const char* userPath);
    bool isSingle(uint16_t id) const;
    int  fileRules() const   { return fileRules_; }
    int  fileIgnored() const { return fileIgnored_; }
    bool fileFound() const   { return fileFound_; }
    int  singleCount() const;
    // Exposed for tests: applies one rule line, returns false if it is invalid.
    bool applyRule(const std::vector<Item>& items, const std::string& line);
private:
    std::vector<uint8_t> flags_;
    int  fileRules_ = 0, fileIgnored_ = 0;
    bool fileFound_ = false;
};

// -------------------------------- favorites --------------------------------
// Built-in favorites (FAV_NAMES in main.cpp, resolved by name like upstream)
// plus a user overlay file on the SD card. The overlay is plain text:
//   +16A2 Nook Miles Ticket     added by the user  (the ID is authoritative,
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
    // Renames the overlay to <file>.bak, back to the 22 built-ins.
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
// Reads exactly 160 bytes into `out`; returns false (and leaves `out` untouched
// for the caller's working buffer) on a missing or truncated file (invariant I12).
bool loadoutRead(const char* path, uint8_t out[POCKETS_BYTES]);
bool loadoutWrite(const char* path, const uint8_t* pockets);

// ------------------------------- SD file utils -----------------------------
bool fileExists(const char* path);
long fileSize(const char* path);                                  // -1 if missing
bool writeFileAll(const char* path, const uint8_t* data, size_t size);  // SD only
bool copyFile(const char* src, const char* dst);                  // SD only

// Read-only diagnostics.
struct RegionRef { uint32_t off; uint32_t len; };
typedef uint32_t (*HashFn)(const uint8_t*, size_t);
int  regionsHashOk(const uint8_t* plain, size_t size, const RegionRef* r, int n, HashFn fn);

} // namespace pp
