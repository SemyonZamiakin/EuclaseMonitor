#include "expr.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace qm {

enum Op { N_NUM, N_VAR, N_NEG, N_NOT, N_ADD, N_SUB, N_MUL, N_DIV, N_MOD, N_LT, N_LE, N_GT, N_GE, N_EQ, N_NE, N_AND, N_OR, N_CALL };

struct ExprParser {
    const std::string& t;
    Expr& e;
    size_t i = 0;
    std::string err;
    int depth = 0;
    ExprParser(const std::string& s, Expr& ex) : t(s), e(ex) {}

    int add(Expr::Node n) { e.nodes_.push_back(std::move(n)); return (int)e.nodes_.size() - 1; }
    void ws() { while (i < t.size() && isspace((unsigned char)t[i])) ++i; }
    bool fail(const std::string& m) { if (err.empty()) err = "col " + std::to_string(i + 1) + ": " + m; return false; }

    int orExpr() {
        int l = andExpr(); if (l < 0) return -1;
        for (;;) { ws(); if (t.compare(i, 2, "||") == 0) { i += 2; int r = andExpr(); if (r < 0) return -1; Expr::Node n; n.op = N_OR; n.a = l; n.b = r; l = add(n); } else return l; }
    }
    int andExpr() {
        int l = cmpExpr(); if (l < 0) return -1;
        for (;;) { ws(); if (t.compare(i, 2, "&&") == 0) { i += 2; int r = cmpExpr(); if (r < 0) return -1; Expr::Node n; n.op = N_AND; n.a = l; n.b = r; l = add(n); } else return l; }
    }
    int cmpExpr() {
        int l = addExpr(); if (l < 0) return -1;
        for (;;) {
            ws(); int op = -1; size_t len = 0;
            if (t.compare(i, 2, "<=") == 0) { op = N_LE; len = 2; } else if (t.compare(i, 2, ">=") == 0) { op = N_GE; len = 2; }
            else if (t.compare(i, 2, "==") == 0) { op = N_EQ; len = 2; } else if (t.compare(i, 2, "!=") == 0) { op = N_NE; len = 2; }
            else if (i < t.size() && t[i] == '<') { op = N_LT; len = 1; } else if (i < t.size() && t[i] == '>') { op = N_GT; len = 1; }
            if (op < 0) return l;
            i += len; int r = addExpr(); if (r < 0) return -1;
            Expr::Node n; n.op = op; n.a = l; n.b = r; l = add(n);
        }
    }
    int addExpr() {
        int l = mulExpr(); if (l < 0) return -1;
        for (;;) {
            ws();
            if (i < t.size() && (t[i] == '+' || t[i] == '-')) { int op = t[i] == '+' ? N_ADD : N_SUB; ++i; int r = mulExpr(); if (r < 0) return -1; Expr::Node n; n.op = op; n.a = l; n.b = r; l = add(n); }
            else return l;
        }
    }
    int mulExpr() {
        int l = unary(); if (l < 0) return -1;
        for (;;) {
            ws();
            if (i < t.size() && (t[i] == '*' || t[i] == '/' || t[i] == '%')) { int op = t[i] == '*' ? N_MUL : t[i] == '/' ? N_DIV : N_MOD; ++i; int r = unary(); if (r < 0) return -1; Expr::Node n; n.op = op; n.a = l; n.b = r; l = add(n); }
            else return l;
        }
    }
    int unary() {
        ws();
        if (i < t.size() && t[i] == '-') { ++i; int a = unary(); if (a < 0) return -1; Expr::Node n; n.op = N_NEG; n.a = a; return add(n); }
        if (i < t.size() && t[i] == '!' && !(i + 1 < t.size() && t[i + 1] == '=')) { ++i; int a = unary(); if (a < 0) return -1; Expr::Node n; n.op = N_NOT; n.a = a; return add(n); }
        return primary();
    }
    int primary() {
        ws();
        if (++depth > 40) { fail("expression nested too deeply"); return -1; }
        int r = primary2();
        --depth;
        return r;
    }
    int primary2() {
        if (i >= t.size()) { fail("unexpected end of expression"); return -1; }
        char c = t[i];
        if (c == '(') {
            ++i; int r = orExpr(); if (r < 0) return -1; ws();
            if (i >= t.size() || t[i] != ')') { fail("missing ')'"); return -1; }
            ++i; return r;
        }
        if (isdigit((unsigned char)c) || c == '.') {
            char* end = nullptr; double v = strtod(t.c_str() + i, &end);
            if (end == t.c_str() + i) { fail("bad number"); return -1; }
            i = end - t.c_str(); Expr::Node n; n.op = N_NUM; n.num = v; return add(n);
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t s = i;
            while (i < t.size() && (isalnum((unsigned char)t[i]) || t[i] == '_' || t[i] == '.')) ++i;
            std::string name = t.substr(s, i - s);
            ws();
            if (i < t.size() && t[i] == '(') {  // function call
                ++i; Expr::Node n; n.op = N_CALL; n.name = name;
                ws();
                if (i < t.size() && t[i] == ')') { ++i; return add(n); }
                for (;;) {
                    int a = orExpr(); if (a < 0) return -1;
                    n.args.push_back(a); ws();
                    if (i < t.size() && t[i] == ',') { ++i; continue; }
                    if (i < t.size() && t[i] == ')') { ++i; break; }
                    fail("expected ',' or ')' in call to " + name); return -1;
                }
                static const struct { const char* n; size_t lo, hi; } F[] = {{"min", 2, 8}, {"max", 2, 8}, {"clamp", 3, 3}, {"abs", 1, 1}, {"round", 1, 1}, {"floor", 1, 1}, {"ceil", 1, 1}, {"if", 3, 3}};
                bool known = false;
                for (auto& f : F) if (name == f.n) { known = true; if (n.args.size() < f.lo || n.args.size() > f.hi) { fail("wrong number of arguments for " + name + "()"); return -1; } }
                if (!known) { fail("unknown function '" + name + "'"); return -1; }
                return add(n);
            }
            Expr::Node n; n.op = N_VAR; n.name = name; return add(n);
        }
        fail(std::string("unexpected '") + c + "'");
        return -1;
    }
};

bool Expr::parse(const std::string& text, std::string& err) {
    nodes_.clear(); root_ = -1; src_ = text;
    if (text.find_first_not_of(" \t\r\n") == std::string::npos) { err = "empty expression"; return false; }
    ExprParser p(text, *this);
    int r = p.orExpr();
    if (r >= 0) { p.ws(); if (p.i != text.size()) { p.fail("unexpected '" + std::string(1, text[p.i]) + "'"); r = -1; } }
    if (r < 0) { err = p.err; nodes_.clear(); return false; }
    root_ = r;
    return true;
}

double Expr::eval(const Metrics& m) const { return root_ < 0 ? NAN : evalNode(root_, m, 0); }

double Expr::evalNode(int idx, const Metrics& m, int depth) const {
    if (depth > 200) return NAN;
    const Node& n = nodes_[idx];
    auto A = [&] { return evalNode(n.a, m, depth + 1); };
    auto B = [&] { return evalNode(n.b, m, depth + 1); };
    switch (n.op) {
        case N_NUM: return n.num;
        case N_VAR: return m.get(n.name);
        case N_NEG: return -A();
        case N_NOT: { double a = A(); return std::isnan(a) ? NAN : (a == 0 ? 1.0 : 0.0); }
        case N_ADD: return A() + B();
        case N_SUB: return A() - B();
        case N_MUL: return A() * B();
        case N_DIV: { double b = B(); return b == 0 ? NAN : A() / b; }
        case N_MOD: { double b = B(); return b == 0 ? NAN : std::fmod(A(), b); }
        case N_LT: { double a = A(), b = B(); return std::isnan(a) || std::isnan(b) ? NAN : (a < b); }
        case N_LE: { double a = A(), b = B(); return std::isnan(a) || std::isnan(b) ? NAN : (a <= b); }
        case N_GT: { double a = A(), b = B(); return std::isnan(a) || std::isnan(b) ? NAN : (a > b); }
        case N_GE: { double a = A(), b = B(); return std::isnan(a) || std::isnan(b) ? NAN : (a >= b); }
        case N_EQ: { double a = A(), b = B(); return std::isnan(a) || std::isnan(b) ? NAN : (a == b); }
        case N_NE: { double a = A(), b = B(); return std::isnan(a) || std::isnan(b) ? NAN : (a != b); }
        case N_AND: { double a = A(); if (std::isnan(a)) return NAN; if (a == 0) return 0; double b = B(); return std::isnan(b) ? NAN : (b != 0); }
        case N_OR: { double a = A(); if (!std::isnan(a) && a != 0) return 1; double b = B(); return std::isnan(b) ? (std::isnan(a) ? NAN : 0) : (b != 0); }
        case N_CALL: {
            auto arg = [&](size_t k) { return evalNode(n.args[k], m, depth + 1); };
            const std::string& f = n.name;
            if (f == "min" || f == "max") {
                double r = arg(0);
                for (size_t k = 1; k < n.args.size(); ++k) { double v = arg(k); if (std::isnan(v) || std::isnan(r)) return NAN; r = f == "min" ? std::fmin(r, v) : std::fmax(r, v); }
                return r;
            }
            if (f == "clamp") { double v = arg(0), lo = arg(1), hi = arg(2); return std::isnan(v) ? NAN : std::fmin(std::fmax(v, lo), hi); }
            if (f == "abs") return std::fabs(arg(0));
            if (f == "round") return std::round(arg(0));
            if (f == "floor") return std::floor(arg(0));
            if (f == "ceil") return std::ceil(arg(0));
            if (f == "if") { double c = arg(0); if (std::isnan(c)) return NAN; return c != 0 ? arg(1) : arg(2); }
            return NAN;
        }
    }
    return NAN;
}

// ---------------- number formatting ----------------
static std::string fixed(double v, int dec) {
    char b[64];
    snprintf(b, sizeof b, "%.*f", dec, v);
    return b;
}

std::string formatNumber(double v, const std::string& fmt) {
    if (std::isnan(v) || std::isinf(v)) return "n/a";
    if (fmt.empty() || fmt == "auto") return fixed(v, std::fabs(v) < 10 && v != std::floor(v) ? 1 : 0);
    if (fmt.size() == 1 && fmt[0] >= '0' && fmt[0] <= '4') return fixed(v, fmt[0] - '0');
    if (fmt == "bytes" || fmt == "rate") {
        static const char* U[] = {"B", "KB", "MB", "GB", "TB"};
        int u = 0; double x = std::fabs(v);
        while (x >= 1024 && u < 4) { x /= 1024; ++u; }
        std::string s = fixed(v < 0 ? -x : x, x >= 100 || u == 0 ? 0 : x >= 10 ? 1 : 2);
        return s + " " + U[u] + (fmt == "rate" ? "/s" : "");
    }
    if (fmt == "mhz") return v >= 1000 ? fixed(v / 1000.0, 2) + " GHz" : fixed(v, 0) + " MHz";
    if (fmt == "uptime") {
        long long s = (long long)v, d = s / 86400, h = s % 86400 / 3600, mi = s % 3600 / 60;
        char b[48];
        if (d > 0) snprintf(b, sizeof b, "%lldd %02lld:%02lld", d, h, mi); else snprintf(b, sizeof b, "%02lld:%02lld", h, mi);
        return b;
    }
    if (fmt == "mbit") return fixed(v * 8.0 / 1e6, 1) + " Mbit/s";  // value is bytes/s
    if (fmt == "pct") return fixed(v, 0) + "%";
    return fixed(v, 0);
}

// ---------------- text templates ----------------
static bool isBareName(const std::string& s) {
    if (s.empty() || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(isalnum((unsigned char)c) || c == '_' || c == '.')) return false;
    return true;
}

bool Template::parse(const std::string& text, std::string& err) {
    segs_.clear(); dynamic_ = false;
    std::string lit;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '{' && i + 1 < text.size() && text[i + 1] == '{') { lit += '{'; ++i; continue; }
        if (c == '}' && i + 1 < text.size() && text[i + 1] == '}') { lit += '}'; ++i; continue; }
        if (c != '{') { lit += c; continue; }
        size_t close = text.find('}', i);
        if (close == std::string::npos) { err = "missing '}' in text \"" + text + "\""; return false; }
        std::string inner = text.substr(i + 1, close - i - 1), fmt;
        size_t colon = inner.rfind(':');
        if (colon != std::string::npos) { fmt = inner.substr(colon + 1); inner = inner.substr(0, colon); }
        if (!lit.empty()) { Seg s; s.lit = lit; segs_.push_back(std::move(s)); lit.clear(); }
        Seg s; s.isExpr = true; s.fmt = fmt;
        std::string e2;
        if (!s.expr.parse(inner, e2)) { err = "in {" + inner + "}: " + e2; return false; }
        if (isBareName(inner)) s.bareName = inner;
        segs_.push_back(std::move(s));
        dynamic_ = true;
        i = close;
    }
    if (!lit.empty()) { Seg s; s.lit = lit; segs_.push_back(std::move(s)); }
    return true;
}

std::string Template::render(const Metrics& m) const {
    std::string out;
    for (const Seg& s : segs_) {
        if (!s.isExpr) { out += s.lit; continue; }
        if (!s.bareName.empty()) {
            if (const std::string* str = m.getStr(s.bareName)) { out += *str; continue; }
        }
        out += formatNumber(s.expr.eval(m), s.fmt);
    }
    return out;
}

}  // namespace qm
