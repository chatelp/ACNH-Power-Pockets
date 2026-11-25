// Unit tests of the update logic (source/pp_update.cpp): versions, GitHub answer, package,
// installation, and every way an installation can be interrupted. Run by tests/host/run.sh.
#include "../../source/pp_update.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <zlib.h>

using namespace pp;

static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; printf("  FAIL line %d: %s\n", __LINE__, #c); } } while (0)

typedef std::map<std::string, std::string> Files;        // relative name -> content
static std::string g_tmp;

static void writeFile(const std::string& path, const std::string& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("cannot write %s\n", path.c_str()); exit(2); }
    fwrite(s.data(), 1, s.size(), f); fclose(f);
}
static std::string readFile(const std::string& path) {
    std::string s; char b[4096]; size_t n;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "<none>";
    while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
    fclose(f);
    return s;
}
static void le16(std::string& o, unsigned v) { o += (char)(v & 255); o += (char)(v >> 8 & 255); }
static void le32(std::string& o, uint32_t v) { le16(o, v & 0xFFFF); le16(o, v >> 16); }

struct ZipOpt { bool store = false, badCrc = false, encrypted = false; int method = -1; };
static std::string deflateRaw(const std::string& s) {
    z_stream z; memset(&z, 0, sizeof z);
    deflateInit2(&z, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    std::string o(s.size() + 1024, '\0');
    z.next_in = (Bytef*)s.data(); z.avail_in = (uInt)s.size(); z.next_out = (Bytef*)&o[0]; z.avail_out = (uInt)o.size();
    deflate(&z, Z_FINISH);
    o.resize(z.total_out); deflateEnd(&z);
    return o;
}
static std::string makeZip(const std::vector<std::pair<std::string, std::string>>& files, ZipOpt opt = ZipOpt()) {
    std::string out, cd;
    for (const auto& f : files) {
        const bool dir = !f.first.empty() && f.first.back() == '/';
        const bool store = opt.store || dir;
        const std::string data = store ? f.second : deflateRaw(f.second);
        uint32_t crc = (uint32_t)crc32(0, (const Bytef*)f.second.data(), (uInt)f.second.size());
        if (opt.badCrc) crc ^= 1;
        const int method = opt.method >= 0 ? opt.method : store ? 0 : 8;
        const uint32_t off = (uint32_t)out.size();
        le32(out, 0x04034b50); le16(out, 20); le16(out, 0); le16(out, method); le32(out, 0);
        le32(out, crc); le32(out, (uint32_t)data.size()); le32(out, (uint32_t)f.second.size());
        le16(out, (unsigned)f.first.size()); le16(out, 0); out += f.first; out += data;
        le32(cd, 0x02014b50); le16(cd, 20); le16(cd, 20); le16(cd, opt.encrypted ? 1 : 0); le16(cd, method); le32(cd, 0);
        le32(cd, crc); le32(cd, (uint32_t)data.size()); le32(cd, (uint32_t)f.second.size());
        le16(cd, (unsigned)f.first.size()); le16(cd, 0); le16(cd, 0); le16(cd, 0); le16(cd, 0); le32(cd, 0); le32(cd, off); cd += f.first;
    }
    const uint32_t cdOff = (uint32_t)out.size();
    out += cd;
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, (unsigned)files.size()); le16(out, (unsigned)files.size());
    le32(out, (uint32_t)cd.size()); le32(out, cdOff); le16(out, 0);
    return out;
}
static std::string nro(const char* tag) { std::string s(0x10, '\1'); s += "NRO0"; s += tag; uint32_t r = 1; for (int i = 0; i < 90000; i++) { r = r * 1664525u + 1013904223u; s += (char)(r >> 24); } return s; }

static const char* P = "switch/acnh_power_pockets/";
static std::vector<std::pair<std::string, std::string>> release(const char* v) {
    const std::string s = std::string(" ") + v, d = P;
    return { { "switch/", "" }, { d, "" }, { d + "licenses/", "" }, { d + "acnh_power_pockets.nro", nro(v) }, { d + "font.ttf", "font" + s }, { d + "items.txt", "items" + s },
             { d + "nonstackable.txt", "rules" + s }, { d + "licenses/GPL-3.0.txt", "gpl" + s }, { d + "licenses/new.txt", "new licence" }, { d + "LICENSE", "mit" + s },
             { d + "brand_new.png", "png" + s } };
}

// An SD card with version 1.0.0 installed, the user's own files, and an update package.
struct World {
    std::string sd;
    UpdateFs fs;
    UpdatePaths p;
    explicit World(const char* name) : sd(g_tmp + "/" + name + "/"), p(sd) {
        std::string cmd = "rm -rf '" + sd + "' && mkdir -p '" + sd + "licenses' '" + sd + "update'";
        if (system(cmd.c_str()) != 0) exit(2);
        writeFile(sd + "acnh_power_pockets.nro", nro("old"));
        writeFile(sd + "font.ttf", "font old"); writeFile(sd + "items.txt", "items old");
        writeFile(sd + "nonstackable.txt", "my own rules"); writeFile(sd + "licenses/GPL-3.0.txt", "gpl old"); writeFile(sd + "LICENSE", "mit old");
        writeFile(sd + "user_favorites.txt", "my favorites"); writeFile(sd + "l_0.dat", "my loadout"); writeFile(sd + "backup_main.dat", "my backup");
        writeFile(p.zip(), makeZip(release("new")));
    }
    Files snap() {                                       // everything but update/ and the kept copy of the app
        Files f;
        const char* names[] = { "acnh_power_pockets.nro", "font.ttf", "items.txt", "nonstackable.txt", "licenses/GPL-3.0.txt", "licenses/new.txt", "LICENSE",
                                "brand_new.png", "user_favorites.txt", "l_0.dat", "backup_main.dat" };
        for (const char* n : names) f[n] = readFile(sd + n);
        return f;
    }
    bool mine() { Files f = snap(); return f["user_favorites.txt"] == "my favorites" && f["l_0.dat"] == "my loadout" && f["backup_main.dat"] == "my backup" && f["nonstackable.txt"] == "my own rules"; }
    bool allOld() {
        Files f = snap();
        return f["acnh_power_pockets.nro"] == nro("old") && f["font.ttf"] == "font old" && f["items.txt"] == "items old" && f["licenses/GPL-3.0.txt"] == "gpl old" &&
               f["LICENSE"] == "mit old" && f["licenses/new.txt"] == "<none>" && f["brand_new.png"] == "<none>" && mine();
    }
    bool allNew() {
        Files f = snap();
        return f["acnh_power_pockets.nro"] == nro("new") && f["font.ttf"] == "font new" && f["items.txt"] == "items new" && f["licenses/GPL-3.0.txt"] == "gpl new" &&
               f["LICENSE"] == "mit new" && f["licenses/new.txt"] == "new licence" && f["brand_new.png"] == "png new" && mine();
    }
    bool launchable() { std::string a = readFile(p.nro()), b = readFile(p.previousNro()); return a == nro("old") || a == nro("new") || b == nro("old"); }
};

// Fails one operation (failAt), or every operation from one on, like a power cut (cutAt).
struct FaultFs : UpdateFs {
    int n = 0, failAt = -1, cutAt = -1;
    bool hit() { n++; return n == failAt || (cutAt > 0 && n >= cutAt); }
    FILE* open(const std::string& a, const char* m) override { if (m[0] == 'w' && hit()) return nullptr; return UpdateFs::open(a, m); }
    bool rename(const std::string& a, const std::string& b) override { if (hit()) return false; return UpdateFs::rename(a, b); }
    bool remove(const std::string& a) override { if (exists(a) && hit()) return false; return UpdateFs::remove(a); }
    bool mkdir(const std::string& a) override { if (hit()) return false; return UpdateFs::mkdir(a); }
    bool rmdir(const std::string& a) override { if (cutAt > 0 && n >= cutAt) return false; return UpdateFs::rmdir(a); }
};

static std::string releaseJson(const std::string& tag, const std::string& digest, const std::string& extra = "", const std::string& size = "4101574", const std::string& repo = "chatelp/ACNH-Power-Pockets") {
    const std::string n = "ACNH-Power-Pockets-" + tag + ".zip";
    return "{\"tag_name\":\"" + tag + "\",\"draft\":false,\"prerelease\":false," + extra + "\"assets\":[{\"name\":\"other.txt\",\"size\":1},{\"name\":\"" + n + "\",\"size\":" + size +
           ",\"digest\":\"" + digest + "\",\"browser_download_url\":\"https://github.com/" + repo + "/releases/download/" + tag + "/" + n + "\"}]," +
           "\"body\":\"intro\\r\\n- not news\\r\\n## What's new\\r\\n\\r\\n- **Browse [collections](https://x.y/z).** A box next to it.\\r\\n- `X` searches \\u00e9\\ud83d\\ude00 from any page\\r\\n## Install\\r\\n- copy\"}";
}
static const std::string HEX64 = "59669f737c8aa42cd2fba8332a92f8297e9b829cf3451ae5f79533f12127576c";

static void testVersions() {
    CHECK(parseVersion("1.4.0").ok && parseVersion("v1.4.0").ok);
    const char* bad[] = { "", "v", "1", "1.4", "1.4.", "1.4.0 ", " 1.4.0", "1.4.0-rc1", "vv1.4.0", "1..0", "1.4.x", "-1.4.0", "123456.0.0", "1.4.0.0" };
    for (const char* b : bad) CHECK(!parseVersion(b).ok);
    auto cmp = [](const char* a, const char* b) { return compareVersions(parseVersion(a), parseVersion(b)); };
    CHECK(cmp("1.3.0", "1.4.0") < 0); CHECK(cmp("1.10.0", "1.9.9") > 0); CHECK(cmp("2.0.0", "1.99.99") > 0); CHECK(cmp("v1.4.0", "1.4.0") == 0); CHECK(cmp("1.4.1", "1.4.10") < 0);
}
static void testSha() {
    Sha256 s; CHECK(s.hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    s.reset(); s.update("abc", 3); CHECK(s.hex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string a(1000000, 'a');
    s.reset(); for (size_t i = 0; i < a.size(); i += 977) s.update(a.data() + i, a.size() - i < 977 ? a.size() - i : 977);
    CHECK(s.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
static void testRelease() {
    ReleaseInfo r;
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64), r) == REL_OK);
    CHECK(r.version == "1.4.1" && r.sha256 == HEX64 && r.size == 4101574);
    CHECK(r.url == "https://github.com/chatelp/ACNH-Power-Pockets/releases/download/v1.4.1/ACNH-Power-Pockets-v1.4.1.zip");
    CHECK(r.highlights.size() == 2 && r.highlights[0] == "Browse collections." && r.highlights[1] == "X searches \xC3\xA9\xF0\x9F\x98\x80 from any page");
    CHECK(parseRelease(releaseJson("v1.4.1", ""), r) == REL_NO_DIGEST);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:1234"), r) == REL_NO_DIGEST);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha512:" + HEX64), r) == REL_NO_DIGEST);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64, "", "0"), r) == REL_NO_PACKAGE);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64, "", "999999999999"), r) == REL_NO_PACKAGE);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64, "", "-5"), r) == REL_NO_PACKAGE);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64, "", "\"x\""), r) == REL_NO_PACKAGE);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64, "", "10", "someone/else"), r) == REL_NO_PACKAGE);   // other repository
    CHECK(parseRelease(releaseJson("1.4.1", "sha256:" + HEX64), r) == REL_BAD_VERSION);
    CHECK(parseRelease(releaseJson("v1.4.1-beta", "sha256:" + HEX64), r) == REL_BAD_VERSION);
    std::string pre = releaseJson("v1.4.1", "sha256:" + HEX64); pre.replace(pre.find("\"prerelease\":false"), 18, "\"prerelease\":true");
    CHECK(parseRelease(pre, r) == REL_NOT_STABLE);
    CHECK(parseRelease("", r) == REL_BAD_JSON); CHECK(parseRelease("[]", r) == REL_BAD_JSON); CHECK(parseRelease("{}", r) == REL_NOT_STABLE);
    CHECK(parseRelease(std::string(100000, '['), r) == REL_BAD_JSON);
    CHECK(parseRelease(releaseJson("v1.4.1", "sha256:" + HEX64) + "x", r) == REL_BAD_JSON);
    // Any truncation or byte change: no crash, and never a package from anywhere else.
    const std::string ok = releaseJson("v1.4.1", "sha256:" + HEX64);
    for (size_t i = 0; i < ok.size(); i++) CHECK(parseRelease(ok.substr(0, i), r) == REL_BAD_JSON);
    srand(7);
    for (int i = 0; i < 20000; i++) {
        std::string m = ok;
        for (int k = 0; k < 3; k++) m[(size_t)rand() % m.size()] = (char)rand();
        if (parseRelease(m, r) == REL_OK) { if (r.url.compare(0, 62, "https://github.com/chatelp/ACNH-Power-Pockets/releases/download") != 0 && r.url.find("https://github.com/chatelp/ACNH-Power-Pockets/releases/download/v") != 0) CHECK(false); if (r.sha256.size() != 64) CHECK(false); }
    }
    // The answer GitHub gave for v1.3.0, when the file is there.
    if (const char* real = getenv("PP_RELEASE_JSON")) {
        CHECK(parseRelease(readFile(real), r) == REL_OK);
        printf("  real release: %s, %ld bytes, %zu highlights\n", r.version.c_str(), r.size, r.highlights.size());
        for (const std::string& h : r.highlights) printf("    - %s\n", h.c_str());
    }
}
static void testEntries() {
    std::string rel;
    const char* good[] = { "acnh_power_pockets.nro", "font.ttf", "LICENSE", "licenses/GPL-3.0.txt", "item_collections.txt", "THIRD_PARTY_NOTICES.md", "author.png" };
    for (const char* g : good) CHECK(allowedEntry(std::string(P) + g, rel) && rel == g);
    const char* bad[] = { "", "other.nro", "licenses/acnh_power_pockets.nro", "../x.txt", "a/../../x.txt", "licenses/../x.txt", "licenses/a/b.txt", "deep/x.txt", "x", "x.dat", "l_0.dat", "backup_main.dat",
                          "session_personal.dat", "session_mark.txt", "user_favorites.txt", "user_favorites.txt.bak", ".hidden.txt", "a b.txt", "a\\b.txt", "x.txt/", "/x.txt", "x..txt", "caf\xC3\xA9.txt", "x.exe", "boot.dat", "licenses/" };
    for (const char* b : bad) CHECK(!allowedEntry(std::string(P) + b, rel));
    CHECK(!allowedEntry("atmosphere/package3", rel)); CHECK(!allowedEntry("switch/other/x.txt", rel)); CHECK(!allowedEntry("font.ttf", rel)); CHECK(!allowedEntry("switch/acnh_power_pockets", rel));
    CHECK(!allowedEntry(std::string(P) + std::string(90, 'a') + ".txt", rel));
}
static UnpackResult unpackOf(World& w, const std::string& zip) {
    std::vector<std::string> f;
    writeFile(w.p.zip(), zip);
    UnpackResult r = unpackPackage(w.fs, w.p, f, nullptr);
    if (r != UNP_OK) { CHECK(f.empty()); CHECK(!w.fs.exists(w.p.fresh())); }
    return r;
}
static void testUnpack() {
    World w("unpack");
    std::vector<std::string> files;
    CHECK(unpackPackage(w.fs, w.p, files, nullptr) == UNP_OK && files.size() == 8);
    CHECK(readFile(w.p.fresh() + "/acnh_power_pockets.nro") == nro("new") && readFile(w.p.fresh() + "/licenses/new.txt") == "new licence");
    ZipOpt st; st.store = true; CHECK(unpackOf(w, makeZip(release("new"), st)) == UNP_OK);
    ZipOpt bc; bc.badCrc = true; CHECK(unpackOf(w, makeZip(release("new"), bc)) == UNP_BAD_ZIP);
    ZipOpt en; en.encrypted = true; CHECK(unpackOf(w, makeZip(release("new"), en)) == UNP_BAD_ENTRY);
    ZipOpt me; me.method = 12; CHECK(unpackOf(w, makeZip(release("new"), me)) == UNP_BAD_ENTRY);
    const char* evil[] = { "../evil.txt", "switch/acnh_power_pockets/../evil.txt", "atmosphere/hosts/default.txt", "switch/acnh_power_pockets/l_0.dat", "switch/acnh_power_pockets/user_favorites.txt", "/abs.txt", "switch/acnh_power_pockets/font.ttf" };
    for (const char* e : evil) { auto f = release("new"); f.push_back({ e, "x" }); CHECK(unpackOf(w, makeZip(f)) == UNP_BAD_ENTRY); }
    auto noNro = release("new"); noNro.erase(noNro.begin() + 3); CHECK(unpackOf(w, makeZip(noNro)) == UNP_INCOMPLETE);
    auto notNro = release("new"); notNro[3].second = "MZ not a Switch executable, really not"; CHECK(unpackOf(w, makeZip(notNro)) == UNP_INCOMPLETE);
    auto many = release("new"); for (int i = 0; i < 100; i++) many.push_back({ std::string(P) + "f" + std::to_string(i) + ".txt", "x" }); CHECK(unpackOf(w, makeZip(many)) == UNP_BAD_ZIP);
    CHECK(unpackOf(w, "") == UNP_BAD_ZIP); CHECK(unpackOf(w, "PK") == UNP_BAD_ZIP); CHECK(unpackOf(w, std::string(5000, 'x')) == UNP_BAD_ZIP);
    const std::string ok = makeZip(release("new"));
    for (size_t i = 0; i < ok.size(); i += 997) CHECK(unpackOf(w, ok.substr(0, i)) == UNP_BAD_ZIP);
    srand(11);
    for (int i = 0; i < 400; i++) {                      // damaged packages: refused or identical, never a crash
        std::string m = ok;
        size_t at = i % 2 ? (size_t)rand() % m.size() : m.size() - 1 - (size_t)rand() % 900;
        m[at] = (char)(m[at] ^ (1 << (rand() % 8)));
        writeFile(w.p.zip(), m);
        std::vector<std::string> f;
        if (unpackPackage(w.fs, w.p, f, nullptr) == UNP_OK) CHECK(readFile(w.p.fresh() + "/acnh_power_pockets.nro") == nro("new"));
    }
    writeFile(w.p.zip(), ok);
    CHECK(unpackPackage(w.fs, w.p, files, []() { return true; }) == UNP_CANCELLED);
    for (int i = 1; i < 14; i++) { FaultFs ff; ff.failAt = i; std::vector<std::string> f; UnpackResult r = unpackPackage(ff, w.p, f, nullptr); CHECK(r == UNP_OK || (r == UNP_WRITE && f.empty())); }
    CHECK(w.allOld());                                   // unpacking never touches the installed files
}
static std::vector<std::string> prepared(World& w) { std::vector<std::string> f; CHECK(unpackPackage(w.fs, w.p, f, nullptr) == UNP_OK); return f; }

static void testInstall() {
    { World w("install"); auto f = prepared(w);
      CHECK(installUpdate(w.fs, w.p, f, "1.0.0", "1.1.0") == INST_OK); CHECK(w.allNew()); CHECK(readFile(w.p.previousNro()) == nro("old"));
      CHECK(recoverUpdate(w.fs, w.p, false) == REC_PENDING_CONFIRM); CHECK(w.allNew());
      CHECK(recoverUpdate(w.fs, w.p, false) == REC_PENDING_CONFIRM);
      confirmUpdate(w.fs, w.p); CHECK(w.allNew()); CHECK(!w.fs.exists(w.p.previousNro()) && readFile(w.p.keptNro()) == nro("old"));
      CHECK(!w.fs.exists(w.p.journal()) && !w.fs.exists(w.p.done()) && !w.fs.exists(w.p.old()) && !w.fs.exists(w.p.fresh()));
      CHECK(recoverUpdate(w.fs, w.p, false) == REC_NONE);
      // ... and once more on top of it.
      writeFile(w.p.zip(), makeZip(release("newer"))); f = prepared(w);
      CHECK(installUpdate(w.fs, w.p, f, "1.1.0", "1.2.0") == INST_OK); confirmUpdate(w.fs, w.p); CHECK(readFile(w.p.keptNro()) == nro("new") && readFile(w.p.nro()) == nro("newer") && w.mine()); }
    { World w("goback"); auto f = prepared(w);           // the new version doesn't start: the user launches the previous one
      CHECK(installUpdate(w.fs, w.p, f, "1.0.0", "1.1.0") == INST_OK);
      CHECK(recoverUpdate(w.fs, w.p, true) == REC_ROLLED_BACK); CHECK(w.allOld()); CHECK(!w.fs.exists(w.p.previousNro()) && !w.fs.exists(w.p.journal())); }
    { World w("norules"); remove((w.sd + "nonstackable.txt").c_str()); auto f = prepared(w);
      CHECK(installUpdate(w.fs, w.p, f, "1.0.0", "1.1.0") == INST_OK); CHECK(readFile(w.sd + "nonstackable.txt") == "rules new"); }
    { World w("refuse"); auto f = prepared(w);
      std::vector<std::string> bad = f; bad.push_back("../x.txt"); CHECK(installUpdate(w.fs, w.p, bad, "1", "2") == INST_NOTHING_CHANGED);
      bad = f; bad.push_back("missing.txt"); CHECK(installUpdate(w.fs, w.p, bad, "1", "2") == INST_NOTHING_CHANGED);
      bad.clear(); bad.push_back("font.ttf"); CHECK(installUpdate(w.fs, w.p, bad, "1", "2") == INST_NOTHING_CHANGED);
      remove(w.p.nro().c_str()); CHECK(installUpdate(w.fs, w.p, f, "1", "2") == INST_NOTHING_CHANGED); }   // renamed app: refused
    // One failing operation, whichever it is.
    int ops = 0, rolled = 0;
    for (int i = 1; i < 400; i++) {
        World w("fail"); auto f = prepared(w);
        FaultFs ff; ff.failAt = i;
        InstallResult r = installUpdate(ff, w.p, f, "1.0.0", "1.1.0");
        if (ff.n < i) { ops = i - 1; break; }
        if (r == INST_OK) CHECK(w.allNew()); else CHECK(w.allOld());
        CHECK(r != INST_BROKEN);
        if (r == INST_ROLLED_BACK) rolled++;
        CHECK(w.launchable());
    }
    CHECK(ops > 15 && rolled > 10);
    // Power cut at every point of the installation, then a normal launch.
    int cutNew = 0, cutOld = 0;
    for (int i = 1; i <= ops + 1; i++) {
        World w("cut"); auto f = prepared(w);
        FaultFs ff; ff.cutAt = i;
        installUpdate(ff, w.p, f, "1.0.0", "1.1.0");
        CHECK(w.launchable());
        const bool previousOnly = !w.fs.exists(w.p.nro());
        RecoverResult r = recoverUpdate(w.fs, w.p, previousOnly);
        if (r == REC_PENDING_CONFIRM) { confirmUpdate(w.fs, w.p); CHECK(w.allNew()); cutNew++; }
        else { CHECK(r != REC_FAILED); CHECK(w.allOld()); cutOld++; }
        CHECK(recoverUpdate(w.fs, w.p, false) == REC_NONE);
    }
    CHECK(cutNew > 0 && cutOld > 10);
    // Power cut during the installation AND during the recovery, then a normal launch.
    for (int i = 1; i <= ops + 1; i++) for (int k = 1; k < 60; k++) {
        World w("cut2"); auto f = prepared(w);
        FaultFs a; a.cutAt = i; installUpdate(a, w.p, f, "1.0.0", "1.1.0");
        FaultFs b; b.cutAt = k; RecoverResult r1 = recoverUpdate(b, w.p, false);
        if (r1 == REC_PENDING_CONFIRM) confirmUpdate(b, w.p);
        const bool finished = b.n < k;
        CHECK(w.launchable());
        RecoverResult r = recoverUpdate(w.fs, w.p, !w.fs.exists(w.p.nro()));
        if (r == REC_PENDING_CONFIRM) confirmUpdate(w.fs, w.p);
        CHECK(r != REC_FAILED); CHECK(w.allNew() || w.allOld());
        if (finished) break;
    }
    // Damaged journals: never acted upon.
    const char* js[] = { "", "PPUPDATE 1\n", "PPUPDATE 1\nr font.ttf\n", "PPUPDATE 2\nr font.ttf\nend\n", "PPUPDATE 1\nr ../x.txt\nend\n", "PPUPDATE 1\nn l_0.dat\nend\n", "garbage" };
    for (const char* j : js) { World w("journal"); writeFile(w.p.journal(), j); CHECK(recoverUpdate(w.fs, w.p, false) == REC_NONE); CHECK(w.allOld()); }
    { World w("discard"); prepared(w); discardDownload(w.fs, w.p); CHECK(!w.fs.exists(w.p.zip()) && !w.fs.exists(w.p.fresh())); CHECK(w.allOld()); }
    { World w("tree"); w.fs.removeTree(w.sd + "licenses"); CHECK(w.allOld()); }   // refuses anything outside update/
}

struct FakeNet : NetClient {
    std::string json, zip;
    NetError err = NET_OK;
    size_t cutAfter = (size_t)-1;
    int calls = 0;
    std::vector<std::string> urls;
    std::function<void()> midway;
    NetError get(const std::string& url, size_t maxBytes, const std::function<bool(const uint8_t*, size_t)>& sink, const std::function<bool()>& cancelled) override {
        calls++; urls.push_back(url);
        if (err != NET_OK) return err;
        const std::string& d = url == UPDATE_API_URL ? json : zip;
        for (size_t i = 0; i < d.size(); i += 3000) {
            if (i >= cutAfter) return NET_OTHER;
            if (i > maxBytes) return NET_TOO_BIG;
            if (midway && i > d.size() / 2) midway();
            if (cancelled()) return NET_CANCELLED;
            if (!sink((const uint8_t*)d.data() + i, d.size() - i < 3000 ? d.size() - i : 3000)) return NET_OTHER;
        }
        return NET_OK;
    }
};
static void testJob() {
    World w("job");
    FakeNet net;
    const std::string zip = makeZip(release("new"));
    Sha256 s; s.update(zip.data(), zip.size());
    const std::string hex = s.hex();
    net.zip = zip;
    net.json = releaseJson("v1.4.1", "sha256:" + hex, "", std::to_string(zip.size()));
    auto check = [&](const char* mine) { UpdateJob j(net, w.fs, w.sd, mine); j.begin(UPD_CHECKING); j.runCheck(); return j.state(); };
    CHECK(check("1.4.0") == UPD_AVAILABLE); CHECK(check("1.4.1") == UPD_UP_TO_DATE); CHECK(check("1.5.0") == UPD_UP_TO_DATE); CHECK(check("host") == UPD_FAILED);
    net.json = releaseJson("v1.3.0", ""); CHECK(check("1.4.0") == UPD_UP_TO_DATE);   // older release without a digest
    net.json = "<html>"; CHECK(check("1.4.0") == UPD_FAILED);
    net.json = std::string(UPDATE_JSON_MAX + 5000, ' '); CHECK(check("1.4.0") == UPD_FAILED);
    net.json = releaseJson("v1.4.1", "sha256:" + hex, "", std::to_string(zip.size()));
    { UpdateJob j(net, w.fs, w.sd, "1.4.0"); net.err = NET_TLS; j.begin(UPD_CHECKING); j.runCheck(); CHECK(j.state() == UPD_FAILED && j.error() == UE_TLS); net.err = NET_OK; }
    { UpdateJob j(net, w.fs, w.sd, "1.4.0"); j.begin(UPD_DOWNLOADING); j.runDownload(); CHECK(j.state() == UPD_FAILED && j.error() == UE_RELEASE); }   // no check first
    auto download = [&](UpdateJob& j, UpdateFs&) { j.begin(UPD_CHECKING); j.runCheck(); CHECK(j.state() == UPD_AVAILABLE); j.begin(UPD_DOWNLOADING); j.runDownload(); };
    { UpdateJob j(net, w.fs, w.sd, "1.4.0"); download(j, w.fs); CHECK(j.state() == UPD_READY && j.files().size() == 8 && j.percent() == 100); CHECK(!w.fs.exists(w.p.zip()) && w.fs.exists(w.p.fresh())); CHECK(w.allOld());
      CHECK(net.urls.back() == "https://github.com/chatelp/ACNH-Power-Pockets/releases/download/v1.4.1/ACNH-Power-Pockets-v1.4.1.zip");
      CHECK(installUpdate(w.fs, w.p, j.files(), "1.4.0", j.release().version) == INST_OK); CHECK(w.allNew()); }
    auto expectFail = [&](const char* name, UpdateError e, std::function<void(FakeNet&)> prep, UpdateFs* fs = nullptr) {
        World v(name); FakeNet n2 = net; prep(n2); UpdateJob j(n2, fs ? *fs : v.fs, v.sd, "1.4.0");
        j.begin(UPD_CHECKING); j.runCheck(); j.begin(UPD_DOWNLOADING); j.runDownload();
        CHECK(j.state() == UPD_FAILED && j.error() == e); CHECK(!v.fs.exists(v.p.fresh())); CHECK(v.allOld());
    };
    expectFail("j1", UE_CHECKSUM, [](FakeNet& n) { n.zip[500] ^= 1; });
    expectFail("j2", UE_CHECKSUM, [](FakeNet& n) { n.zip.resize(n.zip.size() - 10); });
    expectFail("j3", UE_NETWORK, [](FakeNet& n) { n.cutAfter = 20000; });
    expectFail("j4", UE_NETWORK, [](FakeNet& n) { n.zip += std::string(9000, 'x'); });
    expectFail("j5", UE_PACKAGE, [&](FakeNet& n) { auto f = release("new"); f.push_back({ "atmosphere/x.txt", "x" }); n.zip = makeZip(f); Sha256 h; h.update(n.zip.data(), n.zip.size());
        n.json = releaseJson("v1.4.1", "sha256:" + h.hex(), "", std::to_string(n.zip.size())); });   // right checksum, wrong content
    { FaultFs ff; ff.failAt = 2; expectFail("j6", UE_SD, [](FakeNet&) {}, &ff); }
    { World v("j7"); FakeNet n2 = net; UpdateJob j(n2, v.fs, v.sd, "1.4.0"); j.begin(UPD_CHECKING); j.runCheck(); n2.midway = [&]() { j.cancel(); }; j.begin(UPD_DOWNLOADING); j.runDownload();
      CHECK(j.state() == UPD_CANCELLED && !v.fs.exists(v.p.zip()) && !v.fs.exists(v.p.fresh())); CHECK(v.allOld());
      n2.midway = nullptr; j.begin(UPD_DOWNLOADING); j.runDownload(); CHECK(j.state() == UPD_READY); }   // a second try works
}
// The package of the last release, when it is there: what users would really download.
static void testRealPackage() {
    const char* zip = getenv("PP_RELEASE_ZIP");
    if (!zip) return;
    World w("real");
    writeFile(w.p.zip(), readFile(zip));
    std::vector<std::string> f;
    CHECK(unpackPackage(w.fs, w.p, f, nullptr) == UNP_OK);
    printf("  real package: %zu files\n", f.size());
    CHECK(installUpdate(w.fs, w.p, f, "1.0.0", "1.1.0") == INST_OK); CHECK(w.mine());
    CHECK(recoverUpdate(w.fs, w.p, true) == REC_ROLLED_BACK); CHECK(w.allOld());
}

int main() {
    const char* t = getenv("PP_TMP");
    g_tmp = std::string(t ? t : "/tmp") + "/update_test";
    std::string cmd = "rm -rf '" + g_tmp + "' && mkdir -p '" + g_tmp + "'";
    if (system(cmd.c_str()) != 0) return 2;
    testVersions(); testSha(); testRelease(); testEntries(); testUnpack(); testInstall(); testJob(); testRealPackage();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
