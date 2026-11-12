// Smoke tests of the real OS samplers on the machine running them (values must be plausible, not exact).
#include "../src/samplers.h"
#include "../src/util.h"
#include <windows.h>
#include <psapi.h>
#include <cmath>
#include <cstdio>

static int g_run = 0, g_fail = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
using namespace qm;

static void burn(DWORD ms) { DWORD t = GetTickCount(); volatile double x = 0; while (GetTickCount() - t < ms) x += std::sqrt((double)(GetTickCount() & 255)); }

int main() {
    // process list: our own entry must match the OS's own numbers
    std::vector<ProcSnapshotEntry> pl;
    CHECK(readProcessList(pl) && pl.size() > 10);
    PROCESS_MEMORY_COUNTERS pmc{}; GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc);
    bool found = false;
    for (auto& p : pl) if (p.pid == GetCurrentProcessId()) {
        found = true;
        double a = (double)p.workingSet, b = (double)pmc.WorkingSetSize;
        CHECK(std::fabs(a - b) / b < 0.25);                       // struct layout sanity: same working set within 25%
        CHECK(p.name.find("test_samplers") != std::string::npos); // image name decoded
    }
    CHECK(found);

    auto samplers = createSamplers();
    Metrics m;
    for (auto& s : samplers) s->sample(m, 1.0);    // first pass primes the deltas
    burn(700);                                      // real CPU load for the second pass
    for (auto& s : samplers) s->sample(m, 0.7);

    double load = m.get("cpu.load");
    printf("cpu.load=%.1f cores=%.0f clock=%.0f MHz  name=%s\n", load, m.get("cpu.cores"), m.get("cpu.clock"), m.getStr("cpu.name") ? m.getStr("cpu.name")->c_str() : "?");
    CHECK(load >= 0 && load <= 100);
    CHECK(m.get("cpu.cores") >= 1);
    CHECK(m.getStr("cpu.name") && !m.getStr("cpu.name")->empty());
    CHECK(m.get("cpu.core0.load") >= 0);
    CHECK(std::isnan(m.get("cpu.temp")));             // documented as unavailable without a driver

    double used = m.get("ram.used_gb"), total = m.get("ram.total_gb");
    printf("ram %.1f / %.1f GB (%.0f%%)\n", used, total, m.get("ram.used_pct"));
    CHECK(total > 1 && used > 0 && used <= total);
    CHECK(m.get("sys.uptime") > 10);

    printf("net down=%.0f up=%.0f B/s\n", m.get("net.down"), m.get("net.up"));
    CHECK(m.get("net.down") >= 0 && m.get("net.up") >= 0);

    for (auto& s : samplers) s->sample(m, 2.0);
    printf("top cpu: %s %.1f%%   top mem: %s %.0f MB\n", m.getStr("proc.cpu1.name")->c_str(), m.get("proc.cpu1.cpu"),
           m.getStr("proc.mem1.name")->c_str(), m.get("proc.mem1.mem"));
    CHECK(m.getStr("proc.mem1.name") && *m.getStr("proc.mem1.name") != "-");
    CHECK(m.get("proc.mem1.mem") > 10);
    CHECK(m.get("proc.mem1.mem") >= m.get("proc.mem2.mem"));   // sorted

    if (m.has("gpu.name")) {
        printf("gpu %s load=%.0f%% temp=%.0fC clock=%.0f vram=%.1f/%.1f GB power=%.0fW fan=%.0f%%\n", m.getStr("gpu.name")->c_str(), m.get("gpu.load"),
               m.get("gpu.temp"), m.get("gpu.clock"), m.get("gpu.vram_used_gb"), m.get("gpu.vram_total_gb"), m.get("gpu.power_w"), m.get("gpu.fan_pct"));
        CHECK(m.get("gpu.load") >= 0 && m.get("gpu.load") <= 100);
        CHECK(m.get("gpu.temp") > 10 && m.get("gpu.temp") < 120);
        CHECK(m.get("gpu.vram_total_gb") > 0.5);
        CHECK(m.get("gpu0.load") == m.get("gpu.load"));
    } else printf("(no NVIDIA GPU / NVML: gpu checks skipped)\n");

    CHECK(m.getStr("time.hm") && m.getStr("time.hm")->size() == 5);
    // demand: with nothing requested, the expensive samplers leave their metrics alone
    {
        auto lazy = createSamplers();
        Demand d; d.all = false; d.names = {"cpu.load", "gpu.load"};
        for (auto& s : lazy) s->setDemand(d);
        Metrics q;
        for (auto& s : lazy) s->sample(q, 1.0);
        CHECK(q.has("cpu.load"));
        CHECK(!q.has("proc.count") && !q.has("net.down"));          // process list and interfaces were not read
        CHECK(!q.has("gpu.power_w") && !q.has("gpu.fan_pct"));      // no NVML power / fan queries
        CHECK(d.wantPrefix("cpu.") && !d.wantPrefix("proc.") && d.wantGpu("load") && !d.wantGpu("temp"));
        d.names.insert("gpu1.temp");
        CHECK(d.wantGpu("temp"));
        d.all = false; d.names = {"proc.cpu1.name"};
        for (auto& s : lazy) s->setDemand(d);
        Metrics q2;
        for (auto& s : lazy) s->sample(q2, 1.0);
        CHECK(q2.has("proc.count"));                                 // requested again: sampled again
    }

    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
