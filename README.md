# NZXT Kraken Pump plugin for OpenRGB

Controls the pump and fans of NZXT Kraken coolers from OpenRGB, without NZXT CAM, along with your motherboard fans and your graphics card fan, using temperature curves.

[![Release](https://img.shields.io/github/v/release/LeProta/NZXTKrakenPumpPlugin)](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases)
![OpenRGB](https://img.shields.io/badge/OpenRGB-1.1-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey)
[![License](https://img.shields.io/github/license/LeProta/NZXTKrakenPumpPlugin)](LICENSE)

Requires OpenRGB 1.1. If you are still on OpenRGB 1.0rc3, use [v1.0.8](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases/tag/v1.0.8).

## Versions

| Plugin | OpenRGB | lhwm-wrapper |
|--------|---------|--------------|
| 1.1.0 | 1.1 | 1.1.0 or later, 1.2.0 recommended |
| 1.0.8 | 1.0rc3 | 1.1.0 or later |

## Features

What it controls:

- the Kraken pump
- the fans plugged into the Kraken pump head (Z series, 2023, Elite)
- every motherboard fan header that LibreHardwareMonitor can control
- the graphics card fan: AMD Radeon through ADLX, NVIDIA GeForce through NVML

Each one gets its own curve and its own temperature source.

## Usage

Every fan has a mode:

- Silent: a quiet preset curve. This is the default.
- Performance: a more aggressive preset.
- Fixed: one constant speed.
- Custom: draw your own curve. Left click adds a point, right click removes one, and points can be dragged. Editing a preset switches it to Custom.

The temperature source can be the coolant, GPU average, GPU hot spot, CPU average, CPU hot spot, RAM or the motherboard sensor.

You can also:

- limit how fast a fan speeds up or slows down (Step up / Step down)
- reset a fan, or copy its curve to other fans with Apply
- rename a fan by clicking its name
- click a fan's icon to run it at 100 % for 5 seconds and find out which one it is (not available for the pump and the GPU fan)

The pump never goes below 20 %. Motherboard fans go back to the BIOS when the plugin unloads. Settings are saved in `%APPDATA%\OpenRGB\NZXTKrakenPump`.

## Supported coolers

| Model | Pump | Fans on the pump head |
|-------|------|-----------------------|
| Kraken X53 / X63 / X73 (`0x2007`) | yes | no |
| Kraken X53 / X63 / X73 RGB (`0x2014`) | yes | no |
| Kraken Z53 (`0x3009`), Z63 (`0x300A`), Z73 (`0x3008`) | yes | yes |
| Kraken 2023 (`0x300E`) | yes | yes |
| Kraken Elite 360 (`0x300C`) | yes | yes |
| Kraken Elite V2 (`0x3012`, `0x3013`) | yes | yes |

All of them use vendor ID `0x1E71`. Development and testing are done on a Kraken Elite V2. Motherboard fans work with any Super I/O chip LibreHardwareMonitor supports; tested on a Nuvoton NCT6799D (NZXT N7 B650E).

## Installation

1. Download `NZXTKrakenPumpPlugin.dll` from the [releases page](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases).
2. Download `lhwm-wrapper.dll` (v1.2.0 recommended) from [LeProta/lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper/releases) and put it next to `OpenRGB.exe`. The plugin reads the motherboard and CPU sensors through it. The older copy that ships with the OpenRGB Hardware Sync plugin does not expose motherboard fans, so use this one. The [LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) uses the same file.
3. Install [PawnIO](https://pawnio.eu) 2.1.0 or newer. LibreHardwareMonitor uses this driver to talk to the hardware: without it there are no motherboard fans and no CPU temperature.
4. In OpenRGB, go to Settings > Plugins > Install plugin and pick the DLL, or copy it to `%APPDATA%\OpenRGB\plugins`. Restart OpenRGB and a "NZXT Kraken Pump" tab shows up.
5. Run OpenRGB as administrator. The plugin needs it for motherboard fans, CPU temperatures and NVIDIA.

Nothing else is needed: hidapi comes with OpenRGB 1.1.

Keep only plugin files in the plugins folder. OpenRGB tries to load everything in there, backups included.

Close NZXT CAM while using this plugin. It keeps applying its own curves and the two fight each other.

## GPU fans

There is nothing to install for this. AMD uses `amdadlx64.dll` from the Adrenalin driver, NVIDIA uses `nvml.dll` from the GeForce driver.

AMD (ADLX):

- ADLX has no direct fan speed, only a fan table. The plugin writes a flat table with the requested speed.
- From 65 °C the table never goes below the card's original curve, so the card stays protected even if OpenRGB freezes or gets killed.
- The card's zero RPM mode would keep the fan stopped while it is cool, whatever the table says. Zero RPM is therefore turned off while the plugin drives the fan, and turned back on if a curve asks for less than the card's minimum (20 % on an RX 9070 XT).
- The original table and the zero RPM setting are saved to `adlx_recovery_<card>.txt` before the first write. If OpenRGB did not exit cleanly, they are put back at the next start, unless the fan table was changed in Adrenalin in the meantime.
- ADLX sometimes asks for a factory reset before it accepts a change. The plugin always refuses, because the reset would also wipe your Adrenalin overclock. The write fails and a line is added to the log.

NVIDIA (NVML):

- Writing a fan speed switches the card to manual fan control, so the plugin adds a safety floor on top of your curve: at least 60 % from 75 °C and 100 % from 85 °C, checked every second.
- The automatic policy is restored when you hand the fan back or exit. After a crash, a marker file called `nvml_recovery.txt` makes the next start do it.
- The lowest speed is the card's own minimum, usually 30 %. NVML has no zero RPM.
- The RPM shown is the speed NVML is targeting, not a measured one.
- This part was written on an AMD machine and only tested against a fake `nvml.dll`, never on a real NVIDIA card.

## Troubleshooting

| Problem | What to try |
|---------|-------------|
| No "NZXT Kraken Pump" tab | Check that you are on OpenRGB 1.1. Older versions need [v1.0.8](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases/tag/v1.0.8). |
| The tab only says "Cannot load the plugin" | `lhwm-wrapper.dll` is missing from the OpenRGB folder, or the file is damaged. Download it again. |
| No motherboard fans | Wrong or old `lhwm-wrapper.dll`, PawnIO not installed, OpenRGB not running as administrator, or Windows blocking the driver (Memory Integrity, vulnerable driver blocklist). |
| The speed does not change | Close NZXT CAM. |
| CPU temperature shows 0 | Close NZXT CAM, HWiNFO and Ryzen Master, and run OpenRGB as administrator. |
| A fan disappeared from the list | The plugin saw 0 RPM for 15 seconds after setting a speed, decided the header was empty and hid it. It comes back as soon as a tach signal shows up. |
| Fans go back to the BIOS when OpenRGB closes | That is on purpose: motherboard and GPU fans are handed back on exit. |
| The GPU fan stays at a low fixed speed after OpenRGB was killed | Start OpenRGB again. The original table (AMD) or the automatic policy (NVIDIA) is restored from the recovery file. |
| The AMD GPU fan never stops at idle | The plugin turned the card's zero RPM off. Use a curve that goes below the card's minimum at low temperatures. |

Logs are written to `%APPDATA%\OpenRGB\logs\NZXTKrakenPump_<date>.log`. They follow OpenRGB's own log settings, and the last 10 files are kept by default.

## Building from source

You need:

- Visual Studio Build Tools 2022 with the C++ desktop workload
- Qt 6.8.3 (`msvc2022_64`)
- CMake 3.16 or newer (the one that comes with Visual Studio works)
- [lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper) cloned next to this repository
- the AMD ADLX SDK (see below)

The rest is in `third_party/`: the OpenRGB 1.1 plugin API headers, and an import library for the `hidapi-hotplug.dll` that ships with OpenRGB.

The ADLX SDK is not included because AMD's license does not allow redistributing it. Download it from [GPUOpen-LibrariesAndSDKs/ADLX](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX), read its license, and copy its `SDK` folder to `third_party/adlx/SDK`.

From an x64 Native Tools Command Prompt, in a short path (NMake cannot open files whose full path is longer than 260 characters):

```bat
git clone https://github.com/LeProta/lhwm-wrapper
git clone https://github.com/LeProta/NZXTKrakenPumpPlugin
cd NZXTKrakenPumpPlugin

cmake -S . -B build -G "NMake Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build
```

The result is `build/NZXTKrakenPumpPlugin.dll`.

Some notes:

- `-DLHWM_WRAPPER_DIR=<folder>` points to another copy of the wrapper.
- The build is always Release: OpenRGB refuses plugins linked to the debug Qt DLLs, and `lhwm-cpp-wrapper.lib` is built with `/MD`.
- `/EHa` is required so that `catch (...)` also catches the .NET exceptions thrown by the wrapper.
- If OpenRGB changes its hidapi exports, regenerate the import library with `lib /def:hidapi.def /machine:x64 /out:hidapi.lib` in `third_party/hidapi/lib`.

### Tests

The tests do not need any hardware.

```bat
cmake -S tests -B build-tests -G "NMake Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build-tests
set PATH=C:\Qt\6.8.3\msvc2022_64\bin;%PATH%
build-tests\tst_pumpworker.exe
build-tests\tst_nvmlfan.exe
build-tests\tst_gpufan.exe
```

`tst_pumpworker` runs the control loop against fake HID and LibreHardwareMonitor backends, `tst_nvmlfan` runs the NVIDIA code against a fake `nvml.dll`, and `tst_gpufan` covers the AMD table and recovery helpers.

## How it works

- Kraken: HID command `0x72` with a table of speeds per degree. X3 and Z3 coolers take 40 values (20 to 59 °C); Kraken 2023, Elite and Elite V2 take 60 (0 to 59 °C) with different headers per channel. The plugin writes a flat table, recomputed once a second, so any temperature source can drive the pump and not only the coolant sensor. The table forces 100 % from 50 °C of coolant.
- Every second the control loop reads the temperatures, smooths them, interpolates the curve and applies the step limits. A speed is only written when it changes.
- Status is read with `0x74 0x01`, which returns the coolant temperature, pump and fan RPM and the speed the cooler reports. Queued reports are flushed first so the reading is always fresh. If the reported speed does not match what was written, the command is sent again (the Elite V2 sometimes resets itself to 0 %).
- OpenRGB settings (config folder, language, logs) are read through the plugin API and refreshed when they change.
- The pump plugin, the [LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) and OpenRGB's own Kraken driver can all use the cooler at the same time. Each opens its own HID handle and Windows shares the device.

## Credits

- [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) for the plugin API
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) for the sensors, the fan control and the `0x72` protocol reference (MPL-2.0)
- [liquidctl](https://github.com/liquidctl/liquidctl) for the Kraken protocol research
- [lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper), the bridge between this plugin and LibreHardwareMonitor
- [AMD ADLX](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX) for the AMD GPU fan control
- [PawnIO](https://pawnio.eu) for the hardware access driver

See also the [LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin), which drives the screen of the same coolers.

## License

MIT, see [LICENSE](LICENSE). Qt, hidapi, LibreHardwareMonitor and the AMD ADLX SDK keep their own licenses. The OpenRGB plugin API headers in `third_party/openrgb` are GPL-2.0-or-later (see the `LICENSE` file there), and `nlohmann/json.hpp` is MIT.
