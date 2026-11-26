// ACNH Power Pockets - lists from catalogue.ac, see pp_lists.h.
#include "pp_lists.h"
#include "pp_json.h"

#include <algorithm>
#include <map>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>

namespace pp {

// ------------------------------- clothing ids ------------------------------
bool ClothingIds::applyLine(const std::string& raw) {
    std::string line = raw;
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') return true;
    if (line[0] != 'c') return false;
    size_t i = 1;
    int cid = 0;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9' && i < 7) cid = cid * 10 + (line[i++] - '0');
    if (i == 1 || i >= line.size() || line[i] != ' ') return false;
    std::vector<uint16_t> ids;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') i++;
        if (i >= line.size()) break;
        size_t j = i;
        while (j < line.size() && line[j] != ' ') j++;
        uint16_t v = 0;
        if (j - i != 4 || !parseHexId(line.substr(i, 4), v) || ids.size() >= 64) return false;
        ids.push_back(v);
        i = j;
    }
    if (ids.empty()) return false;
    map_[cid] = ids;
    return true;
}
bool ClothingIds::load(const char* path) {
    map_.clear();
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) return false;
    char buf[512];
    int lines = 0;
    while (fgets(buf, sizeof buf, f) && ++lines < 20000) applyLine(buf);
    fclose(f);
    return true;
}
bool ClothingIds::lookup(int cid, int color, uint16_t& id) const {
    auto it = map_.find(cid);
    if (it == map_.end() || color < 0 || color >= (int)it->second.size()) return false;
    id = it->second[(size_t)color];
    return true;
}

// --------------------------------- reading ---------------------------------
namespace {
struct Reader {
    const std::vector<Item>& items;
    const ClothingIds& clothing;
    ListStats& st;
    bool placeable(uint16_t id) const { return id < items.size() && !items[id].name.empty() && validPlaceId(id); }
    static bool digits(const std::string& s, size_t a, size_t b, int& v) {
        if (b <= a || b - a > 6) return false;
        v = 0;
        for (size_t i = a; i < b; i++) { if (s[i] < '0' || s[i] > '9') return false; v = v * 10 + (s[i] - '0'); }
        return true;
    }
    // One key of a list ("79", "79-3", "c652-1", "d632", "brd09"...). False = skipped (counted).
    bool resolve(const std::string& key, uint16_t& id) {
        const size_t dash = key.find('-');
        const std::string base = key.substr(0, dash);
        int var = -1;
        if (dash != std::string::npos && !digits(key, dash + 1, key.size(), var)) { st.unknown++; return false; }
        int n = 0;
        if (digits(base, 0, base.size(), n)) {                       // a game id already
            if (n > 0xFFFF || !placeable((uint16_t)n)) { st.unknown++; return false; }
            if (var > 0) st.variants++;
            id = (uint16_t)n;
            return true;
        }
        if (base.size() > 1 && base[0] == 'c' && digits(base, 1, base.size(), n)) {   // clothing: one id per color
            if (!clothing.lookup(n, var < 0 ? 0 : var, id) || !placeable(id)) { st.unknown++; return false; }
            return true;
        }
        if (base.size() > 1 && base[0] == 'd' && digits(base, 1, base.size(), n)) { st.recipes++; return false; }
        st.notItems++;
        return false;
    }
    void add(ImportedList& l, const std::string& key, double qty) {
        uint16_t id;
        if (!resolve(key, id)) return;
        int q = qty >= 1 && qty < 100000 ? (int)qty : 1;
        for (ListItem& it : l.items) if (it.id == id) { it.qty = std::min(99999, it.qty + q); return; }
        if ((int)l.items.size() < LIST_ITEMS_MAX) l.items.push_back({ id, q });
    }
    // {"name": "...", "items": {"79": 1, "c652-1": [0, 2]}}
    bool list(const Json& j, ImportedList& out) {
        if (j.type != Json::OBJ) return false;
        out.name = j.str("name");
        const Json* items = j.get("items");
        if (!items || items->type != Json::OBJ) return false;
        for (size_t i = 0; i < items->k.size(); i++) {
            const Json& v = items->a[i];
            double q = 1;
            if (v.type == Json::NUM) q = v.n;
            else if (v.type == Json::ARR && v.a.size() == 2 && v.a[1].type == Json::NUM) q = v.a[1].n;   // [have, need]
            else continue;
            add(out, items->k[i], q);
        }
        return true;
    }
};

std::string percentDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2;
        } else o += s[i];
    }
    return o;
}
int b64(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == ' ') return 62;                 // a '+' may have become a space in a query string
    if (c == '/') return 63;
    return -1;
}
bool base64(const std::string& s, std::string& out) {
    uint32_t acc = 0; int bits = 0;
    for (char c : s) {
        if (c == '=') break;
        int v = b64(c);
        if (v < 0) return false;
        acc = acc << 6 | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out += (char)(acc >> bits & 0xFF); }
    }
    return true;
}
// catalogue.ac's own packing: 6 bits per character, 4 bits per symbol of "0123456789:;acdr".
bool unpack(const std::string& s, std::string& out) {
    static const char SYM[] = "0123456789:;acdr";
    std::string bits;
    for (char c : s) {
        int v = b64(c);
        if (v < 0) return false;
        for (int k = 5; k >= 0; k--) bits += (char)('0' + (v >> k & 1));
    }
    for (size_t i = 0; i + 4 <= bits.size(); i += 4) out += SYM[(bits[i] - '0') << 3 | (bits[i + 1] - '0') << 2 | (bits[i + 2] - '0') << 1 | (bits[i + 3] - '0')];
    if (!out.empty() && out.back() == ';') out.pop_back();
    return true;
}
// https://catalogue.ac/?l=<base64 name>-<packed items>
bool shareLink(const std::string& text, Reader& r, ImportedList& out) {
    if (text.find("catalogue.ac") == std::string::npos) return false;
    size_t q = text.find('?');
    if (q == std::string::npos) return false;
    size_t end = q;
    while (end < text.size() && !isspace((unsigned char)text[end])) end++;
    std::string query = text.substr(q + 1, end - q - 1), value;
    size_t i = 0;
    while (i <= query.size()) {
        size_t j = query.find_first_of(";&", i);
        if (j == std::string::npos) j = query.size();
        if (query.compare(i, 2, "l=") == 0) value = query.substr(i + 2, j - i - 2);
        i = j + 1;
    }
    value = percentDecode(value);
    const size_t dash = value.find('-');
    if (value.empty() || dash == std::string::npos || value.size() > LIST_TEXT_MAX) return false;
    std::string name, packed;
    if (!base64(value.substr(0, dash), name) || !unpack(value.substr(dash + 1), packed)) return false;
    out.name = name;
    size_t p = 0;
    while (p < packed.size()) {
        size_t e = packed.find(';', p);
        if (e == std::string::npos) e = packed.size();
        std::string entry = packed.substr(p, e - p);
        p = e + 1;
        if (entry.empty()) continue;
        std::vector<std::string> f;
        size_t s = 0;
        for (;;) { size_t c = entry.find(':', s); f.push_back(entry.substr(s, c == std::string::npos ? std::string::npos : c - s)); if (c == std::string::npos) break; s = c + 1; }
        std::string key = f[0];
        if (key.compare(0, 2, "rr") == 0 || key.compare(0, 2, "dd") == 0) { r.st.notItems++; continue; }   // villagers, special characters
        const size_t a = key.rfind('a');
        if (a != std::string::npos && a > 1) key = key.substr(0, a) + "-" + key.substr(a + 1);   // "79a3" = "79-3"
        double qty = 1;
        if (f.size() == 2) qty = atof(f[1].c_str());
        else if (f.size() >= 3) qty = atof(f[2].c_str());
        r.add(out, key, qty);
    }
    return true;
}
} // namespace

ListResult readListText(const std::string& text, const std::vector<Item>& items, const ClothingIds& clothing,
                        std::vector<ImportedList>& out, ListStats& stats) {
    out.clear();
    stats = ListStats();
    if (text.size() > LIST_TEXT_MAX) return LR_TOO_BIG;
    Reader r { items, clothing, stats };
    size_t first = 0;
    while (first < text.size() && isspace((unsigned char)text[first])) first++;
    if (first < text.size() && text[first] == '{') {
        Json j;
        JsonParser p(text);
        if (!p.parse(j) || j.type != Json::OBJ) return LR_BAD;
        const Json* lists = j.get("lists");
        if (lists) {                                             // a full export
            if (lists->type != Json::ARR) return LR_BAD;
            for (const Json& l : lists->a) {
                ImportedList il;
                if (r.list(l, il) && !il.items.empty() && out.size() < 200) out.push_back(il);
            }
        } else if (j.get("items")) {                             // one list
            ImportedList il;
            if (!r.list(j, il)) return LR_BAD;
            if (!il.items.empty()) out.push_back(il);
        } else return LR_NOT_LIST;
    } else {
        ImportedList il;
        if (!shareLink(text, r, il)) return LR_NOT_LIST;
        if (!il.items.empty()) out.push_back(il);
    }
    for (size_t i = 0; i < out.size(); i++) {
        std::string& n = out[i].name;
        std::string clean;
        for (char c : n) if ((unsigned char)c >= 0x20) clean += c;
        if (clean.size() > 60) {                                 // never cut inside a character
            size_t cut = 60;
            while (cut > 0 && ((unsigned char)clean[cut] & 0xC0) == 0x80) cut--;
            clean.resize(cut);
        }
        n = clean.empty() ? "List " + std::to_string(i + 1) : clean;
    }
    return out.empty() ? LR_EMPTY : LR_OK;
}

// ------------------------------- still needed ------------------------------
std::vector<ListItem> stillNeeded(const ImportedList& list, const Pockets& pockets,
                                  const std::function<bool(uint16_t)>& isSingle, int& already) {
    std::map<uint16_t, int> have;
    for (int s = 1; s <= pockets.count; s++) {
        if (!pockets.valid(s)) continue;
        const SlotRec r = readSlot(pockets, s);
        if (r.id == ID_EMPTY) continue;
        have[r.id] += isSingle(r.id) ? 1 : rawToQty(r.raw);
    }
    std::vector<ListItem> out;
    already = 0;
    for (const ListItem& it : list.items) {
        int& h = have[it.id];
        const int used = std::min(h, it.qty);
        h -= used;                                       // an item listed twice is not counted twice
        already += used;
        if (it.qty > used) out.push_back({ it.id, it.qty - used });
    }
    return out;
}

// ---------------------------------- folder ---------------------------------
namespace {
bool readAll(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[16384];
    size_t n;
    out.clear();
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        if (out.size() + n > LIST_TEXT_MAX) { fclose(f); return false; }
        out.append(buf, n);
    }
    fclose(f);
    return true;
}
bool listName(const std::string& n) {
    if (n.empty() || n[0] == '.' || n.size() > 80) return false;
    const size_t dot = n.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = n.substr(dot + 1);
    for (char& c : ext) c = (char)tolower((unsigned char)c);
    if (ext != "json" && ext != "txt") return false;
    for (char c : n) if (c == '/' || c == '\\') return false;
    return true;
}
std::string slugOf(const std::string& name) {
    std::string s;
    for (unsigned char c : name) {
        if (isalnum(c) && c < 0x80) s += (char)tolower(c);
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    if (s.size() > 40) s.resize(40);
    return s.empty() ? std::string("list") : s;
}
std::string jsonString(const std::string& v) {
    std::string o = "\"";
    for (unsigned char c : v) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c < 0x20) { char u[8]; snprintf(u, sizeof u, "\\u%04x", c); o += u; }
        else o += (char)c;
    }
    return o + "\"";
}
// The stored form of a received list: catalogue.ac's single-list format, with game ids.
std::string listJson(const ImportedList& l) {
    std::string o = "{\"name\":" + jsonString(l.name) + ",\"items\":{";
    for (size_t i = 0; i < l.items.size(); i++) o += (i ? "," : "") + jsonString(std::to_string(l.items[i].id)) + ":" + std::to_string(l.items[i].qty);
    return o + "}}\n";
}
bool writeFile(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = fclose(f) == 0 && ok;
    struct stat st;
    if (ok && stat(path.c_str(), &st) == 0) ok = ::remove(path.c_str()) == 0;   // replace (libnx rename doesn't overwrite)
    ok = ok && ::rename(tmp.c_str(), path.c_str()) == 0;
    if (!ok) ::remove(tmp.c_str());
    return ok;
}
} // namespace

void ImportedLists::load(const std::string& dir, const std::vector<Item>& items, const ClothingIds& clothing) {
    files_.clear();
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (struct dirent* e = readdir(d)) if (listName(e->d_name)) names.push_back(e->d_name);
    closedir(d);
    std::sort(names.begin(), names.end());
    if ((int)names.size() > LIST_FILES_MAX) names.resize(LIST_FILES_MAX);
    for (const std::string& n : names) {
        ListFile f;
        f.file = n;
        struct stat st;
        if (stat((dir + "/" + n).c_str(), &st) == 0) f.mtime = (long)st.st_mtime;
        std::string text;
        f.result = readAll(dir + "/" + n, text) ? readListText(text, items, clothing, f.lists, f.stats) : LR_TOO_BIG;
        files_.push_back(f);
    }
    // One list per name: the newer file wins; on a tie, the file named after the list.
    std::map<std::string, std::pair<size_t, size_t> > best;
    auto better = [&](size_t a, size_t b, const std::string& name) {      // file a beats file b?
        if (files_[a].mtime != files_[b].mtime) return files_[a].mtime > files_[b].mtime;
        return files_[a].file == slugOf(name) + ".json";
    };
    for (size_t fi = 0; fi < files_.size(); fi++)
        for (size_t li = 0; li < files_[fi].lists.size(); li++) {
            const std::string& nm = files_[fi].lists[li].name;
            auto it = best.find(nm);
            if (it == best.end() || better(fi, it->second.first, nm)) best[nm] = std::make_pair(fi, li);
        }
    for (size_t fi = 0; fi < files_.size(); fi++) {
        std::vector<ImportedList> keep;
        for (size_t li = 0; li < files_[fi].lists.size(); li++) {
            auto it = best.find(files_[fi].lists[li].name);
            if (it != best.end() && it->second.first == fi && it->second.second == li) keep.push_back(files_[fi].lists[li]);
        }
        files_[fi].lists.swap(keep);
    }
}
int ImportedLists::listCount() const {
    int n = 0;
    for (const ListFile& f : files_) n += (int)f.lists.size();
    return n;
}
ListResult ImportedLists::add(const std::string& dir, const std::string& text, const std::vector<Item>& items,
                              const ClothingIds& clothing, std::vector<ImportedList>& got, ListStats& stats, int& replaced) {
    replaced = 0;
    const ListResult r = readListText(text, items, clothing, got, stats);
    if (r != LR_OK) return r;
    struct stat st;
    if (stat(dir.c_str(), &st) != 0 && mkdir(dir.c_str(), 0777) != 0) return LR_SD;
    // Two lists of the same name in one export: the second becomes "Name (2)".
    for (size_t i = 0; i < got.size(); i++)
        for (int k = 2, again = 1; again && k < 100; k++) {
            again = 0;
            for (size_t j = 0; j < i; j++) if (got[j].name == got[i].name) { got[i].name = got[i].name.substr(0, 54) + " (" + std::to_string(k) + ")"; again = 1; break; }
        }
    std::map<std::string, std::string> owner;            // file -> name of the list it holds now
    std::vector<std::string> known;
    for (const ListFile& f : files_) {
        if (f.lists.size() == 1) owner[f.file] = f.lists[0].name;
        for (const ImportedList& l : f.lists) known.push_back(l.name);
    }
    std::vector<std::string> written;
    for (const ImportedList& l : got) {
        if (std::find(known.begin(), known.end(), l.name) != known.end()) replaced++;
        std::string file;
        for (int k = 1; k < 100; k++) {                  // "<slug>.json", or "<slug>-2.json" if another list has that name
            file = slugOf(l.name) + (k > 1 ? "-" + std::to_string(k) : std::string()) + ".json";
            if (std::find(written.begin(), written.end(), file) != written.end()) continue;
            auto o = owner.find(file);
            if (o == owner.end() || o->second == l.name) break;
            if (stat((dir + "/" + file).c_str(), &st) != 0) break;
        }
        if (!writeFile(dir + "/" + file, listJson(l))) { load(dir, items, clothing); return LR_SD; }
        written.push_back(file);
    }
    // Files that only held lists replaced just now are gone for good (an older export, say).
    for (const ListFile& f : files_) {
        if (f.result != LR_OK || f.lists.empty() || std::find(written.begin(), written.end(), f.file) != written.end()) continue;
        bool all = true;
        for (const ImportedList& l : f.lists) {
            bool in = false;
            for (const ImportedList& g : got) in |= g.name == l.name;
            all &= in;
        }
        if (all) ::remove((dir + "/" + f.file).c_str());
    }
    load(dir, items, clothing);
    return LR_OK;
}
bool ImportedLists::remove(const std::string& dir, const std::string& file, const std::vector<Item>& items, const ClothingIds& clothing) {
    if (!listName(file)) return false;
    bool known = false;
    for (const ListFile& f : files_) known |= f.file == file;
    const bool ok = known && ::remove((dir + "/" + file).c_str()) == 0;
    load(dir, items, clothing);
    return ok;
}

} // namespace pp
