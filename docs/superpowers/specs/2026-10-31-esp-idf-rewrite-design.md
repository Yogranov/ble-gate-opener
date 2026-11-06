# YogiGate 2: ESP-IDF Rewrite Design

**Date:** 2026-10-31
**Status:** Approved by the owner and implemented on the `esp-idf-rewrite` branch
**Scope:** Firmware and wiring. The case gets its own spec after this firmware has run in the car.
**Amended 2026-11-01, by the owner:** the buzzer is on by default and answers people, with its own sounds. The hotspot starts only on request. The default password is `password`. The page is simple by default, with an Advanced mode. It has live motion graphs, Reset to defaults and Reset logs alongside factory reset, shows the logo, and carries the technician manual on a Help tab. These replace the earlier text below, where they differ.

**Amended 2026-11-02, by the owner:** holding the button for 15 s is a factory reset, so a unit can start from scratch in the car, with no phone.

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

- The unit and the remote sit in the car's glove box, on a phone charger in a socket that turns off with the ignition. A car screen's USB port is not good enough (section 11).
- The beacon is on a parking lot gate. The owner's parking spot is 13 m from it.
- With no RSSI limit, the gate opened from the spot right after ignition, before the driver was ready. `-90 dBm` usually prevents that, but leaves almost no margin: on bad days the car must get very close.
- A second unit may go to someone whose spot is very close to the gate. It must work without per-spot tuning and can share the same beacon.
- The owner wants to set it and forget it. Nothing may sound on a normal trip: the buzzer answers people, and reports faults.
- The repository is public, so no passwords or logs may be committed.

## 3. Goals and non-goals

**Goals**

1. Never press because the car started. Press once when the car is really leaving or arriving.
2. Keep working on bad radio days as far as physics allows.
3. Need no code changes to pair a beacon, tune behaviour or set up a second unit.
4. Every setting and action is on the web page, except starting the hotspot, which takes the button or USB because the page needs the hotspot.
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

The owner's board reads as ESP32-C6 (QFN40) revision 0.2 with 16 MB of flash. It keeps the 8 MB layout, per section 9.3. It is marked MUSE LAB, and has the same GPIOs as the official board but a different size and pin order.

### 4.2 Wiring

| Function | GPIO | Hole | Notes |
|---|---|---|---|
| Remote MOSFET gate | 10 | Q11 | **Through 220 Ω, with 10 kΩ from gate to GND** |
| Buzzer (optional) | 11 | Q12 | Active buzzer, + to the pin and − to GND. On by default, safe to leave unfitted |
| MPU6050 SDA | 2 | G2 | The module has its own I²C pull-ups |
| MPU6050 SCL | 3 | G3 | |
| MPU6050 VCC | 3V3 | Q2 | |
| MPU6050 GND | GND | G1 | |
| Button | 9 | on-board BOOT | |
| External button (optional) | 7 | Q7 | Tactile button on the board: Q7 to R16, and U18 to GND. Works the same as the on-board button |
| RGB LED | 8 | on board | Addressable, driven with `espressif/led_strip` |

MPU6050 AD0 stays unconnected and INT is unused. Firmware probes addresses `0x68` and `0x69`.

Part choices and the hole-by-hole layout are in [hardware/](../../../hardware/), which is also where the holes above come from.

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

- `moved_recently` is true when `now - last_motion <= moved_window_s` and that motion has not been used up. A press uses up all motion before it, and so does re-arming. It is false at boot until the first motion.
- `heard` is true for an advert from the paired beacon with `rssi >= set_point`, or any advert when the set point is "any".
- `ARMED` to `PRESSING`: `moved_recently` and `heard` are both true. Reason `auto`.
- `UNPAIRED`, `ARMED` or `HOLD` to `PRESSING`: a manual press from the button or page. Reason `button` or `web`.
- `PRESSING` to `HOLD`: after `press_ms`. Press requests during `PRESSING` are ignored. If no beacon is paired, it returns to `UNPAIRED` instead.
- `HOLD` to `ARMED`, when either of these is true:
  - `now - max(last_advert, press_time) >= gone_s`
  - `now - max(last_motion, press_time) >= still_s`

Both timers count from no earlier than the press. Otherwise a car that stopped before the press could re-arm too soon and press again while driving through the gate. The same goes for a manual press made when the beacon had not been heard.

Using up motion means that a car that has not moved since re-arming stays quiet, however `moved_window_s`, `still_s` and `gone_s` are set, so no combination of them needs rejecting. A car that moves and then stops in earshot still presses within `moved_window_s`, as it must at a closed gate.

The state is not saved. After a power cut the unit starts in `ARMED`, or `UNPAIRED` if no beacon is stored, with `moved_recently` false.

### 5.4 Set point

- "Any" is the default and gives the most warning.
- It can be set by double-clicking the button, by "set here" on the page, or by typing a dBm value.
- "Set here" stores the strongest advert of the last 3 s, or 10 s while the hotspot is on, when fewer adverts are heard. If nothing was heard in that time, it fails and the page or LED says so.
- Pairing a new beacon resets it to "any".

The set point exists to stop presses when only driving past, and to keep presses within the remote's range (section 11). The trigger does not need it to handle the parking spot, which is the job of the motion check.

The first real drive added a third reason: pressing too early. That gate stays open for 15 s. "Any" pressed about 10 s before the car reached it on the way out and about 17 s before on the way in, by which time it would have been closing. Replaying those logs, -85 dBm presses about 4 s and 8 s before, and it also drops a press from the parking spot at -94 dBm, caused by someone handling the unit. So the first unit runs at -85. "Any" stays the default until more drives show what suits a gate in general.

### 5.5 Sensor failure fallback

A brief fault must not hand control to the fixed threshold, because that can press a parked car. So the sensor counts as failed only in these cases:

- It has not been found on the bus for 5 s. The unit keeps looking 5 times a second, so a slow start costs nothing.
- Its accelerometer does not read 1 g ±0.3 g during its first 5 s. This replaces a strict `WHO_AM_I` check, because clone chips return other IDs. The ID read is logged.
- No read has worked for 5 s. A sensor that has reset sleeps and reads all zeros, which counts as a failed read.
- All samples are identical for 5 s.

Nothing is final. A power glitch can reset the sensor into sleep, where reads still work, so the unit reconfigures it after a run of failed reads and after 1 s of identical samples, and waits for it to wake. After 5 s of good samples the sensor counts as working again. A repeated sample counts for neither side, because the sensor's clock is not the poller's and a slow one repeats itself now and then. After any gap in the samples the motion estimate treats the unit as just moved (section 6), because it may have been while nothing was watching. An outage too short to count as a failure is logged with its length, at most once per 10 s, because it can explain false motion in a log.

When failed, `moved_recently` is always true and the effective set point is `max(set_point, fallback_dbm)`, with `fallback_dbm` defaulting to `-90`. So the gate still works with today's rule, but without today's repeat presses. A press made this way can't know whether the car has moved, so `still_s` does not re-arm after it, even if the sensor recovers, and a sensor that keeps dropping out cannot keep pressing. `gone_s` still re-arms, and so does motion from a working sensor at least `still_s` after that press, so that a driver leaving the spot still gets a press. As today, a strong day can then press from the parking spot. The LED shows an error and the log records it.

### 5.6 Limits accepted

- If the car stalls right after a press and restarts in range, it can press again, because the unit cannot tell how long the power was off.
- The unit cannot see whether the gate opened. A lost press repeats only after the `still_s` rule, or by hand.
- With the button in a glove box, there is no fast manual press while driving, so keeping another remote within reach is sensible.
- The web server handles one request at a time. A press tapped on the page during a log download, firmware upload or log erase waits for that to finish, which can take a minute or more. Automatic presses do not wait.
- Records logged while the logs are being erased are lost.

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
| `hotspot_s` | 0 | 0-3600. 0 means only by triple-click or USB. A requested hotspot lasts this long, or 300 s when it is 0 |
| `hotspot_ssid` | `YogiGate-XXXX` | 1-32 characters |
| `hotspot_password` | `password` | 8-63 characters |
| `wifi_tx_dbm` | 8 | 2-20 |
| `led_enabled` | on | on/off |
| `buzzer_enabled` | on | on/off |
| `buzzer_type` | active | passive/active |
| `debug` | off | on/off |

## 6. Motion detection

Runs in `yg_core` with no hardware dependency, so recorded data can be replayed on a PC.

**Input:** MPU6050 at 50 Hz, ±2 g and ±250 °/s, with its digital low-pass filter at about 10 Hz to remove engine vibration.

**Calibration:** at start, the unit takes whole-second averages of both sensors, in which engine shake cancels out. As soon as two in a row agree, normally after 2 s, it stores them as the gravity direction and gyro bias. A passenger getting in only delays this, and a slow lean can hide at most about 0.3 °/s in it. Until then it reports no motion, so the failure mode is a late press, not an early one. A car already rolling may never agree, so after 20 s it takes the latest average anyway.

**The saved gyro zero:** that last average holds the car's own turning. On the first real drive, the unit started just before the car pulled out and took a zero about 6 °/s off, and it then reported motion however still it sat, in 22 of 25 truly still seconds. So the unit saves a zero it can trust, and at the 20 s limit it uses the saved zero with the latest average's gravity. Replaying that drive's raw data this way gives 8 of 25, and the same with a saved zero 0.5 °/s off. This suits the sensor: on the first day the real zero stayed within 0.3 °/s from 26 to 48 °C. Factory reset erases it, and Reset to defaults keeps it, as it belongs to the sensor.

A wrong saved zero can be worse than none. A sensor swapped for another could report motion in a parked car with someone moving about in it, and press, and a big enough error holds the tilt check on and hides a real pull-out. So:

- The first steady start saves its zero. After that, a 30 s rest replaces it if it has moved by 0.5 °/s or more, and a steady start replaces it if it is 2 °/s or more off around the level axes. Two seconds can agree in a steady bend, but that only hides turning around the vertical.
- At the 20 s limit, the saved zero is used only if the average of the whole start agrees with it within 3 °/s around the level axes, which covers over 99% of real starts' seconds. Otherwise the unit takes the latest average, as it did before.
- A zero over 40 °/s, or not a number, is neither used nor saved. The datasheet allows 20.

What remains accepted: after a swap, an error of 1.5 to 3 °/s gets past these until the next steady start or rest. A 30 s rest in a long smooth bend can save that bend's turning, around the vertical only, which does no harm below about 5 °/s, and the next real rest replaces it.

**Keeping them right:** a real sensor has offsets, scale errors and a gyro zero that moves as it warms up. The accelerometer threshold is only about 2.3° of tilt, so errors must not be able to reach it.

- **Gravity turns with the gyro,** so that a lean leaves no error behind, however slowly it happens.
- **The accelerometer corrects gravity over 3 s.** So the offset and scale errors after a big turn stay under the threshold, as long as the offset is within the datasheet's 50 mg. While above the acceleration threshold it takes 30 s, so that short braking does not look like a tilt once the car has stopped. Long hard braking can still report motion for up to 30 s after the stop, which does no harm. Faster than 3 s would hide more of a slowly rising pull-out, and slower would let these errors through.
- **The gyro's zero is followed once a second,** from that second's average, in which engine shake cancels out. An average under 1 °/s is taken as drift, and followed over 10 s, and the error stays under the threshold while it fades.
- **After 30 s at rest,** gravity and bias are set again from the sensor, which repairs anything else, such as calibrating in a bend. Rest means that the whole-second averages of both sensors stayed put, which also works with the engine shaking. Steady rotation around the vertical looks the same, so a long smooth bend, such as a garage ramp, is taken for bias. That shows as false yaw for up to 30 s after it ends, with the car just having moved anyway.

An earlier version also compared the gyro with the accelerometer once a second, to catch a wrong zero sooner. It took real moves for gyro errors, which caused false motion, so it is gone. A zero that really jumps by more than 1 °/s, which nothing is known to cause, shows as false motion until the next rest.

**A known weakness:** a simulated random shake of 2 to 3 °/s, kept up for 3 minutes, fired the acceleration check in 12% to 33% of 200 runs, as the gyro's noise slowly turns gravity away. A parked car in earshot at the set point would then be pressed. At 1.5 °/s it fired about once in 1500 runs. The other side of it is a missed press: from about 3 °/s the shake holds the tilt check on, so the acceleration check goes deaf, and at 4 °/s and above a 120 mg pull-out was not seen at all, though yaw can still catch the turn. A real car's still seconds, by each second's gyro standard deviation on its worst axis, measured about 0.2 °/s, 0.3 at the 90th percentile, under 1 at the 99th and about 2 at worst, and about 6 minutes of real stops showed no false motion beyond the tail of braking. So nothing changes for it yet, a test holds the rate down, and logs should keep an eye on it.

**Each sample,** with `ĝ` as the unit vector of gravity:

- `yaw_rate = gyro · ĝ`, the rotation around the vertical.
- `tilt_rate = |gyro - yaw_rate · ĝ|`, the rotation around anything else.
- `horizontal = |accel - (accel · ĝ) · ĝ|`, low-passed at about 2 Hz.

**Moving is reported when either is true:**

- **Yaw:** the summed yaw over `motion_yaw_window_ms` reaches `motion_yaw_deg`. A car leaving a perpendicular spot must turn, and door slams do not produce this.
- **Acceleration:** `horizontal >= motion_accel_mg` continuously for `motion_accel_ms`, while not tilting. Any dip below the threshold starts the time again, so that a small lean and a door shutting cannot add up.

**Tilting:** when `tilt_rate > 8 °/s`, the acceleration check is ignored for 1 s and gravity re-estimates over 1 s, or as long as it takes to come within the threshold. A gap of over 100 ms in the samples does the same, at most once per 5 s, because the unit may have moved meanwhile. Opening the glove box is kept from looking like driving this way. A door's shove during the hold barely counts, as it is over so soon.

Thresholds are first guesses. Step 1 of the rollout tests them in the real car before anything else is built on top. All of the above makes a pull-out need about 50 mg in practice, and one rising very slowly is seen late. If gentle ones are missed, try a lower threshold, or letting short dips through. A missed one only waits for the first turn, which yaw catches. A clone with twice the datasheet's offset can report motion from a glove box opened just too slowly to count as tilting, at 6 to 8 °/s.

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

- It starts only when asked, by a triple-click or the USB `wifi` command, because it is rarely needed and should not run on every trip. It answers with three short beeps and a cyan LED, because the LED cannot be seen through the case. Setting `hotspot_s` above 0 makes it also start at every power-on, as it used to.
- It does not wait for the motion sensor, so the page can still report a sensor that is missing.
- A requested hotspot ignores motion, and stops when its time runs out. It stays on while a client is connected, and stops 30 s after the last client leaves if its time has run out.
- A hotspot started by `hotspot_s` also stops when the car first moves, unless `debug` is on.
- Once stopped, it stays off until the next request.
- WPA2 with the default password `password`, which the page can change on its own page. Being public is accepted, because the hotspot is only on after someone presses the button. While it is on, anyone in range who knows the password can press the remote or upload firmware, which is unsigned.
- Low transmit power by default, to limit its reach and its peak current.

**The scan gives way to Wi-Fi.** While the hotspot is on, the scan listens for 25 ms in every 100 ms, and at full time otherwise. It changes before the hotspot starts, so that a phone can join. Measured on the real unit with a phone on the page, which normally makes 2 requests a second:

| Scan | Page requests a second | Adverts heard a second |
|---|---|---|
| None | 2.1 | 0 |
| Open, 25 ms in 100 | 1.7 | 3 |
| Open, 50 ms in 100 | 0.7 | 6 |
| Open, 100 ms in 100 | 0 | 12 |
| Filtered, 100 ms in 100, hearing nothing | 0 | 0 |

So the cause is radio time and not the load of handling adverts. Before this, a paired unit's page did not work at all, because a paired unit always scans. The phone stays joined and recovers as soon as the scan gives way, so the unit sees no disconnect. The cost is that a press with the hotspot on can come about a second later, because a quarter of the adverts are heard. With the hotspot off, nothing changes.

**Why it stops on motion.** Wi-Fi and BLE share one radio. Espressif rates BLE scanning alongside a hotspot as stable while beaconing, and unstable with a client connecting or connected, on the C6 as on the other chips. Stopping on motion gives BLE the radio as the car pulls out. Results during a drive with a phone connected are only a rough guide.

### 8.2 Page

One HTML file with inline CSS and JavaScript, embedded gzipped, with no external resources because the phone has no internet on the hotspot. Target size is under 64 kB gzipped. Charts are drawn on a `<canvas>`.

| Section | Contents |
|---|---|
| Status | State, live signal, live motion graphs, firmware version, press the remote. Advanced adds adverts per second, temperature and hotspot time left |
| Beacon | Current beacon, pair by holding close, pick from list, type a MAC, forget |
| Settings | The set point with a "set here" button, buzzer, Change password, and the Advanced mode switch. Advanced adds everything else in 5.7, and Reset to defaults |
| Logs | Trip list, a chart for each trip with motion and press markers, rate a trip, download all. Advanced adds Reset logs |
| System | Beep, restart. Advanced adds Flash the LED, each sound, firmware upload, factory reset and the debug tools |
| Help | The technician manual, in case the printed one is gone: setup, each sound with a Play button, the button, and what to do when something goes wrong |

Advanced mode only changes what the page shows, so it is stored in the browser and not on the unit. Each phone starts simple.

Reset to defaults puts everything in 5.7 back, with the hotspot name worked out again from the unit's address, and keeps the beacon, logs and trip count. Each change is logged as an ordinary `setting` record, so a replay follows it. Factory reset stays what its name says, and erases the beacon and logs too. A flash keeps stored settings, so this is how a unit picks up a changed default.

Live motion shows two graphs, each with a line at what counts as moving: the horizontal push against `motion_accel_mg`, and the summed turn against `motion_yaw_deg`. The page polls twice a second and the sensor runs at 50 Hz, so the status holds the strongest values of the last half second or more.

### 8.3 API

JSON over HTTP. The page and the PC tools use the same API. Anything that changes state uses POST. The Wi-Fi password is the only authentication, and the hotspot has no internet route.

Every POST must carry the header `X-YogiGate: 1`. A phone with cellular data alongside the hotspot could otherwise be made to press the remote by a page on another site, which a browser will not let add a custom header. POST bodies must be JSON objects, nested at most 8 levels, which is checked before parsing because the parser recurses. `/api/ota` is the exception, with a raw body.

An upload that goes silent is abandoned after 30 s, so a phone dropping off Wi-Fi cannot hold up the web server or stop the hotspot from turning off.

| Method and path | Purpose |
|---|---|
| `GET /api/status` | Live state, polled at 2 Hz |
| `GET`, `POST /api/settings` | Read, or partly update, settings |
| `POST /api/settings/reset` | Put every setting back to its default |
| `POST /api/setpoint` | `{"here":true}`, `{"dbm":-85}` or `{"any":true}` |
| `POST /api/press` | Press the remote. 409 while already pressing, or on the RF variant before a remote is learned |
| `POST /api/rf/learn`, `/forget` | RF variant only: listen for the gate remote until a stable code is heard or the pair timeout ends, or forget the learned one |
| `POST /api/test/led`, `/api/test/beep` | Output tests. They work whatever the settings say. A beep can name a sound, as `{"sound":"error"}` |
| `POST /api/pair/auto`, `/scan`, `/cancel`, `/forget` | Pairing control |
| `GET /api/pair/candidates` | Live candidate list |
| `POST /api/pair/select`, `/api/pair/manual` | Save a beacon from the list, or a typed MAC |
| `GET /api/trips?before=N&limit=M` | Trip summaries, newest first. `limit` counts trip numbers, and some numbers hold nothing, so `next_before` says where to go on from, or 0 at the end |
| `GET /api/trips/N` | All records of one trip |
| `POST /api/trips/N/rating` | `too_early`, `too_late`, `did_not_open`, `no_reason`, `good` |
| `GET /api/logs/export` | Everything, as streamed NDJSON |
| `POST /api/logs/clear` | Erase the logs, and make the running trip number 1. Takes up to about a minute on full logs. There is no restart, because that would turn off the hotspot the request came through |
| `POST /api/time` | The browser's time, stored as a log anchor |
| `POST /api/ota` | Upload firmware as a raw binary body |
| `POST /api/reboot`, `/api/factory-reset` | System |
| `GET /api/system` | Log usage, free memory, running firmware slot, remote pin level |
| `POST /api/debug/simulate-motion`, `/api/debug/capture-now`, `GET /api/debug/live` | Debug flag only |

Unfitted hardware must never make an endpoint fail. A beep with no buzzer simply does nothing.

### 8.4 Button

The only way to start the hotspot without USB. Every other action below is also on the page.

| Gesture | Action |
|---|---|
| Hold 3 s | Pair by holding close |
| Hold 15 s | Factory reset: erase the settings, beacon and logs, then restart |
| Single click | Press the remote |
| Double click | Set point from current signal |
| Triple click | Start the hotspot |
| Double-click then hold 6 s | Learn the gate remote (RF variant only) |

A single click waits 400 ms in case it becomes a double click. On the RF variant, two clicks then a 6 s hold starts learning the gate remote, while a plain 3 s hold still pairs a beacon. After two clicks, a third press over 1 s is that hold, and letting go of it before 6 s does nothing, rather than counting as a third click that would start the hotspot. On the MOSFET build the learn gesture is refused with two rattles.

A 15 s hold passes through the 3 s one, so two short beeps sound on the way and pairing starts, and letting go early leaves only that. A button down since power-on does not count towards the 15 s, because a stuck button would otherwise wipe the unit at every start. It still starts pairing, as it always has. The erase runs in the decision task once the sound has ended, so nothing can press the remote or save a setting before the restart. The next start has no beacon, so its one long beep says the reset is done.

### 8.5 LED and buzzer

| Event | LED | Buzzer, if enabled |
|---|---|---|
| Automatic press | White during the press | None |
| Press by hand, from the button or page | White during the press | One short |
| Pairing, or learning the gate remote (RF variant) | Fast blue blink | Two short, when holding close or learning starts |
| Hotspot requested | Cyan for 2 s | Three short |
| Pair saved, set point set here, gate remote learned or forgotten, or settings reset to defaults | Green for 2 s | Short, short, long |
| Pair or learn timeout, set point rejected, a remote that cannot be learned (once per learn, as a busy band can bring many; a burst too short to be a remote earns none), a press by hand on an RF unit with no remote learned, or the learn gesture on a unit without the radio | Three red blinks | Two rattles |
| Sensor failed | Red double blink every 10 s | Three long, at most once per trip, and not if the sensor is turned off |
| Unpaired | Slow amber blink, first 30 s after boot | One long, at power-on |
| Factory reset, from a 15 s hold | Magenta until the restart | Five short, one long |
| Otherwise | Off | None |

**The LED is for developers.** It is on the C6 inside the case, so nobody fitting or driving with the unit sees it. The manual and the page's Help tab leave it out, its switch and test are in Advanced mode, and a technician goes by the sounds. It keeps working, for debugging with the case open, and the atlas explains each pattern.

The buzzer answers people and reports faults, and stays silent on a normal trip. An active buzzer has one fixed tone, so each sound is a rhythm, set in `yg_core/sound`. The steps are multiples of the buzzer task's 20 ms, and a rattle is 40 ms on and off.

### 8.6 USB console

A small `esp_console` for when Wi-Fi is not available, such as first setup or a forgotten password.

| Command | Action |
|---|---|
| `status` | State, signal, sensor, firmware, remote pin level |
| `wifi` | Show the SSID and password, and start the hotspot |
| `pair <mac>` | Save a beacon by MAC |
| `press` | Press the remote |
| `set <key> <value>` | Change a setting |
| `scan start`, `list`, `stop` | List nearby iBeacons |
| `devices [seconds]` | List every BLE device heard, to find a MAC |
| `motion` | Simulate motion, debug flag only |
| `logs [nocapture]` | Stream the NDJSON export over USB |
| `reboot`, `factory-reset` | Restart, or erase settings and logs |

Bench commands, because the owner's PC has no Wi-Fi and so cannot reach the hotspot:

| Command | Action |
|---|---|
| `api <METHOD> <path> [body]` | HTTP request to the unit's own server over loopback. `NOGUARD` as the method is a POST without the guard header |
| `ota-self [corrupt\|fail\|abandon]` | Uploads the running image to the unit's own `/api/ota`. `corrupt` damages it, which must be refused. `fail` makes the new boot fail its self-check, which must roll back. `abandon` goes silent half way, which must be given up on |
| `log-fill <sectors>` | Fills both logs with dummy records, to test a full log |
| `log-clear` | Erases the logs, and reports the time taken and the longest the decision task was held up |
| `brownout` | Restarts with the reset reason a brownout leaves, to test what follows one. It cannot test a real sag. Debug flag only |
| `wifi-join <ssid> <password>` | Joins a LAN until the next restart. Nothing is stored |

## 9. Logs and debug

### 9.1 What a trip is

A **trip** is one power-on period, numbered in order from a counter in NVS, which Reset logs and a factory reset start again from 1. There is no clock across power cuts. When the page opens, the browser's time is stored as an anchor, so dates in between can be estimated. Power can vanish at any moment, so all logs are append-only and trip summaries are calculated when read.

A start that dies at the radio's first surge has taken its number and logged nothing. So a gap in the numbers before a `boot` record with reset reason 9 counts the failed starts, and `tools/yg_logs.py summary` reports them.

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
| `gyro_zero` | The gyro zero this trip started with and how it got it, and any that a long rest replaced |
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

Raw flash partitions used as append-only rings, not a file system. Oldest data is erased first. A restart finds where to go on writing by looking for blank flash, so the last byte of every record is never 0xFF. Erasing the logs holds the lock for one sector at a time, so that decisions are not held up.

| Partition | Size on 8 MB | Holds | Estimate |
|---|---|---|---|
| `triplog` | 2 MB | Records above | At 4 trips a day, about 6 months of 20-minute trips at 2.8 kB each, and from 2.5 months for 40-minute trips to a year for 10-minute ones. Calculated from real record sizes, and it matched the bench logs. Motion records vary most, so real trips will settle it |
| `capture` | about 2.4 MB | Debug captures | About 90 captures at 25-30 kB each. The first real day held 92, nearly all from driving far from the gate, until section 9.4's rule limited them to where they help |

If the board reads 4 MB of flash, `triplog` becomes 128 kB and `capture` 256 kB, with the rest of the layout unchanged. That is about a month of trips and about 8 captures. A 16 MB board keeps the 8 MB layout.

### 9.4 Debug flag

A web setting that survives restarts. It is off by default and turned on for the first weeks of real use. It adds:

- **Captures** around each motion event and press: raw motion samples at 50 Hz and every advert, from 10 s before to 30 s after. Motion and near misses only start one where the trace is sampling, in the first 2 minutes of a trip and while the beacon has been heard in the last 30 s. A press or a manual capture always does. Before this rule, one hour's driving overwrote the first pull-out and press.
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

**The first review, after one real day:**

- **Motion while parked:** yes, from three causes. A wrong gyro zero (section 6, now fixed), the tail after braking (section 6, accepted), and someone handling the unit. No press came at ignition, or away from the beacon.
- **Warning:** with "any", 18.5 s and 23 s from press to signal peak, and more usefully about 10 s and 17 s from press to reaching the gate. That was too early for a gate open for 15 s (section 5.4).
- **Pull-outs:** detected about 4 s after the car moved, against the 2 s target. The drive from the spot to the gate takes about 15 s, so this leaves enough time, and it stays as it is.
- **Signal:** -69 to -74 dBm at the gate and -85 to -96 at the spot, with 1 to 3 adverts a second near the gate. Bad days need more days. The hottest reading was 48 °C, after a parked hour.
- **Turn rates:** 48 minutes of real driving peaked at 53 °/s, while handling reached 358 °/s. A check could tell them apart, but a closed glove box already stops the handling, so it has not been added.
- **The gate did not open at all:** the remote's battery was low. Each press is counted as an opening for the timing above, but the remote's reach from the glove box is still untested (section 11).
- **Power:** the biggest finding. See section 11.
- **Ratings:** none yet.

## 10. Firmware structure

Plain C, formatted with the existing `.clang-format`, with a `yg_` prefix for public names. It builds with `idf.py` for `esp32c6`.

```
CMakeLists.txt
sdkconfig.defaults
partitions.csv
main/                app_main, event loop, Kconfig.projbuild (pins)
components/
  yg_core/           pure logic, no ESP-IDF includes
  yg_app/            the decision task and its queue
  yg_ble/            NimBLE scanner
  yg_imu/            MPU6050 driver
  yg_io/             remote output, LED, buzzer, button sampling
  yg_settings_nvs/   stores settings in NVS
  yg_log/            log storage on flash partitions
  yg_net/            hotspot, HTTP server, API, firmware upload
  yg_console/        USB console
web/index.html
test/host/           logic tests on a PC, and the replay program
test/bench/          checks that run on a real unit over USB
tools/               yg_serial.py, yg_logs.py, mock_api.py
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
| `yg_core/record`, `ring` | Encodes and decodes log records, and stores them in flash sectors | Nothing |
| `yg_core/trace`, `trips`, `capture` | Trace samples, trip summaries and debug captures | Nothing |
| `yg_core/hotspot_policy`, `sensor_health`, `settings` | The hotspot rules, sensor trust and the settings table | Nothing |
| `yg_core/sound` | The rhythm of each buzzer sound | Nothing |
| `yg_core/engine` | Ties all of the above together, and reaches hardware only through an ops table | Nothing |
| `yg_ble` | Passive scan with filter list, or open scan when pairing. Delivers parsed adverts | NimBLE |
| `yg_imu` | Samples at 50 Hz, delivers samples and failures | `i2c_master` |
| `yg_io` | Remote press with a hard time limit, LED patterns, buzzer, button level | GPIO, `led_strip`, LEDC |
| `yg_settings_nvs` | Loads and saves the settings table, the beacon and the trip counter | NVS |
| `yg_log` | Binds the ring to flash, adds locking, and exports | `esp_partition` |
| `yg_app` | The one decision task, its queue, and the firmware self-check | `yg_core`, hardware layers |
| `yg_net` | Hotspot rules, HTTP server, API, upload | Wi-Fi, `esp_http_server`, OTA |
| `yg_console` | The commands in 8.6 | `esp_console` |
| `main` | Starts everything, with the remote output first | All |

### 10.2 Event flow

Everything posts to one queue: adverts, motion, button gestures, web commands and a 1 Hz tick. One task reads the queue and calls `yg_core`, so decisions never race. Web handlers post a command and wait for its result. Adverts and motion samples that do not fit in the queue are dropped and counted. Rarer events, button gestures and hotspot client counts, wait up to 200 ms for room, because losing one matters more.

### 10.3 BLE scanning

- NimBLE, with the observer role only.
- In normal use: passive scan, scan window equal to the interval at 100 ms, or 25 ms while the hotspot is on (section 8.1), filter list holding the paired beacon, and duplicate filtering off, so every advert reaches the code with its RSSI.
- The filter list means the radio ignores other devices without waking the CPU.
- A 1 Hz check restarts the scan if it ever stops.
- Only one scan mode runs at a time. Changing the filter list stops the scan first.
- Starting the radio is thought to draw the most current of anything the unit does. After a brownout restart, the unit waits 1 s before starting it. On that start `CONFIG_ESP_PHY_REDUCE_TX_POWER` also caps transmit power at 13 dBm. The scan does not transmit and the hotspot defaults to 8 dBm, so neither changes, and only the radio's start-up could gain. That is not measured, so it should get no credit for anything a car test shows.

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
- On the first boot after an update, the hotspot always starts, whatever `hotspot_s` says. The new image must pass a self-check within 30 s of booting: settings store readable, BLE scanning and hotspot started. It then marks itself valid. On failure, or if power is lost first, the previous firmware runs. The page tells the user to keep power on for 30 s after an upload.
- The sensor is not part of that check, because a broken sensor already has a fallback and should not block updates.
- Updates are not signed. Accepted, because uploading requires the Wi-Fi password.
- The remote output has one owner, `yg_io`, which enforces the time limit with a one-shot timer.
- The first line of `app_main` drives the output low.
- The task watchdog restarts a stuck unit. Reset reasons are logged. A USB export gives way every 64 records, because writing to USB never waits, and a full export used to end in this restart after 10 s.

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
| **Weak power ends a start at the radio's first surge.** This is real: on the first car day, on a USB port of the car's multimedia screen, 4 of 6 power-ups lost 15 to 117 starts first, about half a second each, and none of 54 starts on a PC did. Once up, the unit ran for 30 minutes without a reset. A test in the car on firmware 2.0.1 then compared that port, the screen's other port and a good phone charger, over 14 starts. On the screen's ports, both starts from the ignition switch failed first, 1 and 2 times, 5 replugs of a port that was already on came up clean, and 3 engine starts lost 0, 0 and 7. On the charger, 4 starts lost none, and only one of them was an engine start. The log has no clock, so which start used which source is read from the test's order, the unit's pose and its engine vibration. The screen's ports took seconds to rise and fall, the charger's did not, and the sensor module's LED flickered on the ports. No voltage was measured, so the slow rise is the likely cause, not a proven one | **Power the unit from a phone charger, not a screen's USB port.** The 1 s wait after a brownout (section 10.3) stays. The planned test of it, about a third as many failures, could not prove it: each attempt now takes about 1.5 s instead of 0.5 s, so a port that stays weak for a set time gets a third as many attempts anyway. It stays because a good supply never pays for it, since it only runs after a brownout, and because every failed start writes the trip counter to flash. The summary tool counts failed starts, so drives on the charger keep testing this. A 470 µF capacitor was not tried, and is not needed unless failures continue on the charger, because it could not bridge a rise lasting seconds |

## 12. Testing

**On a PC, without a board:** `python test/host/run_tests.py`, which needs only clang or gcc.

- Every row of the table in 5.2 becomes a test of `trigger`.
- `motion` runs on synthetic traces at first, then on real captures: door slam, tilt, pull-out, gentle creep, engine shake.
- `pairing` covers a single beacon, two close beacons, a phone with a rotating address, and timeout.
- `gesture` covers single, double, triple and hold, with bounce.
- `record` and the log ring cover wrap-around and torn writes, on a fake flash backend.
- `replay` runs a downloaded log through the current logic and reports what would change.
- `test/bench/page_check.py` runs the page in headless Edge against `tools/mock_api.py`, with no unit.

**Build:** `idf.py build` for `esp32c6`, with the image fitting its slot.

**On the bench, with a board, over USB:**

- `test/bench/api_check.py`: every API endpoint, including the press output and its time limit.
- `test/bench/ota_check.py`: a damaged image refused, an abandoned upload given up on, an update confirmed, a failed self-check rolled back, and a reset before confirming rolled back.
- `test/bench/log_check.py`: a full export over USB reaching its end with no restart, full logs erased without holding up decisions, and a factory reset on full logs. It erases the settings too.
- `test/bench/soak_check.py`: repeated restarts with no press, the wait after a simulated brownout, the debug snapshot, and hotspot and scan cycles watching for crashes and memory leaks.

**Needs a person:** a phone on the real hotspot, the LED and button by eye and hand, pairing a real beacon by holding it close, and the MOSFET with its resistor over real power cycles, with the sensor on the same power so that it cold starts too.

**In the car:** the checks in step 1 of the rollout, then real use.

## 13. Rollout

The D1 mini stays in the car on the old firmware until step 3, so the gate keeps working throughout.

**Step 0: Toolchain.** Install ESP-IDF v6.1 with `winget install Espressif.EIM-CLI` and `eim install -i v6.1`. Build a minimal project for `esp32c6`, flash it, and read the chip and flash size.

**Step 1: Motion and signal test in the car.** This needs the C6, the MPU6050 and USB power only. The remote is not connected, so a press only toggles an unconnected pin. It uses the full firmware from step 2 with debug on, which logs motion events, captures and adverts to flash, and they are read back over USB afterwards.

| Test | Target |
|---|---|
| Door slams, getting in and out, passenger getting in, engine start, 2 minutes idling, opening and closing the glove box, 50 events in total | At most 1 reports motion |
| Pulling out of the spot, 10 times | At least 9 report motion within about 2 s |
| Adverts heard per minute in the spot, glove box closed, then on the dash | Recorded as the first real numbers |

If motion detection misses these targets, tune it using the captures. If it still fails, switch to the fallback rule designed earlier: press when the signal rises 6 dB above its parked level. The model showed about 1 in 7 starts pressing too early with that rule, so it is the backup, not the plan.

**Step 2: Full firmware.** Build everything in this spec and pass the bench tests. This was done before step 1, at the owner's request, so step 1 can use it.

**Step 3: Into the car.** Move the MOSFET and remote to the C6, adding the pull-down resistor. Turn debug on. First done on 2 November 2026: the presses came when they should, but the remote's battery was low, so the gate did not open. Section 9.5 has what that day showed.

**Step 4: A few weeks of driving,** then a log review following 9.5, tuning and an update over Wi-Fi.

**Step 5: The case,** in a separate spec, designed around this board. Heat matters: a parked car gets hot, so PLA is out.

**Second unit:** same firmware and same steps. It pairs to the same beacon by typing the MAC on the page.

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
