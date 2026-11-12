// Developer tool: measures where one tick of Euclase Monitor spends its time.
// Times every sampler, the skin update and the GDI+ draw, using live metrics from this PC.
//   bench_tick <skin folder> [ticks=200] [scale=1] [--all]
// By default the samplers get the same demand as in the app (what the skin, the tray tooltip and the
// metrics ring read); --all samples everything, which is what version 0.1.0 always did.
#include "../src/render.h"
#include "../src/samplers.h"
#include "../src/util.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static double nowUs() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

struct Stat {
    std::string name;
    std::vector<double> us;
    void add(double v) { us.push_back(v); }
    double median() { auto v = us; std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }
    double p95() { auto v = us; std::sort(v.begin(), v.end()); return v.empty() ? 0 : v[(size_t)(v.size() * 0.95)]; }
    double mean() { double s = 0; for (double x : us) s += x; return us.empty() ? 0 : s / us.size(); }
};

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: bench_tick <skin folder> [ticks=200] [scale=1]\n"); return 2; }
    int ticks = argc > 2 ? atoi(argv[2]) : 200;
    float scale = argc > 3 ? (float)atof(argv[3]) : 1.f;
    bool all = argc > 4 && std::string(argv[4]) == "--all";
    qm::Skin skin; std::string err;
    if (!qm::loadSkin(qm::widen(argv[1]), skin, err)) { printf("ERROR: %s\n", err.c_str()); return 1; }

    qm::Renderer r;
    auto samplers = qm::createSamplers();
    qm::Metrics m;
    if (!all) {
        qm::Demand d; d.all = false;
        std::vector<std::string> names;
        qm::collectMetricNames(skin, names);
        for (const char* n : {"cpu.load", "gpu.load", "ram.used_pct", "cpu.clock", "ram.used_gb", "gpu.temp", "gpu.vram_used_pct", "net.down", "net.up"}) names.push_back(n);
        d.names.insert(names.begin(), names.end());
        for (auto& s : samplers) s->setDemand(d);
    }
    int w = (int)(skin.width * scale + 0.999f), h = (int)(skin.height * scale + 0.999f);
    std::vector<unsigned char> bits((size_t)w * h * 4);

    std::vector<Stat> st(samplers.size());
    for (size_t i = 0; i < samplers.size(); ++i) st[i].name = samplers[i]->name();
    Stat upd{"skin update", {}}, draw{"draw (GDI+)", {}}, total{"tick total", {}};
    for (int k = 0; k < 3; ++k) { for (auto& s : samplers) s->sample(m, 0.25); qm::updateSkinState(skin, m); Sleep(50); }   // prime deltas

    for (int k = 0; k < ticks; ++k) {
        double t0 = nowUs();
        for (size_t i = 0; i < samplers.size(); ++i) {
            double a = nowUs();
            samplers[i]->sample(m, 0.05);
            st[i].add(nowUs() - a);
        }
        double a = nowUs(); qm::updateSkinState(skin, m); upd.add(nowUs() - a);
        a = nowUs(); r.draw(bits.data(), w, h, skin, m, scale); draw.add(nowUs() - a);
        total.add(nowUs() - t0);
        Sleep(20);
    }
    printf("skin %s  %dx%d px  %d ticks\n", skin.name.c_str(), w, h, ticks);
    printf("%-16s %10s %10s %10s\n", "stage", "median us", "p95 us", "mean us");
    auto row = [](Stat& s) { printf("%-16s %10.1f %10.1f %10.1f\n", s.name.c_str(), s.median(), s.p95(), s.mean()); };
    for (auto& s : st) row(s);
    row(upd); row(draw); row(total);
    return 0;
}
