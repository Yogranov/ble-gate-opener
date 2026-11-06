<p align="center"><img src="logo.png" width="280" alt="YogiGate"></p>

# YogiGate

Opens a gate without anyone touching a remote. A battery beacon sits on the gate. A unit in the car listens for it and presses the button of a remote that is already paired with the gate.

It presses **once, when the car has moved in the last minute and can hear the beacon**. Starting the car never opens the gate by itself. The design is in [docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md](docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md).

| Read | For |
|---|---|
| [manual.html](manual.html) | Fitting a unit, with what each sound means. It prints on both sides of one A4 sheet, which is cut out, folded and kept inside the case |
| [atlas.html](atlas.html) | The whole picture: how it decides, a trip you can play, the board, and the case in 3D |
| [AGENTS.md](AGENTS.md) | Rules for changing this repo, including what to update when something changes |

## Hardware

- MuseLab ESP32-C6 board with two USB-C ports. The ESP32-C6-DevKitC-1 has the same GPIOs, but a different size and pin order
- MPU6050 module
- P30N06LE MOSFET across the remote's button, with 220 Ω in series with the gate and 10 kΩ from gate to GND. IRLB8743 also works. IRF520 and IRF540N are not suitable, because 3.3 V may not turn them on
- A BLE beacon, normally an iBeacon
- Optional: an active buzzer and an external button

The hole-by-hole layout for a 50 × 70 mm matrix board is in [hardware/](hardware/). The holes below are from that layout.

| Function | GPIO | Hole | Notes |
|---|---|---|---|
| Remote MOSFET gate | 10 | Q11 | **Through 220 Ω, with 10 kΩ from gate to GND** |
| Buzzer (optional) | 11 | Q12 | Active buzzer, + to the pin and − to GND. Must draw under about 20 mA at 3.3 V. On by default, and safe to leave unfitted |
| MPU6050 SDA | 2 | G2 | |
| MPU6050 SCL | 3 | G3 | |
| MPU6050 VCC | 3V3 | Q2 | |
| MPU6050 GND | GND | G1 | |
| Button | 9 | on-board BOOT | |
| External button (optional) | 7 | Q7 | Tactile button on the board: Q7 to R16, and U18 to GND. Works the same as the on-board button |
| RGB LED | 8 | on board | |

Do not use GPIO18-23 for the MOSFET: on the C6 they are pulled up during reset, which could press the remote at every power-on. Do not put an external button on GPIO9: held down at power-on, it stops the unit starting. Pins can be changed in `idf.py menuconfig`, under YogiGate pins.

Keep the remote on its own battery. It then takes no power from the unit, and its only link to the unit is the two wires across the MOSFET. The firmware cannot see a dead remote battery, so replace it every year.

Power the unit from a phone charger in a socket that turns off with the ignition, not from a car screen's USB port. In the first car tests a screen's port took seconds to rise and fall, and the unit often failed several times before it started. Section 11 of the design spec has the numbers.

Fix the unit so it cannot slide or tumble. Any orientation works.

## Board variants

The firmware builds for two boards, chosen in `idf.py menuconfig` under "YogiGate pins", Gate driver:

- **MOSFET** (default, `YG_GATE_DRIVER_MOSFET`): presses a wired remote's button, as above.
- **RF433** (`YG_GATE_DRIVER_RF433`): no MOSFET and no wired remote. A CC1101 433 MHz transceiver learns a fixed-code remote once and transmits it to open the gate. Wiring and parts are in [hardware/design.md](hardware/design.md) section 9, the design in the [RF variant spec](docs/superpowers/specs/2026-11-06-rf433-board-variant-design.md). The board layout, case and printed manual wait for the real module to be measured.

## Build and flash

Install ESP-IDF v6.1:

```powershell
winget install Espressif.EIM-CLI
eim install -i v6.1 -t esp32c6
```

Then, in a terminal with the ESP-IDF environment active:

```powershell
idf.py build
idf.py -p COM3 flash
```

Connect through the C6's native USB port, marked ESP32C6, for the console.

## First setup

1. Start the hotspot by triple-clicking the button. You hear three short beeps. Its name is `YogiGate-XXXX`, and its password is `password` until you change it on the page. Over USB, this does the same and prints both:

   ```powershell
   python tools/yg_serial.py wifi
   ```

2. Pair the beacon. The MAC is usually the easiest route:

   ```powershell
   python tools/yg_serial.py pair c5:b0:e6:53:59:02
   ```

   You can also do it on the page, or hold the button for 3 s and hold the beacon against the unit.

3. Fit it in the car and forget about it.

A second unit needs the same steps. It can use the same beacon.

## The web page

The hotspot only starts when you ask, by triple-clicking the button or with the USB `wifi` command. The one exception is the first start after a firmware update, which needs it for its self-check. It goes off after 5 minutes, or 30 s after the last phone leaves if that is later. Wi-Fi and Bluetooth share one radio, so while it is on the unit listens for the beacon a quarter of the time, and a press can come about a second later. To use it, triple-click, join the hotspot and open `http://192.168.4.1`.

The page starts simple: state, live signal and motion, pairing, the set point, the buzzer switch, trip ratings, Change password, and a Help tab with the technician manual, in case the printed one is gone. Advanced mode, at the bottom of the Settings tab, adds every setting, Reset to defaults, the LED test, each sound, firmware upload, Reset logs, factory reset and the debug tools. Each phone remembers its own choice.

A flash keeps the settings stored on a unit, so a changed default only reaches it after **Reset to defaults**, on the Settings tab in Advanced mode. That keeps the beacon and logs. **Factory reset**, on the System tab or by holding the button for 15 s, erases those too.

| Button gesture | Action |
|---|---|
| Hold 3 s | Pair a beacon held close |
| Hold 15 s | Factory reset. A button already down at power-on does not count, so a stuck one cannot wipe the unit at every start |
| Single click | Press the remote |
| Double click | Set the set point from the current signal |
| Triple click | Start the hotspot |
| Double-click then hold 6 s | Learn the gate remote (RF variant only) |

## Sounds

The buzzer is on by default. It sounds when someone does something on the unit, or when something is wrong, and stays quiet when the gate opens by itself. An active buzzer has one fixed tone, so each sound is a rhythm, set in `components/yg_core/yg_sound.c`.

| Sound | Means |
|---|---|
| One short | The remote was pressed by hand |
| Two short | Pairing started |
| Three short | The hotspot is starting |
| Short, short, long | Success |
| Two rattles | That did not work |
| One long, at power-on | No beacon is paired yet |
| Three long, at most once per trip | The motion sensor failed |
| Five short, one long | Factory reset, after holding the button for 15 s |

## Logs

`tools/yg_logs.py` fetches and analyses logs. Log files go in `logs/`, which git ignores because they show when the owner comes and goes. The format is in [docs/log-format.md](docs/log-format.md).

```powershell
python tools/yg_logs.py fetch --port COM3
python tools/yg_logs.py summary logs/<file>.ndjson
python tools/yg_logs.py replay logs/<file>.ndjson --trip 12 --set motion_accel_mg=60
```

Replay runs a trip through the current logic, starting from the settings that trip really had.

**Reset logs**, on the Logs tab in Advanced mode, erases the logs and starts counting trips from 1 again, with no restart. Download them first if you want to keep them, and keep files from before a reset apart, because the trip numbers repeat.

Rate trips on the page when something feels wrong. The unit cannot see the gate, so your rating is the only record of whether it really opened.

To have logs reviewed, give the downloaded file to a Claude session in this repo and ask it to review them. Debug mode records raw captures, which make tuning much easier in the first weeks.

## Tests

All decisions live in `components/yg_core`, which has no ESP-IDF dependency and needs only clang or gcc to test:

```powershell
python test/host/run_tests.py
```

That also runs `tools/check_docs.py`, which fails if the docs, the page or the mock no longer match the firmware.

These run on a real unit over USB, using the ESP-IDF Python:

| Check | What it does |
|---|---|
| `test/bench/api_check.py` | Every API endpoint, including bad requests. **Presses the remote output, forgets the beacon and resets the settings** |
| `test/bench/ota_check.py` | Firmware update and rollback. About three minutes. **Erases the logs** |
| `test/bench/log_check.py` | Erasing full logs and a factory reset. **Erases the settings, and the hotspot password goes back to the default** |
| `test/bench/soak_check.py` | Repeated restarts and hotspot cycles, watching for crashes and leaks. **Erases the logs** |

`tools/mock_api.py` serves the page with a fake API, for working on it without a unit, and `test/bench/page_check.py` tests the page against that in headless Edge.

Every POST to the API needs the header `X-YogiGate: 1`, which stops a page on another site from pressing the remote.

## Layout

| Path | What |
|---|---|
| `components/yg_core` | All the logic: trigger, motion, pairing, logs, engine |
| `components/yg_app` | The decision task that connects logic to hardware |
| `components/yg_ble`, `yg_imu`, `yg_io`, `yg_log`, `yg_settings_nvs` | Thin hardware layers |
| `components/yg_net` | Hotspot, HTTP API, firmware upload |
| `components/yg_console` | USB console |
| `web/index.html` | The page, embedded gzipped into the firmware |
| `manual.html`, `atlas.html` | The fold-up technician manual, and the full picture |
| `test/host`, `test/bench` | Logic tests and on-device checks |
| `tools` | Serial helper, log tools, mock API |
| `hardware` | Board layout, part choices, and details of the real parts |
