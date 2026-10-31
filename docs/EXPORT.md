# Metrics export: feed your own displays, scripts and panels

Quasar Monitor can publish its numbers as JSON over HTTP, so an **ESP32 with an SPI display**, a Raspberry Pi, a script or
a web page can show them. It is **off by default**.

## Turn it on

Edit `%APPDATA%\QuasarMonitor\settings.json` while the app is closed:

```json
{
  "export": { "enabled": true, "port": 8099, "lan": false }
}
```

Start the app. The log (tray → Open log) says `export: http://127.0.0.1:8099/metrics`.

| Setting | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Start the web server |
| `port` | `8099` | 1024..65535 |
| `lan` | `false` | `false`: reachable only from this PC. `true`: reachable from your local network (see the warning below) |

## Endpoints

| Request | Answer |
|---|---|
| `GET /metrics` | All metrics as one JSON object |
| `GET /metrics?keys=cpu.load,gpu.temp` | Only the listed ones (a much smaller reply: use it on microcontrollers) |
| `GET /health` | `ok` |

Numbers are plain JSON numbers (`"cpu.load": 23.4`), text is a string (`"gpu.name": "NVIDIA ..."`), and a metric that is
unavailable on this PC is `null`. Names are listed in [METRICS.md](METRICS.md). The data refreshes with the skin's
`"update"` interval.

```
$ curl "http://127.0.0.1:8099/metrics?keys=cpu.load,gpu.load,gpu.temp,ram.used_pct"
{"cpu.load":23.4,"gpu.load":57,"gpu.temp":69,"ram.used_pct":53}
```

PowerShell: `(Invoke-RestMethod http://127.0.0.1:8099/metrics).'cpu.load'`
Python: `requests.get("http://127.0.0.1:8099/metrics").json()["cpu.load"]`

## ⚠️ Read this before enabling `lan`

The server has **no password and no encryption**. With `"lan": true` anybody on your network can read your CPU, GPU,
RAM and network numbers. To limit the damage Quasar Monitor **never sends the list of running programs
(`proc.*`) in LAN mode**, but the hardware numbers are still public to the network. Use it only on a network you trust,
and only for the time you need it. The first time you run it Windows Firewall will ask whether to allow access.

Limits: one request is served at a time, a client that connects and sends nothing blocks the server for up to 2 seconds
(then it moves on). Only `GET` is accepted.

## Example: an ESP32 + SPI display showing your PC (QuasarUI)

> **Not tested on real hardware.** The HTTP side is tested; this sketch is a starting point written against the
> documented API. It needs `lan: true`, the PC's IP address, and the [ArduinoJson](https://arduinojson.org) library.
> Please report back if you try it.

```cpp
// ESP32 (Arduino core): polls Quasar Monitor every second and draws two gauges with QuasarUI.
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <QuasarUI.h>   // https://github.com/SemyonZamiakin/QuasarUI

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASS = "your-password";
const char* URL = "http://192.168.1.50:8099/metrics?keys=cpu.load,gpu.load,gpu.temp";   // your PC's address

// ... create the Bus / Panel / Canvas / Screen as in the QuasarUI Dashboard example, plus two Gauge widgets ...

void loop() {
    static uint32_t last = 0;
    if (millis() - last < 1000) return;
    last = millis();
    HTTPClient http;
    http.begin(URL);
    http.setTimeout(1500);
    if (http.GET() == 200) {
        StaticJsonDocument<256> doc;
        if (!deserializeJson(doc, http.getString())) {
            cpuGauge.setValue(doc["cpu.load"] | 0);
            gpuGauge.setValue(doc["gpu.load"] | 0);
        }
    }
    http.end();
    screen->update();   // QuasarUI sends only the rectangles that changed
}
```
