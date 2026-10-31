# Log format

Logs are exported as NDJSON: one JSON object per line. `GET /api/logs/export`, the page's Download button and `tools/yg_logs.py fetch` all produce this format.

A **trip** is one power-on period. Every record has:

| Field | Meaning |
|---|---|
| `k` | Record kind |
| `trip` | Trip number |
| `t` | Milliseconds since this trip's power-on |

The HTTP export starts with one `meta` line: `format`, `fw`, `current_trip`, `ssid`. A USB export has no `meta` line, and `tools/yg_logs.py fetch` warns if it ends early.

Records are in the order they were written, not strictly by time. A `trace` record is written up to 5 s after its samples, so sort by `t` when building a timeline. Its `t` is the time of its first sample.

There is no clock across power cuts. To place trips in calendar time, use `time_anchor` records, which are written when the page is opened.

## Trip records

| `k` | Fields | Written when |
|---|---|---|
| `boot` | `fw`, `reset`, `sensor_id`, `beacon`, `beacon_type`, `debug`, `set_point`, and the main settings | Power-on |
| `first_heard` | `rssi` | First advert from the beacon in this trip |
| `motion` | `cause` (`yaw` or `accel`), `mg`, `yaw` | Driving detected, at most once per 10 s |
| `press` | `reason` (`auto`, `button`, `web`, `fallback`), `rssi`, `since_motion_ms`, `set_point` | Remote pressed |
| `rearm` | `reason` (`gone`, `still` or `moved`) | Ready to press again. `moved` follows a `fallback` press, once a working sensor sees the car move |
| `hotspot` | `on`, `clients` | Hotspot or client count changed |
| `pair` | `event` (`saved`, `timeout`, `forget`), `mac`, `type` | Pairing result |
| `setting` | `key`, `value` | Setting changed. A password shows as `changed` |
| `ota` | `event` (`uploaded`, `failed`, `confirmed`) | Firmware update |
| `error` | `code`, `detail` | See below |
| `trace` | `period`, `s` | See below |
| `temp` | `c` | Sensor temperature, once a minute |
| `rating` | `rated_trip`, `rating` | The owner rated a trip: `good`, `too_early`, `too_late`, `did_not_open`, `no_reason` |
| `time_anchor` | `epoch` | Browser time in seconds, when the page opened |
| `env` | `ble`, `wifi`, `wifi_best`, `chan` | Debug only: nearby BLE devices, Wi-Fi networks, strongest Wi-Fi RSSI, networks on channels 1 to 13. The Wi-Fi fields are `null` if the scan did not finish, which is not the same as 0 networks |

`reset` is the ESP-IDF `esp_reset_reason_t` value: 1 power-on, 3 software restart, 4 panic, 5 interrupt watchdog, 6 task watchdog, 7 other watchdog, 9 brownout.

Error `code` values: `sensor_absent`, `sensor_gravity`, `sensor_reads`, `sensor_stuck`, `sensor_recovered`, `queue_dropped`, and `sensor_glitch`, an outage too short to count as a failure, with its length in ms as `detail`, at most once per 10 s. Look for these around false motion.

## Trace samples

`s` is a list of `[rssi, adverts, accel_mg, yaw_dps, flags]`, one per `period` seconds.

| Item | Meaning |
|---|---|
| `rssi` | Strongest advert in the period. Ignore it when `adverts` is 0 |
| `adverts` | Adverts heard in the period |
| `accel_mg` | Peak horizontal acceleration, in steps of 10 mg, up to 2550 |
| `yaw_dps` | Peak turn rate in degrees per second, up to 255 |
| `flags` | 1 moved, 2 near miss, 4 tilting |

The period is 1 s during the first 2 minutes of a trip and while the beacon has been heard in the last 30 s. It is 30 s once the car has been still for over 2 minutes. There are no samples once the beacon has been silent for 30 s, after the first 2 minutes.

## Debug captures

These are written only with the debug flag on, to a separate log. They cover about 10 s before and 30 s after a trigger.

| `k` | Fields |
|---|---|
| `cap_start` | `cap` (capture ID), `cause` (`motion`, `press`, `near_miss`, `manual`), `pre` (samples held from before the trigger). `t` is the trigger time |
| `cap_imu` | `cap`, `first` (sample index), `n`, `b64` |
| `cap_adv` | `cap`, `n`, `b64` |
| `cap_end` | `cap`, `samples`, `adverts`. `t` is when it ended. Missing if the power went first |

`cap_imu` data decodes to `n` samples at 50 Hz, each six little-endian signed 16-bit values: `ax ay az gx gy gz`. Acceleration has 16384 counts per g, and rotation has 131 counts per degree per second. Sample number `i` was taken at `t + (i - pre) * 20 ms`, using `t` and `pre` from `cap_start`, where `i` is `first` plus the position within the record.

`cap_adv` data decodes to `n` adverts, each a little-endian unsigned 32-bit time in milliseconds followed by a signed 8-bit RSSI.

## Questions for a log review

1. Did motion ever fire while parked? Look at captures with cause `motion` that are not followed by a real drive, and at owner ratings of `too_early` or `no_reason`.
2. How much warning does a press give? `tools/yg_logs.py summary` shows the time from press to peak signal.
3. Do bad days show weaker signal or fewer adverts? Compare parked signal, peak signal and adverts per second across trips. The summary's adverts per second covers only the seconds when the beacon was heard, so a long drive does not lower it. A trip that never reached a 1 s trace period shows `-`.
4. Does temperature or the `env` snapshot line up with bad days?
5. Do the owner's ratings agree with the data?

`tools/yg_logs.py replay` answers "what would this change have done?". It starts from the settings in the trip's `boot` record, follows `setting` and sensor `error` records, and uses raw captures where there are any and the trace elsewhere. The trace holds motion only as logged, at most once per 10 s, so raw captures give the better answer.
