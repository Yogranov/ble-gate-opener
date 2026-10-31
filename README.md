<p align="center"><img src="logo.png" width="280" alt="YogiGate"></p>

# YogiGate

Opens a gate without anyone touching a remote. A battery beacon sits on the gate. A unit in the car listens for it and presses the button of a remote that is already paired with the gate.

It presses **once, when the car has moved in the last minute and can hear the beacon**. Starting the car never opens the gate by itself. The design is in [docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md](docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md).

## Hardware

- ESP32-C6 devkit (ESP32-C6-DevKitC-1 style)
- MPU6050 module
- MOSFET across the remote's button, with a resistor from gate to GND
- A BLE beacon, normally an iBeacon
- Optional: a buzzer and an external button

| Function | GPIO | Header | Notes |
|---|---|---|---|
| Remote MOSFET gate | 10 | J1-10 | **Needs a 10-100 kΩ resistor from gate to GND** |
| Buzzer (optional) | 11 | J1-11 | Off by default |
| MPU6050 SDA | 2 | J1-12 | |
| MPU6050 SCL | 3 | J1-13 | |
| MPU6050 VCC | 3V3 | J1-1 | |
| MPU6050 GND | GND | J1-15 | |
| Button | 9 | on-board BOOT | An external one can be added at J3-11 to GND |
| RGB LED | 8 | on board | |

Do not use GPIO18-23 for the MOSFET: on the C6 they are pulled up during reset, which could press the remote at every power-on. Pins can be changed in `idf.py menuconfig`, under YogiGate pins.

Fix the unit so it cannot slide or tumble. Any orientation works.

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

Connect through the C6's native USB port for the console.

## First setup

1. Read the hotspot name and password over USB. Each unit creates a random password on first start.

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

The hotspot comes on at every start. It goes off when the car moves or after 5 minutes, unless a phone is connected. To use it, start the car, stay parked, join the hotspot and open `http://192.168.4.1`.

Everything is on the page: live signal, pairing, all settings, tests, logs, firmware update and the debug flag.

| Button gesture | Action |
|---|---|
| Hold 3 s | Pair a beacon held close |
| Single click | Press the remote |
| Double click | Set the set point from the current signal |
| Triple click | Start the hotspot |

## Logs

`tools/yg_logs.py` fetches and analyses logs. Log files go in `logs/`, which git ignores because they show when the owner comes and goes. The format is in [docs/log-format.md](docs/log-format.md).

```powershell
python tools/yg_logs.py fetch --port COM3
python tools/yg_logs.py summary logs/<file>.ndjson
python tools/yg_logs.py replay logs/<file>.ndjson --trip 12 --set motion_accel_mg=60
```

Replay runs a trip through the current logic, starting from the settings that trip really had.

Rate trips on the page when something feels wrong. The unit cannot see the gate, so your rating is the only record of whether it really opened.

To have logs reviewed, give the downloaded file to a Claude session in this repo and ask it to review them. Debug mode records raw captures, which make tuning much easier in the first weeks.

## Tests

All decisions live in `components/yg_core`, which has no ESP-IDF dependency and needs only clang or gcc to test:

```powershell
python test/host/run_tests.py
```

These run on a real unit over USB, using the ESP-IDF Python:

| Check | What it does |
|---|---|
| `test/bench/api_check.py` | Every API endpoint, including bad requests |
| `test/bench/ota_check.py` | Firmware update and rollback. About three minutes |
| `test/bench/log_check.py` | Erasing full logs and a factory reset. **Erases the settings, and the hotspot gets a new password** |
| `test/bench/soak_check.py` | Repeated restarts and hotspot cycles, watching for crashes and leaks |

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
| `test/host`, `test/bench` | Logic tests and on-device checks |
| `tools` | Serial helper, log tools, mock API |
