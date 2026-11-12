// Ring export tests: the EMS1 snapshot format round-trips, and a Corundum reader sees what the app publishes.
#include "../src/ring_export.h"
#include "../third_party/corundum/corundum.h"
#include "../include/euclase/metrics_wire.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_run = 0, g_fail = 0;
#define CHECK(c) do { ++g_run; if (!(c)) { ++g_fail; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
using namespace qm;

static bool decode(const void* data, size_t size, Metrics& out, ems_header& h) {
    ems_iter it{};
    if (!ems_begin(data, size, &h, &it)) return false;
    ems_entry e;
    uint32_t n = 0;
    while (ems_next(&it, &e)) {
        std::string name(e.name, e.name_len);
        if (e.kind == EMS_KIND_NUMBER) out.set(name, e.number); else out.setStr(name, std::string(e.str, e.str_len));
        ++n;
    }
    return n == h.count && it.p == it.end;
}

int main() {
    Metrics m;
    m.set("cpu.load", 42.5); m.set("gpu.temp", NAN); m.setStr("gpu.name", "NVIDIA GeForce GTX 1080 Ti");
    m.setStr("proc.cpu1.name", std::string(70000, 'x'));   // longer than a u16: truncated, not corrupted

    // format round trip
    size_t size = encodeSnapshot(m, 7, 1234567, nullptr);
    std::vector<unsigned char> buf(size);
    CHECK(encodeSnapshot(m, 7, 1234567, buf.data()) == size);
    Metrics back; ems_header h{};
    CHECK(decode(buf.data(), buf.size(), back, h));
    CHECK(h.count == 4 && h.seq == 7 && h.unix_ms == 1234567);
    CHECK(back.get("cpu.load") == 42.5 && std::isnan(back.get("gpu.temp")));
    CHECK(back.getStr("gpu.name") && *back.getStr("gpu.name") == "NVIDIA GeForce GTX 1080 Ti");
    CHECK(back.getStr("proc.cpu1.name") && back.getStr("proc.cpu1.name")->size() == 65535);

    // malformed input is rejected, never read out of bounds
    ems_iter it{}; ems_entry e{};
    CHECK(!ems_begin(buf.data(), 10, &h, &it));
    CHECK(ems_begin(buf.data(), buf.size() - 3, &h, &it));
    int good = 0; while (ems_next(&it, &e)) ++good;
    CHECK(good == 3);                                  // the cut-off last entry is refused
    std::vector<unsigned char> bad = buf; bad[0] = 'X';
    CHECK(!ems_begin(bad.data(), bad.size(), &h, &it));

    // end to end through shared memory
    const char* name = "/euclase-monitor-test";
    RingExport ring;
    CHECK(ring.start(name, 64 * 1024));
    crd_reader r;
    CHECK(crd_reader_open(&r, name, 0) == CRD_OK);
    Metrics small; small.set("cpu.load", 10); small.setStr("cpu.name", "Test CPU");
    for (int i = 0; i < 5; ++i) { small.set("cpu.load", 10 + i); ring.publish(small); }
    CHECK(crd_wait(&r, 0) == CRD_OK);
    crd_msg msg;
    int got = 0; double last = 0; uint64_t lastSeq = 99;
    while (crd_read(&r, &msg) == CRD_OK) {
        CHECK(msg.type == EMS_RECORD_SNAPSHOT);
        Metrics q; ems_header qh{};
        CHECK(decode(msg.data, msg.size, q, qh));
        last = q.get("cpu.load"); lastSeq = qh.seq; ++got;
    }
    crd_release(&r);
    CHECK(got == 5 && last == 14 && lastSeq == 4 && ring.published() == 5 && ring.dropped() == 0);

    // a reader that never reads: the writer drops snapshots instead of blocking
    Metrics big = m;
    for (int i = 0; i < 100; ++i) ring.publish(big);
    CHECK(ring.dropped() > 0 && ring.published() + ring.dropped() == 105);
    crd_reader_close(&r);
    ring.stop();
    CHECK(crd_reader_open(&r, name, 0) == CRD_ENOENT);   // stopping removes the ring

    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
