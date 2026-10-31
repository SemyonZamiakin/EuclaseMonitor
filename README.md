<div align="center">

# Quasar Monitor

**A tiny system monitor that lives in your tray and looks the way *you* want.**
CPU · GPU · RAM · network · top processes, in a skinnable overlay, for about 5 MB of memory.

![platform](https://img.shields.io/badge/Windows-10%20%7C%2011-4C8DFF)
![size](https://img.shields.io/badge/exe-~400%20KB-5FD18A)
![deps](https://img.shields.io/badge/dependencies-none-8B6CFF)
![license](https://img.shields.io/badge/license-MIT-lightgrey)

<img src="docs/img/gallery.png" width="860" alt="Three bundled skins with live data">

<sub>The three bundled skins, rendered with live data from the author's PC (i9-9900K, GTX 1080 Ti).</sub>

</div>

---

## Why

Task Manager is heavy, and the classic hardware monitors are cluttered, ugly, need plugins, or never stop nagging.
Quasar Monitor is one ~400 KB `.exe`: no installer, no .NET, no accounts, no ads, no telemetry.
It shows what matters, where you want it, in any look you can describe in a text file.

## Features

- **Live overlay** (rounded, translucent, click-through) or a draggable window. Hotkey to show/hide (`Ctrl+Alt+F10`).
- **Metrics:** CPU total and per core, clock, RAM, GPU load / temperature / clocks / VRAM / power / fan (NVIDIA), network speed, disks, busiest programs, time. [Full list](docs/METRICS.md).
- **Skins you can edit in Notepad:** backgrounds (colour, gradient, **your own picture**), fonts, colours, layout, rings, bars, graphs, per-core bars, conditions ("show only when the GPU is busy"). **Saved changes appear instantly**, a broken file never breaks the overlay.
- **[The customization guide](docs/CUSTOMIZE.md):** a step-by-step tutorial where every example is tested automatically.
- **Metrics export** (off by default) as JSON over local HTTP, so an **ESP32 / SPI display**, a script or another panel can show your PC's numbers. [Details](docs/EXPORT.md).
- Tray menu: switch skins, click-through on/off, reload, **"Customize this skin..."** (makes your own copy and opens it), start with Windows.
- Open source, MIT.

## Get started

1. Download `QuasarMonitor-<version>.zip` from [Releases](../../releases) and unzip anywhere.
2. Run `QuasarMonitor.exe`. The overlay appears in the top-right corner; the tray icon has the menu.
3. Tray → **Skins** to pick `glass`, `minimal` or `aurora`. Tray → **Customize this skin...** to make it yours.

Windows may warn about an unknown publisher: the exe is not code-signed yet. Click *More info → Run anyway*,
check the SHA-256 on the release page, or build it yourself (below).

## Make it yours

```jsonc
// my-skin/skin.json: the whole overlay is this file
{
  "name": "Mine",
  "window": { "width": 220, "height": 70, "anchor": "top-left", "margin": [20, 20] },
  "background": { "image": "wallpaper.jpg", "imageOpacity": 0.5, "color": "#0b0e1c", "radius": 16 },
  "widgets": [
    { "type": "text", "x": 14, "y": 10, "text": "CPU {cpu.load:0}%   GPU {gpu.temp:0}°C", "color": "#ffffff" },
    { "type": "bar",  "x": 14, "y": 42, "w": 192, "h": 8, "radius": 4, "value": "cpu.load", "color": "#5b8cff",
      "colors": [ { "above": 90, "color": "#ff5d6c" } ] }
  ]
}
```

Change the background by swapping one picture or one colour. Everything else (positions, fonts, which numbers, when
things appear) is just as easy: see **[docs/CUSTOMIZE.md](docs/CUSTOMIZE.md)**.

## Footprint

Measured on the author's PC (Windows 11, i9-9900K 16 threads, 4K, GTX 1080 Ti), real exe, refresh once per second,
after 45 seconds of running (`tools/measure.ps1` reproduces it):

| Skin | Working set | Private working set (Task Manager "Memory") | CPU use |
|---|---|---|---|
| `glass` | 5.2 MB | 2.2 MB | 1.2% of one core (0.08% of the whole CPU) |
| `aurora` (picture, graphs, 43 widgets) | 7.7 MB | 3.2 MB | 1.35% of one core |

The exe is 404 KB. It reserves about 18 MB of address space (committed, mostly not resident), which is what "Private bytes" shows.
While the overlay is hidden it samples every 3 s and hands its memory back to Windows.

## Honest limits (v0.1)

- **CPU temperature is not shown.** Windows has no user-mode API for it on most PCs; reading it needs a kernel driver (e.g. PawnIO). It is on the roadmap as an opt-in; the metric `cpu.temp` is `n/a` for now.
- **GPU metrics: NVIDIA only** (through the driver's NVML). AMD / Intel GPUs: not yet.
- `cpu.clock` is what Windows reports, often the base clock rather than the live boost clock.
- The overlay shows over windowed and borderless games, **not over exclusive-fullscreen** games.
- The metrics export has no password: keep it on `localhost` unless you trust your network. In LAN mode the program list is never sent.
- Tested on one machine (Windows 11, NVIDIA). Reports from other setups are very welcome.

## Build

Requires Windows and [MSYS2](https://www.msys2.org) mingw64 `g++` (`pacman -S mingw-w64-x86_64-gcc`). Nothing else.

```powershell
./build.ps1          # builds build\QuasarMonitor.exe and runs all unit tests
./package.ps1        # also makes dist\QuasarMonitor-<version>.zip
```

More: [Architecture and tests](docs/ARCHITECTURE.md).

<details>
<summary><b>Коротко по-русски</b></summary>

**Quasar Monitor** живёт в трее, занимает около 5 МБ памяти и показывает загрузку CPU/GPU, температуру видеокарты, память, сеть и самые активные программы в красивом оверлее. Вид полностью настраивается текстовым файлом `skin.json`: фон (цвет, градиент или своя картинка), шрифты, цвета, расположение, условия показа. Сохранили файл, и оверлей обновился сразу. Подробный туториал: [docs/CUSTOMIZE.md](docs/CUSTOMIZE.md) (на английском, все примеры проверяются тестами).

Запуск: распакуйте архив из Releases, запустите `QuasarMonitor.exe`, меню в трее: «Skins» (скины), «Customize this skin...» (сделать свою копию и открыть её), «Click-through» (чтобы перетащить оверлей мышью).
Ограничения: температуры процессора пока нет (нужен драйвер ядра), метрики видеокарты только для NVIDIA, поверх игр в эксклюзивном полноэкранном режиме оверлей не виден.
</details>

## License

MIT. See [LICENSE](LICENSE). Made by Quasar Soft.
