# Metrics reference

These are the names you can use in `skin.json` (`"value": "cpu.load"`, `"text": "{ram.used_gb:1} GB"`) and in the
[HTTP export](EXPORT.md). A metric that does not exist on your PC (no NVIDIA GPU, for example) is `n/a` in text and
`null` in the export; it never breaks a skin.

`<N>` is a number starting at 0 (core index, GPU index). `<K>` is a rank starting at 1.

> This table is checked by `tests/check_metrics_doc.ps1` against the real running app: every name below must exist.

## CPU

| Metric | Unit | Notes |
|---|---|---|
| `cpu.load` | % | Total load of all logical processors |
| `cpu.core<N>.load` | % | One logical processor (a 16-thread CPU has `cpu.core0.load` .. `cpu.core15.load`) |
| `cpu.cores` | count | Number of logical processors |
| `cpu.name` | text | e.g. `Intel(R) Core(TM) i9-9900K CPU @ 3.60GHz` |
| `cpu.clock` | MHz | Average clock as reported by Windows. **Approximate**: many systems report the base clock, not the live boost clock |
| `cpu.clock_max` | MHz | Highest of the logical processors, same caveat |
| `cpu.temp` | °C | **Not available** in this version (always `n/a`): reading it needs a kernel driver. See the roadmap |

## Memory and system

| Metric | Unit | Notes |
|---|---|---|
| `ram.used_pct` | % | Physical memory in use |
| `ram.used_gb` / `ram.free_gb` / `ram.total_gb` | GB | |
| `sys.uptime` | seconds | Use with the `uptime` format: `{sys.uptime:uptime}` |
| `proc.count` | count | Number of running processes |

## Network

Counted over real (hardware) adapters that are up; virtual adapters and VPN tunnels are skipped so bytes are not counted twice.

| Metric | Unit | Notes |
|---|---|---|
| `net.down` / `net.up` | bytes/s | Use the `rate` format: `{net.down:rate}` gives `2.50 MB/s`; `mbit` gives megabits |
| `net.total_down_gb` / `net.total_up_gb` | GB | Since the adapters came up (Windows counters) |

## Disks (fixed drives, refreshed every 10 s)

| Metric | Unit |
|---|---|
| `disk.<X>.used_pct` | % |
| `disk.<X>.free_gb` / `disk.<X>.total_gb` | GB |

`<X>` is the drive letter in capitals, for example `disk.C.used_pct`.

## GPU (NVIDIA, through the driver's NVML; nothing extra to install)

`gpu.*` is the first GPU; `gpu<N>.*` addresses each one (`gpu0.load`, `gpu1.load`...).

| Metric | Unit | Notes |
|---|---|---|
| `gpu.count` | count | Exists only when an NVIDIA GPU was found: handy for `"visible": "gpu.count > 0"` |
| `gpu.name` | text | |
| `gpu.load` | % | |
| `gpu.temp` | °C | |
| `gpu.clock` / `gpu.mem_clock` | MHz | Graphics / memory clock |
| `gpu.vram_used_gb` / `gpu.vram_total_gb` | GB | |
| `gpu.vram_used_pct` | % | |
| `gpu.power_w` | W | |
| `gpu.fan_pct` | % | `n/a` on passively cooled or hybrid cards that do not report it |

AMD and Intel GPUs are not covered in this version.

## Busiest programs

Processes are grouped by program name (all `chrome.exe` together). Refreshed every 2 s. Two top-8 lists:
by CPU use (`proc.cpu<K>.*`) and by memory (`proc.mem<K>.*`).

| Metric | Unit |
|---|---|
| `proc.cpu<K>.name` / `proc.mem<K>.name` | text |
| `proc.cpu<K>.cpu` / `proc.mem<K>.cpu` | % of the whole CPU |
| `proc.cpu<K>.mem` / `proc.mem<K>.mem` | MB (working set) |

## Time

| Metric | Notes |
|---|---|
| `time.hm` | text `19:47` |
| `time.hms` | text `19:47:05` |
| `time.date` | text `31.10.2026` |
| `time.hour` `time.minute` `time.second` `time.day` `time.month` `time.year` | numbers |
