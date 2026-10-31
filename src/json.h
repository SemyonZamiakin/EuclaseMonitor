// Tiny lenient JSON ("JSONC") parser for skin files: allows // and /* */ comments and trailing commas,
// reports errors with line/column. Not a general-purpose library on purpose (keeps the exe small).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace qm {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;  // keeps file order

    bool isObj() const { return type == Obj; }
    bool isArr() const { return type == Arr; }
    bool isStr() const { return type == Str; }
    bool isNum() const { return type == Num; }
    const Json* find(const std::string& key) const;
    // Typed getters with defaults (never throw).
    std::string str(const std::string& key, const std::string& def = "") const;
    double num(const std::string& key, double def = 0) const;
    bool boolean(const std::string& key, bool def = false) const;
};

// Returns false and fills `err` ("line 3, col 14: expected ':'") on failure.
bool parseJson(const std::string& text, Json& out, std::string& err);
std::string jsonEscape(const std::string& s);

}  // namespace qm
