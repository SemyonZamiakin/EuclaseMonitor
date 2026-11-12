#include "ring_export.h"
#include "util.h"
#include "../third_party/corundum/corundum.h"
#include "../include/euclase/metrics_wire.h"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <vector>

namespace qm {

struct RingWriter { crd_writer w; };

static unsigned char* put(unsigned char* p, const void* src, size_t n) { memcpy(p, src, n); return p + n; }

size_t encodeSnapshot(const Metrics& m, uint64_t seq, uint64_t unixMs, unsigned char* out) {
    std::vector<std::string> names = m.names();
    std::sort(names.begin(), names.end());
    size_t size = EMS_HEADER_SIZE;
    uint32_t count = 0;
    unsigned char* p = out ? out + EMS_HEADER_SIZE : nullptr;
    for (const std::string& n : names) {
        if (n.empty() || n.size() > 255) continue;
        const std::string* s = m.getStr(n);
        const size_t vlen = s ? std::min<size_t>(s->size(), 65535) : 8;
        size += 4 + n.size() + vlen;
        ++count;
        if (!p) continue;
        const uint8_t kind = s ? EMS_KIND_STRING : EMS_KIND_NUMBER, nlen = (uint8_t)n.size();
        const uint16_t vl = (uint16_t)vlen;
        p = put(p, &kind, 1); p = put(p, &nlen, 1); p = put(p, &vl, 2);
        p = put(p, n.data(), n.size());
        if (s) p = put(p, s->data(), vlen);
        else { double v = m.get(n); p = put(p, &v, 8); }
    }
    if (out) {
        memcpy(out, "EMS1", 4);
        memcpy(out + 4, &count, 4);
        memcpy(out + 8, &seq, 8);
        memcpy(out + 16, &unixMs, 8);
    }
    return size;
}

RingExport::RingExport() {}
RingExport::~RingExport() { stop(); }
bool RingExport::running() const { return w_ != nullptr; }

bool RingExport::start(const char* name, uint64_t capacity) {
    stop();
    crd_status s = crd_create(name, capacity, 0600, CRD_CREATE_REPLACE);
    std::unique_ptr<RingWriter> rw(new RingWriter);
    // EEXIST on Windows: a reader (e.g. Euclase Light) still holds the ring of a previous Monitor run.
    // Re-use it: the dead writer's slot is reclaimed and the reader keeps working without re-attaching.
    if (s == CRD_OK || s == CRD_EEXIST) s = crd_writer_open(&rw->w, name, CRD_OPEN_PREFAULT);
    if (s != CRD_OK) {
        logLine(std::string("ring export: cannot create ") + name + ": " + crd_strerror(s));
        crd_unlink(name);
        return false;
    }
    w_ = std::move(rw);
    name_ = name;
    logLine(std::string("ring export: publishing metrics on Corundum ring ") + name);
    return true;
}

void RingExport::stop() {
    if (!w_) return;
    crd_writer_close(&w_->w);
    crd_unlink(name_.c_str());
    w_.reset();
}

void RingExport::publish(const Metrics& m) {
    if (!w_) return;
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const uint64_t ms = ((((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ull) / 10000;
    const size_t size = encodeSnapshot(m, seq_, ms, nullptr);
    void* dst = nullptr;
    if (size > crd_max_payload(&w_->w) || crd_reserve(&w_->w, (uint32_t)size, &dst) != CRD_OK) { ++dropped_; ++seq_; return; }
    encodeSnapshot(m, seq_++, ms, (unsigned char*)dst);   // built straight in shared memory
    crd_commit(&w_->w, (uint32_t)size, EMS_RECORD_SNAPSHOT);
    crd_notify(&w_->w);
    ++published_;
}

}  // namespace qm
