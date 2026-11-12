// ACNH Power Pockets - UI model implementation. See pp_model.h for the rules.
#include "pp_model.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <unordered_map>

namespace pp {

// ------------------------------- byte helpers ------------------------------
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void     wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ------------------------------- quantities --------------------------------
static int clampQty(int q) { return q < QTY_MIN ? QTY_MIN : (q > QTY_MAX ? QTY_MAX : q); }

uint16_t qtyToRaw(int qty) {
    int q = clampQty(qty);
    return (uint16_t)(kCountZeroBased ? q - 1 : q);
}
int rawToQty(uint16_t raw) { return kCountZeroBased ? (int)raw + 1 : (int)raw; }
uint16_t placeRaw(bool single, int qty) { return single ? 0 : qtyToRaw(qty); }

// ---------------------------------- slots ----------------------------------
bool validSlot(int slot) { return slot >= 1 && slot <= SLOT_COUNT; }
bool validPlaceId(uint16_t id) { return id <= ID_MAX_VALID; }

static uint8_t*       rec(uint8_t* pockets, int slot)       { return pockets + (slot - 1) * REC_SIZE; }
static const uint8_t* rec(const uint8_t* pockets, int slot) { return pockets + (slot - 1) * REC_SIZE; }

SlotRec readSlot(const uint8_t* pockets, int slot) {
    SlotRec r = { ID_EMPTY, 0 };
    if (!pockets || !validSlot(slot)) return r;
    const uint8_t* p = rec(pockets, slot);
    r.id = rd16(p); r.raw = rd16(p + 4);
    return r;
}
bool slotEmpty(const uint8_t* pockets, int slot) { return readSlot(pockets, slot).id == ID_EMPTY; }

int countEmpty(const uint8_t* pockets) {
    int n = 0;
    for (int s = 1; s <= SLOT_COUNT; s++) if (slotEmpty(pockets, s)) n++;
    return n;
}
int countItems(const uint8_t* pockets) { return SLOT_COUNT - countEmpty(pockets); }

int nextEmptySlot(const uint8_t* pockets, int from) {
    if (!pockets) return -1;
    if (!validSlot(from)) from = 1;
    for (int i = 0; i < SLOT_COUNT; i++) {
        int s = ((from - 1 + i) % SLOT_COUNT) + 1;
        if (slotEmpty(pockets, s)) return s;
    }
    return -1;
}

bool placeInEmpty(uint8_t* pockets, int slot, uint16_t id, uint16_t raw) {
    // Safety: never overwrite an occupied slot (no inherited wrapping / wear bytes).
    if (!pockets || !validSlot(slot) || !validPlaceId(id) || !slotEmpty(pockets, slot)) return false;
    uint8_t* p = rec(pockets, slot);
    wr16(p, id); p[2] = 0; p[3] = 0; wr16(p + 4, raw); wr16(p + 6, 0);
    return true;
}

bool clearSlot(uint8_t* pockets, int slot) {
    if (!pockets || !validSlot(slot)) return false;
    uint8_t* p = rec(pockets, slot);
    static const uint8_t EMPTY_REC[REC_SIZE] = { 0xFE, 0xFF, 0, 0, 0, 0, 0, 0 };
    if (memcmp(p, EMPTY_REC, REC_SIZE) == 0) return false;
    memcpy(p, EMPTY_REC, REC_SIZE);
    return true;
}

bool setSlotRaw(uint8_t* pockets, int slot, uint16_t raw) {
    if (!pockets || !validSlot(slot) || slotEmpty(pockets, slot)) return false;
    wr16(rec(pockets, slot) + 4, raw);
    return true;
}

// ------------------------------ base / pending -----------------------------
void snapshotBase(Base& b, const uint8_t* pockets, const uint32_t* vals) {
    memcpy(b.pockets, pockets, POCKETS_BYTES);
    for (int i = 0; i < 4; i++) b.vals[i] = vals[i];
    b.valid = true;
}
bool slotModified(const Base& b, const uint8_t* pockets, int slot) {
    if (!b.valid || !validSlot(slot)) return false;
    return memcmp(rec(b.pockets, slot), rec(pockets, slot), REC_SIZE) != 0;
}
bool valModified(const Base& b, const uint32_t* vals, int idx) {
    return b.valid && idx >= 0 && idx < 4 && b.vals[idx] != vals[idx];
}
int pendingPockets(const Base& b, const uint8_t* pockets) {
    int n = 0;
    for (int s = 1; s <= SLOT_COUNT; s++) if (slotModified(b, pockets, s)) n++;
    return n;
}
int pendingVals(const Base& b, const uint32_t* vals) {
    int n = 0;
    for (int i = 0; i < 4; i++) if (valModified(b, vals, i)) n++;
    return n;
}
int pendingCount(const Base& b, const uint8_t* pockets, const uint32_t* vals) {
    return pendingPockets(b, pockets) + pendingVals(b, vals);
}
void revertSlot(const Base& b, uint8_t* pockets, int slot) {
    if (!b.valid || !validSlot(slot)) return;
    memcpy(rec(pockets, slot), rec(b.pockets, slot), REC_SIZE);
}
void revertVal(const Base& b, uint32_t* vals, int idx) {
    if (b.valid && idx >= 0 && idx < 4) vals[idx] = b.vals[idx];
}
void discardAll(const Base& b, uint8_t* pockets, uint32_t* vals) {
    if (!b.valid) return;
    memcpy(pockets, b.pockets, POCKETS_BYTES);
    for (int i = 0; i < 4; i++) vals[i] = b.vals[i];
}

void syncSaveSlotVars(const uint8_t* pockets, int& gSlot, int32_t& gNewId, int32_t& gNewCount) {
    if (!validSlot(gSlot)) gSlot = 1;
    SlotRec r = readSlot(pockets, gSlot);
    gNewId = r.id;
    gNewCount = r.raw;
}

// --------------------------------- values ----------------------------------
uint32_t valMax(int idx) { return (idx == 2 || idx == 3) ? 9999999u : 999999999u; }
uint32_t clampVal(int idx, int64_t v) {
    if (v < 0) return 0;
    if (v > (int64_t)valMax(idx)) return valMax(idx);
    return (uint32_t)v;
}

// ------------------------------ engine status ------------------------------
static bool startsWith(const char* s, const char* p) { return s && strncmp(s, p, strlen(p)) == 0; }
static bool contains(const char* s, const char* p) { return s && strstr(s, p) != nullptr; }

EngineResult parseSaveStatus(const char* s) {
    if (startsWith(s, "SAVED")) return ER_OK;
    if (startsWith(s, "BAK")) return ER_BACKUP_FAILED;
    if (contains(s, "NAND write failed")) return ER_NAND_FAILED;
    if (contains(s, "malloc failed")) return ER_MALLOC_FAILED;
    return ER_OTHER;
}
EngineResult parseRestoreStatus(const char* s) {
    if (startsWith(s, "RESTORED")) return ER_OK;
    if (contains(s, "backup files missing")) return ER_RESTORE_MISSING;
    if (contains(s, "size mismatch")) return ER_RESTORE_SIZE;
    if (contains(s, "read error")) return ER_RESTORE_READ;
    if (contains(s, "NAND write failed")) return ER_NAND_FAILED;
    return ER_OTHER;
}
EngineResult parseDoctorStatus(const char* s) {
    if (startsWith(s, "REPAIRED (no forensic")) return ER_OK_NO_FORENSIC;
    if (startsWith(s, "REPAIRED")) return ER_OK;
    if (contains(s, "NAND write failed")) return ER_NAND_FAILED;
    return ER_OTHER;
}

// ------------------------------- item helpers ------------------------------
std::string lower(const std::string& s) {
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++) o[i] = (char)std::tolower((unsigned char)o[i]);
    return o;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b-1] == ' ' || s[b-1] == '\t' || s[b-1] == '\r' || s[b-1] == '\n')) b--;
    return s.substr(a, b - a);
}

bool parseHexId(const std::string& in, uint16_t& out) {
    std::string s = trim(in);
    if (s.size() == 6 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.size() != 4) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < 4; i++) {
        char c = s[i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = (uint16_t)v;
    return true;
}

std::vector<int> searchItems(const std::vector<Item>& items, const std::string& query) {
    std::vector<int> out;
    std::string q = lower(trim(query));
    if (q.empty()) return out;
    uint16_t hex = 0;
    int hexIdx = -1;
    if (parseHexId(q, hex) && hex < items.size() && !items[hex].name.empty()) {
        hexIdx = hex;
        out.push_back(hexIdx);
    }
    for (size_t i = 0; i < items.size(); i++) {
        if ((int)i == hexIdx || items[i].name.empty()) continue;
        if (lower(items[i].name).find(q) != std::string::npos) out.push_back((int)i);
    }
    return out;
}

std::string formatThousands(uint64_t v) {
    char buf[32];
    snprintf(buf, sizeof buf, "%llu", (unsigned long long)v);
    std::string s = buf, o;
    int n = (int)s.size();
    for (int i = 0; i < n; i++) {
        o += s[i];
        int left = n - 1 - i;
        if (left > 0 && left % 3 == 0) o += ',';
    }
    return o;
}

// ------------------------------ non-stackables -----------------------------
// Built-in rules: tools only (certain: they carry wear and never stack).
// Everything else lives in the editable nonstackable.txt shipped in assets/.
static const char* BUILTIN_SINGLE_RULES[] = {
    "axe", "flimsy axe", "stone axe", "golden axe",
    "net", "flimsy net", "star net", "colorful net", "outdoorsy net", "golden net",
    "fishing rod", "flimsy fishing rod", "fish fishing rod", "colorful fishing rod",
    "outdoorsy fishing rod", "golden rod",
    "shovel", "flimsy shovel", "golden shovel", "outdoorsy shovel", "printed-design shovel",
    "colorful shovel",
    "watering can", "flimsy watering can", "golden watering can", "colorful watering can",
    "elephant watering can", "outdoorsy watering can",
    "slingshot", "golden slingshot", "outdoorsy slingshot", "colorful slingshot",
    "ladder", "vaulting pole",
};

bool StackRules::applyRule(const std::vector<Item>& items, const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return true;          // not a rule, not an error
    if (flags_.size() != items.size()) flags_.assign(items.size(), 0);
    uint8_t set = 1;
    if (line[0] == '-') { set = 0; line = trim(line.substr(1)); }
    if (line.empty()) return false;

    uint16_t id = 0;
    if (line.size() == 6 && line[0] == '0' && (line[1] == 'x' || line[1] == 'X')) {
        if (!parseHexId(line, id) || id >= items.size()) return false;
        flags_[id] = set;
        return true;
    }
    bool sub = false;
    if (line[0] == '~') { sub = true; line = trim(line.substr(1)); if (line.empty()) return false; }
    std::string key = lower(line);
    bool any = false;
    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].name.empty()) continue;
        std::string n = lower(items[i].name);
        if (sub ? (n.find(key) != std::string::npos) : (n == key)) { flags_[i] = set; any = true; }
    }
    return any;
}

void StackRules::build(const std::vector<Item>& items, const char* userPath, const GlyphMap* glyphs,
                       const StackLimits* limits) {
    flags_.assign(items.size(), 0);
    limit_.assign(items.size(), 0);
    fileRules_ = fileIgnored_ = 0;
    fileFound_ = false;
    for (size_t i = 0; i < sizeof(BUILTIN_SINGLE_RULES) / sizeof(BUILTIN_SINGLE_RULES[0]); i++)
        applyRule(items, BUILTIN_SINGLE_RULES[i]);
    // Whole kinds of items that never stack (clothes, furniture, fish, recipes...): for them
    // the count field is a variant, so writing a quantity there would pick a wrong variant.
    if (glyphs && glyphs->loaded())
        for (size_t i = 0; i < items.size(); i++)
            if (glyphs->singleKind((uint16_t)i)) flags_[i] = 1;
    // The game's own limits, when known, settle it: 1 never stacks, more always stacks.
    if (limits && limits->loaded())
        for (size_t i = 0; i < items.size(); i++) {
            int m = limits->maxOf((uint16_t)i);
            limit_[i] = (uint8_t)m;
            if (m == 1) flags_[i] = 1;
            else if (m > 1) flags_[i] = 0;
        }
    if (!userPath) return;
    FILE* f = fopen(userPath, "rb");
    if (!f) return;                                            // optional file: silent
    fileFound_ = true;
    char line[256];
    int lines = 0;
    while (fgets(line, sizeof line, f) && lines < 2048) {      // hard cap, corrupted file safety
        lines++;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (applyRule(items, t)) fileRules_++; else fileIgnored_++;
    }
    fclose(f);
}

bool StackRules::isSingle(uint16_t id) const { return id < flags_.size() && flags_[id] != 0; }
int StackRules::maxQty(uint16_t id) const {
    if (isSingle(id)) return 1;
    int m = id < limit_.size() ? limit_[id] : 0;
    return m > 1 ? (m < QTY_MAX ? m : QTY_MAX) : QTY_MAX;
}
bool StackRules::overMax(uint16_t id, uint16_t raw) const {
    return !isSingle(id) && rawToQty(raw) > maxQty(id);
}

// ------------------------------ stack limits -------------------------------
bool StackLimits::applyLine(const std::string& raw) {
    std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return true;
    size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos) return false;
    std::string range = line.substr(0, sp), val = trim(line.substr(sp + 1));
    size_t dash = range.find('-');
    uint16_t a = 0, b = 0;
    if (!parseHexId(range.substr(0, dash), a)) return false;
    if (dash == std::string::npos) b = a;
    else if (!parseHexId(range.substr(dash + 1), b)) return false;
    if (b < a || val.empty() || val.size() > 3) return false;
    int v = 0;
    for (size_t i = 0; i < val.size(); i++) {
        if (val[i] < '0' || val[i] > '9') return false;
        v = v * 10 + (val[i] - '0');
    }
    if (v < 1 || v > 255) return false;
    for (uint32_t i = a; i <= b && i < max_.size(); i++) max_[i] = (uint8_t)v;
    return true;
}
bool StackLimits::load(const char* path, size_t itemCount) {
    max_.assign(itemCount, 0);
    loaded_ = false;
    ignored_ = 0;
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) return false;
    char line[256];
    int lines = 0;
    while (fgets(line, sizeof line, f) && lines < 65536) {
        lines++;
        if (!applyLine(line)) ignored_++;
    }
    fclose(f);
    loaded_ = true;
    return true;
}
int StackLimits::known() const {
    int n = 0;
    for (size_t i = 0; i < max_.size(); i++) if (max_[i]) n++;
    return n;
}
int StackRules::singleCount() const {
    int n = 0;
    for (size_t i = 0; i < flags_.size(); i++) if (flags_[i]) n++;
    return n;
}

// ------------------------------- item glyphs -------------------------------
// Mirrors tools/make_item_glyphs.py (parse_rules, rule_matches, classify,
// find_color, item_color). "The LAST matching rule wins", so each item keeps the
// highest index among the rules that match it.
namespace {

struct GlyphDef { int idx; bool tint; };

std::vector<std::string> splitWords(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        size_t j = i;
        while (j < s.size() && !isspace((unsigned char)s[j])) j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}
bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}
bool hexN(const std::string& s, size_t pos, size_t n, uint32_t& out) {
    if (pos + n > s.size()) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[pos + i]; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | (uint32_t)d;
    }
    out = v;
    return true;
}
// "0x16DB" or "0x08A7-0x08EC" (whole string).
bool parseIdRange(const std::string& s, uint32_t& a, uint32_t& b) {
    auto prefix = [&](size_t p) { return p + 1 < s.size() && s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X'); };
    if (!prefix(0) || !hexN(s, 2, 4, a)) return false;
    if (s.size() == 6) { b = a; return true; }
    return s.size() == 13 && s[6] == '-' && prefix(7) && hexN(s, 9, 4, b);
}
// Name without its " (variant)" and without any " with ..." part.
std::string baseName(const std::string& n) {
    std::string b = n;
    size_t i = b.find(" (");
    if (i != std::string::npos && i > 0) b = b.substr(0, i);
    size_t j = b.find(" with ");
    if (j != std::string::npos && j > 0) b = b.substr(0, j);
    return b;
}
std::string variantOf(const std::string& n) {
    size_t i = n.find(" (");
    if (i == std::string::npos || i == 0) return std::string();
    std::string v = n.substr(i + 2);
    while (!v.empty() && v[v.size() - 1] == ')') v.erase(v.size() - 1);
    return v;
}
bool wordChar(char c) { return isalnum((unsigned char)c) || ((unsigned char)c & 0x80); }

struct ColorDef { std::string name; uint32_t rgb; };
// First color word of `t` (already lower-case); at each position the longest name wins.
bool findColor(const std::string& t, const std::vector<ColorDef>& byLength, uint32_t& rgb) {
    for (size_t pos = 0; pos < t.size(); pos++) {
        if (pos && wordChar(t[pos - 1])) continue;
        for (size_t k = 0; k < byLength.size(); k++) {
            const std::string& n = byLength[k].name;
            size_t end = pos + n.size();
            if (end > t.size() || t[pos] != n[0] || t.compare(pos, n.size(), n) != 0) continue;
            if (end == t.size() || !wordChar(t[end])) { rgb = byLength[k].rgb; return true; }
        }
    }
    return false;
}
void keepLast(std::unordered_map<std::string, int>& m, const std::string& key, int rule) {
    std::unordered_map<std::string, int>::iterator it = m.find(key);
    if (it == m.end()) m[key] = rule; else if (rule > it->second) it->second = rule;
}

} // namespace

void GlyphMap::build(const std::vector<Item>& items, const char* path) {
    glyph_.assign(items.size(), 0);
    color_.assign(items.size(), 0);
    singleGlyph_.clear();
    loaded_ = false;
    glyphCount_ = rules_ = ignored_ = recognized_ = colored_ = 0;
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) return;                                           // optional file: no pictures

    std::unordered_map<std::string, GlyphDef> glyphs;
    std::vector<ColorDef> colors;
    struct RawRule { std::string glyph, pattern; };
    std::vector<RawRule> raw;
    char buf[256];
    int lines = 0;
    while (fgets(buf, sizeof buf, f) && lines < 8192) {       // hard cap: corrupted / huge file
        lines++;
        std::string line = trim(buf);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> w = splitWords(line);
        if (w[0] == "glyph" && w.size() >= 3 && allDigits(w[1]) && w[1].size() <= 3) {
            int idx = atoi(w[1].c_str());
            bool tint = false, single = false, flagsOk = idx <= 255;   // one byte per item
            for (size_t k = 3; k < w.size(); k++) {
                if (w[k] == "tint") tint = true;
                else if (w[k] == "single") single = true;
                else flagsOk = false;
            }
            if (!flagsOk) { ignored_++; continue; }
            GlyphDef d = { idx, tint };
            glyphs[w[2]] = d;
            if ((int)singleGlyph_.size() <= idx) singleGlyph_.resize(idx + 1, 0);
            singleGlyph_[idx] = single && idx != 0 ? 1 : 0;   // "unknown" is never single
            if (idx + 1 > glyphCount_) glyphCount_ = idx + 1;
            continue;
        }
        uint32_t rgb = 0;
        if (w[0] == "color" && w.size() >= 3 && w[1].size() == 6 && hexN(w[1], 0, 6, rgb)) {
            std::string name = w[2];
            for (size_t i = 3; i < w.size(); i++) name += " " + w[i];
            ColorDef c = { lower(name), rgb };
            colors.push_back(c);
            continue;
        }
        if (w.size() < 2) { ignored_++; continue; }
        RawRule r = { w[0], trim(line.substr(line.find(w[0]) + w[0].size())) };
        raw.push_back(r);
    }
    fclose(f);
    if (glyphs.empty()) { ignored_ += (int)raw.size(); return; }
    loaded_ = true;

    // Sort the rules by kind. The value kept everywhere is the rule's position in the file.
    std::vector<int> ruleGlyph, ruleTint;                     // per rule: cell index, tintable
    std::vector<int> best(items.size(), -1);                  // per item: last matching rule
    std::unordered_map<std::string, int> exact, suffix;
    std::vector<std::pair<int, std::string> > subs;           // "~text", in file order
    int star = -1;
    for (size_t k = 0; k < raw.size(); k++) {
        std::unordered_map<std::string, GlyphDef>::const_iterator g = glyphs.find(raw[k].glyph);
        const int rule = (int)ruleGlyph.size();
        if (g == glyphs.end()) { ignored_++; continue; }       // unknown glyph name
        const std::string& pat = raw[k].pattern;
        uint32_t a = 0, b = 0;
        bool ok = true;
        if (pat == "*") star = rule;
        else if (parseIdRange(pat, a, b)) {
            for (uint32_t i = a; i <= b && i < items.size(); i++) best[i] = rule;   // rules come in increasing order
        } else {
            std::string r = lower(pat);
            if (r[0] == '~') { r = trim(r.substr(1)); if (r.empty()) ok = false; else subs.push_back(std::make_pair(rule, r)); }
            else if (r[0] == '$') { r = trim(r.substr(1)); if (r.empty()) ok = false; else keepLast(suffix, r, rule); }
            else keepLast(exact, r, rule);
        }
        if (!ok) { ignored_++; continue; }
        ruleGlyph.push_back(g->second.idx);
        ruleTint.push_back(g->second.tint ? 1 : 0);
        rules_++;
    }

    std::vector<ColorDef> byLength = colors;                  // longest first, file order among equals
    std::stable_sort(byLength.begin(), byLength.end(),
                     [](const ColorDef& x, const ColorDef& y) { return x.name.size() > y.name.size(); });

    for (size_t i = 0; i < items.size(); i++) {
        if (trim(items[i].name).empty()) continue;            // unnamed id: "?" and no color
        const std::string low = lower(items[i].name);
        const std::string base = baseName(low);
        int b = best[i];
        if (star > b) b = star;
        std::unordered_map<std::string, int>::const_iterator it = exact.find(low);
        if (it != exact.end() && it->second > b) b = it->second;
        // "$word": the base name is the word, or ends with it after a space or a hyphen.
        for (size_t p = 0; p < base.size(); p++) {
            if (p && base[p - 1] != ' ' && base[p - 1] != '-') continue;
            it = suffix.find(p ? base.substr(p) : base);
            if (it != suffix.end() && it->second > b) b = it->second;
        }
        for (size_t k = subs.size(); k-- > 0; ) {              // later rules first: stop at the first hit
            if (subs[k].first < b) break;
            if (low.find(subs[k].second) != std::string::npos) { b = subs[k].first; break; }
        }
        if (b < 0) continue;
        glyph_[i] = (uint8_t)ruleGlyph[b];
        if (glyph_[i] != 0) recognized_++;
        if (!ruleTint[b]) continue;
        uint32_t rgb = 0;
        const std::string var = variantOf(low);
        if ((!var.empty() && findColor(var, byLength, rgb)) || findColor(base, byLength, rgb)) {
            color_[i] = 0x01000000u | rgb;
            colored_++;
        }
    }
}

int GlyphMap::glyphOf(uint16_t id) const { return id < glyph_.size() ? glyph_[id] : 0; }
bool GlyphMap::singleKind(uint16_t id) const {
    if (!loaded_ || id >= glyph_.size()) return false;
    const uint8_t g = glyph_[id];
    return g < singleGlyph_.size() && singleGlyph_[g] != 0;
}
bool GlyphMap::colorOf(uint16_t id, uint32_t& rgb) const {
    if (id >= color_.size() || color_[id] == 0) return false;
    rgb = color_[id] & 0xFFFFFFu;
    return true;
}

// -------------------------------- favorites --------------------------------
static bool labelLess(const Fav& a, const Fav& b) {
    // Same order as upstream favAlphaLess: case-insensitive A-Z.
    const std::string la = lower(a.label), lb = lower(b.label);
    if (la != lb) return la < lb;
    return a.id < b.id;
}
static bool hasId(const std::vector<uint16_t>& v, uint16_t id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}
static void eraseId(std::vector<uint16_t>& v, uint16_t id) {
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
}

bool Favorites::parseFile(const std::string& p, bool requireEnd,
                          std::vector<uint16_t>& added, std::vector<uint16_t>& hidden, int& ignored) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    added.clear(); hidden.clear(); ignored = 0;
    bool sawEnd = false;
    char buf[256];
    int lines = 0;
    while (fgets(buf, sizeof buf, f) && lines < 1024) {        // cap: corrupted / huge file
        lines++;
        std::string line = trim(buf);
        if (line.empty()) continue;
        if (line[0] == '#') { if (line == "# end") sawEnd = true; continue; }
        uint16_t id = 0;
        if ((line[0] != '+' && line[0] != '-') || line.size() < 5 || !parseHexId(line.substr(1, 4), id)) {
            ignored++; continue;
        }
        if (line.size() > 5 && line[5] != ' ') { ignored++; continue; }
        bool known = id < items_->size() && !(*items_)[id].name.empty();
        if (line[0] == '+') {
            if (!known || hasId(added, id)) { ignored++; continue; }
            added.push_back(id);
        } else {
            bool isBuiltin = false;
            for (size_t i = 0; i < builtins_.size(); i++) if (builtins_[i].id == id) isBuiltin = true;
            if (!isBuiltin || hasId(hidden, id)) { ignored++; continue; }
            hidden.push_back(id);
        }
    }
    fclose(f);
    if (requireEnd && !sawEnd) return false;
    return true;
}

void Favorites::load(const std::vector<Item>& items, const char* const* builtinNames, int builtinCount,
                     const std::string& path) {
    items_ = &items;
    path_ = path;
    builtins_.clear(); added_.clear(); hidden_.clear();
    ignored_ = missing_ = 0;
    fromTmp_ = bakDone_ = false;

    // 1) Built-ins, resolved by exact name like upstream findItemIdByName().
    for (int i = 0; i < builtinCount; i++) {
        int found = -1;
        for (size_t k = 0; k < items.size(); k++) if (items[k].name == builtinNames[i]) { found = (int)k; break; }
        if (found < 0) { missing_++; continue; }              // hidden instead of showing FFFE
        bool dup = false;
        for (size_t k = 0; k < builtins_.size(); k++) if (builtins_[k].id == found) dup = true;
        if (!dup) builtins_.push_back({ (uint16_t)found, items[found].name, true });
    }

    // 2) Overlay. A complete .tmp means a rewrite was interrupted after writing the
    //    new content: it is the newest data. Otherwise read the main file.
    //    Any problem only drops lines; the built-ins always load.
    if (!items.empty()) {
        std::vector<uint16_t> a, h; int ign = 0;
        if (parseFile(path_ + ".tmp", true, a, h, ign)) { fromTmp_ = true; }
        else if (!parseFile(path_, false, a, h, ign)) { a.clear(); h.clear(); ign = 0; }
        // Enforce the limit on load too.
        int room = FAV_LIMIT - ((int)builtins_.size() - (int)h.size());
        if (room < 0) room = 0;
        if ((int)a.size() > room) { ign += (int)a.size() - room; a.resize(room); }
        added_ = a; hidden_ = h; ignored_ = ign;
    }
    rebuild();
}

void Favorites::rebuild() {
    list_.clear();
    for (size_t i = 0; i < builtins_.size(); i++)
        if (!hasId(hidden_, builtins_[i].id)) list_.push_back(builtins_[i]);
    for (size_t i = 0; i < added_.size(); i++) {
        uint16_t id = added_[i];
        bool dup = false;
        for (size_t k = 0; k < list_.size(); k++) if (list_[k].id == id) dup = true;
        if (!dup && items_ && id < items_->size()) list_.push_back({ id, (*items_)[id].name, false });
    }
    std::sort(list_.begin(), list_.end(), labelLess);
}

bool Favorites::isFav(uint16_t id) const {
    for (size_t i = 0; i < list_.size(); i++) if (list_[i].id == id) return true;
    return false;
}
bool Favorites::isUserAdded(uint16_t id) const {
    for (size_t i = 0; i < list_.size(); i++) if (list_[i].id == id) return !list_[i].builtin;
    return false;
}

Favorites::Result Favorites::add(uint16_t id) {
    if (readOnly()) return READ_ONLY;
    if (id >= items_->size() || (*items_)[id].name.empty()) return INVALID;
    if (isFav(id)) return ALREADY;
    if ((int)list_.size() >= FAV_LIMIT) return FULL;
    std::vector<uint16_t> oldA = added_, oldH = hidden_;
    if (hasId(hidden_, id)) eraseId(hidden_, id); else added_.push_back(id);
    rebuild();
    if (!save()) { added_ = oldA; hidden_ = oldH; rebuild(); return IO_ERROR; }
    return ADDED;
}

Favorites::Result Favorites::remove(uint16_t id) {
    if (readOnly()) return READ_ONLY;
    if (!isFav(id)) return NOT_FAV;
    std::vector<uint16_t> oldA = added_, oldH = hidden_;
    bool builtin = false;
    for (size_t i = 0; i < builtins_.size(); i++) if (builtins_[i].id == id) builtin = true;
    eraseId(added_, id);
    if (builtin && !hasId(hidden_, id)) hidden_.push_back(id);
    rebuild();
    if (!save()) { added_ = oldA; hidden_ = oldH; rebuild(); return IO_ERROR; }
    return REMOVED;
}

bool Favorites::save() {
    // SD card only. Keep one copy of the user's original file before the first rewrite.
    if (!bakDone_ && fileExists(path_.c_str())) copyFile(path_.c_str(), (path_ + ".bak").c_str());
    bakDone_ = true;

    std::string out =
        "# ACNH Power Pockets - user favorites (managed by the app)\n"
        "# +XXXX name = added favorite, -XXXX name = hidden built-in favorite\n"
        "# XXXX is the hex item id (authoritative); the name is informative only.\n";
    char line[160];
    for (size_t i = 0; i < added_.size(); i++) {
        snprintf(line, sizeof line, "+%04X %.120s\n", (unsigned)added_[i], (*items_)[added_[i]].name.c_str());
        out += line;
    }
    for (size_t i = 0; i < hidden_.size(); i++) {
        snprintf(line, sizeof line, "-%04X %.120s\n", (unsigned)hidden_[i], (*items_)[hidden_[i]].name.c_str());
        out += line;
    }
    out += "# end\n";

    const std::string tmp = path_ + ".tmp";
    if (!writeFileAll(tmp.c_str(), (const uint8_t*)out.data(), out.size())) {
        ::remove(tmp.c_str());
        return false;                       // old file untouched
    }
    ::remove(path_.c_str());                // FAT rename does not overwrite
    if (rename(tmp.c_str(), path_.c_str()) != 0) {
        // The complete .tmp (with "# end") stays and is preferred on next load.
        return fileExists(tmp.c_str());
    }
    return true;
}

bool Favorites::resetToDefaults() {
    if (readOnly()) return false;
    const std::string bak = path_ + ".bak";
    if (fileExists(path_.c_str())) {
        ::remove(bak.c_str());
        if (rename(path_.c_str(), bak.c_str()) != 0) return false;
    }
    ::remove((path_ + ".tmp").c_str());
    added_.clear(); hidden_.clear();
    bakDone_ = true;
    rebuild();
    return true;
}

// --------------------------------- loadouts --------------------------------
bool pocketsPlausible(const uint8_t* pockets) {
    if (!pockets) return false;
    for (int s = 1; s <= SLOT_COUNT; s++) {
        uint16_t id = readSlot(pockets, s).id;
        if (id != ID_EMPTY && !validPlaceId(id)) return false;
    }
    return true;
}

bool loadoutRead(const char* path, uint8_t out[POCKETS_BYTES]) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t tmp[POCKETS_BYTES];
    size_t rd = fread(tmp, 1, POCKETS_BYTES, f);
    fclose(f);
    if (rd != (size_t)POCKETS_BYTES || !pocketsPlausible(tmp)) return false;
    memcpy(out, tmp, POCKETS_BYTES);
    return true;
}

bool loadoutWrite(const char* path, const uint8_t* pockets) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t wr = fwrite(pockets, 1, POCKETS_BYTES, f);
    int rc = fclose(f);
    return wr == (size_t)POCKETS_BYTES && rc == 0;
}

// ------------------------------- SD file utils -----------------------------
bool fileExists(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

long fileSize(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

bool writeFileAll(const char* path, const uint8_t* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    size_t wr = size ? fwrite(data, 1, size, f) : 0;
    bool ok = (wr == size) && fflush(f) == 0 && !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool copyFile(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    if (!in) return false;
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }
    static uint8_t buf[64 * 1024];
    bool ok = true;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (ferror(in)) ok = false;
    fclose(in);
    if (fflush(out) != 0) ok = false;
    if (fclose(out) != 0) ok = false;
    return ok;
}

// ------------------------------ backup checking ----------------------------
size_t diffOutside(const uint8_t* a, const uint8_t* b, size_t size, const ByteRange* allowed, int n) {
    if (!a || !b) return size ? size : 1;
    size_t bad = 0;
    for (size_t i = 0; i < size; i++) {
        if (a[i] == b[i]) continue;
        bool ok = false;
        for (int k = 0; k < n && !ok; k++)
            ok = i >= allowed[k].off && i - allowed[k].off < allowed[k].len;
        if (!ok) bad++;
    }
    return bad;
}

// ---------------------------- pre-edit backup mark --------------------------
static uint64_t fnv1a(uint64_t h, const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; i++) { h ^= d[i]; h *= 0x100000001B3ull; }
    return h;
}
std::string stateSignature(const uint8_t* a, size_t na, const uint8_t* b, size_t nb) {
    uint64_t h = 0xCBF29CE484222325ull;
    if (a) h = fnv1a(h, a, na);
    h ^= (uint64_t)na; h *= 0x100000001B3ull;                  // sizes are part of the state
    if (b) h = fnv1a(h, b, nb);
    h ^= (uint64_t)nb; h *= 0x100000001B3ull;
    char buf[24];
    snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}
bool writeStateMark(const char* path, const std::string& signature, bool pairPresent) {
    const std::string s = signature + (pairPresent ? " ok\n" : " nopair\n");
    return writeFileAll(path, (const uint8_t*)s.data(), s.size());
}
bool stateMarkMatches(const char* path, const std::string& signature) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char buf[64];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return !signature.empty() && trim(buf) == signature + " ok";
}

int regionsHashOk(const uint8_t* plain, size_t size, const RegionRef* r, int n, HashFn fn) {
    int ok = 0;
    if (!plain || !r || !fn) return 0;
    for (int i = 0; i < n; i++) {
        if ((size_t)r[i].off + 4 + r[i].len > size) continue;
        if (fn(plain + r[i].off + 4, r[i].len) == rd32(plain + r[i].off)) ok++;
    }
    return ok;
}

} // namespace pp
