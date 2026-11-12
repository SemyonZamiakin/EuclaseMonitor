// Skin loader tests: every way a hand-edited skin.json can go wrong must give a clear message, never a crash.
#include "../src/render.h"
#include "../src/skin.h"
#include "../src/util.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <vector>

static int g_run = 0, g_fail = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
using namespace qm;

static std::wstring tmpDir() {
    wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t);
    std::wstring d = std::wstring(t) + L"qm_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount());
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
static bool load(const std::string& json, Skin& s, std::string& err) {
    std::wstring d = tmpDir();
    writeFileAtomic(joinPath(d, L"skin.json"), json);
    return loadSkin(d, s, err);
}

int main() {
    Skin s; std::string err;
    CHECK(load("{ \"widgets\": [ {\"type\":\"text\",\"text\":\"hi {cpu.load:0}\"} ] }", s, err));
    CHECK(s.widgets.size() == 1 && s.name == "Unnamed" && s.updateMs == 1000);

    // colours and variables
    Color c; std::map<std::string, std::string> v{{"a", "#112233"}, {"b", "$a"}};
    CHECK(parseColor("#ff8000", v, c) && c.r == 255 && c.g == 128 && c.b == 0 && c.a == 255);
    CHECK(parseColor("#ff800080", v, c) && c.a == 0x80);
    CHECK(parseColor("#f80", v, c) && c.r == 0xff && c.g == 0x88);
    CHECK(parseColor("$b", v, c) && c.r == 0x11);               // variable referencing a variable
    CHECK(!parseColor("$missing", v, c) && !parseColor("red-ish", v, c) && !parseColor("#12345", v, c));
    CHECK(parseColor("transparent", v, c) && c.a == 0);

    // broken files -> error message, nothing thrown
    CHECK(!load("{ \"widgets\": [ ", s, err) && err.find("line") != std::string::npos);
    CHECK(!load("[1,2,3]", s, err));
    CHECK(!load("{ \"name\": \"x\" }", s, err) && err.find("widgets") != std::string::npos);
    CHECK(!loadSkin(L"Z:\\definitely\\not\\here", s, err));

    // soft problems -> warnings, the rest of the skin still loads
    CHECK(load("{ \"widgets\": [ {\"type\":\"banana\"}, {\"type\":\"bar\",\"w\":10,\"h\":4}, {\"type\":\"bar\",\"value\":\"cpu.load +\",\"w\":10,\"h\":4}, "
               "{\"type\":\"text\",\"text\":\"ok\",\"color\":\"nope\"}, {\"type\":\"text\",\"text\":\"{broken\"} ] }", s, err));
    CHECK(s.widgets.size() == 2 && s.warnings.size() >= 4);       // the valid text widgets survive
    bool sawUnknown = false; for (auto& w : s.warnings) if (w.find("unknown widget type") != std::string::npos) sawUnknown = true;
    CHECK(sawUnknown);

    // repeat: $i / $i1 substitution and offsets
    CHECK(load("{ \"widgets\": [ {\"type\":\"repeat\",\"count\":3,\"x\":10,\"y\":5,\"dx\":20,\"dy\":2,"
               "\"template\":{\"type\":\"bar\",\"w\":5,\"h\":5,\"value\":\"cpu.core$i.load\"} } ] }", s, err));
    CHECK(s.widgets.size() == 3 && s.widgets[2].x == 50 && s.widgets[2].y == 9);
    Metrics m; m.set("cpu.core2.load", 77); m.set("cpu.core0.load", 11);
    CHECK(s.widgets[2].value.eval(m) == 77 && s.widgets[0].value.eval(m) == 11);
    CHECK(load("{ \"widgets\": [ {\"type\":\"repeat\",\"count\":2,\"template\":{\"type\":\"text\",\"text\":\"{proc.cpu$i1.name}\"} } ] }", s, err));
    CHECK(s.widgets.size() == 2);
    CHECK(load("{ \"widgets\": [ {\"type\":\"repeat\",\"count\":100000,\"template\":{\"type\":\"rect\",\"w\":1,\"h\":1} } ] }", s, err) && s.widgets.size() == 128);   // capped

    // groups offset their children and can hide them
    CHECK(load("{ \"widgets\": [ {\"type\":\"group\",\"x\":100,\"y\":50,\"visible\":\"gpu.temp>1\",\"children\":[ {\"type\":\"rect\",\"x\":1,\"y\":2,\"w\":3,\"h\":4} ]} ] }", s, err));
    CHECK(s.widgets.size() == 1 && s.widgets[0].x == 101 && s.widgets[0].y == 52 && s.widgets[0].hasVisible);

    // thresholds pick the highest matching colour
    CHECK(load("{ \"vars\":{\"r\":\"#ff0000\"}, \"widgets\": [ {\"type\":\"bar\",\"w\":5,\"h\":5,\"value\":\"x\",\"color\":\"#00ff00\","
               "\"colors\":[{\"above\":50,\"color\":\"#ffff00\"},{\"above\":90,\"color\":\"$r\"}] } ] }", s, err));
    CHECK(pickThreshold(s.widgets[0], 10).g == 255 && pickThreshold(s.widgets[0], 60).r == 255 && pickThreshold(s.widgets[0], 60).g == 255 && pickThreshold(s.widgets[0], 95).g == 0);

    // limits
    std::string many = "{ \"widgets\": [";
    for (int i = 0; i < 600; ++i) many += "{\"type\":\"rect\",\"w\":1,\"h\":1},";
    many += "] }";
    CHECK(!load(many, s, err) && err.find("too many") != std::string::npos);

    // graph history is bounded
    CHECK(load("{ \"widgets\": [ {\"type\":\"graph\",\"w\":50,\"h\":20,\"value\":\"cpu.load\",\"history\":5} ] }", s, err));
    for (int i = 0; i < 20; ++i) { m.set("cpu.load", i); updateSkinState(s, m); }
    CHECK(s.widgets[0].hist.size() == 5 && s.widgets[0].hist.back() == 19);

    // end to end: the real renderer draws the bundled skins without crashing, and the PNG has content
    Renderer r;
    Metrics full;
    full.set("cpu.load", 40); full.set("gpu.load", 60); full.set("ram.used_pct", 50); full.setStr("cpu.name", "Test CPU");
    for (const wchar_t* name : {L"glass", L"minimal", L"aurora"}) {
        std::wstring dir = joinPath(exeDir(), std::wstring(L"..\\skins\\") + name);
        Skin b; std::string e2;
        CHECK(loadSkin(dir, b, e2));
        CHECK(b.warnings.empty());
        std::wstring png = joinPath(tmpDir(), L"out.png");
        CHECK(r.drawToPng(png, b, full, 1.5f));
        std::string bytes; CHECK(readFile(png, bytes) && bytes.size() > 800);
    }
    // background cache: a cached frame must be byte-identical to a frame drawn from scratch
    for (const wchar_t* name : {L"glass", L"aurora"}) {
        std::wstring dir = joinPath(exeDir(), std::wstring(L"..\\skins\\") + name);
        Skin b; std::string e2;
        CHECK(loadSkin(dir, b, e2));
        const int w = (int)(b.width * 1.25f + 0.999f), h = (int)(b.height * 1.25f + 0.999f);
        std::vector<unsigned char> a((size_t)w * h * 4, 0x11), c((size_t)w * h * 4, 0x22), fresh((size_t)w * h * 4, 0x33);
        Renderer r1;
        r1.draw(a.data(), w, h, b, full, 1.25f);        // fills the cache
        r1.draw(c.data(), w, h, b, full, 1.25f);        // uses it
        Renderer r2;
        r2.draw(fresh.data(), w, h, b, full, 1.25f);    // independent renderer, no cache
        CHECK(a == fresh);
        CHECK(c == fresh);
        r1.clearCaches();                                // a skin reload must invalidate it
        b.bgColor = Color{200, 10, 10, 255}; b.bgImage.clear(); b.bgGradient = false;
        r1.draw(c.data(), w, h, b, full, 1.25f);
        r2.clearCaches();
        r2.draw(fresh.data(), w, h, b, full, 1.25f);
        CHECK(c == fresh && c != a);
    }

    // metric names a skin reads (drives which OS queries run)
    {
        Skin t; std::string e3;
        CHECK(load("{ \"widgets\": [ {\"type\":\"text\",\"text\":\"{cpu.load:0}% {net.down:rate}\",\"visible\":\"gpu.temp > 0\"},"
                   " {\"type\":\"bar\",\"value\":\"max(ram.used_pct, 0)\",\"w\":10,\"h\":4} ] }", t, e3));
        std::vector<std::string> names;
        collectMetricNames(t, names);
        auto has = [&](const char* n) { for (auto& x : names) if (x == n) return true; return false; };
        CHECK(has("cpu.load") && has("net.down") && has("gpu.temp") && has("ram.used_pct"));
        CHECK(!has("max") && !has("proc.cpu1.name"));
    }

    // images must stay inside the skin folder (no ..\ escapes from downloaded skins)
    Skin evil; std::wstring d = tmpDir();
    writeFileAtomic(joinPath(d, L"skin.json"), "{ \"widgets\": [ {\"type\":\"image\",\"file\":\"..\\\\..\\\\Windows\\\\win.ini\",\"w\":10,\"h\":10} ] }");
    CHECK(loadSkin(d, evil, err) && r.drawToPng(joinPath(tmpDir(), L"e.png"), evil, full, 1.f));   // draws nothing, does not read outside, does not crash

    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
