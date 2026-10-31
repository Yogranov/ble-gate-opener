# YogiGate 2 Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the PlatformIO Arduino firmware with the ESP-IDF v6.1 firmware for ESP32-C6 described in `docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md`.

**Architecture:** All decisions live in `components/yg_core`, pure C with no ESP-IDF includes, so they run under test on a PC. `yg_engine` composes them and talks to hardware only through an ops table. Thin components wrap NimBLE, I²C, GPIO, NVS, flash and Wi-Fi. One task reads one queue and calls the engine.

**Tech Stack:** C17, ESP-IDF v6.1, NimBLE, `esp_http_server`, `espressif/led_strip`, vanilla JS page, Python tools, clang for host tests.

**Scope note:** interfaces and test cases are spelled out here. Function bodies are written during execution, driven by the tests and the spec section named in each task.

---

## Bench constraints found while planning

- The C6 is on **COM3**, native USB Serial/JTAG. MAC `E8:F6:0A:F8:0F:00`.
- This PC is Ethernet-only and has no Wi-Fi adapter, so it cannot join the hotspot. API tests run through a USB console command that makes HTTP requests to the unit's own server over loopback. A RAM-only `wifi-join` console command lets the C6 join a LAN when real-network tests from this PC are wanted.
- The car tests need the owner. The full firmware doubles as the step 1 test firmware: with the remote disconnected, a press only toggles an unconnected pin.

## File map

```
CMakeLists.txt  sdkconfig.defaults  partitions.csv  .gitignore  README.md
main/           CMakeLists.txt  Kconfig.projbuild  main.c
components/yg_core/include/  yg_types.h yg_settings.h yg_trigger.h yg_motion.h yg_sensor_health.h
                             yg_ibeacon.h yg_pairing.h yg_gesture.h yg_record.h yg_ring.h
                             yg_trace.h yg_trips.h yg_hotspot_policy.h yg_capture.h yg_engine.h
components/yg_core/          one .c per header, CMakeLists.txt
components/yg_settings_nvs/  NVS load and save, trip counter, password   (spec name: yg_settings)
components/yg_io/            remote output, LED, buzzer, button
components/yg_imu/           MPU6050
components/yg_ble/           NimBLE scanner
components/yg_log/           rings on esp_partition, mutex, export
components/yg_net/           hotspot, HTTP API, firmware upload, embedded page
components/yg_console/       USB console
web/index.html
test/host/      CMakeLists.txt  test_*.c  fake_flash.c  replay.c
tools/          yg_serial.py  yg_logs.py  mock_api.py
docs/log-format.md
```

`yg_settings` in the spec becomes two parts: the pure table in `yg_core` and the NVS binding in `yg_settings_nvs`, so validation runs on a PC.

---

## Task 0: Toolchain and board

- [ ] Wait for `eim install -i v6.1 -t esp32c6` to finish.
- [ ] Find the activation script under `C:\Espressif\tools` and confirm `idf.py --version` reports v6.1.
- [ ] Run `esptool --port COM3 flash-id`. Expected: ESP32-C6, flash size. If it is not 8 MB, apply the sizes in spec 9.3.

## Task 1: Skeleton project replaces PlatformIO

**Files:** delete `platformio.ini`, `src/main.cpp`. Create `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`, `main/CMakeLists.txt`, `main/main.c`. Rewrite `.gitignore`.

- [ ] Write `partitions.csv` from spec 10.4.
- [ ] Write `sdkconfig.defaults`: target esp32c6, 8 MB flash, custom partitions, rollback enabled, NimBLE observer only, coexistence, USB Serial/JTAG console, task watchdog panic.
- [ ] `idf.py set-target esp32c6 && idf.py build`. Expected: image builds and fits `ota_0`.
- [ ] `idf.py -p COM3 flash`, then read the boot banner with `tools/yg_serial.py`.
- [ ] Commit `build: replace PlatformIO with ESP-IDF v6.1 skeleton`.

## Task 2: Host test harness and settings table

**Files:** `test/host/CMakeLists.txt`, `test/host/test_settings.c`, `components/yg_core/yg_settings.c`, `include/yg_settings.h`, `include/yg_types.h`.

```c
typedef enum { YG_SET_I8, YG_SET_U8, YG_SET_U16, YG_SET_BOOL, YG_SET_STR, YG_SET_ENUM } yg_setting_type_t;
typedef struct {
    const char *key;      /* JSON name, as in spec 5.7 */
    const char *nvs;      /* NVS key, at most 15 characters */
    yg_setting_type_t type;
    uint16_t offset, size;
    int32_t min, max, def;
    bool any_allowed;     /* set_point_dbm only: 0 means "any" */
} yg_setting_desc_t;

void yg_settings_defaults(yg_settings_t *s);
const yg_setting_desc_t *yg_settings_table(size_t *count);
const yg_setting_desc_t *yg_settings_find(const char *key);
bool yg_settings_set_int(yg_settings_t *s, const char *key, int32_t value);
bool yg_settings_set_str(yg_settings_t *s, const char *key, const char *value);
int  yg_settings_to_json(const yg_settings_t *s, char *buf, size_t len, bool include_password);
```

Tests:
- Defaults equal every value in spec 5.7, with the buzzer off and debug off.
- Every range in spec 5.7 rejects min-1 and max+1 and accepts min and max.
- `set_point_dbm` accepts 0 as "any".
- Password length rejects 7 and 64, and accepts 8 and 63. SSID rejects empty.
- JSON output parses and contains metadata (min, max, type) for the page. The password is left out when `include_password` is false.
- Every `nvs` key is at most 15 characters and unique.

- [ ] Write the tests, confirm they fail, implement, confirm they pass, commit.

Commands used by every core task:

```powershell
cmake -S test/host -B build-host -G Ninja -DCMAKE_C_COMPILER=clang
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

## Task 3: Trigger state machine (spec 5.2, 5.3, 5.5)

```c
typedef enum { YG_TRIG_UNPAIRED, YG_TRIG_ARMED, YG_TRIG_PRESSING, YG_TRIG_HOLD } yg_trig_state_t;
typedef enum { YG_PRESS_NONE, YG_PRESS_AUTO, YG_PRESS_BUTTON, YG_PRESS_WEB, YG_PRESS_FALLBACK } yg_press_reason_t;
typedef enum { YG_REARM_NONE, YG_REARM_GONE, YG_REARM_STILL } yg_rearm_reason_t;

void yg_trigger_init(yg_trigger_t *t, bool paired);
void yg_trigger_set_paired(yg_trigger_t *t, bool paired);
void yg_trigger_set_suspended(yg_trigger_t *t, bool suspended);
void yg_trigger_set_sensor_ok(yg_trigger_t *t, bool ok);
void yg_trigger_on_motion(yg_trigger_t *t, uint32_t now);
yg_press_reason_t yg_trigger_on_advert(yg_trigger_t *t, const yg_settings_t *s, uint32_t now, int8_t rssi);
yg_press_reason_t yg_trigger_manual(yg_trigger_t *t, uint32_t now, yg_press_reason_t reason);
yg_rearm_reason_t yg_trigger_tick(yg_trigger_t *t, const yg_settings_t *s, uint32_t now);
```

Tests, one per row of spec 5.2 plus:
- No press at boot on advert, with `moved_recently` false.
- Press on advert within the window after motion, none after the window expires.
- Set point: -85 ignores -90, accepts -85.
- During `PRESSING`, adverts and manual requests return `NONE`.
- `HOLD` re-arms on `gone_s` and on `still_s` measured from `max(last_motion, press_time)`.
- The spec's closed-gate case: motion, 59 s stopped, press, then motion at press+70 s does not press again.
- Manual press from `UNPAIRED` returns to `UNPAIRED`.
- Suspended: no auto press, manual still works.
- Fallback when `sensor_ok` is false or `motion_enabled` is off: no motion needed, the effective set point is `max(set_point, fallback)`, reason `FALLBACK`, and only `gone_s` re-arms.
- Time wrap: `now` near `UINT32_MAX` behaves.

- [ ] Tests, fail, implement, pass, commit.

## Task 4: Motion detection (spec 6)

```c
typedef struct { float ax, ay, az, gx, gy, gz; } yg_imu_sample_t;
typedef enum { YG_MOTION_NONE, YG_MOTION_YAW, YG_MOTION_ACCEL } yg_motion_cause_t;
typedef struct {
    yg_motion_cause_t cause;
    bool near_miss, tilting, calibrated;
    float horizontal_mg, yaw_sum_deg;
} yg_motion_out_t;

void yg_motion_init(yg_motion_t *m);
void yg_motion_update(yg_motion_t *m, const yg_settings_t *s, uint32_t now, const yg_imu_sample_t *in, yg_motion_out_t *out);
void yg_motion_take_peaks(yg_motion_t *m, uint16_t *accel_mg, uint16_t *yaw_dps);
```

Tests use synthetic 50 Hz traces, each in two sensor orientations and with a 10 °/s gyro bias:
- Still: no motion over 60 s, and calibration completes within about 1 s.
- Noise and engine shake (±0.02 g, ±1.5 °/s, 25 Hz): no motion, and calibration falls back at 10 s.
- Pull-out: 0.1 g for 2 s reports `ACCEL` within 1 s.
- Slow creep: 0.03 g gives no `ACCEL`.
- Turn: 20 °/s around vertical for 1 s reports `YAW`.
- Door slam: 0.3 g decaying over 0.2 s: no motion, `near_miss` true.
- Passenger: 1.5° roll in 0.5 s, then held: no motion.
- Glove box: 45° rotation around a horizontal axis in 1 s, then held in the new attitude: no motion, and gravity re-settles.
- Already moving at boot: calibrates by 10 s and later turns are detected.

- [ ] Tests, fail, implement, pass, commit.

## Task 5: Sensor health (spec 5.5)

```c
typedef enum { YG_SENSOR_STARTING, YG_SENSOR_OK, YG_SENSOR_FAILED } yg_sensor_state_t;
typedef enum { YG_SENSOR_ERR_NONE, YG_SENSOR_ERR_ABSENT, YG_SENSOR_ERR_GRAVITY, YG_SENSOR_ERR_READS, YG_SENSOR_ERR_STUCK } yg_sensor_err_t;
void yg_sensor_health_init(yg_sensor_health_t *h);
void yg_sensor_health_absent(yg_sensor_health_t *h);
void yg_sensor_health_on_sample(yg_sensor_health_t *h, uint32_t now, const yg_imu_sample_t *s);
void yg_sensor_health_on_read_failed(yg_sensor_health_t *h);
```

Tests: good start gives `OK`; 0.5 g at start gives `ERR_GRAVITY`; 10 consecutive failures, but not 9, give `ERR_READS`; identical samples for 5 s give `ERR_STUCK`; recovery after 250 good, changing samples; `absent` stays failed.

- [ ] Tests, fail, implement, pass, commit.

## Task 6: iBeacon parsing and pairing (spec 7)

```c
typedef struct { bool valid; uint8_t uuid[16]; uint16_t major, minor; int8_t tx_power; } yg_ibeacon_t;
bool yg_ibeacon_parse(const uint8_t *adv, size_t len, yg_ibeacon_t *out);
bool yg_addr_is_stable(const yg_addr_t *a);
bool yg_addr_parse(const char *text, uint8_t out[6]);
void yg_addr_format(const uint8_t addr[6], char out[18]);

typedef enum { YG_PAIR_IDLE, YG_PAIR_AUTO, YG_PAIR_LIST } yg_pair_mode_t;
typedef enum { YG_PAIR_RESULT_NONE, YG_PAIR_RESULT_SAVED, YG_PAIR_RESULT_TIMEOUT } yg_pair_result_t;
void yg_pairing_start(yg_pairing_t *p, yg_pair_mode_t mode, uint32_t now);
void yg_pairing_cancel(yg_pairing_t *p);
void yg_pairing_on_advert(yg_pairing_t *p, uint32_t now, const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib);
yg_pair_result_t yg_pairing_tick(yg_pairing_t *p, const yg_settings_t *s, uint32_t now, yg_addr_t *winner);
int  yg_pairing_candidates_json(const yg_pairing_t *p, uint32_t now, char *buf, size_t len);
```

Tests:
- Parses the real frame format with UUID `fda50693-a4e2-4fb1-afcf-c6eb07647825`. Rejects other company IDs, and truncated data.
- `c5:b0:...` random is stable, `45:...` random is not, public is stable.
- Auto: a single close beacon saves after `pair_hold_s`. Two close beacons within the margin never save. A close beacon with a distant one saves. Too weak never saves. Rotating addresses and non-iBeacons are ignored. Timeout returns `TIMEOUT`. Dropping below the threshold restarts the hold.
- List mode never saves by itself. Candidates JSON is sorted by signal and drops stale entries.

- [ ] Tests, fail, implement, pass, commit.

## Task 7: Button gestures (spec 8.4)

```c
typedef enum { YG_GESTURE_NONE, YG_GESTURE_SINGLE, YG_GESTURE_DOUBLE, YG_GESTURE_TRIPLE, YG_GESTURE_HOLD } yg_gesture_t;
void yg_gesture_init(yg_gesture_detector_t *g);
yg_gesture_t yg_gesture_update(yg_gesture_detector_t *g, uint32_t now, bool pressed);
```

Tests: single fires 400 ms after release; double and triple; hold fires once at 3 s and the release adds no click; 10 ms bounce is ignored; a floating unfitted button (never pressed) gives nothing.

- [ ] Tests, fail, implement, pass, commit.

## Task 8: Records and ring (spec 9.2, 9.3)

Flash record: `A5 type len 00 | trip u32 | uptime u32 | payload | crc16`, padded to 4 bytes, payload at most 240 bytes. Sector: 4096 bytes with a 16-byte header `magic u32, seq u32, first_trip u32, crc16, pad`.

```c
typedef struct { uint8_t type, len; uint32_t trip, uptime_ms; uint8_t payload[YG_RECORD_MAX_PAYLOAD]; } yg_record_t;
size_t yg_record_encode(const yg_record_t *r, uint8_t *out);
bool   yg_record_next(const uint8_t *sector, size_t size, size_t *pos, yg_record_t *out);
int    yg_record_to_json(const yg_record_t *r, char *buf, size_t len);
/* one builder per record type in spec 9.2, for example: */
void yg_rec_press(yg_record_t *r, uint32_t trip, uint32_t now, uint8_t reason, int8_t rssi, uint32_t since_motion, int8_t set_point);

typedef struct {
    int (*read)(void *ctx, uint32_t off, void *buf, uint32_t len);
    int (*write)(void *ctx, uint32_t off, const void *buf, uint32_t len);
    int (*erase)(void *ctx, uint32_t off);
    void *ctx;
    uint32_t size;
} yg_ring_flash_t;
int  yg_ring_open(yg_ring_t *r, const yg_ring_flash_t *f);
int  yg_ring_append(yg_ring_t *r, const yg_record_t *rec);
int  yg_ring_clear(yg_ring_t *r);
uint32_t yg_ring_sectors_used(const yg_ring_t *r);
bool yg_ring_sector_at(const yg_ring_t *r, uint32_t age, uint32_t *sector, uint32_t *first_trip);
int  yg_ring_read_sector(const yg_ring_t *r, uint32_t sector, uint8_t *buf);
```

Tests on `fake_flash.c`, which models NOR flash (writes AND bits, erase sets 0xFF, a cut-after-N-bytes option):
- Round trip of every record type, and JSON for each parses and carries its fields.
- CRC detects a flipped bit.
- Append and read back in order across sectors.
- Wrap: oldest sector goes first, order stays correct over several wraps.
- Reopen continues at the same place with the next sequence number.
- A torn record is skipped, and later records in the sector still read.
- A torn sector header and a half-erased sector do not break open or read.
- `clear` empties the ring.

- [ ] Tests, fail, implement, pass, commit.

## Task 9: Trace and trip summaries (spec 9.1, 9.2)

```c
void yg_trace_init(yg_trace_t *t, uint32_t now);
void yg_trace_on_advert(yg_trace_t *t, uint32_t now, int8_t rssi);
void yg_trace_on_motion(yg_trace_t *t, uint32_t now);
void yg_trace_on_peaks(yg_trace_t *t, uint16_t accel_mg, uint16_t yaw_dps);
bool yg_trace_tick(yg_trace_t *t, uint32_t now, uint32_t trip, yg_record_t *out);

void yg_trip_summary_init(yg_trip_summary_t *s, uint32_t trip);
void yg_trip_summary_add(yg_trip_summary_t *s, const yg_record_t *r);
int  yg_trip_summary_to_json(const yg_trip_summary_t *s, char *buf, size_t len);
```

Tests:
- The three rate rules in spec 9.2, in order. One sample per second in the first 2 minutes with no beacon. None afterwards. One per 30 s after 2 minutes still. Back to 1 Hz on motion.
- Records flush at 40 samples, or within 5 s of the first buffered sample.
- A summary built from a scripted trip gives first heard, first motion, press count, parked signal, peak and its time, adverts, duration, rating and warning time.

- [ ] Tests, fail, implement, pass, commit.

## Task 10: Hotspot policy (spec 8.1)

```c
void yg_hotspot_init(yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now, bool force_on);
void yg_hotspot_on_motion(yg_hotspot_policy_t *h, uint32_t now);
void yg_hotspot_set_clients(yg_hotspot_policy_t *h, uint32_t now, uint8_t clients);
void yg_hotspot_request(yg_hotspot_policy_t *h, uint32_t now);
bool yg_hotspot_tick(yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now);  /* desired state */
```

Tests: on at start; off on motion with no client; stays on with a client, then off 30 s after the client leaves; off at `hotspot_s`; `hotspot_s` of 0 stays off until requested; debug ignores motion but not the timer; off stays off until a request; a manual or forced start ignores motion and uses the timer.

- [ ] Tests, fail, implement, pass, commit.

## Task 11: Capture (spec 9.4) and engine

```c
typedef struct {
    void (*press)(void *ctx, uint32_t ms);
    void (*led)(void *ctx, yg_led_event_t e);
    void (*buzz)(void *ctx, yg_buzz_event_t e);
    void (*log)(void *ctx, const yg_record_t *r);
    void (*capture)(void *ctx, const yg_record_t *r);
    void (*hotspot)(void *ctx, bool on);
    void (*scan)(void *ctx, yg_scan_mode_t mode, const yg_beacon_t *beacon);
    void (*save_beacon)(void *ctx, const yg_beacon_t *b);
    void (*save_settings)(void *ctx);
    void (*env_start)(void *ctx);
    void *ctx;
} yg_engine_ops_t;

void yg_engine_init(yg_engine_t *e, const yg_engine_ops_t *ops, yg_settings_t *s, const yg_beacon_t *b, const yg_boot_info_t *boot, uint32_t now);
void yg_engine_on_advert(yg_engine_t *e, uint32_t now, const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib);
void yg_engine_on_imu(yg_engine_t *e, uint32_t now, const yg_imu_sample_t *s, float temp_c);
void yg_engine_on_imu_failed(yg_engine_t *e, uint32_t now);
void yg_engine_on_gesture(yg_engine_t *e, uint32_t now, yg_gesture_t g);
void yg_engine_on_clients(yg_engine_t *e, uint32_t now, uint8_t clients);
void yg_engine_on_env(yg_engine_t *e, uint32_t now, const yg_env_t *env);
void yg_engine_tick(yg_engine_t *e, uint32_t now);
bool yg_engine_press(yg_engine_t *e, uint32_t now, yg_press_reason_t reason);
bool yg_engine_setpoint_here(yg_engine_t *e, uint32_t now);
void yg_engine_setpoint_dbm(yg_engine_t *e, uint32_t now, int8_t dbm);
void yg_engine_pair_start(yg_engine_t *e, uint32_t now, yg_pair_mode_t mode);
void yg_engine_pair_cancel(yg_engine_t *e, uint32_t now);
bool yg_engine_pair_set(yg_engine_t *e, uint32_t now, const uint8_t addr[6], uint8_t type);
void yg_engine_pair_forget(yg_engine_t *e, uint32_t now);
void yg_engine_simulate_motion(yg_engine_t *e, uint32_t now);
void yg_engine_capture_now(yg_engine_t *e, uint32_t now);
void yg_engine_settings_changed(yg_engine_t *e, uint32_t now, const char *key);
void yg_engine_rate_trip(yg_engine_t *e, uint32_t now, uint32_t trip, uint8_t rating);
void yg_engine_time_anchor(yg_engine_t *e, uint32_t now, uint32_t epoch_s);
int  yg_engine_status_json(const yg_engine_t *e, uint32_t now, char *buf, size_t len);
```

Scenario tests drive the whole engine with a fake ops table and check presses, scan mode, LED events, buzzer events, hotspot state and logged records:
- Every row of spec 5.2 end to end, including the 12 situations by name.
- The owner's reported failure: boot in the spot with the beacon heard for 60 s gives no press, and pulling out gives exactly one.
- The buzzer never sounds with `buzzer_enabled` off, and `led_enabled` off suppresses LED events.
- All four gestures, and every action also works by engine call with no gesture.
- Pairing: starting sets open scan and suspends presses. Success saves the beacon, resets the set point to "any" and returns to filtered scan. Timeout keeps the old beacon.
- A typed MAC saves with type "any", so both types go on the filter list.
- Set point "here" uses the strongest advert of the last 3 s, and fails with nothing heard.
- Sensor failure logs an error, gives the LED event and uses fallback.
- Debug: capture records appear around motion and press, start 10 s back, end after 30 s; near-miss captures rate-limit; none with debug off. Env scan starts 10 s after boot only if not moved, and cancels on motion.
- First heard logs once per trip. Motion logs are limited to one per 10 s.

- [ ] Tests, fail, implement, pass, commit.

## Task 12: Replay tool

**Files:** `test/host/replay.c`.

Reads lines `A,t,rssi`, `M,t`, `I,t,ax,ay,az,gx,gy,gz`, `T,t` and `key=value` overrides. Prints `PRESS,t,reason`, `REARM,t,reason`, `MOTION,t,cause`.

- [ ] Add a test that feeds a small script and compares output. Commit.

## Task 13: Hardware components

Each is built with `idf.py build`.

- [ ] `yg_settings_nvs`: `yg_nvs_init`, `yg_nvs_load(settings, beacon)`, `yg_nvs_save_changed`, `yg_nvs_save_beacon`, `yg_nvs_next_trip`, password generated once with `esp_fill_random`, default SSID from MAC. Commit.
- [ ] `yg_io`: `yg_io_remote_init()` drives GPIO low first and uses input-output mode so the level can be read back, `yg_io_press(ms)` clamps to 200-5000 and uses a one-shot `esp_timer`, LED pattern task on `led_strip`, buzzer on LEDC, button task at 20 ms calling `yg_gesture_update`. Commit.
- [ ] `yg_imu`: `i2c_master` bus on SDA 2, SCL 3. Probe 0x68 then 0x69. Registers: `PWR_MGMT_1=0x01`, `SMPLRT_DIV=19`, `CONFIG=5`, `GYRO_CONFIG=0`, `ACCEL_CONFIG=0`. Burst read 14 bytes from `0x3B` at 50 Hz. Temperature = raw/340 + 36.53. Commit.
- [ ] `yg_ble`: NimBLE init, `yg_ble_scan(mode, beacon)`, adverts delivered by callback, both `DISC` and `EXT_DISC`, filter list via `ble_gap_wl_set`, passive with window equal to interval at 100 ms, duplicates off, `yg_ble_ensure_scanning()`, open-scan address count for the env snapshot. Commit.
- [ ] `yg_log`: `esp_partition` binding of `yg_ring`, mutex, `yg_log_export(cb)`, `yg_log_trip_records(trip, cb)`, `yg_log_trip_list(before, limit, cb)`. Commit.

## Task 14: Main loop and console

**Files:** `main/main.c`, `components/yg_console/`, `tools/yg_serial.py`.

- [ ] Event queue, decision task, `yg_app_call` for running a function in the decision task, 100 ms tick, 1 Hz scan check, watchdog subscription.
- [ ] Console commands from spec 8.6, plus `api <METHOD> <path> [body]` for loopback HTTP tests and `wifi-join <ssid> <pass>`, RAM only, for LAN tests.
- [ ] Flash. Check on hardware:
  - Boot banner, then `status`.
  - The sensor is detected, or the failed state is reported.
  - Adverts arrive: open scan lists nearby devices, and a typed MAC of a stable nearby device is heard through the filter.
  - With debug on, simulated motion plus an advert causes one press. Pin readback confirms high during the press and low after, within the limit.
  - No press after 20 resets.
- [ ] Commit.

## Task 15: Hotspot, HTTP API, firmware upload

**Files:** `components/yg_net/`.

- [ ] Wi-Fi AP with low TX power, client count events, `yg_net_hotspot(bool)`. Start and stop `httpd` with it.
- [ ] Every endpoint in spec 8.3, with commands run through `yg_app_call`. Check real JSON through the `api` console command.
- [ ] Streamed NDJSON export, first line metadata. Captures are base64.
- [ ] `POST /api/ota` with `OTA_WITH_SEQUENTIAL_WRITES`, delayed restart, self-check and rollback per spec 10.5.
- [ ] Env snapshot: Wi-Fi scan in APSTA mode plus BLE count.
- [ ] Flash, run every endpoint through `api`, commit.

## Task 16: Web page

**Files:** `web/index.html`, `tools/mock_api.py`, CMake rule that gzips and embeds it.

- [ ] Build all sections in spec 8.2. Settings render from the metadata in `GET /api/settings`. Trip chart on canvas with motion and press markers. Rating, download, firmware upload, debug. Phone-first layout, dark and light.
- [ ] Run against `mock_api.py` in headless Edge and inspect screenshots at phone width. Check that the mock's responses match real ones captured through `api`.
- [ ] Confirm gzipped size under 64 kB. Commit.

## Task 17: Tools and docs

- [ ] `tools/yg_logs.py`: `fetch --port COM3` over USB, `fetch --host` over Wi-Fi, `summary`, `replay`. Test against a real export from the board.
- [ ] `docs/log-format.md`, `README.md` with wiring, flashing, first setup, second unit, log review.
- [ ] Commit.

## Task 18: Final verification

- [ ] Clean rebuild, all host tests, flash.
- [ ] Bench list from spec 12 that can run here. Record which items need the owner: real phone on the hotspot, firmware upload over Wi-Fi unless `wifi-join` is available, the external pull-down, 20 real power cycles with the MOSFET fitted, and the car tests.
- [ ] Code review on the full diff. Fix findings.
- [ ] Report what was verified and what was not.
