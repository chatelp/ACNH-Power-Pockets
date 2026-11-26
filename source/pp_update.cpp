// ACNH Power Pockets - update logic, see pp_update.h.
#include "pp_update.h"
#include "pp_json.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace pp {

// The repository can be overridden at build time to test updates (make UPDATE_REPO=owner/name).
#ifndef UPDATE_REPO_STR
#define UPDATE_REPO_STR "chatelp/ACNH-Power-Pockets"
#endif
const char* const UPDATE_API_URL = "https://api.github.com/repos/" UPDATE_REPO_STR "/releases/latest";
static const char* const DOWNLOAD_BASE = "https://github.com/" UPDATE_REPO_STR "/releases/download/";
static const char* const ZIP_PREFIX = "switch/acnh_power_pockets/";
static const char* const NRO_NAME = "acnh_power_pockets.nro";
static const char* const KEEP_IF_PRESENT = "nonstackable.txt";   // the user may have edited it
static const size_t MAX_ENTRIES = 64;
static const uint64_t MAX_UNPACKED = 64ull * 1024 * 1024;

// ------------------------------- versions ----------------------------------
Version parseVersion(const std::string& s) {
    Version v;
    size_t i = (!s.empty() && s[0] == 'v') ? 1 : 0;
    int part[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) {
        size_t start = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - start < 5) part[k] = part[k] * 10 + (s[i++] - '0');
        if (i == start) return v;
        if (k < 2) { if (i >= s.size() || s[i] != '.') return v; i++; }
    }
    if (i != s.size()) return v;
    v.major = part[0]; v.minor = part[1]; v.patch = part[2]; v.ok = true;
    return v;
}
int compareVersions(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

// -------------------------------- SHA-256 ----------------------------------
static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
void Sha256::reset() {
    static const uint32_t init[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    memcpy(h_, init, sizeof h_);
    total_ = 0; used_ = 0;
}
void Sha256::block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}
void Sha256::update(const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    total_ += n;
    while (n) {
        size_t k = sizeof buf_ - used_;
        if (k > n) k = n;
        memcpy(buf_ + used_, p, k);
        used_ += k; p += k; n -= k;
        if (used_ == sizeof buf_) { block(buf_); used_ = 0; }
    }
}
std::string Sha256::hex() {
    uint64_t bits = total_ * 8;
    uint8_t one = 0x80, zero = 0;
    update(&one, 1);
    while (used_ != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    update(len, 8);
    std::string s;
    char t[9];
    for (int i = 0; i < 8; i++) { snprintf(t, sizeof t, "%08x", (unsigned)h_[i]); s += t; }
    return s;
}

namespace {
bool isHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
// One bullet of the release notes as plain text: no bold, no code marks, no links.
std::string plainLine(const std::string& md) {
    std::string o;
    for (size_t i = 0; i < md.size(); i++) {
        char c = md[i];
        if (c == '*' || c == '`') continue;
        if (c == '[') continue;
        if (c == ']') {                                  // "](address)": drop the address
            if (i + 1 < md.size() && md[i + 1] == '(') { size_t e = md.find(')', i); if (e == std::string::npos) break; i = e; }
            continue;
        }
        if ((unsigned char)c < 0x20) continue;
        o += c;
    }
    while (!o.empty() && o[0] == ' ') o.erase(0, 1);
    return o;
}
// Up to 4 bullets of the "What's new" section (or the first ones of the notes).
std::vector<std::string> highlightsOf(const std::string& body) {
    std::vector<std::string> all, news;
    bool inNews = false;
    size_t i = 0;
    while (i < body.size() && all.size() < 200) {
        size_t e = body.find('\n', i);
        std::string l = body.substr(i, (e == std::string::npos ? body.size() : e) - i);
        i = e == std::string::npos ? body.size() : e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.compare(0, 2, "##") == 0) { inNews = l.find("new") != std::string::npos; continue; }
        if (l.compare(0, 2, "- ") != 0 && l.compare(0, 2, "* ") != 0) continue;
        std::string p = plainLine(l.substr(2));
        if (p.empty()) continue;
        size_t dot = p.find(". ");                       // the first sentence is enough
        if (dot != std::string::npos && dot >= 12) p.erase(dot + 1);
        all.push_back(p);
        if (inNews) news.push_back(p);
    }
    std::vector<std::string>& src = news.empty() ? all : news;
    if (src.size() > 4) src.resize(4);
    return src;
}
} // namespace

ReleaseResult parseRelease(const std::string& json, ReleaseInfo& out) {
    out = ReleaseInfo();
    Json j;
    JsonParser p(json);
    if (!p.parse(j) || j.type != Json::OBJ) return REL_BAD_JSON;
    const Json *pre = j.get("prerelease"), *draft = j.get("draft"), *assets = j.get("assets");
    if (!pre || pre->type != Json::BOOL || pre->b || !draft || draft->type != Json::BOOL || draft->b) return REL_NOT_STABLE;
    const std::string tag = j.str("tag_name");
    if (tag.empty() || tag[0] != 'v' || !parseVersion(tag).ok) return REL_BAD_VERSION;
    out.version = tag.substr(1);
    if (!assets || assets->type != Json::ARR) return REL_NO_PACKAGE;
    const std::string want = "ACNH-Power-Pockets-" + tag + ".zip";
    const Json* pkg = nullptr;
    for (const Json& a : assets->a) if (a.str("name") == want) { pkg = &a; break; }
    if (!pkg) return REL_NO_PACKAGE;
    out.url = pkg->str("browser_download_url");
    const Json* size = pkg->get("size");
    if (out.url != DOWNLOAD_BASE + tag + "/" + want) return REL_NO_PACKAGE;
    if (!size || size->type != Json::NUM || !(size->n >= 1) || size->n > (double)UPDATE_ZIP_MAX) return REL_NO_PACKAGE;
    out.size = (long)size->n;
    const std::string digest = pkg->str("digest");
    if (digest.compare(0, 7, "sha256:") != 0 || !isHex64(digest.substr(7))) return REL_NO_DIGEST;
    out.sha256 = digest.substr(7);
    out.highlights = highlightsOf(j.str("body"));
    return REL_OK;
}

// ------------------------------- files (SD) --------------------------------
FILE* UpdateFs::open(const std::string& path, const char* mode) { return fopen(path.c_str(), mode); }
bool UpdateFs::rename(const std::string& from, const std::string& to) {
    if (exists(to)) return false;                        // same rule everywhere: never overwrite by renaming
    return ::rename(from.c_str(), to.c_str()) == 0;
}
bool UpdateFs::remove(const std::string& path) { return !exists(path) || (::remove(path.c_str()) == 0 && !exists(path)); }
bool UpdateFs::mkdir(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
    return ::mkdir(path.c_str(), 0777) == 0;
}
bool UpdateFs::exists(const std::string& path) { struct stat st; return stat(path.c_str(), &st) == 0; }
bool UpdateFs::rmdir(const std::string& path) { return ::rmdir(path.c_str()) == 0; }
std::vector<std::string> UpdateFs::list(const std::string& dir) {
    std::vector<std::string> v;
    DIR* d = opendir(dir.c_str());
    if (!d) return v;
    while (struct dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") v.push_back(n);
    }
    closedir(d);
    return v;
}
void UpdateFs::removeTree(const std::string& dir) {
    if (dir.find("/update/") == std::string::npos) return;   // belt and braces: nothing outside update/
    for (const std::string& n : list(dir)) {
        const std::string p = dir + "/" + n;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) removeTree(p);
        else remove(p);
    }
    rmdir(dir);
}

bool allowedEntry(const std::string& zipName, std::string& rel) {
    const size_t pl = strlen(ZIP_PREFIX);
    if (zipName.compare(0, pl, ZIP_PREFIX) != 0) return false;
    rel = zipName.substr(pl);
    if (rel.empty() || rel.size() > 80) return false;
    for (char c : rel) if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == '/')) return false;
    std::string base = rel;
    const size_t slash = rel.find('/');
    if (slash != std::string::npos) {                    // one sub-folder is known
        if (rel.compare(0, slash, "licenses") != 0 || rel.find('/', slash + 1) != std::string::npos) return false;
        base = rel.substr(slash + 1);
    }
    if (base.empty() || base[0] == '.' || base.find("..") != std::string::npos) return false;
    if (slash == std::string::npos && base == "LICENSE") return true;
    const size_t dot = base.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : base.substr(dot + 1);
    if (ext == "nro") return rel == NRO_NAME;
    if (ext != "txt" && ext != "png" && ext != "ttf" && ext != "md") return false;   // no .dat, ever
    if (base.compare(0, 14, "user_favorites") == 0 || base == "session_mark.txt") return false;
    return true;
}

// -------------------------------- package ----------------------------------
namespace {
struct Entry { std::string rel; uint16_t method; uint32_t crc, csize, usize, offset; };
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
bool readAt(FILE* f, long off, void* buf, size_t n) { return fseek(f, off, SEEK_SET) == 0 && fread(buf, 1, n, f) == n; }

// Reads the central directory. Every entry must be a plain file this app knows how to place.
UnpackResult readDirectory(FILE* f, std::vector<Entry>& out) {
    if (fseek(f, 0, SEEK_END) != 0) return UNP_BAD_ZIP;
    const long size = ftell(f);
    if (size < 22 || size > UPDATE_ZIP_MAX) return UNP_BAD_ZIP;
    const size_t tailN = (size_t)(size < 1024 ? size : 1024);
    std::vector<uint8_t> tail(tailN);
    if (!readAt(f, size - (long)tailN, tail.data(), tailN)) return UNP_BAD_ZIP;
    long eocd = -1;
    for (long k = (long)tailN - 22; k >= 0; k--) if (rd32(&tail[(size_t)k]) == 0x06054b50) { eocd = k; break; }
    if (eocd < 0) return UNP_BAD_ZIP;
    const uint8_t* e = &tail[(size_t)eocd];
    const size_t count = rd16(e + 10);
    const uint32_t cdSize = rd32(e + 12), cdOff = rd32(e + 16);
    if (rd16(e + 8) != count || count == 0 || count > MAX_ENTRIES + 8) return UNP_BAD_ZIP;
    if (cdSize > 64 * 1024 || (uint64_t)cdOff + cdSize > (uint64_t)size) return UNP_BAD_ZIP;
    std::vector<uint8_t> cd(cdSize);
    if (cdSize == 0 || !readAt(f, (long)cdOff, cd.data(), cdSize)) return UNP_BAD_ZIP;
    size_t p = 0;
    uint64_t total = 0;
    for (size_t n = 0; n < count; n++) {
        if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50) return UNP_BAD_ZIP;
        const uint16_t flags = rd16(&cd[p + 8]), nl = rd16(&cd[p + 28]), xl = rd16(&cd[p + 30]), cl = rd16(&cd[p + 32]);
        if (p + 46u + nl + xl + cl > cd.size()) return UNP_BAD_ZIP;
        Entry en;
        en.method = rd16(&cd[p + 10]); en.crc = rd32(&cd[p + 16]); en.csize = rd32(&cd[p + 20]);
        en.usize = rd32(&cd[p + 24]); en.offset = rd32(&cd[p + 42]);
        const std::string name((const char*)&cd[p + 46], nl);
        p += 46u + nl + xl + cl;
        if (!name.empty() && name.back() == '/') {       // folders are created when needed
            if (en.usize != 0) return UNP_BAD_ENTRY;
            continue;
        }
        if (flags & 1) return UNP_BAD_ENTRY;             // encrypted
        if (en.method != 0 && en.method != 8) return UNP_BAD_ENTRY;
        if (!allowedEntry(name, en.rel)) return UNP_BAD_ENTRY;
        for (const Entry& o : out) if (o.rel == en.rel) return UNP_BAD_ENTRY;
        total += en.usize;
        if (total > MAX_UNPACKED || (uint64_t)en.offset + 30 + en.csize > (uint64_t)size) return UNP_BAD_ZIP;
        out.push_back(en);
    }
    return out.empty() || out.size() > MAX_ENTRIES ? UNP_BAD_ZIP : UNP_OK;
}

UnpackResult extractEntry(UpdateFs& fs, FILE* zip, const Entry& en, const std::string& dest, const std::function<bool()>& cancelled) {
    uint8_t lh[30];
    if (!readAt(zip, (long)en.offset, lh, sizeof lh) || rd32(lh) != 0x04034b50) return UNP_BAD_ZIP;
    if (fseek(zip, (long)en.offset + 30 + rd16(lh + 26) + rd16(lh + 28), SEEK_SET) != 0) return UNP_BAD_ZIP;
    FILE* o = fs.open(dest, "wb");
    if (!o) return UNP_WRITE;
    const size_t CH = 64 * 1024;
    std::vector<uint8_t> in(CH), outb(CH);
    UnpackResult res = UNP_OK;
    uint32_t left = en.csize, crc = (uint32_t)crc32(0, nullptr, 0);
    uint64_t written = 0;
    z_stream z;
    memset(&z, 0, sizeof z);
    const bool deflated = en.method == 8;
    bool zopen = false, ended = !deflated;
    if (deflated) {
        if (inflateInit2(&z, -MAX_WBITS) == Z_OK) zopen = true; else res = UNP_BAD_ZIP;
    }
    auto emit = [&](const uint8_t* b, size_t n) {
        if (written + n > en.usize) { res = UNP_BAD_ZIP; return; }
        if (fwrite(b, 1, n, o) != n) { res = UNP_WRITE; return; }
        crc = (uint32_t)crc32(crc, b, (uInt)n);
        written += n;
    };
    while (res == UNP_OK && left > 0) {
        if (cancelled && cancelled()) { res = UNP_CANCELLED; break; }
        size_t n = left < CH ? left : CH;
        if (fread(in.data(), 1, n, zip) != n) { res = UNP_BAD_ZIP; break; }
        left -= (uint32_t)n;
        if (!deflated) { emit(in.data(), n); continue; }
        z.next_in = in.data(); z.avail_in = (uInt)n;
        do {
            z.next_out = outb.data(); z.avail_out = (uInt)CH;
            int r = inflate(&z, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) { res = UNP_BAD_ZIP; break; }
            emit(outb.data(), CH - z.avail_out);
            if (r == Z_STREAM_END) { ended = true; break; }
        } while (res == UNP_OK && (z.avail_in > 0 || z.avail_out == 0));
        if (ended && left > 0) res = UNP_BAD_ZIP;
    }
    if (zopen) inflateEnd(&z);
    if (fflush(o) != 0 && res == UNP_OK) res = UNP_WRITE;
    if (fclose(o) != 0 && res == UNP_OK) res = UNP_WRITE;
    if (res == UNP_OK && (!ended || written != en.usize || crc != en.crc)) res = UNP_BAD_ZIP;
    // Read it back: a worn-out SD card should be noticed now, not after the swap.
    if (res == UNP_OK) {
        FILE* r = fs.open(dest, "rb");
        uint32_t c2 = (uint32_t)crc32(0, nullptr, 0);
        uint64_t got = 0;
        if (!r) return UNP_WRITE;
        for (;;) {
            size_t n = fread(in.data(), 1, CH, r);
            if (n == 0) break;
            c2 = (uint32_t)crc32(c2, in.data(), (uInt)n);
            got += n;
        }
        fclose(r);
        if (c2 != en.crc || got != en.usize) res = UNP_WRITE;
    }
    return res;
}
} // namespace

UnpackResult unpackPackage(UpdateFs& fs, const UpdatePaths& p, std::vector<std::string>& files, const std::function<bool()>& cancelled) {
    files.clear();
    FILE* z = fs.open(p.zip(), "rb");
    if (!z) return UNP_BAD_ZIP;
    std::vector<Entry> list;
    UnpackResult res = readDirectory(z, list);
    bool hasNro = false, hasFont = false, hasItems = false;
    for (const Entry& e : list) {
        hasNro |= e.rel == NRO_NAME; hasFont |= e.rel == "font.ttf"; hasItems |= e.rel == "items.txt";
    }
    if (res == UNP_OK && !(hasNro && hasFont && hasItems)) res = UNP_INCOMPLETE;
    if (res == UNP_OK) {
        fs.removeTree(p.fresh());
        if (!fs.mkdir(p.fresh())) res = UNP_WRITE;
    }
    for (size_t i = 0; res == UNP_OK && i < list.size(); i++) {
        const std::string dest = p.fresh() + "/" + list[i].rel;
        const size_t slash = list[i].rel.find('/');
        if (slash != std::string::npos && !fs.mkdir(p.fresh() + "/" + list[i].rel.substr(0, slash))) { res = UNP_WRITE; break; }
        res = extractEntry(fs, z, list[i], dest, cancelled);
        if (res == UNP_OK && list[i].rel == NRO_NAME) {  // really a Switch executable?
            uint8_t head[0x14];
            FILE* n = fs.open(dest, "rb");
            if (!n || fread(head, 1, sizeof head, n) != sizeof head || memcmp(head + 0x10, "NRO0", 4) != 0) res = UNP_INCOMPLETE;
            if (n) fclose(n);
        }
    }
    fclose(z);
    if (res != UNP_OK) { fs.removeTree(p.fresh()); return res; }
    for (const Entry& e : list) files.push_back(e.rel);
    return UNP_OK;
}

// ------------------------------ installation -------------------------------
namespace {
struct JournalLine { bool replaced; std::string rel; };  // replaced: an old file was there
bool writeJournal(UpdateFs& fs, const UpdatePaths& p, const std::vector<JournalLine>& lines, const std::string& from, const std::string& to) {
    FILE* f = fs.open(p.journal(), "wb");
    if (!f) return false;
    bool ok = fprintf(f, "PPUPDATE 1\nfrom %s\nto %s\n", from.c_str(), to.c_str()) > 0;
    for (const JournalLine& l : lines) ok = ok && fprintf(f, "%c %s\n", l.replaced ? 'r' : 'n', l.rel.c_str()) > 0;
    ok = ok && fprintf(f, "end\n") > 0 && fflush(f) == 0;
    return fclose(f) == 0 && ok;
}
// False when there is no journal, or only a half-written one (nothing was swapped yet then).
bool readJournal(UpdateFs& fs, const UpdatePaths& p, std::vector<JournalLine>& lines) {
    lines.clear();
    FILE* f = fs.open(p.journal(), "rb");
    if (!f) return false;
    char buf[160];
    bool header = false, end = false, bad = false;
    while (fgets(buf, sizeof buf, f)) {
        std::string l = buf;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (!header) { if (l != "PPUPDATE 1") { bad = true; break; } header = true; continue; }
        if (l == "end") { end = true; break; }
        if (l.compare(0, 5, "from ") == 0 || l.compare(0, 3, "to ") == 0) continue;
        JournalLine j;
        std::string rel;
        if (l.size() < 3 || (l[0] != 'r' && l[0] != 'n') || l[1] != ' ' || !allowedEntry(ZIP_PREFIX + l.substr(2), rel)) { bad = true; break; }
        j.replaced = l[0] == 'r'; j.rel = rel;
        lines.push_back(j);
    }
    fclose(f);
    return header && end && !bad;
}
bool moveOver(UpdateFs& fs, const std::string& from, const std::string& to) { return fs.remove(to) && fs.rename(from, to); }

// Puts everything back. Can be interrupted and run again.
bool rollback(UpdateFs& fs, const UpdatePaths& p, const std::vector<JournalLine>& lines) {
    bool ok = true;
    if (fs.exists(p.previousNro())) ok = moveOver(fs, p.previousNro(), p.nro());
    for (size_t i = lines.size(); i-- > 0;) {
        const JournalLine& l = lines[i];
        if (l.rel == NRO_NAME) continue;
        const std::string dest = p.sd + l.rel, oldf = p.old() + "/" + l.rel, newf = p.fresh() + "/" + l.rel;
        if (l.replaced) { if (fs.exists(oldf)) ok = moveOver(fs, oldf, dest) && ok; }
        else if (!fs.exists(newf) && fs.exists(dest)) ok = fs.remove(dest) && ok;
    }
    return ok && fs.exists(p.nro());
}
// After a successful rollback. The journal goes first: without it the rest is only clutter.
void forget(UpdateFs& fs, const UpdatePaths& p) {
    if (!fs.remove(p.journal())) return;
    fs.remove(p.done());
    fs.remove(p.zip());
    fs.removeTree(p.fresh());
    fs.removeTree(p.old());
}
} // namespace

InstallResult installUpdate(UpdateFs& fs, const UpdatePaths& p, const std::vector<std::string>& files, const std::string& from, const std::string& to) {
    std::vector<JournalLine> lines;
    bool hasNro = false;
    for (const std::string& rel : files) {
        std::string r;
        if (!allowedEntry(ZIP_PREFIX + rel, r) || !fs.exists(p.fresh() + "/" + rel)) return INST_NOTHING_CHANGED;
        const bool there = fs.exists(p.sd + rel);
        if (rel == KEEP_IF_PRESENT && there) continue;
        if (rel == NRO_NAME) { hasNro = true; continue; }
        lines.push_back({ there, rel });
    }
    if (!hasNro || !fs.exists(p.nro()) || fs.exists(p.journal())) return INST_NOTHING_CHANGED;
    lines.push_back({ true, NRO_NAME });                 // last, see below
    // Everything that can fail without consequence comes first.
    fs.removeTree(p.old());
    if (!fs.mkdir(p.old()) || !fs.remove(p.previousNro()) || !fs.remove(p.done())) return INST_NOTHING_CHANGED;
    for (const JournalLine& l : lines) {
        const size_t slash = l.rel.find('/');
        if (slash == std::string::npos) continue;
        const std::string sub = l.rel.substr(0, slash);
        if (!fs.mkdir(p.old() + "/" + sub) || !fs.mkdir(p.sd + sub)) return INST_NOTHING_CHANGED;
    }
    if (!writeJournal(fs, p, lines, from, to)) { fs.remove(p.journal()); return INST_NOTHING_CHANGED; }

    bool ok = true;
    for (size_t i = 0; ok && i + 1 < lines.size(); i++) {
        const std::string dest = p.sd + lines[i].rel;
        if (lines[i].replaced) ok = fs.rename(dest, p.old() + "/" + lines[i].rel);
        ok = ok && fs.rename(p.fresh() + "/" + lines[i].rel, dest);
    }
    // The app itself, last. Between the two renames the old one is still there under
    // another name, in the same folder: the Homebrew Menu can start it.
    ok = ok && fs.rename(p.nro(), p.previousNro());
    ok = ok && fs.rename(p.fresh() + "/" + NRO_NAME, p.nro());
    if (ok) {
        FILE* f = fs.open(p.done(), "wb");               // a missing marker is not a failure, see recoverUpdate
        if (f) { fputs(to.c_str(), f); fclose(f); }
        return INST_OK;
    }
    if (!rollback(fs, p, lines)) return INST_BROKEN;
    forget(fs, p);
    return INST_ROLLED_BACK;
}

RecoverResult recoverUpdate(UpdateFs& fs, const UpdatePaths& p, bool runningPrevious) {
    if (!fs.exists(p.journal())) return REC_NONE;
    std::vector<JournalLine> lines;
    if (!readJournal(fs, p, lines)) {                    // half-written: the swap had not started
        fs.remove(p.journal());
        return REC_NONE;
    }
    bool complete = fs.exists(p.done());
    if (!complete) {                                     // switched off between the last rename and the marker?
        complete = fs.exists(p.nro()) && fs.exists(p.previousNro());
        for (const JournalLine& l : lines) if (fs.exists(p.fresh() + "/" + l.rel)) complete = false;
    }
    if (complete && !runningPrevious) return REC_PENDING_CONFIRM;
    // Interrupted swap, or the user started the previous version because the new one doesn't work.
    if (!rollback(fs, p, lines)) return REC_FAILED;
    forget(fs, p);
    return REC_ROLLED_BACK;
}

void confirmUpdate(UpdateFs& fs, const UpdatePaths& p) {
    if (!fs.remove(p.journal())) return;                 // first: from here on there is nothing to undo
    fs.remove(p.done());
    fs.remove(p.zip());
    fs.removeTree(p.fresh());
    fs.removeTree(p.old());
    if (fs.exists(p.previousNro())) moveOver(fs, p.previousNro(), p.keptNro());
}

void discardDownload(UpdateFs& fs, const UpdatePaths& p) {
    if (fs.exists(p.journal())) return;
    fs.remove(p.zip());
    fs.removeTree(p.fresh());
}

// ---------------------------------- job ------------------------------------
void UpdateJob::fail(NetError e) {
    if (cancel_ || e == NET_CANCELLED) { finish(UPD_CANCELLED); return; }
    switch (e) {
        case NET_OFFLINE: finish(UPD_FAILED, UE_OFFLINE); break;
        case NET_TLS: finish(UPD_FAILED, UE_TLS); break;
        case NET_HTTP: finish(UPD_FAILED, UE_HTTP); break;
        case NET_LIMIT: finish(UPD_FAILED, UE_LIMIT); break;
        default: finish(UPD_FAILED, UE_NETWORK); break;
    }
}

void UpdateJob::runCheck() {
    std::string body;
    NetError e = net_.get(UPDATE_API_URL, UPDATE_JSON_MAX,
                          [&](const uint8_t* d, size_t n) { body.append((const char*)d, n); return body.size() <= UPDATE_JSON_MAX; },
                          [this]() { return cancel_.load(); });
    if (e != NET_OK || cancel_) { fail(e); return; }
    ReleaseInfo r;
    ReleaseResult rr = parseRelease(body, r);
    const Version mine = parseVersion(version_), theirs = parseVersion(r.version);
    // Not newer: nothing else in the answer matters (an old release may have no digest).
    if (mine.ok && theirs.ok && compareVersions(theirs, mine) <= 0) { rel_ = r; finish(UPD_UP_TO_DATE); return; }
    if (rr != REL_OK || !mine.ok) { finish(UPD_FAILED, UE_RELEASE); return; }
    rel_ = r;
    finish(UPD_AVAILABLE);
}

void UpdateJob::runDownload() {
    files_.clear();
    if (rel_.url.empty() || fs_.exists(paths_.journal())) { finish(UPD_FAILED, UE_RELEASE); return; }
    discardDownload(fs_, paths_);
    FILE* f = fs_.mkdir(paths_.dir()) ? fs_.open(paths_.zip(), "wb") : nullptr;
    if (!f) { finish(UPD_FAILED, UE_SD); return; }
    Sha256 sha;
    long got = 0;
    bool sdError = false;
    NetError e = net_.get(rel_.url, (size_t)rel_.size, [&](const uint8_t* d, size_t n) {
        if (got + (long)n > rel_.size) return false;
        if (fwrite(d, 1, n, f) != n) { sdError = true; return false; }
        sha.update(d, n);
        got += (long)n;
        percent_ = (int)(got * 100 / rel_.size);
        return true;
    }, [this]() { return cancel_.load(); });
    if (fflush(f) != 0) sdError = true;
    if (fclose(f) != 0) sdError = true;
    auto stop = [&]() { discardDownload(fs_, paths_); };
    if (sdError) { stop(); finish(UPD_FAILED, UE_SD); return; }
    if (cancel_ || (e != NET_OK && got <= rel_.size && e != NET_TOO_BIG)) { stop(); fail(e); return; }
    if (got != rel_.size || sha.hex() != rel_.sha256) { stop(); finish(UPD_FAILED, UE_CHECKSUM); return; }
    state_.store(UPD_PREPARING, std::memory_order_release);
    UnpackResult u = unpackPackage(fs_, paths_, files_, [this]() { return cancel_.load(); });
    if (u == UNP_OK) { fs_.remove(paths_.zip()); finish(UPD_READY); return; }
    stop();
    if (u == UNP_CANCELLED) finish(UPD_CANCELLED);
    else finish(UPD_FAILED, u == UNP_WRITE ? UE_SD : UE_PACKAGE);
}

} // namespace pp
