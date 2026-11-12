# Metrics export: feed your own displays, scripts and panels

Euclase Monitor can publish its numbers as JSON over HTTP, so an **ESP32 with an SPI display**, a Raspberry Pi, a script or
a web page can show them. It is **off by default**.

## Turn it on

Edit `%APPDATA%\EuclaseMonitor\settings.json` while the app is closed:

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
RAM and network numbers. To limit the damage Euclase Monitor **never sends the list of running programs
(`proc.*`) in LAN mode**, but the hardware numbers are still public to the network. Use it only on a network you trust,
and only for the time you need it. The first time you run it Windows Firewall will ask whether to allow access.

Limits: one request is served at a time, a client that connects and sends nothing blocks the server for up to 2 seconds
(then it moves on). Only `GET` is accepted.

## Example: an ESP32 + SPI display showing your PC (EuclaseUI)

> **Not tested on real hardware.** The HTTP side is tested; this sketch is a starting point written against the
> documented API. It needs `lan: true`, the PC's IP address, and the [ArduinoJson](https://arduinojson.org) library.
> Please report back if you try it.

```cpp
// ESP32 (Arduino core): polls Euclase Monitor every second and draws two gauges with EuclaseUI.
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <EuclaseUI.h>   // https://github.com/SemyonZamiakin/EuclaseUI

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASS = "your-password";
const char* URL = "http://192.168.1.50:8099/metrics?keys=cpu.load,gpu.load,gpu.temp";   // your PC's address

// ... create the Bus / Panel / Canvas / Screen as in the EuclaseUI Dashboard example, plus two Gauge widgets ...

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
    screen->update();   // EuclaseUI sends only the rectangles that changed
}
```

## Shared-memory ring for local programs (new in 1.1)

For programs on the **same PC** there is a faster path than HTTP: every metrics update is published on a
[Corundum HFT](https://github.com/SemyonZamiakin/CorundumHFT) ring in shared memory. A reader gets each snapshot without
a socket, without parsing JSON and without a single system call per update. [Euclase Light](https://github.com/SemyonZamiakin/EuclaseLight)
1.1 uses it for its "colour follows system load" mode.

It is **on by default** and visible only to programs of your own Windows session. To turn it off:

```json
{
  "ring": { "enabled": false }
}
```

| Item | Value |
|---|---|
| Ring name | `/euclase-monitor` (Windows object `Local\corundum.euclase-monitor`), 256 KiB |
| Record | one per update, Corundum record type `1`, wire format EMS1 |
| Format | [`include/euclase/metrics_wire.h`](../include/euclase/metrics_wire.h): header-only C/C++ decoder, bounds-checked |
| If nobody reads | the ring fills up and new snapshots are dropped; Monitor never waits for a reader |
| Which metrics | whatever Monitor samples: the metrics your skin uses, plus a core set that is always included (`cpu.load`, `cpu.clock`, `ram.used_pct`, `ram.used_gb`, `gpu.load`, `gpu.temp`, `gpu.vram_used_pct`, `net.down`, `net.up`). With the HTTP export on, every metric is sampled |

Reading it from C or C++ (link Corundum: [third_party/corundum](../third_party/corundum)):

```c
#include "corundum.h"
#include "euclase/metrics_wire.h"

crd_reader r;
if (crd_reader_open(&r, EMS_RING_NAME, 0) == CRD_OK) {
    while (crd_wait(&r, 2000000000) == CRD_OK) {        /* sleeps until Monitor publishes */
        crd_msg m;
        while (crd_read(&r, &m) == CRD_OK) {
            ems_header h; ems_iter it; ems_entry e;
            if (!ems_begin(m.data, m.size, &h, &it)) continue;
            while (ems_next(&it, &e))
                if (e.kind == EMS_KIND_NUMBER && ems_is(&e, "cpu.load")) printf("CPU %.1f%%\n", e.number);
        }
        crd_release(&r);
    }
    crd_reader_close(&r);
}
```

Only one reader can be attached at a time (Corundum is single-producer / single-consumer). If you need several readers,
use the HTTP export for the others.
