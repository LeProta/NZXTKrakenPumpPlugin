# NZXT Kraken Pump — OpenRGB Plugin

_Pump & fan curve control for NZXT Kraken coolers and motherboard fans._

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

| Channel | Control |
|---------|---------|
| **Kraken pump** | Speed curve over HID. Always driven (no Auto), 20 % safety floor, 100 % forced at the top of the curve. |
| **Kraken fan channel** | Speed curve over HID for the fans plugged into the pump head (Z-series / 2023 / Elite). |
| **Motherboard fans** | Every controllable header exposed by the Super I/O chip (via LibreHardwareMonitor). `Auto` hands the channel back to the BIOS. |

### Curve editor

- Free-form points on a 0–100 °C axis: **left-click** to add, **right-click** to remove, drag points on both axes.
- Drag guides show the exact **%** and **°** while moving a point; editing a preset curve switches to *Custom* automatically.
- Live temperature cursor for the selected source.
- Presets per channel: **Silent / Normal / Performance / Custom** (+ **Auto** for motherboard fans).

### Temperature sources (per channel)

Liquid (coolant) · GPU Average · GPU Hot Spot · CPU Average · CPU Hot Spot · RAM · Motherboard

### General

- Channels grouped per hardware (Kraken, motherboard) with a collapsible curve per channel — click anywhere on a row to expand, **double-click a name to rename it**.
- Live RPM and duty (%) for every channel, including the pump.
- Software curve loop at 1 Hz — writes only when the computed duty changes; motherboard channels are **returned to BIOS control** on exit or when set to Auto.
- Handles the Elite V2 firmware quirk where the pump occasionally resets itself to 0 %: the reported duty is checked every second and the command is re-sent on mismatch.
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

**Download `lhwm-wrapper.dll` from this repo's [Releases](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases)** and place it in the **same folder as `OpenRGB.exe`**.

> ⚠️ Use the copy from this repo's Releases — it is a custom build (LibreHardwareMonitor 0.9.4 + the *return control to BIOS* API). The older copy shipped with the OpenRGB Hardware Sync plugin will not expose motherboard fans. It is fully backward-compatible, including with the [NZXT Kraken LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin).

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
| No motherboard fans listed | `lhwm-wrapper.dll` missing or outdated (use the one from this repo's Releases); OpenRGB not running as Administrator; or *Memory Integrity (HVCI)* / the *Vulnerable Driver Blocklist* is blocking the sensor driver. |
| Fan/pump % does not change | Close **NZXT CAM** (it keeps re-applying its own curves and fights the plugin). |
| CPU temperature source shows 0 | Close NZXT CAM / HWiNFO / Ryzen Master; run OpenRGB as Administrator. |
| Fans revert to BIOS behaviour after closing OpenRGB | By design — motherboard channels are handed back to the BIOS on exit. |

Logs are written next to OpenRGB's own log files (`NZXTKrakenPump_<timestamp>.log`), and make sure you have enabled logs in the OpenRGB settings for the logs to appear.

---

## How it works

- **Kraken channels.** HID command `0x72` with a duty-per-°C table. Two protocol variants: X3/Z3 use 40 duties (20–59 °C), Kraken 2023 / Elite / Elite V2 use different per-channel headers with 60 duties (0–59 °C). The plugin writes a *flat* table recomputed by a 1 Hz software loop, so any temperature source can drive the pump — not just the liquid sensor.
- **Status.** `0x74 0x01` request → liquid temperature, pump/fan RPM and reported duty. The reported duty is compared to the expected value every tick and the command is re-sent on mismatch (Elite V2 self-reset quirk).
- **Motherboard fans.** LibreHardwareMonitor through a C++/CLI bridge (`lhwm-wrapper.dll`); every touched channel is released with `Control.SetDefault()` (BIOS) on exit or when set to Auto.
- **Coexistence.** Runs alongside the [NZXT Kraken LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) on the same cooler: each plugin opens its own HID handle, Windows duplicates input reports to every handle and serializes writes.

---

## Acknowledgements

- [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) — host application and plugin SDK.
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) — sensor/fan backend and the Kraken `0x72` protocol reference (MPL-2.0).
- [liquidctl](https://github.com/liquidctl/liquidctl) — Kraken protocol research.
- `lhwm-cpp-wrapper` — C++/CLI wrapper over LibreHardwareMonitor.

---

## Related

- [NZXT Kraken LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) — drives the LCD screen of the same coolers.

---

## License

Released under the **MIT License** — see [`LICENSE`](LICENSE).

Third-party components (LibreHardwareMonitor, Qt, hidapi) remain under their respective licenses.
