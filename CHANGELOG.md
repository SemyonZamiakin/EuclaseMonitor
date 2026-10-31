# Changelog

## 0.1.0
- First release: tray app with a skinnable GDI+ overlay (click-through or draggable), hotkey, hot reload of skins.
- Metrics: CPU (total, per core, clock), RAM, GPU via NVML (NVIDIA), network, disks, busiest programs, time.
- Skin format: JSON with comments, variables, expressions, thresholds, conditions, `group` and `repeat`.
- Bundled skins: glass, minimal, aurora. Customization guide with tested examples.
- Optional HTTP metrics export (loopback by default; no program list in LAN mode).
