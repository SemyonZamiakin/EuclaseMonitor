// Publishes every metrics snapshot on a Corundum shared-memory ring (wire format: include/euclase/metrics_wire.h),
// so other local programs (Euclase Light, your own scripts or GUIs) get the numbers without polling HTTP and
// without a single extra system call on the hot path.
#pragma once
#include "metrics.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace qm {

// Writes the EMS1 snapshot of `m` into `out` (if non-null) and returns its size in bytes.
size_t encodeSnapshot(const Metrics& m, uint64_t seq, uint64_t unixMs, unsigned char* out);

struct RingWriter;

class RingExport {
public:
    RingExport();
    ~RingExport();
    // Creates the ring (replacing a stale one) and attaches as its writer. Returns false and logs on failure.
    bool start(const char* name, uint64_t capacity);
    void stop();
    bool running() const;
    // Publishes one snapshot and wakes a sleeping reader. Drops the snapshot if the reader is behind.
    void publish(const Metrics& m);
    uint64_t published() const { return published_; }
    uint64_t dropped() const { return dropped_; }

private:
    std::unique_ptr<RingWriter> w_;
    std::string name_;
    uint64_t seq_ = 0, published_ = 0, dropped_ = 0;
};

}  // namespace qm
