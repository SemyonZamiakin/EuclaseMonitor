# Make it yours: the Quasar Monitor customization guide

Everything you see on the overlay (background, colours, fonts, layout, which numbers are shown) comes from **one text file**:
`skin.json`. Edit it with any editor, save, and the overlay updates **instantly**, no restart. If you make a mistake the
overlay keeps showing the previous version and the tray tells you what is wrong.

Every code block in this guide is tested automatically against the real skin loader, so you can copy it as it is.

---

## 1. Your first change in 60 seconds

1. Right-click the tray icon → **Customize this skin...**
   Quasar Monitor copies the skin into your own folder (`%APPDATA%\QuasarMonitor\skins\<name>-mine`), switches to the copy
   and opens `skin.json`. The original skins are never modified.
2. Find `"background"` and change the colour, for example `"color": "#1b2a49cc"`.
3. Save the file (Ctrl+S). The overlay changes within a moment.

To **drag** the overlay somewhere else: tray → uncheck **Click-through**, drag it, check it again. The position is remembered per skin.

---

## 2. What a skin looks like

A skin is a folder with a `skin.json` and, optionally, pictures next to it:

```
my-skin/
  skin.json
  background.png     (any png / jpg you like)
  logo.png
```

The smallest working skin:

```json skin
{
  "name": "Hello",
  "window": { "width": 200, "height": 50 },
  "widgets": [
    { "type": "text", "x": 12, "y": 12, "text": "CPU {cpu.load:0}%", "color": "#ffffff" }
  ]
}
```

**Rules of the file format** (it is JSON, with a few conveniences):

- Comments are allowed: `// like this` and `/* like this */`.
- A trailing comma after the last item is fine.
- Colours are `"#RRGGBB"`, `"#RRGGBBAA"` (AA = opacity, `ff` = solid, `80` = half) or `"$name"` (a variable, see section 5).
- All sizes and positions are in **units**: 1 unit = 1 pixel at 100% Windows scaling. The overlay scales itself on
  4K screens and with `window.scale`.
- The origin `(0, 0)` is the top-left corner of the overlay.

---

## 3. The window

```json skin
{
  "name": "Window demo",
  "window": {
    "width": 320,            // size of the overlay
    "height": 120,
    "anchor": "bottom-right", // top-left, top-center, top-right, center-left, center, center-right, bottom-left, bottom-center, bottom-right
    "margin": [20, 20],       // distance from the screen edge (x, y); or one number for both
    "monitor": 0,             // 0 = main monitor, 1 = the next one...
    "opacity": 0.9,           // 0.1 .. 1: fades the whole overlay
    "scale": 1.0,             // 1.5 makes everything 50% bigger
    "topmost": true,          // stay above other windows
    "clickThrough": true      // mouse clicks pass through to the game / window below
  },
  "update": 1000,             // how often the numbers refresh, in milliseconds (250 .. 10000)
  "widgets": []
}
```

Notes: the overlay shows over normal and borderless-fullscreen games. It cannot appear on top of games that use
**exclusive** fullscreen: switch the game to borderless/windowed mode.

---

## 4. Background: colour, gradient, picture

```json skin
{
  "name": "Backgrounds",
  "window": { "width": 300, "height": 100 },
  "background": {
    "gradient": { "from": "#101733ee", "to": "#2a1245ee", "angle": 60 },
    "radius": 24,
    "border": { "color": "#ffffff30", "width": 1 }
  },
  "widgets": []
}
```

- `"color": "#rrggbbaa"`: a flat colour. Use `transparent` or an `aa` of `00` for no background at all.
- `"gradient": { "from", "to", "angle" }`: replaces the flat colour (`angle` 0 = left to right, 90 = top to bottom).
- `"image": "bg.png"`: a picture from the skin folder, drawn **over** the colour. `"fit"`: `cover` (fill, crop the edges),
  `contain` (fit inside) or `stretch`. `"imageOpacity": 0.5` lets the colour show through, which is the easiest way to
  keep text readable on a busy picture.
- `"radius"`: corner roundness. `0` = sharp corners.

**To use your own wallpaper:** copy the skin (section 1), put your picture into the folder, and set `"image"` to its file name.
You can overwrite the picture while Quasar Monitor is running; it is picked up on save.

---

## 5. Colours once, everywhere: `vars`

Put your palette at the top and use `$name` everywhere. Re-theme the whole skin by editing 3 lines.

```json skin
{
  "name": "Palette",
  "vars": { "text": "#eef1fb", "accent": "#5b8cff", "danger": "#ff5d6c" },
  "window": { "width": 220, "height": 70 },
  "widgets": [
    { "type": "text", "x": 12, "y": 10, "text": "CPU {cpu.load:0}%", "color": "$text" },
    { "type": "bar", "x": 12, "y": 40, "w": 196, "h": 8, "radius": 4, "value": "cpu.load", "color": "$accent",
      "colors": [ { "above": 90, "color": "$danger" } ] }
  ]
}
```

---

## 6. Fonts

Declare fonts once under `"fonts"` and refer to them by name. Any font installed on the PC works.
If the font does not exist on another person's computer, Segoe UI is used instead.

```json skin
{
  "name": "Fonts",
  "fonts": {
    "main":  { "family": "Segoe UI", "size": 12 },
    "big":   { "family": "Consolas", "size": 30, "bold": true }
  },
  "window": { "width": 200, "height": 70 },
  "widgets": [
    { "type": "text", "x": 12, "y": 8,  "font": "big",  "text": "{cpu.load:0}%" },
    { "type": "text", "x": 12, "y": 48, "font": "main", "text": "{cpu.name}", "color": "#9aa3c0" }
  ]
}
```

`size` is the text height in units. `main` is used by every text that does not name another font.

---

## 7. Widgets: the building blocks

A skin is a list of widgets drawn in order (later ones on top). Common to all: `type`, `x`, `y`,
`opacity` (0..1) and `visible` (see section 10).

### text
Shows text with live numbers. Put values in braces: `{metric}` or `{metric:format}`.

```json widgets
[
  { "type": "text", "x": 10, "y": 10, "text": "GPU {gpu.load:0}%  {gpu.temp:0}°C  {gpu.clock:mhz}", "color": "#38d9f0" },
  { "type": "text", "x": 10, "y": 34, "w": 180, "align": "right", "text": "↓ {net.down:rate}" },
  { "type": "text", "x": 0, "y": 0, "w": 200, "h": 100, "align": "center", "valign": "middle", "text": "centred in a box" }
]
```

Without `w`/`h` the text starts at `x`/`y`. With `w` (and `h`) it lives in a box: use `align` (`left`, `center`, `right`)
and `valign` (`top`, `middle`, `bottom`); too-long text is cut with "...". To print a literal brace write `{{` or `}}`.

### bar
A progress bar from any value.

```json widgets
[
  { "type": "bar", "x": 10, "y": 10, "w": 180, "h": 10, "radius": 5, "value": "ram.used_pct", "min": 0, "max": 100,
    "color": "#9d7bff", "colorTo": "#38d9f0", "track": "#ffffff1a" },
  { "type": "bar", "x": 10, "y": 30, "w": 12, "h": 60, "vertical": true, "value": "cpu.load", "color": "#7aa2ff" }
]
```

`colorTo` makes a gradient along the bar. `min`/`max` set what "empty" and "full" mean. `track` is the empty part.

### ring
A circular gauge. `w`/`h` define the size (the smaller one is used); `thickness` the stroke.
By default it is a 270° gauge that opens at the bottom; change `startAngle` and `sweep` for other shapes
(`"startAngle": -90, "sweep": 360` is a full circle starting at the top).

```json widgets
[
  { "type": "ring", "x": 10, "y": 10, "w": 80, "h": 80, "thickness": 9, "value": "cpu.load", "color": "#5b8cff" },
  { "type": "text", "x": 10, "y": 10, "w": 80, "h": 80, "align": "center", "valign": "middle", "text": "{cpu.load:0}" },
  { "type": "ring", "x": 110, "y": 10, "w": 80, "h": 80, "thickness": 6, "startAngle": -90, "sweep": 360, "value": "ram.used_pct", "color": "#9d7bff" }
]
```

### graph
A history line of the last N updates. The value can be any metric or expression.

```json widgets
[
  { "type": "graph", "x": 10, "y": 10, "w": 200, "h": 50, "value": "gpu.load", "history": 60,
    "color": "#38d9f0", "fill": true, "lineWidth": 2, "track": "#00000040", "radius": 8 }
]
```

`history` = number of points (2..600). With `update: 1000`, `60` points = the last minute.

### rect, line, image

```json widgets
[
  { "type": "rect", "x": 10, "y": 10, "w": 100, "h": 40, "radius": 10, "color": "#ffffff14", "border": "#ffffff30", "borderWidth": 1 },
  { "type": "rect", "x": 120, "y": 10, "w": 60, "h": 40, "radius": 10, "color": "#5b8cff", "colorTo": "#38d9f0" },
  { "type": "line", "x": 10, "y": 60, "x2": 180, "y2": 60, "color": "#ffffff30", "lineWidth": 1 }
]
```

`image` draws a picture from the skin folder (`png`, `jpg`, `gif`, `bmp`): `{ "type": "image", "file": "logo.png", "x": 10, "y": 10, "w": 48, "h": 48, "fit": "contain" }`.
For safety, pictures must be **inside the skin folder**.

---

## 8. Colours that react to the numbers: `colors`

Bars, rings and graphs can change colour with the value. List the thresholds; the highest one that is reached wins.

```json widgets
[
  { "type": "ring", "x": 10, "y": 10, "w": 70, "h": 70, "thickness": 8, "value": "gpu.temp", "min": 30, "max": 100, "color": "#38d9f0",
    "colors": [ { "above": 70, "color": "#ffb454" }, { "above": 82, "color": "#ff5d6c" } ] }
]
```

Here the ring is cyan below 70 °C, orange from 70 and red from 82.

---

## 9. Repeating things: `repeat` and `group`

`group` moves several widgets together. `repeat` makes copies of a template. Inside the template, `$i` is the
copy number starting at **0** and `$i1` starts at **1**. `dx`/`dy` is the step between copies.

One thin bar per CPU core (16 threads):

```json widgets
[
  { "type": "repeat", "count": 16, "x": 10, "y": 10, "dx": 14,
    "template": { "type": "bar", "w": 10, "h": 40, "vertical": true, "radius": 4, "value": "cpu.core$i.load", "color": "#7aa2ff" } }
]
```

A table of the 4 busiest programs:

```json widgets
[
  { "type": "repeat", "count": 4, "x": 0, "y": 10, "dy": 18,
    "template": { "type": "group", "children": [
      { "type": "text", "x": 10,  "w": 150, "text": "{proc.cpu$i1.name}" },
      { "type": "text", "x": 170, "w": 60, "align": "right", "color": "#7aa2ff", "text": "{proc.cpu$i1.cpu:1}%" }
    ] } }
]
```

---

## 10. Showing things only when they matter: `visible`

`visible` is an expression. The widget is drawn only when it is true (not 0, and not "n/a").
Metrics that do not exist on a PC (no NVIDIA GPU, for instance) are "n/a", so this hides the block automatically.

```json widgets
[
  { "type": "text", "x": 10, "y": 10, "text": "GPU {gpu.load:0}%", "visible": "gpu.count > 0" },
  { "type": "text", "x": 10, "y": 34, "text": "GPU is hot!", "color": "#ff5d6c", "visible": "gpu.temp >= 80" },
  { "type": "group", "x": 10, "y": 60, "visible": "net.down > 1000000", "children": [
      { "type": "text", "text": "Downloading: {net.down:rate}", "color": "#38d9f0" }
  ] }
]
```

---

## 11. Expressions and formats

Anywhere a value is expected (`value`, `visible`, and inside `{...}` in text) you can write an expression:

| You write | Meaning |
|---|---|
| `cpu.load` | a metric (full list: [METRICS.md](METRICS.md)) |
| `ram.used_gb / ram.total_gb * 100` | arithmetic: `+ - * / %` and brackets |
| `max(cpu.load, gpu.load)` | `min`, `max`, `clamp(x, lo, hi)`, `abs`, `round`, `floor`, `ceil` |
| `if(gpu.temp > 75, 1, 0)` | choose by a condition |
| `cpu.load > 80 && gpu.load > 80` | comparisons `< <= > >= == !=` and `&& || !` |

In text, add a **format** after a colon: `{value:format}`

| Format | Example result | Use for |
|---|---|---|
| `0` `1` `2` `3` `4` | `42`, `42.4`, `42.37` | number of decimals |
| `rate` | `2.50 MB/s` | bytes per second (network) |
| `bytes` | `34.0 GB` | sizes in bytes |
| `mhz` | `4.70 GHz` | clock speeds in MHz |
| `uptime` | `1d 02:15` | seconds as days / hours / minutes |
| `mbit` | `18.4 Mbit/s` | bytes per second as megabits |
| (none) | `7.5` / `42` | sensible automatic choice |

A missing or unavailable value prints `n/a`, never a crash.

---

## 12. Recipes

**A slim bar on top of the screen**

```json skin
{
  "name": "Slim bar",
  "window": { "width": 700, "height": 26, "anchor": "top-center", "margin": [0, 0] },
  "background": { "color": "#000000b0", "radius": 0, "border": { "width": 0 } },
  "fonts": { "main": { "family": "Segoe UI", "size": 12 } },
  "widgets": [
    { "type": "text", "x": 0, "y": 0, "w": 700, "h": 26, "align": "center", "valign": "middle", "color": "#dfe6ff",
      "text": "CPU {cpu.load:0}%   GPU {gpu.load:0}% {gpu.temp:0}°C   RAM {ram.used_pct:0}%   ↓{net.down:rate}  ↑{net.up:rate}   {time.hm}" }
  ]
}
```

**A vertical sidebar** (tall and narrow, on the left edge)

```json skin
{
  "name": "Sidebar",
  "window": { "width": 90, "height": 340, "anchor": "center-left", "margin": [10, 0] },
  "background": { "color": "#0b0e1cc8", "radius": 18 },
  "widgets": [
    { "type": "ring", "x": 10, "y": 12,  "w": 70, "h": 70, "thickness": 8, "value": "cpu.load", "color": "#7aa2ff" },
    { "type": "text", "x": 10, "y": 12,  "w": 70, "h": 70, "align": "center", "valign": "middle", "text": "{cpu.load:0}" },
    { "type": "ring", "x": 10, "y": 98,  "w": 70, "h": 70, "thickness": 8, "value": "gpu.load", "color": "#38d9f0" },
    { "type": "text", "x": 10, "y": 98,  "w": 70, "h": 70, "align": "center", "valign": "middle", "text": "{gpu.load:0}" },
    { "type": "ring", "x": 10, "y": 184, "w": 70, "h": 70, "thickness": 8, "value": "ram.used_pct", "color": "#9d7bff" },
    { "type": "text", "x": 10, "y": 184, "w": 70, "h": 70, "align": "center", "valign": "middle", "text": "{ram.used_pct:0}" },
    { "type": "text", "x": 0, "y": 280, "w": 90, "align": "center", "color": "#9aa3c0", "text": "{time.hm}" }
  ]
}
```

**Show the full dashboard only while the GPU is busy ("game mode")**: wrap everything in a `group` with `"visible": "gpu.load > 30"`.

**Fully transparent text-only overlay**: `"background": { "color": "#00000000", "border": { "width": 0 } }`.

---

## 13. Settings file and hotkey

`%APPDATA%\QuasarMonitor\settings.json` (created automatically; edit it while the app is closed):

```json
{
  "skin": "glass",
  "visible": true,
  "hotkey": "ctrl+alt+f10",
  "export": { "enabled": false, "port": 8099, "lan": false }
}
```

- `hotkey`: show/hide the overlay. Format `ctrl+alt+f10`, `ctrl+shift+m`... (needs at least one of ctrl/alt/shift/win).
  If another program already uses it, the tray tells you.
- `export`: a tiny local web page with the numbers for ESP32 / SPI displays and scripts, see [EXPORT.md](EXPORT.md).

---

## 14. When something does not work

- **The overlay did not change after saving.** Open the tray menu → **Open log**. The last lines say exactly what is wrong,
  with the line and column for syntax errors, for example `skin.json: line 12, col 5: expected ',' or '}'`.
- **A number shows `n/a`.** That metric does not exist on this PC (see [METRICS.md](METRICS.md)), or its name has a typo.
- **My picture does not appear.** The file must be inside the skin folder; check the name and the `png` / `jpg` extension in the log.
- **I broke everything.** Copy the skin again from the bundled folder (`skins\...` next to the exe), or delete your
  `-mine` folder in `%APPDATA%\QuasarMonitor\skins` and choose the original skin in the tray menu.

## 15. Share your skin

A skin is just a folder: zip it and send it, or open a pull request adding it to `skins/` in the repository.
Please keep pictures you own or that are free to use.

Unknown settings are ignored, so a skin made for a newer version still loads in an older one (the parts it cannot
understand are skipped and listed in the log).
