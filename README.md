# NZXT Kraken Pump plugin for OpenRGB

An OpenRGB plugin that sets the speed of your NZXT Kraken pump and fans, your motherboard fans and your graphics card fan, using temperature curves. It works without NZXT CAM.

Windows only (10 and 11, 64-bit).

## What it controls

- The Kraken pump
- The fans plugged into the Kraken pump head (Z series, 2023, Elite)
- Every fan header on the motherboard that LibreHardwareMonitor can control
- The graphics card fan: AMD Radeon through ADLX, NVIDIA GeForce through NVML

Each of them gets its own curve and its own temperature source.

## Using it

Every fan has a mode:

- Silent: a quiet preset curve. This is the default.
- Performance: a more aggressive preset.
- Fixed: one constant speed.
- Custom: draw your own curve. Left click adds a point, right click removes one, and you can drag points around. If you edit a preset it switches to Custom by itself.

The temperature source can be the coolant, GPU average, GPU hot spot, CPU average, CPU hot spot, RAM or the motherboard sensor.

Other things you can do:

- Limit how fast a fan speeds up or slows down (Step up / Step down).
- Reset a fan, or copy its curve to other fans with Apply.
- Click the name of a fan to rename it.
- Click the icon of a fan to run it at 100 % for 5 seconds so you can tell which one it is. This does not exist for the pump and the GPU fan.

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

All of them use vendor ID `0x1E71`. The plugin is developed and tested on a Kraken Elite V2. Motherboard fans work with any Super I/O chip LibreHardwareMonitor supports; it was tested on a Nuvoton NCT6799D (NZXT N7 B650E).

## Install

1. Download `NZXTKrakenPumpPlugin.dll` from the [releases page](https://github.com/LeProta/NZXTKrakenPumpPlugin/releases).
2. Download `lhwm-wrapper.dll` from [LeProta/lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper/releases) and put it next to `OpenRGB.exe`. It is how the plugin reads motherboard and CPU sensors. The older copy that ships with the OpenRGB Hardware Sync plugin does not expose motherboard fans, so use this one. The [LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) uses the same file.
3. Install [PawnIO](https://pawnio.eu) (2.1.0 or newer). It is the driver LibreHardwareMonitor uses to talk to the hardware. Without it there are no motherboard fans and no CPU temperature.
4. In OpenRGB go to Settings, Plugins, Install plugin and pick the DLL. You can also copy it to `%APPDATA%\OpenRGB\plugins`. Restart OpenRGB and a "NZXT Kraken Pump" tab shows up.
5. Run OpenRGB as administrator. The plugin needs it for motherboard fans, CPU temperatures and NVIDIA.

Without `lhwm-wrapper.dll` the tab only says "Cannot load the plugin" and nothing else starts.

Keep only plugin files in the plugins folder. OpenRGB tries to load everything in there, backups included.

Close NZXT CAM while using this plugin. It keeps applying its own curves and the two fight each other.

## GPU fans

You do not have to install anything for this. AMD uses `amdadlx64.dll` from the Adrenalin driver, NVIDIA uses `nvml.dll` from the GeForce driver.

AMD (ADLX):

- ADLX has no direct fan speed, only a fan table. The plugin writes a flat table with the speed you asked for.
- From 65 °C the table never goes below the card's original curve. If OpenRGB freezes or gets killed, the card is still protected.
- The card's own zero RPM mode would keep the fan stopped when it is cool, whatever the table says. So while the plugin drives the fan, zero RPM is turned off. If a curve asks for less than the card's minimum (20 % on an RX 9070 XT), zero RPM is turned back on.
- The original table and the zero RPM setting are saved to `adlx_recovery_<card>.txt` before the first write. If OpenRGB did not exit cleanly, they are put back at the next start, unless you changed the fan table in Adrenalin in the meantime.
- ADLX sometimes asks for a factory reset before it accepts a change. The plugin always says no, because the reset would also wipe your Adrenalin overclock. The write fails and a line is added to the log.

NVIDIA (NVML):

- Writing a fan speed switches the card to manual fan control, so the plugin adds a safety floor on top of your curve: at least 60 % from 75 °C and 100 % from 85 °C. It is checked every second.
- The automatic policy is restored when you hand the fan back or exit. After a crash, a marker file called `nvml_recovery.txt` makes the next start do it.
- The lowest speed is the card's own minimum, usually 30 %. There is no zero RPM through NVML.
- The RPM shown is the speed NVML says it wants, not a measured one.
- This part was written on an AMD machine. It was only tested against a fake `nvml.dll`, never on a real NVIDIA card.

## Building it yourself

You need:

- Visual Studio Build Tools 2019 or 2022 with the C++ desktop workload
- Qt 6.8.3 (`msvc2022_64`), for OpenRGB 1.1 (plugin API 5)
- CMake 3.16 or newer (the one that comes with Visual Studio is fine)
- hidapi: the bundled `third_party/hidapi/lib/hidapi.lib` imports OpenRGB 1.1's own `hidapi-hotplug.dll` (regenerate with `lib /def:hidapi.def /machine:x64`)
- [lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper) cloned next to this repository
- the AMD ADLX SDK, see below

The ADLX SDK is not in this repository because AMD's license does not allow redistributing it. Download it from [GPUOpen-LibrariesAndSDKs/ADLX](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX) and copy its `SDK` folder to `third_party/adlx/SDK`. Read the license that comes with it first.

Open an x64 Native Tools Command Prompt and use a short path, because NMake cannot open files whose full path is longer than 260 characters.

```bat
git clone https://github.com/LeProta/lhwm-wrapper
git clone https://github.com/LeProta/NZXTKrakenPumpPlugin
cd NZXTKrakenPumpPlugin

cmake -S . -B build -G "NMake Makefiles" ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64 ^
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake

cmake --build build
```

The result is `build/NZXTKrakenPumpPlugin.dll`.

If you do not use vcpkg, pass `-DHIDAPI_ROOT=<folder>` instead of the toolchain file. `-DLHWM_WRAPPER_DIR=<folder>` points to a different copy of the wrapper. The build is always Release, because OpenRGB refuses plugins linked to the debug Qt DLLs and `lhwm-cpp-wrapper.lib` is built with `/MD`. `/EHa` is needed so `catch (...)` can catch the .NET exceptions coming from the wrapper.

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

`tst_pumpworker` runs the control loop against fake HID and LibreHardwareMonitor backends. `tst_nvmlfan` runs the NVIDIA code against a fake `nvml.dll`. `tst_gpufan` covers the AMD table and recovery helpers.

## Problems

| What you see | What to try |
|--------------|-------------|
| No "NZXT Kraken Pump" tab, or "Cannot load the plugin" | `lhwm-wrapper.dll` is missing from the OpenRGB folder, or the DLL is corrupted. Download it again. |
| No motherboard fans | Wrong or old `lhwm-wrapper.dll`, PawnIO not installed, OpenRGB not running as administrator, or Windows (Memory Integrity, the vulnerable driver blocklist) is blocking the driver. |
| The speed does not change | Close NZXT CAM. |
| CPU temperature shows 0 | Close NZXT CAM, HWiNFO and Ryzen Master, and run OpenRGB as administrator. |
| OpenRGB crashes a few seconds after start with `0xC0000005` in `*_unloaded` | Update to 1.0.5 or newer. The graphics driver was unloading one of its DLLs while a sensor was being read. |
| A fan disappeared from the list | The plugin thought the header was empty (a speed was set and it read 0 RPM for 15 seconds) and hid it. Plug a fan in and it comes back as soon as it sees a tach signal. |
| Fans go back to the BIOS when OpenRGB closes | That is on purpose. Motherboard and GPU fans are handed back on exit. |
| The GPU fan stays at a low fixed speed after OpenRGB was killed | Start OpenRGB again. The original table (AMD) or the automatic policy (NVIDIA) is restored from the recovery file. |
| The AMD GPU fan never stops at idle | The plugin turned the card's zero RPM off. Use a curve that goes below the card's minimum at low temperatures. |

Logs go next to the OpenRGB logs, in `NZXTKrakenPump_<timestamp>.log`. You have to turn logging on in the OpenRGB settings to get them.

## Notes for developers

- Kraken: HID command `0x72` with a table of speeds per degree. X3/Z3 coolers take 40 values (20 to 59 °C), Kraken 2023, Elite and Elite V2 take 60 (0 to 59 °C) with different headers per channel. The plugin writes a flat table, recomputed once a second, so any temperature source can drive the pump and not only the coolant sensor. The table forces 100 % from 50 °C of coolant.
- Each second the control loop reads the temperatures, smooths them, interpolates the curve and applies the step limits. A write only happens if the speed changed.
- Status is read with `0x74 0x01`. It returns coolant temperature, pump and fan RPM and the speed the cooler reports. If that does not match what was written, the command is sent again (the Elite V2 resets itself to 0 % sometimes).
- The pump plugin and the [LCD plugin](https://github.com/LeProta/NZXTKrakenLCDPlugin) can run on the same cooler. Each one opens its own HID handle and Windows takes care of sharing it.

## Credits

- [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB) for the plugin interface
- [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) for the sensors, the fan control and the `0x72` protocol reference (MPL-2.0)
- [liquidctl](https://github.com/liquidctl/liquidctl) for the Kraken protocol research
- [lhwm-wrapper](https://github.com/LeProta/lhwm-wrapper), the bridge between this plugin and LibreHardwareMonitor
- [AMD ADLX](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX) for the AMD GPU fan control
- [PawnIO](https://pawnio.eu) for the hardware access driver

## License

MIT, see [LICENSE](LICENSE). Qt, hidapi, LibreHardwareMonitor and the AMD ADLX SDK keep their own licenses. The OpenRGB 1.1 plugin API headers in `third_party/openrgb` are GPL-2.0-or-later (see `third_party/openrgb/LICENSE`); `nlohmann/json.hpp` is MIT.
