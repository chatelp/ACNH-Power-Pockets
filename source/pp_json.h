// ============================================================================
// ACNH Power Pockets - a small, strict JSON reader for files that come from
// outside (GitHub's release answer, catalogue.ac exports). Depth and size are
// bounded; anything malformed is refused whole. Tested on the host.
// ============================================================================
#pragma once

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pp {

struct Json {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;                                 // array items, or object values
    std::vector<std::string> k;                          // object keys, same order as `a`
    const Json* get(const char* key) const {
        if (type != OBJ) return nullptr;
        for (size_t i = 0; i < k.size(); i++) if (k[i] == key) return &a[i];
        return nullptr;
    }
    std::string str(const char* key) const { const Json* j = get(key); return j && j->type == STR ? j->s : std::string(); }
};
struct JsonParser {
    const std::string& t;
    size_t i = 0;
    int nodes = 0;
    explicit JsonParser(const std::string& text) : t(text) {}
    void ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) i++; }
    bool lit(const char* w) { size_t n = strlen(w); if (t.compare(i, n, w) != 0) return false; i += n; return true; }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | c >> 6); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | c >> 12); o += (char)(0x80 | (c >> 6 & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | c >> 18); o += (char)(0x80 | (c >> 12 & 63)); o += (char)(0x80 | (c >> 6 & 63)); o += (char)(0x80 | (c & 63)); }
    }
    bool hex4(uint32_t& v) {
        if (i + 4 > t.size()) return false;
        v = 0;
        for (int k = 0; k < 4; k++) {
            char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& o) {
        if (i >= t.size() || t[i] != '"') return false;
        i++;
        while (i < t.size()) {
            char c = t[i++];
            if (c == '"') return true;
            if ((unsigned char)c < 0x20) return false;
            if (c != '\\') { o += c; continue; }
            if (i >= t.size()) return false;
            char e = t[i++];
            switch (e) {
                case '"': case '\\': case '/': o += e; break;
                case 'b': case 'f': break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    uint32_t v, lo;
                    if (!hex4(v)) return false;
                    if (v >= 0xD800 && v < 0xDC00) {
                        if (i + 2 > t.size() || t[i] != '\\' || t[i + 1] != 'u') return false;
                        i += 2;
                        if (!hex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                        v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (v >= 0xDC00 && v <= 0xDFFF) return false;
                    if (v) utf8(o, v);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(Json& j, int depth) {
        if (depth > 24 || ++nodes > 20000) return false;
        ws();
        if (i >= t.size()) return false;
        char c = t[i];
        if (c == '"') { j.type = Json::STR; return string(j.s); }
        if (c == 't') { j.type = Json::BOOL; j.b = true; return lit("true"); }
        if (c == 'f') { j.type = Json::BOOL; return lit("false"); }
        if (c == 'n') return lit("null");
        if (c == '[' || c == '{') {
            const bool obj = c == '{';
            const char end = obj ? '}' : ']';
            j.type = obj ? Json::OBJ : Json::ARR;
            i++; ws();
            if (i < t.size() && t[i] == end) { i++; return true; }
            for (;;) {
                if (obj) {
                    std::string key;
                    ws();
                    if (!string(key)) return false;
                    ws();
                    if (i >= t.size() || t[i++] != ':') return false;
                    j.k.push_back(key);
                }
                j.a.push_back(Json());
                if (!value(j.a.back(), depth + 1)) return false;
                ws();
                if (i >= t.size()) return false;
                char d = t[i++];
                if (d == end) return true;
                if (d != ',') return false;
            }
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t st = i;
            while (i < t.size() && strchr("+-.eE0123456789", t[i]) && i - st < 32) i++;
            j.type = Json::NUM;
            j.n = strtod(t.substr(st, i - st).c_str(), nullptr);
            return true;
        }
        return false;
    }
    bool parse(Json& j) { if (!value(j, 0)) return false; ws(); return i == t.size(); }
};

} // namespace pp
