// Data sources. Each sampler fills a few metrics; all are read-only OS queries (no driver, no admin).
// Metric names are documented in docs/METRICS.md.
#pragma once
#include "metrics.h"
#include <memory>
#include <vector>

namespace qm {

class Sampler {
public:
    virtual ~Sampler() {}
    virtual const char* name() const = 0;
    virtual double intervalSec() const { return 0; }   // 0 = every tick
    virtual void sample(Metrics& m, double dt) = 0;     // dt = seconds since this sampler last ran
};

struct SamplerOptions {
    bool cpu = true, memory = true, network = true, disk = true, processes = true, gpu = true, time = true;
};

std::vector<std::unique_ptr<Sampler>> createSamplers(const SamplerOptions& o = SamplerOptions());

// Exposed for tests.
struct ProcSnapshotEntry { unsigned long long pid; std::string name; unsigned long long cpu100ns; unsigned long long workingSet; };
bool readProcessList(std::vector<ProcSnapshotEntry>& out);

}  // namespace qm
