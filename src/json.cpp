#include "json.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace qm {

const Json* Json::find(const std::string& key) const {
    if (type != Obj) return nullptr;
    for (auto& kv : o) if (kv.first == key) return &kv.second;
    return nullptr;
}
std::string Json::str(const std::string& key, const std::string& def) const {
    const Json* j = find(key);
    return j && j->type == Str ? j->s : def;
}
double Json::num(const std::string& key, double def) const {
    const Json* j = find(key);
    return j && j->type == Num ? j->n : def;
}
bool Json::boolean(const std::string& key, bool def) const {
    const Json* j = find(key);
    return j && j->type == Bool ? j->b : def;
}

namespace {

struct P {
    const std::string& t;
    size_t i = 0;
    std::string err;
    int depth = 0;
    explicit P(const std::string& s) : t(s) {}

    bool fail(const std::string& m) {
        if (!err.empty()) return false;
        int line = 1, col = 1;
        for (size_t k = 0; k < i && k < t.size(); ++k) { if (t[k] == '\n') { ++line; col = 1; } else ++col; }
        err = "line " + std::to_string(line) + ", col " + std::to_string(col) + ": " + m;
        return false;
    }

    void ws() {
        for (;;) {
            while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\r' || t[i] == '\n')) ++i;
            if (i + 1 < t.size() && t[i] == '/' && t[i + 1] == '/') { while (i < t.size() && t[i] != '\n') ++i; continue; }
            if (i + 1 < t.size() && t[i] == '/' && t[i + 1] == '*') {
                i += 2;
                while (i + 1 < t.size() && !(t[i] == '*' && t[i + 1] == '/')) ++i;
                i = i + 2 <= t.size() ? i + 2 : t.size();
                continue;
            }
            break;
        }
    }

    static void putUtf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += char(cp);
        else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
        else { s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 0x3F)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F)); }
    }

    bool hex4(unsigned& v) {
        if (i + 4 > t.size()) return fail("bad \\u escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail("bad \\u escape");
        }
        return true;
    }

    bool string(std::string& out) {
        if (t[i] != '"') return fail("expected string");
        ++i;
        out.clear();
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c == '\n') { --i; return fail("unterminated string"); }
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) return fail("bad escape");
            char e = t[i++];
            switch (e) {
                case '"': out += '"'; break;  case '\\': out += '\\'; break; case '/': out += '/'; break;
                case 'b': out += '\b'; break; case 'f': out += '\f'; break;  case 'n': out += '\n'; break;
                case 'r': out += '\r'; break; case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        i += 2;
                        unsigned lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        if (i >= t.size()) return fail("unterminated string");
        ++i;
        return true;
    }

    bool value(Json& j) {
        ws();
        if (i >= t.size()) return fail("unexpected end of file");
        if (++depth > 64) return fail("nesting too deep");
        bool ok = value2(j);
        --depth;
        return ok;
    }

    bool value2(Json& j) {
        char c = t[i];
        if (c == '{') {
            ++i; j.type = Json::Obj;
            for (;;) {
                ws();
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                std::string key;
                if (i >= t.size() || !string(key)) return fail("expected a quoted key");
                ws();
                if (i >= t.size() || t[i] != ':') return fail("expected ':' after key \"" + key + "\"");
                ++i;
                Json v;
                if (!value(v)) return false;
                j.o.emplace_back(std::move(key), std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            ++i; j.type = Json::Arr;
            for (;;) {
                ws();
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                Json v;
                if (!value(v)) return false;
                j.a.push_back(std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { j.type = Json::Str; return string(j.s); }
        if (!strncmp(t.c_str() + i, "true", 4)) { i += 4; j.type = Json::Bool; j.b = true; return true; }
        if (!strncmp(t.c_str() + i, "false", 5)) { i += 5; j.type = Json::Bool; j.b = false; return true; }
        if (!strncmp(t.c_str() + i, "null", 4)) { i += 4; j.type = Json::Null; return true; }
        if (c == '-' || (c >= '0' && c <= '9') || c == '.') {
            char* end = nullptr;
            double v = strtod(t.c_str() + i, &end);
            if (end == t.c_str() + i || !std::isfinite(v)) return fail("bad number");
            i = end - t.c_str();
            j.type = Json::Num; j.n = v;
            return true;
        }
        return fail(std::string("unexpected character '") + c + "'");
    }
};

}  // namespace

bool parseJson(const std::string& text, Json& out, std::string& err) {
    P p(text);
    out = Json();
    if (!p.value(out)) { err = p.err; return false; }
    p.ws();
    if (p.i != text.size()) { p.fail("unexpected text after the end of the document"); err = p.err; return false; }
    return true;
}

std::string jsonEscape(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += char(c); }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); r += b; }
        else r += char(c);
    }
    return r;
}

}  // namespace qm
