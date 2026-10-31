# YogiGate 2: ESP-IDF Rewrite Design

**Date:** 2026-10-31
**Status:** Awaiting owner review
**Scope:** Firmware and wiring. The case gets its own spec after this firmware has run in the car.

## 1. Summary

YogiGate opens a gate without anyone touching a remote. A battery beacon is glued to the gate. A unit in the car listens for it and, through a MOSFET, presses the button of a remote that is already paired with the gate.

Today's firmware is one Arduino file on PlatformIO with a hard-coded beacon address and a fixed `-90 dBm` threshold. This rewrite:

- Moves to native ESP-IDF v6.1 with NimBLE.
- Replaces the threshold rule with **moving + heard**, using an MPU6050 motion sensor.
- Removes all hard-coding: beacon, set point and timings are stored on the unit.
- Makes everything controllable from a web page served by the unit, so a button is optional.
- Records logs that can be downloaded and used to tune the firmware over time.
- Allows firmware updates over Wi-Fi, with automatic rollback.

## 2. Setup and constraints

- The unit and the remote sit in the car's glove box, on power that turns off with the ignition.
- The beacon is on a parking lot gate. The owner's parking spot is 13 m from it.
- With no RSSI limit, the gate opened from the spot right after ignition, before the driver was ready. `-90 dBm` usually prevents that, but leaves almost no margin: on bad days the car must get very close.
- A second unit may go to someone whose spot is very close to the gate. It must work without per-spot tuning and can share the same beacon.
- The owner wants to set it and forget it. No attention-seeking feedback by default.
- The repository is public, so no passwords or logs may be committed.

## 3. Goals and non-goals

**Goals**

1. Never press because the car started. Press once when the car is really leaving or arriving.
2. Keep working on bad radio days as far as physics allows.
3. Need no code changes to pair a beacon, tune behaviour or set up a second unit.
4. Need no button: every setting and action is on the web page.
5. Produce logs good enough to find out what really happens, and to tune from.

**Non-goals for this spec**

- The case.
- Exact dates in logs. The owner chose numbered trips, so there is no clock module.
- Internet connectivity, cloud upload or automatic updates.
- Multiple beacons per unit.
- BLE long range, which would need a different beacon.
- A captive portal. The page opens at `http://192.168.4.1`.

## 4. Hardware

### 4.1 Board: ESP32-C6-DevKitC-1 style devkit

Chosen over the three other boards the owner has. The hearing limit is the datasheet's typical BLE 1 Mbps sensitivity.

| Board | Hears down to | Why not |
|---|---|---|
| **C6 devkit** | -98.5 dBm | Chosen. WROOM-1 module with PCB antenna, rated to 85 °C, 8 MB flash on the official board |
| C3 mini | -97 dBm on paper | Small boards of this style with a ceramic chip antenna have a poor reputation. Hobbyist antenna mods report gains of 6 to 10 dB, which suggests that much loss. That would cancel the chip's advantage in a glove box |
| S3 devkit | -97.5 dBm | No hearing advantage over the C6, and modules with R8 in the part number are rated to only 65 °C |
| D1 mini (ESP32) | -93 dBm | Weakest hearing and BLE 4.2. It stays in the car on the old firmware until step 3 of the rollout, as the fallback |

The first time the board is connected, read its chip type and flash size with `esptool`. Section 9.3 covers the flash layout if it is not 8 MB.

### 4.2 Wiring

| Function | GPIO | Header | Notes |
|---|---|---|---|
| Remote MOSFET gate | 10 | J1-10 | **Requires a 10-100 kΩ resistor from gate to GND** |
| Buzzer (optional) | 11 | J1-11 | Off by default, safe to leave unfitted |
| MPU6050 SDA | 2 | J1-12 | The module has its own I²C pull-ups |
| MPU6050 SCL | 3 | J1-13 | |
| MPU6050 VCC | 3V3 | J1-1 | |
| MPU6050 GND | GND | J1-15 | |
| Button | 9 | on-board BOOT | An external button can be added at J3-11 to GND |
| RGB LED | 8 | on board | Addressable, driven with `espressif/led_strip` |

MPU6050 AD0 stays unconnected and INT is unused. Firmware probes addresses `0x68` and `0x69`.

**Why GPIO10 and not the current GPIO18.** On the C6, GPIO18-23 have their internal pull-up enabled during reset. On the MOSFET, that could press the remote at every ignition. GPIO10 has no reset pull and is not a strapping pin. The resistor is still required, because a floating gate is also unsafe.

**Pins to avoid:** 4, 5, 8, 9 and 15 are strapping pins. 12 and 13 are USB. 16 and 17 are the UART console. 18-23 have the reset pull-up. Pins are compile-time menuconfig options, not web settings, because a wrong pin can press the remote.

### 4.3 Mounting

Any orientation works, because the firmware finds "down" at each start. The unit must not be able to slide or tumble. Wedged or taped is fine, loose is not, because a loose unit can look like driving.

## 5. Behaviour

### 5.1 The rule

> **Press once when the car has moved in the last minute and hears the paired beacon at or above the set point.**

By default the set point is "any", so a single advert from the paired beacon is enough.

### 5.2 Situations

| Situation | What it does |
|---|---|
| Ignition on, driver getting ready in the spot | Nothing, because the car hasn't moved |
| Driver pulls out and the beacon is heard | Presses, aiming for about 2 s after moving starts |
| Weak day, beacon not heard from the spot | Presses when the beacon is first heard on the way |
| Arriving from the street | Presses when the beacon is first heard |
| Stopped at a closed gate | Still presses, because the car moved in the last minute |
| After any press | Quiet until the beacon is gone for 1 minute, or the car is still for 2 minutes |
| Ignition off after a press | The next start begins fresh |
| Moving the car inside the lot | Presses. Accepted, because it is rare and harmless |
| Driving past on the street | Can press. Raising the set point fixes this |
| Motion sensor fails | Falls back to a fixed threshold (5.5), and the LED shows an error |
| No beacon paired | Never auto-presses. Manual presses still work |
| Pairing in progress | Never auto-presses. Manual presses still work |

### 5.3 State machine

States are `UNPAIRED`, `ARMED`, `PRESSING` and `HOLD`. Pairing suspends automatic presses and then returns to the previous state.

- `moved_recently` is true when `now - last_motion <= moved_window_s`. It is false at boot until the first motion.
- `heard` is true for an advert from the paired beacon with `rssi >= set_point`, or any advert when the set point is "any".
- `ARMED` to `PRESSING`: `moved_recently` and `heard` are both true. Reason `auto`.
- `UNPAIRED`, `ARMED` or `HOLD` to `PRESSING`: a manual press from the button or page. Reason `button` or `web`.
- `PRESSING` to `HOLD`: after `press_ms`. Press requests during `PRESSING` are ignored. If no beacon is paired, it returns to `UNPAIRED` instead.
- `HOLD` to `ARMED`, when either of these is true:
  - `now - max(last_advert, press_time) >= gone_s`
  - `now - max(last_motion, press_time) >= still_s`

Both timers count from no earlier than the press. Otherwise a car that stopped before the press could re-arm too soon and press again while driving through the gate. The same goes for a manual press made when the beacon had not been heard.

The state is not saved. After a power cut the unit starts in `ARMED`, or `UNPAIRED` if no beacon is stored, with `moved_recently` false.

### 5.4 Set point

- "Any" is the default and gives the most warning.
- It can be set by double-clicking the button, by "set here" on the page, or by typing a dBm value.
- "Set here" stores the strongest advert of the last 3 s. If nothing was heard in that time, it fails and the page or LED says so.
- Pairing a new beacon resets it to "any".

The set point exists to stop presses when only driving past, and to keep presses within the remote's range (section 11). The trigger does not need it to handle the parking spot, which is the job of the motion check.

### 5.5 Sensor failure fallback

The sensor counts as failed in any of these cases:

- It is not found on the bus.
- Its accelerometer does not read 1 g ±0.3 g at start. This replaces a strict `WHO_AM_I` check, because clone chips return other IDs. The ID read is logged.
- 10 consecutive I²C reads fail.
- All samples are identical for 5 s.

When failed, `moved_recently` is always true and the effective set point is `max(set_point, fallback_dbm)`, with `fallback_dbm` defaulting to `-90`. In `HOLD`, only `gone_s` can re-arm. So the gate still works with today's rule, but without today's repeat presses. As today, a strong day can then press from the parking spot. The LED shows an error and the log records it.

### 5.6 Limits accepted

- If the car stalls right after a press and restarts in range, it can press again, because the unit cannot tell how long the power was off.
- The unit cannot see whether the gate opened. A lost press repeats only after the `still_s` rule, or by hand.
- With the button in a glove box, there is no fast manual press while driving, so keeping another remote within reach is sensible.

### 5.7 Settings

All are editable on the page and stored in NVS. Values are starting points, to be tuned from logs. The API rejects out-of-range values.

| Setting | Default | Range |
|---|---|---|
| `set_point_dbm` | any | any, or -100 to -30 |
| `press_ms` | 2000 | 200-5000 |
| `moved_window_s` | 60 | 10-300 |
| `gone_s` | 60 | 10-600 |
| `still_s` | 120 | 30-1800 |
| `fallback_dbm` | -90 | -100 to -30 |
| `motion_enabled` | on | on/off. Off uses the fallback rule |
| `motion_accel_mg` | 40 | 10-300 |
| `motion_accel_ms` | 800 | 200-3000 |
| `motion_yaw_deg` | 15 | 5-90 |
| `motion_yaw_window_ms` | 3000 | 1000-10000 |
| `pair_min_dbm` | -45 | -80 to -20 |
| `pair_margin_db` | 15 | 3-40 |
| `pair_hold_s` | 2 | 1-10 |
| `pair_timeout_s` | 60 | 10-300 |
| `hotspot_s` | 300 | 0-3600. 0 means only by triple-click or USB |
| `hotspot_ssid` | `YogiGate-XXXX` | 1-32 characters |
| `hotspot_password` | random | 8-63 characters |
| `wifi_tx_dbm` | 8 | 2-20 |
| `led_enabled` | on | on/off |
| `buzzer_enabled` | **off** | on/off |
| `buzzer_type` | passive | passive/active |
| `debug` | off | on/off |

## 6. Motion detection

Runs in `yg_core` with no hardware dependency, so recorded data can be replayed on a PC.

**Input:** MPU6050 at 50 Hz, ±2 g and ±250 °/s, with its digital low-pass filter at about 10 Hz to remove engine vibration.

**Calibration:** at start, the unit waits for 1 s of stillness, then stores the gravity direction and gyro bias. Until then it reports no motion, so the failure mode is a late press, not an early one. If stillness does not come within 10 s, for example because of engine shake or a car already rolling, it calibrates from a 2 s average and detection starts normally. While still, both values keep tracking slowly.

**Each sample,** with `ĝ` as the unit vector of gravity:

- `yaw_rate = gyro · ĝ`, the rotation around the vertical.
- `tilt_rate = |gyro - yaw_rate · ĝ|`, the rotation around anything else.
- `horizontal = |accel - (accel · ĝ) · ĝ|`, low-passed at about 2 Hz.

**Moving is reported when either is true:**

- **Yaw:** the summed yaw over `motion_yaw_window_ms` reaches `motion_yaw_deg`. A car leaving a perpendicular spot must turn, and door slams do not produce this.
- **Acceleration:** `horizontal >= motion_accel_mg` continuously for `motion_accel_ms`, while not tilting.

**Tilting:** when `tilt_rate > 8 °/s`, the acceleration check is ignored for 1 s and gravity re-estimates quickly. This is how opening the glove box, which rotates around a horizontal axis, is kept from looking like driving.

Thresholds are first guesses. Step 1 of the rollout tests them in the real car before anything else is built on top.

## 7. Pairing

A beacon is identified by its MAC address, which must be public or random static. Rotating private addresses, used by phones and watches, are rejected. iBeacon UUIDs are not used for identity because they can be shared between beacons.

| Way to pair | Works by |
|---|---|
| Button, hold 3 s | Auto-pairing rule below |
| Page, "pair by holding close" | Auto-pairing rule below |
| Page, pick from list | Shows nearby iBeacons with live signal, UUID, major and minor |
| Page or USB, type a MAC | For a beacon already on the gate. No trip to the gate needed |

**Auto-pairing rule.** Only iBeacon frames with a stable address count. A beacon is saved when, for `pair_hold_s`:

- Its RSSI is at least `pair_min_dbm`.
- It is at least `pair_margin_db` stronger than any other candidate.

Timing out after `pair_timeout_s` keeps the old beacon. A success saves the beacon and resets the set point. The numeric defaults are checked against the real beacon during bench testing.

**Typed MACs:** the filter list needs an address type, which a typed MAC does not have, so both public and random entries are added.

## 8. Web page, hotspot and API

### 8.1 Hotspot

- It starts at every power-on, after BLE scanning and the motion sensor are running, unless `hotspot_s` is 0.
- It stops when the car first moves, or when `hotspot_s` expires, whichever comes first. It stays on while a client is connected, and stops 30 s after the last client leaves if a stop condition has been met.
- With `debug` on, motion does not stop it.
- Once stopped, it stays off until the next power-on or a triple-click.
- WPA2 with a random 12-character password created at first boot. The USB console shows it, and the page can change it. It never appears in the repository.
- Low transmit power by default, to limit its reach and its peak current.

**Why it stops on motion.** Wi-Fi and BLE share one radio. Espressif rates BLE scanning alongside a hotspot as stable while beaconing, and unstable with a client connecting or connected, on the C6 as on the other chips. Stopping on motion gives BLE the radio as the car pulls out. Results during a drive with a phone connected are only a rough guide.

### 8.2 Page

One HTML file with inline CSS and JavaScript, embedded gzipped, with no external resources because the phone has no internet on the hotspot. Target size is under 64 kB gzipped. Charts are drawn on a `<canvas>`.

| Section | Contents |
|---|---|
| Status | Live signal, adverts per second, motion, state, temperature, sensor state, hotspot time left, firmware version |
| Beacon | Current beacon, pair by holding close, pick from list, type a MAC, forget |
| Settings | Everything in 5.7, with a "set here" button for the set point |
| Test | Press the remote, beep, flash the LED |
| Logs | Trip list, a chart for each trip with motion and press markers, rate a trip, download all, clear |
| System | Firmware upload, reboot, factory reset, debug flag |

### 8.3 API

JSON over HTTP. The page and the PC tools use the same API. Anything that changes state uses POST. The Wi-Fi password is the only authentication, and the hotspot has no internet route.

| Method and path | Purpose |
|---|---|
| `GET /api/status` | Live state, polled at 2 Hz |
| `GET`, `POST /api/settings` | Read, or partly update, settings |
| `POST /api/setpoint` | `{"here":true}`, `{"dbm":-85}` or `{"any":true}` |
| `POST /api/press` | Press the remote |
| `POST /api/test/led`, `/api/test/beep` | Output tests |
| `POST /api/pair/auto`, `/scan`, `/cancel`, `/forget` | Pairing control |
| `GET /api/pair/candidates` | Live candidate list |
| `POST /api/pair/select`, `/api/pair/manual` | Save a beacon from the list, or a typed MAC |
| `GET /api/trips?before=N&limit=M` | Trip summaries, newest first |
| `GET /api/trips/N` | All records of one trip |
| `POST /api/trips/N/rating` | `too_early`, `too_late`, `did_not_open`, `no_reason`, `good` |
| `GET /api/logs/export` | Everything, as streamed NDJSON |
| `POST /api/logs/clear` | Erase logs |
| `POST /api/time` | The browser's time, stored as a log anchor |
| `POST /api/ota` | Upload firmware as a raw binary body |
| `POST /api/reboot`, `/api/factory-reset` | System |
| `POST /api/debug/simulate-motion`, `GET /api/debug/live` | Debug flag only |

Unfitted hardware must never make an endpoint fail. A beep with no buzzer simply does nothing.

### 8.4 Button

Optional in practice, and fitted on this devkit. Every action below is also on the page.

| Gesture | Action |
|---|---|
| Hold 3 s | Pair by holding close |
| Single click | Press the remote |
| Double click | Set point from current signal |
| Triple click | Start the hotspot |

A single click waits 400 ms in case it becomes a double click.

### 8.5 LED and buzzer

| Event | LED | Buzzer, if enabled |
|---|---|---|
| Pressing | White during the press | One short beep |
| Pairing | Fast blue blink | None |
| Pair saved | Green for 2 s | Two beeps |
| Pair timeout or set point rejected | Three red blinks | One long beep |
| Sensor failed | Red double blink every 10 s | None |
| Unpaired | Slow amber blink, first 30 s after boot | None |
| Otherwise | Off | None |

### 8.6 USB console

A small `esp_console` for when Wi-Fi is not available, such as first setup or a forgotten password.

| Command | Action |
|---|---|
| `status` | State, signal, sensor, firmware |
| `wifi` | Show the SSID and password, and start the hotspot |
| `pair <mac>` | Save a beacon by MAC |
| `press` | Press the remote |
| `logs` | Stream the NDJSON export over USB |
| `factory-reset` | Erase settings and logs |

## 9. Logs and debug

### 9.1 What a trip is

A **trip** is one power-on period, numbered in order from a counter in NVS. There is no clock across power cuts. When the page opens, the browser's time is stored as an anchor, so dates in between can be estimated. Power can vanish at any moment, so all logs are append-only and trip summaries are calculated when read.

### 9.2 Records

Trip log records:

| Record | Contents |
|---|---|
| `boot` | Firmware version, reset reason, sensor ID, beacon, settings |
| `first_heard` | Time, RSSI |
| `motion` | Time, cause (yaw or acceleration), strength |
| `press` | Time, reason, RSSI, time since motion, set point |
| `rearm` | Time, reason (gone or still) |
| `hotspot` | On, off, client count |
| `pair`, `setting`, `ota`, `error` | What changed or failed |
| `trace` | Samples, each with strongest RSSI, advert count, peak acceleration, peak yaw rate and flags |
| `temp` | MPU6050 temperature, once a minute |
| `rating` | Owner's rating of a trip |
| `time_anchor` | Browser time, with trip and uptime |
| `env` | Debug only: nearby Wi-Fi networks and BLE devices |

**Trace rate,** using the first rule that matches:

1. Beacon not heard for 30 s and the trip is past its first 2 minutes: no samples.
2. Car still for over 2 minutes: one sample per 30 s.
3. Otherwise: one sample per second.

**Writing:** buffered for at most 5 s. `boot`, `motion` and `press` flush at once, so a power cut loses little. Each record has a length and CRC. A torn record is skipped on read.

### 9.3 Storage

Raw flash partitions used as append-only rings, not a file system. Oldest data is erased first.

| Partition | Size on 8 MB | Holds | Estimate |
|---|---|---|---|
| `triplog` | 2 MB | Records above | Over a year at 4 trips a day and about 1 kB per trip |
| `capture` | about 2.4 MB | Debug captures | About 80 captures at 25-30 kB each |

If the board reads 4 MB of flash, `triplog` becomes 128 kB and `capture` 256 kB, with the rest of the layout unchanged. That is about a month of trips and about 8 captures. A 16 MB board keeps the 8 MB layout.

### 9.4 Debug flag

A web setting that survives restarts. It is off by default and turned on for the first weeks of real use. It adds:

- **Captures** around each motion event and press: raw motion samples at 50 Hz and every advert, from 10 s before to 30 s after.
- **Environment snapshot:** once per trip, 10 s after power-on if the car has not moved, count nearby Wi-Fi networks and BLE devices. It is cancelled if the car moves. This tests the theory that interference causes bad days.
- **Hotspot stays on while moving.**
- **Live raw view** on the page.
- **Simulate motion,** for bench testing.
- **Verbose USB logs.**

### 9.5 Reviewing logs

The unit is offline and Claude only runs when a session is started, so a review goes like this:

1. The owner taps Download on the page, or runs `tools/yg_logs.py fetch` on a laptop connected to the hotspot. That laptop has no internet while connected, so the review itself happens afterwards.
2. The owner gives the file to a Claude session in this repo.
3. Claude runs the analysis and replays the trips through the logic, then proposes changes and builds new firmware.
4. The owner uploads it on the page.

Log files stay out of git because they show when the owner comes and goes. `logs/` is ignored. The format is documented in `docs/log-format.md`.

Questions the first review must answer:

- Did motion ever fire while parked?
- How many seconds of warning does a press give, measured from the press to the signal peak at the gate?
- Do bad days show weaker signal or fewer adverts?
- Does temperature or the environment snapshot line up with bad days?
- Do the owner's ratings agree with the data?

## 10. Firmware structure

Plain C, formatted with the existing `.clang-format`, with a `yg_` prefix for public names. It builds with `idf.py` for `esp32c6`.

```
CMakeLists.txt
sdkconfig.defaults
partitions.csv
main/                app_main, event loop, Kconfig.projbuild (pins)
components/
  yg_core/           pure logic, no ESP-IDF includes
  yg_ble/            NimBLE scanner
  yg_imu/            MPU6050 driver
  yg_io/             remote output, LED, buzzer, button sampling
  yg_settings/       NVS load, validate, save
  yg_log/            ring storage, trace, capture
  yg_net/            hotspot, HTTP server, API, firmware upload
  yg_console/        USB console
web/index.html
test/host/           logic tests on a PC
tools/               yg_logs.py, replay, mock_api.py
docs/log-format.md
README.md            wiring, flashing, first setup
```

### 10.1 Modules

| Module | Does | Depends on |
|---|---|---|
| `yg_core/trigger` | The state machine in 5.3 | Nothing |
| `yg_core/motion` | The detection in section 6 | Nothing |
| `yg_core/pairing` | The auto-pairing rule and candidate table | Nothing |
| `yg_core/gesture` | Turns button level over time into gestures | Nothing |
| `yg_core/record` | Encodes and decodes log records | Nothing |
| `yg_ble` | Passive scan with filter list, or open scan when pairing. Delivers parsed adverts | NimBLE |
| `yg_imu` | Samples at 50 Hz, delivers samples and failures | `i2c_master` |
| `yg_io` | Remote press with a hard time limit, LED patterns, buzzer, button level | GPIO, `led_strip`, LEDC |
| `yg_settings` | Typed settings with defaults and range checks | NVS |
| `yg_log` | Append, read and export, over a replaceable flash backend | `esp_partition` |
| `yg_net` | Hotspot rules, HTTP server, API, upload | Wi-Fi, `esp_http_server`, OTA |
| `yg_console` | The commands in 8.6 | `esp_console` |
| `main` | Wires it together and runs the only decision task | All |

### 10.2 Event flow

Everything posts to one queue: adverts, motion, button gestures, web commands and a 1 Hz tick. One task reads the queue and calls `yg_core`, so decisions never race. Web handlers post a command and wait for its result. Adverts that do not fit in the queue are dropped and counted.

### 10.3 BLE scanning

- NimBLE, with the observer role only.
- In normal use: passive scan, scan window equal to the interval at 100 ms, filter list holding the paired beacon, and duplicate filtering off, so every advert reaches the code with its RSSI.
- The filter list means the radio ignores other devices without waking the CPU.
- A 1 Hz check restarts the scan if it ever stops.
- Only one scan mode runs at a time. Changing the filter list stops the scan first.

To verify at implementation: Espressif's C6 NimBLE example sets `CONFIG_BT_NIMBLE_EXT_ADV=y`, where reports arrive as `BLE_GAP_EVENT_EXT_DISC` instead of `BLE_GAP_EVENT_DISC`. `yg_ble` handles whichever the build uses.

### 10.4 Flash layout

| Name | Type | Offset | Size |
|---|---|---|---|
| `nvs` | data, nvs | `0x9000` | 24 kB |
| `otadata` | data, ota | `0xF000` | 8 kB |
| `phy_init` | data, phy | `0x11000` | 4 kB |
| `ota_0` | app | `0x20000` | 1.75 MB |
| `ota_1` | app | `0x1E0000` | 1.75 MB |
| `triplog` | data, custom | `0x3A0000` | 2 MB |
| `capture` | data, custom | `0x5A0000` | 2.375 MB |

### 10.5 Firmware updates and safety

- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on from the first flash, because the bootloader cannot be changed later over Wi-Fi.
- On the first boot after an update, the hotspot always starts, whatever `hotspot_s` says. The new image must pass a self-check within 30 s of booting: settings readable, BLE scanning and hotspot started. It then marks itself valid. On failure, or if power is lost first, the previous firmware runs. The page tells the user to keep power on for 30 s after an upload.
- The sensor is not part of that check, because a broken sensor already has a fallback and should not block updates.
- Updates are not signed. Accepted, because uploading requires the Wi-Fi password.
- The remote output has one owner, `yg_io`, which enforces the time limit with a one-shot timer.
- The first line of `app_main` drives the output low.
- The task watchdog restarts a stuck unit. Reset reasons are logged.

### 10.6 Removed

`platformio.ini`, `src/main.cpp` and PlatformIO's placeholder folders go away. `.gitignore` is rewritten: it currently ignores `test`, `lib` and `include`, which would hide the new tests. It will ignore `build/`, `sdkconfig`, `sdkconfig.old`, `managed_components/`, `logs/`, `.vscode/` and `.pio/`. The old firmware stays in git history.

## 11. Risks

| Risk | Plan |
|---|---|
| Motion detection confuses door slams, passengers or engine shake with driving | Tested first, in step 1 of the rollout. If it can't be fixed, use the fallback in section 13 |
| The glove box limits the signal too much | Measured in step 1. Improve the beacon side first: a spacer if it sits on metal, more TX power, faster adverts. An external antenna would need a module with a connector, which is a later decision |
| Hearing farther makes street false presses more likely | Raise the set point. Logs and ratings show how often it happens |
| **BLE range outgrows the remote's range.** The remote is in the glove box too, and one press per trip means no retry | Before boosting the beacon, test how far the remote reaches from the closed glove box. The set point keeps presses inside that distance |
| RSSI scale differs between chips, so old numbers may not carry over | `fallback_dbm` is checked on the C6 in step 1 and `pair_min_dbm` in step 2 |
| Clone MPU6050 behaves differently | No reliance on `WHO_AM_I`. Sanity checked by gravity |
| Wi-Fi start at each power-on loads a weak car USB port | Low TX power. Brownouts are logged as reset reasons |

## 12. Testing

**On a PC, without a board** (clang is available on this machine, and CMake and Ninja come with ESP-IDF):

- Every row of the table in 5.2 becomes a test of `trigger`.
- `motion` runs on synthetic traces at first, then on real captures: door slam, tilt, pull-out, gentle creep, engine shake.
- `pairing` covers a single beacon, two close beacons, a phone with a rotating address, and timeout.
- `gesture` covers single, double, triple and hold, with bounce.
- `record` and the log ring cover wrap-around and torn writes, on a fake flash backend.
- `replay` runs a downloaded log through the current logic and reports what would change.
- The page runs in a desktop browser against `tools/mock_api.py`.

**Build:** `idf.py build` for `esp32c6`, with the image fitting its slot.

**On the bench, with a board:** press output and its time limit, no press at power-on (checked over 20 power cycles), pairing all four ways, every page section, firmware upload, rollback using a deliberately bad image, and a power cut during logging followed by reading the log back.

**In the car:** the checks in step 1 of the rollout, then real use.

## 13. Rollout

The D1 mini stays in the car on the old firmware until step 3, so the gate keeps working throughout.

**Step 0: Toolchain.** Install ESP-IDF v6.1 with `winget install Espressif.EIM-CLI` and `eim install -i v6.1`. Build a minimal project for `esp32c6`, flash it, and read the chip and flash size.

**Step 1: Motion and signal test in the car.** This needs the C6, the MPU6050 and USB power only. The remote is not connected, so nothing can be pressed. The firmware logs motion events, captures and adverts to flash, and they are read back over USB afterwards.

| Test | Target |
|---|---|
| Door slams, getting in and out, passenger getting in, engine start, 2 minutes idling, opening and closing the glove box, 50 events in total | At most 1 reports motion |
| Pulling out of the spot, 10 times | At least 9 report motion within about 2 s |
| Adverts heard per minute in the spot, glove box closed, then on the dash | Recorded as the first real numbers |

If motion detection misses these targets, tune it using the captures. If it still fails, switch to the fallback rule designed earlier: press when the signal rises 6 dB above its parked level. The model showed about 1 in 7 starts pressing too early with that rule, so it is the backup, not the plan.

**Step 2: Full firmware.** Build everything in this spec and pass the bench tests.

**Step 3: Into the car.** Move the MOSFET and remote to the C6, adding the pull-down resistor. Turn debug on.

**Step 4: A few weeks of driving,** then a log review following 9.5, tuning and an update over Wi-Fi.

**Step 5: The case,** in a separate spec, designed around this board. Heat matters: a parked car gets hot, so PLA is out.

**Second unit:** same firmware and same steps, with its own password. It pairs to the same beacon by typing the MAC on the page.

## Appendix: why this rule

These numbers come from a throwaway simulation calibrated to the owner's three observations: no limit fires from the spot, `-90` usually does not, and a weak day means very close. That calibration implies about -71 dBm at 1 m, while a beacon in the open is commonly near -59. Treat the numbers as comparisons between ideas, not predictions. Real recordings replace them.

Leaving a spot 13 m from the gate at 9 km/h, with adverts every 300 ms, on the D1 mini's radio:

| Rule | Early press on a normal day | Normal day | 6 dB weaker | 12 dB weaker |
|---|---|---|---|---|
| Today's `-90` | 10% | 5.4 m | 2.1 m | 2.0 m |
| Rise 6 dB above parked level | 15% | 6.4 m | 4.6 m | 2.9 m |
| Moving + heard (perfect sensor assumed) | 0% | 8.6 m | 6.1 m | 3.2 m |

The metre columns give the distance from the gate at the press.

The same rule on each board, including assumed antenna losses for the C3 mini:

| Board | Normal day | 6 dB weaker | 12 dB weaker | No press before the gate on the 12 dB day |
|---|---|---|---|---|
| C6 devkit | 9.3 m | 8.4 m | 5.7 m | 3% |
| D1 mini | 8.6 m | 6.1 m | 3.3 m | 28% |
| C3 mini, 5 dB antenna loss | 8.4 m | 5.6 m | 3.0 m | 37% |
| C3 mini, 10 dB antenna loss | 6.3 m | 3.4 m | 2.6 m | 82% |

Ideas tested and dropped:

- **Pressing when the signal is climbing:** no better than a threshold, and worse on bad days.
- **Calibrating from each gate pass:** helped in the model, but moving + heard removes the problem it solved when leaving. It remains an option for arrivals if logs show the need.
