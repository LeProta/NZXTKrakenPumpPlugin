# NZXT Kraken Pump — OpenRGB Plugin

_OpenRGB plugin for Pump & fan curve control for NZXT Kraken coolers and motherboard fans._

![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20(x64)-0078D6)
![Qt](https://img.shields.io/badge/Qt-5.15-41CD52)
![OpenRGB](https://img.shields.io/badge/OpenRGB-plugin-CC1010)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![License](https://img.shields.io/badge/license-MIT-3DA639)

Control the pump speed of **NZXT Kraken** AIO coolers, the Kraken fan channel and **every motherboard fan header** straight from [OpenRGB](https://openrgb.org) — with per-channel temperature curves and **without NZXT CAM**.

> **Windows-only.** The Kraken channel uses a raw HID transport and the motherboard sensor/fan backend relies on a .NET assembly plus a ring-0 driver. There is no Linux/macOS build for now.

---

## Features

### Channels

| Channel | Description |
|---------|-------------|
| **Kraken pump** | Speed curve over HID, driven by any temperature source. 20 % safety floor. |
| **Kraken fan** | The fans plugged into the pump head (Z-series / 2023 / Elite), over HID. |
| **Motherboard fans** | Every controllable header exposed by the Super I/O chip (via LibreHardwareMonitor). Empty headers are hidden automatically and re-appear when a fan is plugged in. |

### Modes (per channel)

| Mode | Description |
|------|-------------|
| **Silent** | Quiet preset curve (default). |
| **Performance** | Aggressive preset curve. |
| **Fixed** | Constant duty — two linked points, moving one moves the other. |
| **Custom** | Free-form curve: left-click to add a point, right-click to remove, drag on both axes with %/° guides and a live temperature cursor. Editing a preset switches to Custom automatically. |

### Available temperature sources

Liquid (coolant) · GPU Average · GPU Hot Spot · CPU Average · CPU Hot Spot · RAM · Motherboard

### General

- Per-channel **Step up / Step down** rate limits (%/s), **Reset**, and **Apply** to copy a curve to other fans.
- **Identify**: click a fan's icon — the fan spins at 100 % for 5 s. Single-click a name to rename it.
- Smoothed 1 Hz control loop: anti-oscillation (EMA + deadband), Zero-RPM hysteresis with spin-up kick, dead sensors never drive a fan, Elite V2 0 % self-reset handled.
- Motherboard channels are returned to **BIOS control** when the plugin unloads.
- °C / °F follows the OpenRGB locale setting.
- Settings stored in `%APPDATA%\OpenRGB\NZXTKrakenPump`.

> **GPU fans are intentionally not exposed.** On recent AMD Radeon cards (RDNA3/RDNA4) the driver API used by LibreHardwareMonitor rejects fan writes (`ADL_ERR_NOT_SUPPORTED`), so the channel would silently do nothing.

---

## Supported devices

| Model | PID | Pump | Fan channel |
|-------|-----|------|-------------|
| NZXT Kraken X53/X63/X73 | `0x2007` | ✔ | — |
| NZXT Kraken X53/X63/X73 RGB | `0x2014` | ✔ | — |
| NZXT Kraken Z53 | `0x3009` | ✔ | ✔ |
| NZXT Kraken Z63 | `0x300A` | ✔ | ✔ |
| NZXT Kraken Z73 | `0x3008` | ✔ | ✔ |
| NZXT Kraken 2023 | `0x300E` | ✔ | ✔ |
| NZXT Kraken Elite 360 | `0x300C` | ✔ | ✔ |
| NZXT Kraken Elite V2 (2024) | `0x3012` | ✔ | ✔ |
| NZXT Kraken Elite V2 (alt) | `0x3013` | ✔ | ✔ |

All devices share VID `0x1E71`. The plugin speaks both `0x72` protocol variants (X3/Z3 and Elite/2023/Elite V2). Primary development target: **Kraken Elite V2 (`0x3012`)**.

Motherboard fans: any Super I/O chip supported by [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) (developed and tested on a Nuvoton NCT6799D — NZXT N7 B650E).

---

## Installation

### 1. Get the plugin

Download the prebuilt **`NZXTKrakenPumpPlugin.dll`** from the [Releases](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases) page, or [build it yourself](#building-from-source).

### 2. Place `lhwm-wrapper.dll` next to `OpenRGB.exe`

Motherboard fans and CPU/GPU temperature sources are read through [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) via this bridge DLL.

**Download `lhwm-wrapper.dll` from [LeProta/lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper/releases)** and place it in the **same folder as `OpenRGB.exe`**.

> ⚠️ Use that build — it ships LibreHardwareMonitor 0.9.4 plus the *return control to BIOS* API this plugin needs. The older copy distributed with the OpenRGB Hardware Sync plugin will not expose motherboard fans. It is fully backward-compatible, including with the [NZXT Kraken LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin).

Without `lhwm-wrapper.dll` the plugin still loads and controls the Kraken pump/fan — only the motherboard channels and system temperature sources disappear.

### 3. Install the plugin

In OpenRGB: **Settings → Plugins → Install plugin** → select `NZXTKrakenPumpPlugin.dll`.

Or drop it directly into:
```
%APPDATA%\OpenRGB\plugins
```

Restart OpenRGB — a **NZXT Kraken Pump** tab will appear.

### 4. Run OpenRGB as Administrator

Required for motherboard fan control and CPU temperatures (ring-0 sensor driver). The Kraken pump/fan channel and the liquid temperature source work without it.

---

## Building from source

### Requirements

| Tool | Version |
|------|---------|
| Visual Studio Build Tools | 2019 / 2022 — *Desktop development with C++* + *.NET Framework 4.x SDK* |
| Qt | 5.15.2 `msvc2019_64` |
| vcpkg | latest — provides `hidapi` (`x64-windows`) |
| CMake | ≥ 3.16 |

### Steps

Use an **x64 Native Tools Command Prompt for VS**:

```bat
git clone https://github.com/LeProta/NZXTKrakenPumpPlugin
cd NZXTKrakenPumpPlugin

cmake -S . -B build -G "NMake Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64

cmake --build build
```

Output: `build/NZXTKrakenPumpPlugin.dll`.

> If `hidapi` is not found automatically, pass the vcpkg toolchain:
> `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake`

> The build is forced to Release — `lhwm-cpp-wrapper.lib` is compiled with `/MD` and mixing runtimes causes `LNK2038`.

---

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| No **NZXT Kraken Pump** tab / *"Cannot load the plugin"* | Wrong or corrupted DLL — re-download from Releases. |
| No motherboard fans listed | `lhwm-wrapper.dll` missing or outdated (use the one from [LeProta/lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper/releases)); OpenRGB not running as Administrator; or *Memory Integrity (HVCI)* / the *Vulnerable Driver Blocklist* is blocking the sensor driver. |
| Fan/pump % does not change | Close **NZXT CAM** (it keeps re-applying its own curves and fights the plugin). |
| CPU temperature source shows 0 | Close NZXT CAM / HWiNFO / Ryzen Master; run OpenRGB as Administrator. |
| A fan channel disappeared from the list | It was detected as an **empty header** (duty commanded, 0 RPM for 15 s) and hidden. Plug a fan in — it re-appears as soon as a tachometer signal is seen. |
| Fans revert to BIOS behaviour after closing OpenRGB | By design — motherboard channels are handed back to the BIOS on exit. |

Logs are written next to OpenRGB's own log files (`NZXTKrakenPump_<timestamp>.log`), and make sure you have enabled logs in the OpenRGB settings for the logs to appear.

---

## How it works

- **Kraken channels.** HID command `0x72` with a duty-per-°C table. Two protocol variants: X3/Z3 use 40 duties (20–59 °C), Kraken 2023 / Elite / Elite V2 use different per-channel headers with 60 duties (0–59 °C). The plugin writes a *flat* table recomputed by a 1 Hz software loop, so any temperature source can drive the pump — not just the liquid sensor.
- **Control loop.** Each tick: temperatures read once and smoothed (EMA), curve interpolation, then rate limiting (Step up/down) — a write only happens when the duty actually changes.
- **Status.** `0x74 0x01` request → liquid temperature, pump/fan RPM and reported duty. The reported duty is compared to the expected value every tick and the command is re-sent on mismatch (Elite V2 self-reset quirk).
- **Motherboard fans.** LibreHardwareMonitor through a C++/CLI bridge (`lhwm-wrapper.dll`); every touched channel is released with `Control.SetDefault()` (BIOS) when the plugin unloads.
- **Coexistence.** Runs alongside the [NZXT Kraken LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) on the same cooler: each plugin opens its own HID handle, Windows duplicates input reports to every handle and serializes writes.

---

## Acknowledgements

- [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) — host application and plugin SDK.
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) — sensor/fan backend and the Kraken `0x72` protocol reference (MPL-2.0).
- [liquidctl](https://github.com/liquidctl/liquidctl) — Kraken protocol research.
- [lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper) — C++/CLI bridge over LibreHardwareMonitor.

---

## Related

- [NZXT Kraken LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) — drives the LCD screen of the same coolers.
- [lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper) — the sensor/fan bridge DLL used by both plugins.

---

## License

Released under the **MIT License** — see [`LICENSE`](LICENSE).

Third-party components (LibreHardwareMonitor, Qt, hidapi) remain under their respective licenses.
