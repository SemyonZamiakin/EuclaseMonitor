#include "../src/export_filter.h"
#include "../src/expr.h"
#include "../src/json.h"
#include <cmath>
#include <cstdio>
#include <string>

static int g_run = 0, g_fail = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace qm;

static double ev(const char* src, const Metrics& m) {
    Expr e; std::string err;
    if (!e.parse(src, err)) { printf("  parse error for '%s': %s\n", src, err.c_str()); return NAN; }
    return e.eval(m);
}

static void test_json() {
    Json j; std::string err;
    CHECK(parseJson("{ // comment\n \"a\": 1, /* c */ \"b\": [1,2,3,], \"c\": \"x\\u00e9\\n\", \"d\": true, }", j, err));
    CHECK(j.num("a") == 1 && j.find("b")->a.size() == 3 && j.str("c") == "x\xC3\xA9\n" && j.boolean("d"));
    CHECK(j.num("missing", 7) == 7 && j.str("a", "def") == "def");   // wrong type -> default, no crash
    CHECK(!parseJson("{ \"a\" 1 }", j, err) && err.find("line 1") != std::string::npos && err.find("':'") != std::string::npos);
    CHECK(!parseJson("{\n\n \"a\": [1, 2 }", j, err) && err.find("line 3") != std::string::npos);
    CHECK(!parseJson("{\"a\": 1} trailing", j, err));
    CHECK(!parseJson("", j, err));
    CHECK(!parseJson("{\"s\": \"unterminated", j, err));
    std::string deep(200, '['); CHECK(!parseJson(deep, j, err));          // nesting bomb is rejected, not a stack overflow
    CHECK(parseJson("\"\\ud83d\\ude00\"", j, err) && j.s == "\xF0\x9F\x98\x80");   // surrogate pair
    CHECK(jsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\u000a");
}

static void test_expr() {
    Metrics m; m.set("cpu.load", 42); m.set("ram.used", 8); m.set("ram.total", 32); m.set("gpu.temp", 71);
    CHECK(ev("1 + 2 * 3", m) == 7);
    CHECK(ev("(1 + 2) * 3", m) == 9);
    CHECK(ev("-cpu.load + 2", m) == -40);
    CHECK(ev("ram.used / ram.total * 100", m) == 25);
    CHECK(ev("max(cpu.load, gpu.temp, 3)", m) == 71);
    CHECK(ev("min(cpu.load, 10)", m) == 10);
    CHECK(ev("clamp(150, 0, 100)", m) == 100);
    CHECK(ev("if(gpu.temp > 70, 1, 2)", m) == 1);
    CHECK(ev("gpu.temp >= 70 && cpu.load < 50", m) == 1);
    CHECK(ev("!(cpu.load > 50) || 0", m) == 1);
    CHECK(ev("10 % 4", m) == 2);
    CHECK(ev("round(2.5) + floor(2.9) + ceil(2.1)", m) == 3 + 2 + 3);
    CHECK(std::isnan(ev("missing.metric + 1", m)));      // unknown metric -> NaN, propagates
    CHECK(std::isnan(ev("1 / 0", m)));                   // no crash, no inf
    CHECK(std::isnan(ev("missing > 3", m)));
    Expr e; std::string err;
    CHECK(!e.parse("1 +", err) && !err.empty());
    CHECK(!e.parse("foo(1)", err) && err.find("unknown function") != std::string::npos);
    CHECK(!e.parse("min(1)", err));                      // arity checked
    CHECK(!e.parse("(1 + 2", err));
    CHECK(!e.parse("", err));
    CHECK(!e.parse("1 2", err));
    std::string deep(500, '('); deep += "1"; deep += std::string(500, ')');
    CHECK(!e.parse(deep, err));                          // depth limit instead of stack overflow
}

static void test_format() {
    CHECK(formatNumber(3.14159, "2") == "3.14");
    CHECK(formatNumber(41.6, "0") == "42");
    CHECK(formatNumber(NAN, "0") == "n/a");
    CHECK(formatNumber(512, "bytes") == "512 B");
    CHECK(formatNumber(1536, "bytes") == "1.50 KB");
    CHECK(formatNumber(5.5 * 1024 * 1024, "rate") == "5.50 MB/s");
    CHECK(formatNumber(34 * 1024.0 * 1024 * 1024, "bytes") == "34.0 GB");
    CHECK(formatNumber(4700, "mhz") == "4.70 GHz");
    CHECK(formatNumber(800, "mhz") == "800 MHz");
    CHECK(formatNumber(3725, "uptime") == "01:02");
    CHECK(formatNumber(90061, "uptime") == "1d 01:01");
}

static void test_template() {
    Metrics m; m.set("cpu.load", 42.4); m.set("net.down", 2 * 1024 * 1024); m.setStr("cpu.name", "Intel i9");
    Template t; std::string err;
    CHECK(t.parse("CPU {cpu.load:0}%  {net.down:rate}  {cpu.name}", err));
    CHECK(t.render(m) == "CPU 42%  2.00 MB/s  Intel i9");
    CHECK(t.hasDynamic());
    CHECK(t.parse("literal {{braces}}", err) && t.render(m) == "literal {braces}" && !t.hasDynamic());
    CHECK(t.parse("{cpu.load / 2:1}", err) && t.render(m) == "21.2");
    CHECK(t.parse("{nope}", err) && t.render(m) == "n/a");
    CHECK(!t.parse("broken {cpu.load", err));
    CHECK(!t.parse("bad {1 +}", err));
}

static void test_export_filter() {
    CHECK(exportAllowed("proc.cpu1.name", false));        // loopback: everything
    CHECK(exportAllowed("cpu.load", true) && exportAllowed("gpu.temp", true) && exportAllowed("ram.used_gb", true));
    CHECK(!exportAllowed("proc.cpu1.name", true) && !exportAllowed("proc.mem3.mem", true) && !exportAllowed("proc.count", true));   // LAN: no program list
}

int main() {
    test_json(); test_expr(); test_format(); test_template(); test_export_filter();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
