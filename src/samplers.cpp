#include "samplers.h"
#include "util.h"
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <powrprof.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace qm {

namespace {

typedef LONG(NTAPI* NtQsiFn)(ULONG, PVOID, ULONG, PULONG);
NtQsiFn ntQsi() {
    static NtQsiFn fn = (NtQsiFn)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    return fn;
}

// Not in the MinGW headers (documented in the Windows SDK, powrprof.h).
struct PROCESSOR_POWER_INFORMATION { ULONG Number, MaxMhz, CurrentMhz, MhzLimit, MaxIdleState, CurrentIdleState; };

// ---------------------------------------------------------------- CPU
struct CpuTimes { long long idle, kernel, user; };
struct SysProcPerf { LARGE_INTEGER Idle, Kernel, User, Dpc, Interrupt; ULONG InterruptCount; };

class CpuSampler : public Sampler {
public:
    CpuSampler() {
        wchar_t name[128] = {};
        DWORD sz = sizeof name;
        HKEY k;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &k) == ERROR_SUCCESS) {
            RegQueryValueExW(k, L"ProcessorNameString", nullptr, nullptr, (LPBYTE)name, &sz);
            RegCloseKey(k);
        }
        std::string n = narrow(name);
        size_t a = n.find_first_not_of(' ');
        name_ = a == std::string::npos ? "CPU" : n.substr(a);
    }
    const char* name() const override { return "cpu"; }
    void sample(Metrics& m, double) override {
        static const ULONG kSysProcPerf = 8;
        std::vector<SysProcPerf> cur(256);
        ULONG ret = 0;
        if (!ntQsi() || ntQsi()(kSysProcPerf, cur.data(), (ULONG)(cur.size() * sizeof(SysProcPerf)), &ret) != 0) return;
        size_t n = ret / sizeof(SysProcPerf);
        cur.resize(n);
        double total = 0;
        int counted = 0;
        for (size_t i = 0; i < n; ++i) {
            CpuTimes t{cur[i].Idle.QuadPart, cur[i].Kernel.QuadPart, cur[i].User.QuadPart};  // kernel time includes idle
            double load = NAN;
            if (i < prev_.size()) {
                long long dIdle = t.idle - prev_[i].idle, dBusy = (t.kernel - prev_[i].kernel) + (t.user - prev_[i].user);
                if (dBusy > 0) load = std::min(100.0, std::max(0.0, 100.0 * (1.0 - (double)dIdle / (double)dBusy)));
            }
            if (!std::isnan(load)) { total += load; ++counted; }
            char key[40];
            snprintf(key, sizeof key, "cpu.core%zu.load", i);
            m.set(key, load);
            if (i < prev_.size()) prev_[i] = t; else prev_.push_back(t);
        }
        m.set("cpu.load", counted ? total / counted : NAN);
        m.set("cpu.cores", (double)n);
        m.setStr("cpu.name", name_);

        std::vector<PROCESSOR_POWER_INFORMATION> ppi(n ? n : 1);
        if (n && CallNtPowerInformation(ProcessorInformation, nullptr, 0, ppi.data(), (ULONG)(ppi.size() * sizeof(ppi[0]))) == 0) {
            double sum = 0, mx = 0;
            for (auto& p : ppi) { sum += p.CurrentMhz; mx = std::max(mx, (double)p.CurrentMhz); }
            m.set("cpu.clock", sum / ppi.size());
            m.set("cpu.clock_max", mx);
        }
        m.set("cpu.temp", NAN);  // needs a kernel driver (MSR access); see docs/METRICS.md
    }

private:
    std::vector<CpuTimes> prev_;
    std::string name_;
};

// ---------------------------------------------------------------- memory + uptime
class MemSampler : public Sampler {
public:
    const char* name() const override { return "memory"; }
    void sample(Metrics& m, double) override {
        MEMORYSTATUSEX s{};
        s.dwLength = sizeof s;
        if (!GlobalMemoryStatusEx(&s)) return;
        const double G = 1024.0 * 1024 * 1024;
        m.set("ram.used_pct", (double)s.dwMemoryLoad);
        m.set("ram.total_gb", s.ullTotalPhys / G);
        m.set("ram.used_gb", (s.ullTotalPhys - s.ullAvailPhys) / G);
        m.set("ram.free_gb", s.ullAvailPhys / G);
        m.set("sys.uptime", GetTickCount64() / 1000.0);
    }
};

// ---------------------------------------------------------------- network
class NetSampler : public Sampler {
public:
    const char* name() const override { return "network"; }
    void sample(Metrics& m, double dt) override {
        MIB_IF_TABLE2* t = nullptr;
        if (GetIfTable2(&t) != NO_ERROR || !t) return;
        unsigned long long in = 0, out = 0;
        for (ULONG i = 0; i < t->NumEntries; ++i) {
            const MIB_IF_ROW2& r = t->Table[i];
            if (r.OperStatus != IfOperStatusUp || r.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (!r.InterfaceAndOperStatusFlags.HardwareInterface) continue;  // skip VPN/virtual adapters (their bytes are already counted on the real NIC)
            in += r.InOctets; out += r.OutOctets;
        }
        FreeMibTable(t);
        if (have_ && dt > 0 && in >= pin_ && out >= pout_) {
            m.set("net.down", (double)(in - pin_) / dt);
            m.set("net.up", (double)(out - pout_) / dt);
        } else {
            m.set("net.down", 0); m.set("net.up", 0);
        }
        m.set("net.total_down_gb", in / 1073741824.0);
        m.set("net.total_up_gb", out / 1073741824.0);
        pin_ = in; pout_ = out; have_ = true;
    }
private:
    unsigned long long pin_ = 0, pout_ = 0;
    bool have_ = false;
};

// ---------------------------------------------------------------- disks (free space only, cheap)
class DiskSampler : public Sampler {
public:
    const char* name() const override { return "disk"; }
    double intervalSec() const override { return 10; }
    void sample(Metrics& m, double) override {
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) {
            if (!(mask & (1u << i))) continue;
            wchar_t root[4] = {(wchar_t)(L'A' + i), L':', L'\\', 0};
            if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
            ULARGE_INTEGER freeB, total, totalFree;
            if (!GetDiskFreeSpaceExW(root, &freeB, &total, &totalFree) || total.QuadPart == 0) continue;
            std::string k = std::string("disk.") + (char)('A' + i);
            m.set(k + ".total_gb", total.QuadPart / 1073741824.0);
            m.set(k + ".free_gb", freeB.QuadPart / 1073741824.0);
            m.set(k + ".used_pct", 100.0 * (1.0 - (double)freeB.QuadPart / (double)total.QuadPart));
        }
    }
};

// ---------------------------------------------------------------- time
class TimeSampler : public Sampler {
public:
    const char* name() const override { return "time"; }
    void sample(Metrics& m, double) override {
        SYSTEMTIME st;
        GetLocalTime(&st);
        m.set("time.hour", st.wHour); m.set("time.minute", st.wMinute); m.set("time.second", st.wSecond);
        m.set("time.day", st.wDay); m.set("time.month", st.wMonth); m.set("time.year", st.wYear);
        char b[32];
        snprintf(b, sizeof b, "%02d:%02d", st.wHour, st.wMinute); m.setStr("time.hm", b);
        snprintf(b, sizeof b, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond); m.setStr("time.hms", b);
        snprintf(b, sizeof b, "%02d.%02d.%04d", st.wDay, st.wMonth, st.wYear); m.setStr("time.date", b);
    }
};

// ---------------------------------------------------------------- GPU (NVIDIA via NVML, loaded dynamically)
class NvmlSampler : public Sampler {
    typedef int (*Init)();
    typedef int (*Shutdown)();
    typedef int (*GetCount)(unsigned*);
    typedef int (*GetHandle)(unsigned, void**);
    typedef int (*GetName)(void*, char*, unsigned);
    struct Util { unsigned gpu, memory; };
    struct Mem { unsigned long long total, free, used; };
    typedef int (*GetUtil)(void*, Util*);
    typedef int (*GetTemp)(void*, int, unsigned*);
    typedef int (*GetClock)(void*, int, unsigned*);
    typedef int (*GetMem)(void*, Mem*);
    typedef int (*GetPower)(void*, unsigned*);
    typedef int (*GetFan)(void*, unsigned*);

    HMODULE dll_ = nullptr;
    GetUtil util_ = nullptr; GetTemp temp_ = nullptr; GetClock clock_ = nullptr; GetMem mem_ = nullptr; GetPower power_ = nullptr; GetFan fan_ = nullptr;
    Shutdown shutdown_ = nullptr;
    std::vector<void*> dev_;
    std::vector<std::string> names_;

public:
    NvmlSampler() {
        dll_ = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll_) return;
        auto P = [&](const char* n) -> void* { return (void*)GetProcAddress(dll_, n); };
        Init init = (Init)P("nvmlInit_v2");
        GetCount count = (GetCount)P("nvmlDeviceGetCount_v2");
        GetHandle handle = (GetHandle)P("nvmlDeviceGetHandleByIndex_v2");
        GetName name = (GetName)P("nvmlDeviceGetName");
        shutdown_ = (Shutdown)P("nvmlShutdown");
        util_ = (GetUtil)P("nvmlDeviceGetUtilizationRates"); temp_ = (GetTemp)P("nvmlDeviceGetTemperature");
        clock_ = (GetClock)P("nvmlDeviceGetClockInfo"); mem_ = (GetMem)P("nvmlDeviceGetMemoryInfo");
        power_ = (GetPower)P("nvmlDeviceGetPowerUsage"); fan_ = (GetFan)P("nvmlDeviceGetFanSpeed");
        if (!init || !count || !handle || !util_ || init() != 0) { unload(); return; }
        unsigned n = 0;
        if (count(&n) != 0) { unload(); return; }
        for (unsigned i = 0; i < n && i < 8; ++i) {
            void* h = nullptr;
            if (handle(i, &h) != 0) continue;
            char nm[96] = "NVIDIA GPU";
            if (name) name(h, nm, sizeof nm);
            dev_.push_back(h); names_.push_back(nm);
        }
        if (dev_.empty()) unload();
    }
    ~NvmlSampler() override { unload(); }
    bool available() const { return !dev_.empty(); }
    const char* name() const override { return "gpu-nvml"; }

    void sample(Metrics& m, double) override {
        for (size_t i = 0; i < dev_.size(); ++i) {
            std::string p = "gpu" + std::to_string(i) + ".";
            auto put = [&](const std::string& k, double v) { m.set(p + k, v); if (i == 0) m.set("gpu." + k, v); };
            m.setStr(p + "name", names_[i]);
            if (i == 0) m.setStr("gpu.name", names_[i]);
            Util u{};
            put("load", util_(dev_[i], &u) == 0 ? (double)u.gpu : NAN);
            unsigned v = 0;
            put("temp", temp_ && temp_(dev_[i], 0, &v) == 0 ? (double)v : NAN);
            put("clock", clock_ && clock_(dev_[i], 0, &v) == 0 ? (double)v : NAN);       // graphics MHz
            put("mem_clock", clock_ && clock_(dev_[i], 2, &v) == 0 ? (double)v : NAN);
            Mem mi{};
            bool okm = mem_ && mem_(dev_[i], &mi) == 0;
            put("vram_used_gb", okm ? mi.used / 1073741824.0 : NAN);
            put("vram_total_gb", okm ? mi.total / 1073741824.0 : NAN);
            put("vram_used_pct", okm && mi.total ? 100.0 * (double)mi.used / (double)mi.total : NAN);
            put("power_w", power_ && power_(dev_[i], &v) == 0 ? v / 1000.0 : NAN);
            put("fan_pct", fan_ && fan_(dev_[i], &v) == 0 ? (double)v : NAN);
        }
        m.set("gpu.count", (double)dev_.size());
    }

private:
    void unload() {
        if (shutdown_ && !dev_.empty()) shutdown_();
        dev_.clear();
        if (dll_) { FreeLibrary(dll_); dll_ = nullptr; }
    }
};

// ---------------------------------------------------------------- processes
struct UniStr { USHORT Length, MaximumLength; PWSTR Buffer; };   // UNICODE_STRING
struct SysProcInfo {
    ULONG NextEntryOffset, NumberOfThreads;
    LARGE_INTEGER WorkingSetPrivateSize;
    ULONG HardFaultCount, NumberOfThreadsHighWatermark;
    ULONGLONG CycleTime;
    LARGE_INTEGER CreateTime, UserTime, KernelTime;
    UniStr ImageName;
    LONG BasePriority;
    HANDLE UniqueProcessId, InheritedFromUniqueProcessId;
    ULONG HandleCount, SessionId;
    ULONG_PTR UniqueProcessKey;
    SIZE_T PeakVirtualSize, VirtualSize;
    ULONG PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize;
};

class ProcSampler : public Sampler {
public:
    const char* name() const override { return "processes"; }
    double intervalSec() const override { return 2; }
    void sample(Metrics& m, double dt) override {
        std::vector<ProcSnapshotEntry> list;
        if (!readProcessList(list)) return;
        SYSTEM_INFO si; GetSystemInfo(&si);
        double ncpu = std::max<DWORD>(1, si.dwNumberOfProcessors);
        struct Agg { double cpu = 0; double memMb = 0; };
        std::map<std::string, Agg> byName;
        std::unordered_map<unsigned long long, unsigned long long> now;
        for (auto& p : list) {
            if (p.pid == 0) continue;  // System Idle Process
            now[p.pid] = p.cpu100ns;
            Agg& a = byName[p.name];
            a.memMb += p.workingSet / 1048576.0;
            auto it = prev_.find(p.pid);
            if (it != prev_.end() && dt > 0 && p.cpu100ns >= it->second)
                a.cpu += 100.0 * ((double)(p.cpu100ns - it->second) / 1e7) / dt / ncpu;
        }
        prev_.swap(now);
        std::vector<std::pair<std::string, Agg>> v(byName.begin(), byName.end());
        m.set("proc.count", (double)list.size());
        auto fill = [&](const char* prefix, bool byCpu) {
            std::sort(v.begin(), v.end(), [&](auto& a, auto& b) { return byCpu ? a.second.cpu > b.second.cpu : a.second.memMb > b.second.memMb; });
            for (int i = 0; i < kTop; ++i) {
                std::string k = std::string("proc.") + prefix + std::to_string(i + 1);
                if (i < (int)v.size()) {
                    m.setStr(k + ".name", v[i].first);
                    m.set(k + ".cpu", v[i].second.cpu);
                    m.set(k + ".mem", v[i].second.memMb);   // MB
                } else { m.setStr(k + ".name", "-"); m.set(k + ".cpu", NAN); m.set(k + ".mem", NAN); }
            }
        };
        fill("cpu", true);
        fill("mem", false);
    }
private:
    static const int kTop = 8;
    std::unordered_map<unsigned long long, unsigned long long> prev_;
};

}  // namespace

bool readProcessList(std::vector<ProcSnapshotEntry>& out) {
    if (!ntQsi()) return false;
    static std::vector<char> buf(512 * 1024);  // reused between calls: no per-tick allocation churn
    ULONG need = 0;
    LONG st;
    for (int attempt = 0; attempt < 4; ++attempt) {
        st = ntQsi()(5 /*SystemProcessInformation*/, buf.data(), (ULONG)buf.size(), &need);
        if (st == (LONG)0xC0000004L /*STATUS_INFO_LENGTH_MISMATCH*/) { buf.resize(std::max<size_t>(need + 64 * 1024, buf.size() * 2)); continue; }
        break;
    }
    if (st != 0) return false;
    out.clear();
    char* p = buf.data();
    for (;;) {
        SysProcInfo* s = (SysProcInfo*)p;
        ProcSnapshotEntry e;
        e.pid = (unsigned long long)(ULONG_PTR)s->UniqueProcessId;
        e.name = s->ImageName.Buffer ? narrow(std::wstring(s->ImageName.Buffer, s->ImageName.Length / 2)) : (e.pid == 0 ? "Idle" : "System");
        e.cpu100ns = (unsigned long long)(s->UserTime.QuadPart + s->KernelTime.QuadPart);
        e.workingSet = s->WorkingSetSize;
        out.push_back(std::move(e));
        if (!s->NextEntryOffset) break;
        p += s->NextEntryOffset;
        if (p >= buf.data() + buf.size()) break;
    }
    return true;
}

std::vector<std::unique_ptr<Sampler>> createSamplers(const SamplerOptions& o) {
    std::vector<std::unique_ptr<Sampler>> v;
    if (o.cpu) v.emplace_back(new CpuSampler);
    if (o.memory) v.emplace_back(new MemSampler);
    if (o.network) v.emplace_back(new NetSampler);
    if (o.disk) v.emplace_back(new DiskSampler);
    if (o.time) v.emplace_back(new TimeSampler);
    if (o.processes) v.emplace_back(new ProcSampler);
    if (o.gpu) {
        std::unique_ptr<NvmlSampler> g(new NvmlSampler);
        if (g->available()) v.push_back(std::move(g));
        else logLine("NVML not available: GPU metrics disabled");
    }
    return v;
}

}  // namespace qm
