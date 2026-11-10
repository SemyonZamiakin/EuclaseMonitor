# Architecture

One small native Windows program (C++17, Win32 + GDI+, no runtime to install). About 2,400 lines of source.

```
 samplers (OS queries)  ──►  Metrics (name → number/text)  ──►  skin expressions  ──►  GDI+ renderer ──► layered window
 CPU  NtQuerySystemInformation        ▲                                                   (premultiplied ARGB, per-pixel alpha)
 RAM  GlobalMemoryStatusEx            │
 NET  GetIfTable2                     └──►  HTTP export (loopback by default)
 GPU  NVML (dynamic, optional)
 PROC NtQuerySystemInformation
 DISK / TIME
```

| File | Role |
|---|---|
| `src/samplers.cpp` | Data sources. Each fills a few metrics. Read-only OS calls: no driver, no admin. NVML is loaded at run time, so the exe starts on PCs without NVIDIA |
| `src/metrics.h` | The registry: name → number or string. A missing value is NaN |
| `src/expr.cpp` | Expression parser/evaluator and `{metric:format}` text templates. Parsed once when the skin loads |
| `src/json.cpp` | Lenient JSON (comments, trailing commas) with line/column errors |
| `src/skin.cpp` | Loads `skin.json` into widgets. Never throws: problems become warnings, an unusable file is rejected and the old skin stays |
| `src/render.cpp` | Draws widgets with GDI+ onto a DIB. Images are loaded into memory copies so files are never locked |
| `src/main.cpp` | Tray, overlay window, hotkey, settings, folder watcher (hot reload), HTTP export |

## Why it is small

- No framework: plain Win32 messages; GDI+ ships with Windows.
- The overlay redraws only on a metrics tick (once per second by default), never per frame.
- While hidden the app samples every 3 s, skips rendering and returns its memory pages to Windows.
- Graph history is a bounded array per widget; the process list buffer is reused between samples.

Measured numbers are in the [README](../README.md#footprint).

## Safety decisions

- Pictures in a skin must be inside the skin folder, so a downloaded skin cannot read other files.
- Skins can contain no code, only data and expressions with a fixed set of functions. Limits: 500 widgets, 128 repeats, 64 levels of JSON nesting, 40 levels of expression nesting.
- The metrics export is off by default, binds to `127.0.0.1`, serves only `GET /metrics` and `/health`, and never exposes the program list over LAN.
- The only registry write is the optional "Start with Windows" entry under `HKCU\...\Run`.

## Tests

| Test | What it proves |
|---|---|
| `tests/test_core.cpp` | JSON parser (errors, bombs), expressions, formats, templates, export filter |
| `tests/test_samplers.cpp` | Real samplers on this PC give plausible numbers; the process struct layout matches the OS |
| `tests/test_skin.cpp` | Loader behaviour on broken skins; the three bundled skins render without warnings |
| `tests/test_docs.cpp` | Every example in `docs/CUSTOMIZE.md` loads without warnings |
| `tests/e2e_hotreload.ps1` | The real exe: hot reload, rejected broken skin, recovery, debounce |
| `tests/e2e_export.ps1` | The real exe: HTTP endpoints, error codes, garbage clients, loopback-only |
| `tests/check_metrics_doc.ps1` | Every name in `docs/METRICS.md` exists in the live app |

## Roadmap

- CPU temperature (needs a signed kernel driver such as PawnIO to read the sensors; opt-in)
- AMD / Intel GPU metrics
- A skin gallery and an in-app skin picker with previews
- An example ESP32 firmware verified on real hardware with EuclaseUI
