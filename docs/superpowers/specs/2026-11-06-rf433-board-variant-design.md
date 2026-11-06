# RF433 board variant: learn and replay a fixed-code remote — design

**Date:** 2026-11-06
**Branch:** `rf433-board-variant` (off `master` at `2174d18`)
**Status:** Design. No hardware in hand yet. Nothing proven on a bench or a gate.
**Firmware base:** 2.0.1

## 1. Intent

Add a second car-unit board variant. The brain is unchanged: a beacon on the gate plus
motion in the car decide *when* to open. Only the actuator changes. Instead of a MOSFET
pressing the button of a wired physical remote, a CC1101 433 MHz transceiver transmits a
fixed-code signal that the unit learned once from the owner's own remote.

One codebase, one `yg_core`, two build-time firmware images. A unit is either a MOSFET
unit (today's board) or an RF unit (this variant), chosen at build time.

## 2. Scope and non-goals

In scope: learning one fixed-code (OOK) remote that the owner physically has, and
replaying it to open the owner's own gate.

Explicit non-goals, stated here because the repo is public:

- No defeating of rolling codes or KeeLoq. The learn step refuses anything whose bursts
  are not identical, which is exactly what a rolling code is not.
- No jamming, no code scanning, no brute force.
- No capturing of remotes the owner does not possess.

The learned code is the owner's own gate code, which their handheld remote already
broadcasts. It lives only in the unit's NVS. It is never committed and never leaves the
unit in a downloadable log, same as the existing privacy rule for logs.

## 3. The seam this builds on

The engine never touches hardware directly. It opens the gate through one function
pointer, `yg_engine_ops_t.press(ctx, ms)` (see `components/yg_core/include/yg_engine.h`),
bound in `components/yg_app/yg_app.c` by `op_press` to `yg_io_press(ms)`. The decision of
*when* lives entirely in `components/yg_core/yg_trigger.c`.

So the RF variant is a second actuator behind the same `press` call. `yg_trigger.c` and
the engine's press logic do **not** change. This preserves the rule that all decisions
live in `yg_core` and are tested on a PC.

## 4. Decisions

| Decision | Choice | Why |
|---|---|---|
| RF chip | CC1101 | Purpose-built sub-GHz OOK transceiver. One SPI module learns (RX) and opens (TX). The SX1268 the owner had is a LoRa/FSK part with no documented OOK mode, poor for raw OOK learn/replay. |
| Learn strategy | Raw capture and replay, with a stability check | Works for any OOK fixed-code remote without knowing its chip. The stability check doubles as the rolling-code guard. |
| Packaging | Two build-time variants, one codebase | `yg_core` stays identical; each unit runs the image for its board; the page and status report which. |
| Capture/replay mechanism | CC1101 async OOK mode + the C6 RMT peripheral | RMT timestamps edges on receive and plays edge durations on transmit. The LED already uses RMT; other channels are free. |
| Learned-code storage | NVS blob, mirrors the beacon | Survives Reset to defaults, wiped by Factory reset. |
| Learn entry point | The page, Advanced mode only | Matches "every setting and action has a place on the page" and "new things are advanced". No new button gesture. |
| New user settings | None if avoidable | Carrier frequency is fixed at 433.92 MHz unless the HackRF capture shows otherwise; if it must vary it becomes one Advanced setting. |
| Sounds / LED | Reuse the pairing-style saved/rejected cues | Avoids new sounds. The LED is developer-only now, so learn feedback is primarily the buzzer. |

## 5. Architecture

### 5.1 Variant selection

A new Kconfig choice `YG_GATE_DRIVER` in `main/Kconfig.projbuild`, values `MOSFET`
(default) and `RF433`. It selects which hardware component compiles and which actuator
`op_press` binds to. `yg_core` compiles the same either way.

`/api/status` reports the active driver and, on RF, whether a code is learned, so a unit
self-identifies and the page can show the right controls.

### 5.2 The actuator, generalised

`press(ctx, ms)` keeps its meaning "actuate the gate for `ms` milliseconds".

- MOSFET build: pulse the pin for `ms` (unchanged `yg_io_press`).
- RF build: transmit the stored OOK frame, repeated with its inter-frame gap, for `ms`.
  A held remote behaves the same way, so `press_ms` still means "how long the button is
  held".

`yg_io_remote_level()` (surfaced in `/api/status` and the console) has no meaning on RF.
It generalises to "actuator asserted right now", true while a MOSFET press is high or an
RF transmit is in progress. The RF build adds a learned-code-present flag alongside it.
Exact JSON shape is finalised in the plan and must stay in step across
`yg_http.c`/`yg_engine.c`, `tools/mock_api.py`, `test/bench/api_check.py` and spec 8.3.

### 5.3 The learn flow

New pure-logic module `components/yg_core/yg_rfcode.{c,h}`, host-tested, no ESP-IDF:

1. **Input:** an edge stream captured while the owner holds the remote, as an array of
   durations in microseconds with a starting level. The hardware layer fills this from
   RMT; tests feed synthetic arrays.
2. **Segment** the stream into frames using the long inter-frame gap.
3. **Normalise** each frame by quantising durations within a tolerance.
4. **Stability check:** require at least N frames (start with 3) that match within
   tolerance. Matching frames are saved. If too few match, or the matching frames
   disagree, reject. Disagreement means a rolling or noisy code, which replay cannot open,
   so refusing is correct and is the rolling-code guard.
5. **Output:** one canonical frame plus its inter-frame gap. That is the stored code.

**Replay:** given the canonical frame, the gap, and a target `ms`, generate the edge
stream to transmit by repeating frame-plus-gap until `ms` elapses. The hardware layer
plays it on the CC1101 data pin in async OOK transmit.

The learn **state machine** mirrors beacon pairing (`components/yg_core/yg_pairing.c` and
`yg_engine_pair_*`): enter learn mode, collect a candidate, validate, save, confirm or
reject with the pairing-style cues. The engine gains a learn API that parallels the pair
API. The segmentation, matching, stability and replay-generation are all in `yg_rfcode`
and covered by `test/host/test_rfcode.c`, including a synthetic rolling input that must be
rejected. Write those tests first and watch them fail.

### 5.4 Storage

The canonical frame plus metadata is an NVS blob. New ops `save_rf_code` and a boot-time
load parallel `op_save_beacon`/`yg_nvs_save_beacon` and the beacon load in `yg_app.c`. The
engine holds the learned code like it holds `beacon`. Reset to defaults keeps it; factory
reset erases it, same as the beacon.

### 5.5 Firmware module layout

- New thin component `components/yg_rf` (ESP-IDF): CC1101 SPI driver, async OOK mode, RMT
  capture and replay, and a presence/health self-check by reading the CC1101 version
  register. Compiled only in the RF build. Mirrors `yg_io`'s role.
- `yg_io` keeps the LED, buzzer and button for both builds. Its MOSFET press compiles only
  in the MOSFET build.
- `yg_core` gains `yg_rfcode` and the learn state machine, compiled always (pure logic,
  tiny), inert on the MOSFET build because no RF hardware drives it.
- `yg_app` binds `op_press` and the save/load ops per variant.

## 6. Hardware

### 6.1 Pins

A common CC1101 module is 3.3 V and 8-pin: VCC, GND, SCK, MOSI, MISO, CSN, GDO0, GDO2.
SPI plus GDO0 as the async data pin is enough; GDO2 is optional. About six GPIO. The RF
build frees GPIO10 and both gate resistors. The C6 GPIO matrix routes SPI and RMT to
almost any pin, so assignment is flexible and is finalised in the plan against the owner's
actual module, then propagated to `hardware/design.md`, `hardware/board-layout.html`,
`hardware/check_layout.py`, `atlas.html` (WIRES and the pin table), `README.md` and
`main/Kconfig.projbuild`.

Safety constraints on the pin map:

- CSN must idle deasserted (high) at reset. Drive it high at the earliest boot step, the
  way `yg_io_remote_init` drives the MOSFET low first.
- Keep CSN and GDO0 off GPIO18–23 (pulled up during reset) and off the strapping pins,
  following the existing rules in `design.md`.
- A floating SPI bus cannot cause a transmit, because a transmit needs an explicit SPI
  command sequence. This is inherently safer than the MOSFET's pulled-up-gate failure, but
  the constraints above still stand.

### 6.2 Antenna and case

433.92 MHz quarter-wave is about 17.3 cm of wire, routed along the case edge, or a helical
antenna. The bulky BFT remote (64 × 40 × 17 mm) is gone, so the RF internals are smaller:
C6, MPU6050, buzzer, button and the CC1101 module (about 27 × 16 mm). The case becomes an
RF variant of `hardware/case/build_case.py` with an antenna channel, reflected in
`hardware/case/README.md`, the `CASE` model in `atlas.html`, and the manual's folded size,
which must still fit under the top.

## 7. Safety

Every product rule holds unchanged, automatically, because the decision layer is
untouched: never open because the car started, motion plus beacon required, the buzzer
answers people, the 15 s factory reset, and the rest.

New considerations for a transmitter that reaches the gate from range:

- Transmit only on an explicit engine press. No transmit at boot.
- CC1101 idle or in powerdown at boot, CSN high early.
- The stability check refuses to learn anything it cannot replay faithfully, so a unit
  never stores an ambiguous code.
- The CC1101 is a separate sub-GHz radio on SPI. It does not share the C6's BLE radio, so
  the hotspot/scan timing rule is not affected by RF transmits. Any EMI between the two is
  a bench check, not a logic concern.
- Regulatory: 433.92 MHz is licence-free ISM at low power in region 1, including Israel.
  CC1101 output near 10 dBm is within typical limits. Documented, not enforced in code.

## 8. Test and verification plan

**Now, no RF hardware (this and the next session):**

- Host tests for `yg_rfcode`: segmentation, normalisation, stability match, rolling
  rejection, replay-stream generation. Written first.
- Host tests for the learn state machine in the engine.
- `tools/mock_api.py` serves the RF variant status and the learn endpoints; `page_check.py`
  exercises the new card.
- `check_docs.py` once docs are edited. Note: the manual-page-count step needs a working
  headless print environment, which this design session does not have.

**Now, with the HackRF only (owner):**

- Capture the owner's actual remote with the HackRF (URH or rtl_433) to get the carrier
  frequency, confirm OOK, and read the symbol timing, frame structure and repeat gap. This
  is the golden reference and the design constants, and it confirms the remote is
  fixed-code (identical captures) before any CC1101 arrives.

**Later, with the CC1101 (bench):**

- Self-check reads the CC1101 version register to confirm the module is present, feeding an
  RF-module health like the existing motion-sensor health.
- Learn from the real remote, replay, and capture the replay on the HackRF. Compare
  frequency, timing and framing with the golden reference.

**Later, with the gate:**

- The replay opens the gate from the parking spot. Range check like the beacon RSSI check.

## 9. Docs to keep in step

Per the current `AGENTS.md` table, the plan will carry this as a task checklist:

| Row | Touched by this variant |
|---|---|
| Setting add/remove | Only if a carrier-frequency setting proves necessary (prefer none) |
| A sound | Only if learn needs its own sound (prefer reuse) |
| When a sound plays | Learn confirm/reject cues in `yg_engine.c` and its test, spec 8.5, atlas, manual, Help |
| An LED pattern | Only if an RF-module-failed pattern is added (`yg_io.c`, `yg_engine.h`, spec 8.5, atlas LED table) |
| The press rule / `yg_trigger.c` | **Not changed.** The rule is identical |
| Button gestures | **Not changed.** Learn is page-only |
| Hotspot | Not changed |
| The page | New Advanced "Gate remote" card: `web/index.html`, `page_check.py`, `mock_api.py`, the atlas phone picture |
| The API / `/api/status` | Generalised actuator status and learn endpoints: `yg_http.c`/`yg_engine.c`, `mock_api.py`, `api_check.py`, spec 8.3 |
| Board layout / GPIO | CC1101 pins: `board-layout.html`, `design.md`, `check_layout.py`, atlas WIRES and pin table, README, Kconfig |
| A case size | RF case variant: `build_case.py`, case README, atlas CASE model, manual folded size |
| Scan timing / radio | Not affected (separate radio), but note a bench EMI check |
| Log records | Reuse the press record for transmit; add a learn/forget event only if useful (`yg_record.c`, `docs/log-format.md`, `yg_logs.py`) |
| How setup goes | RF manual variant and RF Help tab gain "learn the remote": `manual.html`, Help, atlas, README |
| Not proven yet / status strip | Add the RF variant's untested items: atlas, `hardware/README.md`, case README, atlas status strip |

## 10. Open questions and assumptions

- Carrier frequency assumed 433.92 MHz OOK. Confirm from the HackRF capture.
- The target remote is assumed fixed-code. The stability check and the HackRF capture
  confirm it. If it is rolling, this variant cannot open that gate and we stop.
- The exact CC1101 module footprint and pinout finalise against the module the owner
  orders.
- Manual: a separate RF variant is assumed, rather than one shared manual, so each stays
  within its folded sheet.

## 11. Sequencing

- Done: `master` fast-forwarded to include the LED-for-developers work; `rf433-board-variant`
  branched; isolated worktree at `D:\coding\YogiGate-rf433`. Nothing pushed.
- This session: this spec, then the implementation plan, then a handoff prompt for the
  next session.
- Next session: execute the plan. Host-testable core first (tests before code), then the
  `yg_rf` driver, the page, the schematic and the case. Bench and gate tests wait for the
  CC1101 and the HackRF.
