# Changelog

## 1.1.0 (2026-11-12)
- **Faster ticks.** One metrics update now takes 1.3 ms instead of 8.1 ms with the default `glass` skin (median, `tools/bench_tick.exe`), and the app uses about half as much CPU (average of two measuring rounds); numbers for every skin are in the README.
  - Sampling on demand: only the metrics the skin, the tray tooltip and the metrics ring read are queried. The process snapshot (~3 ms) and the NVIDIA power reading (~4 ms) are skipped when nothing shows them.
  - Network: the adapter list is refreshed every 10 s and only the counters of physical adapters are read on each tick (0.12 ms instead of 1 ms).
  - Rendering: the skin background (fill, picture, border) is drawn once and reused; `aurora` draws in 0.9 ms instead of 13 ms.
- **Metrics ring for local programs.** Every update is published on a [Corundum HFT](https://github.com/SemyonZamiakin/CorundumHFT) shared-memory ring (`/euclase-monitor`, on by default, `"ring": {"enabled": false}` turns it off). Format and a header-only decoder: `include/euclase/metrics_wire.h`. Euclase Light 1.1 uses it to drive RGB lighting by system load.
- `tools/bench_tick.exe` measures every stage of a tick; `tools/measure.ps1` and the end-to-end tests no longer stop a copy of the app you are running.
- Graph history is kept when the same skin is saved again (hot reload no longer clears graphs).
- Bilingual README (Russian, English), Russian customization guide, CI, security policy, contributing guide.

## 0.1.0
- First release: tray app with a skinnable GDI+ overlay (click-through or draggable), hotkey, hot reload of skins.
- Metrics: CPU (total, per core, clock), RAM, GPU via NVML (NVIDIA), network, disks, busiest programs, time.
- Skin format: JSON with comments, variables, expressions, thresholds, conditions, `group` and `repeat`.
- Bundled skins: glass, minimal, aurora. Customization guide with tested examples.
- Optional HTTP metrics export (loopback by default; no program list in LAN mode).
