// Data sources. Each sampler fills a few metrics; all are read-only OS queries (no driver, no admin).
// Metric names are documented in docs/METRICS.md.
#pragma once
#include "metrics.h"
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace qm {

// Which metrics somebody actually reads (the skin, the tray tooltip, an export). Samplers skip the
// OS queries nobody needs: e.g. the process list (~3 ms) or the GPU power reading (~4 ms on NVIDIA).
struct Demand {
    bool all = true;                      // sample everything (HTTP export on, tests, tools)
    std::set<std::string> names;          // exact metric names, e.g. "gpu.temp", "proc.cpu1.name"
    bool want(const std::string& n) const { return all || names.count(n) > 0; }
    bool wantPrefix(const std::string& p) const {
        if (all) return true;
        auto it = names.lower_bound(p);
        return it != names.end() && it->compare(0, p.size(), p) == 0;
    }
    // GPU fields exist as "gpu.<field>" (first GPU) and "gpu<N>.<field>"
    bool wantGpu(const std::string& field) const {
        if (all || names.count("gpu." + field)) return true;
        for (int i = 0; i < 8; ++i) if (names.count("gpu" + std::to_string(i) + "." + field)) return true;
        return false;
    }
};

class Sampler {
public:
    virtual ~Sampler() {}
    virtual const char* name() const = 0;
    virtual double intervalSec() const { return 0; }   // 0 = every tick
    virtual void sample(Metrics& m, double dt) = 0;     // dt = seconds since this sampler last ran
    virtual void setDemand(const Demand& d) { (void)d; }  // default: always sample everything this sampler has
};

struct SamplerOptions {
    bool cpu = true, memory = true, network = true, disk = true, processes = true, gpu = true, time = true;
};

std::vector<std::unique_ptr<Sampler>> createSamplers(const SamplerOptions& o = SamplerOptions());

// Exposed for tests.
struct ProcSnapshotEntry { unsigned long long pid; std::string name; unsigned long long cpu100ns; unsigned long long workingSet; };
bool readProcessList(std::vector<ProcSnapshotEntry>& out);

}  // namespace qm
