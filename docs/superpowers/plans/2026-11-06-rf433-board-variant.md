# RF433 board variant Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a second car-unit board variant that learns and replays a fixed-code 433 MHz remote (CC1101), as an alternative to the MOSFET unit, with the decision layer unchanged.

**Architecture:** One codebase, two build-time images selected by `YG_GATE_DRIVER` (MOSFET or RF433). `yg_core` is identical for both and stays PC-tested. The engine opens the gate through the existing `ops.press(ctx, ms)` seam; on RF that transmits a learned OOK frame instead of pulsing a pin. Learning captures the remote's raw edge timing, a pure-logic module (`yg_rfcode`) verifies the bursts are stable (which also rejects rolling codes), and the frame is stored in NVS like the beacon.

**Tech Stack:** C17, ESP-IDF v6.1 (ESP32-C6), CC1101 over SPI with the C6 RMT peripheral for async OOK capture/replay, the existing host test harness (`test/host`, clang/gcc), Python mock API and bench checks.

**Reference:** design spec `docs/superpowers/specs/2026-11-06-rf433-board-variant-design.md`.

**Open values to confirm before the hardware tasks (Task 7, 11):** carrier frequency (assume 433.92 MHz OOK, confirm from a HackRF capture of the real remote), exact CC1101 pin map, and the module footprint. These are flagged inline as confirmation steps, not guesses to bake in.

---

## File structure

**New (host-testable, built everywhere):**
- `components/yg_core/include/yg_rfcode.h` — learned-code type and pure functions.
- `components/yg_core/yg_rfcode.c` — segmentation, stability match, replay-stream generation.
- `test/host/test_rfcode.c` — tests for `yg_rfcode` (auto-discovered by `run_tests.py`).

**New (RF build only, ESP-IDF):**
- `components/yg_rf/include/yg_rf.h`, `components/yg_rf/yg_rf.c`, `components/yg_rf/CMakeLists.txt` — the CC1101 driver: SPI, presence check, RMT async OOK receive (learn) and transmit (replay).

**Modified:**
- `components/yg_core/include/yg_engine.h` — new ops (`save_rf_code`, `rf_listen`), learn API, learned-code + learn state in `yg_engine_t`, generalised actuator status.
- `components/yg_core/yg_engine.c` — learn orchestration, learned-code in status JSON, RF press path.
- `components/yg_app/yg_app.c` — per-variant `op_press`, learn/capture ops, rf-code save/load wiring.
- `components/yg_settings_nvs/include/yg_nvs.h`, `components/yg_settings_nvs/yg_nvs.c` — `yg_nvs_save_rf_code` / load, kept on reset-to-defaults, wiped on factory reset.
- `main/Kconfig.projbuild` — `YG_GATE_DRIVER` choice and the RF pins.
- `main/main.c` — initialise the actuator for the active variant.
- `main/CMakeLists.txt` and component `CMakeLists.txt` — include `yg_rf` and skip the MOSFET press on the RF build, and vice versa.
- `components/yg_io/yg_io.c`, `components/yg_io/include/yg_io.h` — guard the MOSFET press under the MOSFET build; keep LED, buzzer, button for both.
- `components/yg_net/yg_http.c` — generalised `/api/status`, learn endpoints.
- `tools/mock_api.py`, `test/bench/api_check.py` — RF variant status and learn endpoints.
- `web/index.html` — Advanced "Gate remote" card, driver-aware; Help tab learn steps.
- `test/bench/page_check.py` — the new card.
- `hardware/design.md`, `hardware/board-layout.html`, `hardware/check_layout.py`, `README.md`, `atlas.html` (WIRES, pin table), `hardware/case/build_case.py`, `hardware/case/README.md` — RF schematic, pins, case.
- `manual.html` (RF variant), `atlas.html` (CASE model, status strip, "Not proven yet"), `hardware/README.md`.
- `docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md` — RF notes in sections 5, 8.3, 8.5.

Milestones: Tasks 1–4 are the host-testable core and finish with no hardware. Tasks 5–6 wire storage and build variants. Task 7 is the driver (needs the module to fully verify). Tasks 8–14 are integration, page, docs, case, and the final pass.

---

## Task 1: yg_rfcode type and frame match

**Files:**
- Create: `components/yg_core/include/yg_rfcode.h`
- Create: `components/yg_core/yg_rfcode.c`
- Test: `test/host/test_rfcode.c`

- [ ] **Step 1: Write the failing test**

```c
/* test/host/test_rfcode.c */
#include "yg_rfcode.h"
#include "yg_test.h"

static void frames_match_within_tolerance(void) {
    uint16_t a[] = {300, 900, 300, 900, 300};
    uint16_t b[] = {320, 870, 290, 930, 310};   /* each within ~25% or the 80us floor */
    uint16_t c[] = {300, 900, 300, 1500, 300};  /* one edge far off */
    CHECK(yg_rfcode_frames_match(a, b, 5));
    CHECK(!yg_rfcode_frames_match(a, c, 5));
}

int main(void) {
    frames_match_within_tolerance();
    printf("%d checks, %d failures\n", yg_test_checks, yg_test_failures);
    return yg_test_failures != 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `python test/host/run_tests.py rfcode`
Expected: build FAIL, `yg_rfcode.h` not found.

- [ ] **Step 3: Write the header**

```c
/* components/yg_core/include/yg_rfcode.h
 * A learned fixed-code OOK frame and the pure logic to learn and replay it. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define YG_RF_MAX_EDGES   300u   /* durations in one frame */

typedef struct {
    bool learned;
    uint16_t count;              /* durations used in edges[] */
    uint16_t edges[YG_RF_MAX_EDGES]; /* microseconds, edges[0] is carrier-on */
    uint16_t gap_us;             /* quiet gap between repeats of the frame */
} yg_rfcode_t;

/* True if every duration in a and b is within tolerance (25% or an 80us floor). */
bool yg_rfcode_frames_match(const uint16_t *a, const uint16_t *b, uint16_t n);
```

- [ ] **Step 4: Write the implementation**

```c
/* components/yg_core/yg_rfcode.c */
#include "yg_rfcode.h"
#include <string.h>

#define TOL_PERCENT 25u
#define TOL_FLOOR_US 80u

static bool close_enough(uint16_t a, uint16_t b) {
    uint16_t hi = a > b ? a : b;
    uint16_t lo = a > b ? b : a;
    uint32_t slack = (uint32_t)a * TOL_PERCENT / 100u;
    if (slack < TOL_FLOOR_US) slack = TOL_FLOOR_US;
    return (uint32_t)(hi - lo) <= slack;
}

bool yg_rfcode_frames_match(const uint16_t *a, const uint16_t *b, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        if (!close_enough(a[i], b[i])) return false;
    }
    return true;
}
```

- [ ] **Step 5: Run to verify it passes**

Run: `python test/host/run_tests.py rfcode`
Expected: PASS, "N checks, 0 failures".

- [ ] **Step 6: Commit**

```bash
git add components/yg_core/include/yg_rfcode.h components/yg_core/yg_rfcode.c test/host/test_rfcode.c
git commit -m "Add yg_rfcode frame-match with tolerance"
```

---

## Task 2: Learn a stable frame, reject rolling

**Files:** Modify `components/yg_core/include/yg_rfcode.h`, `components/yg_core/yg_rfcode.c`, `test/host/test_rfcode.c`.

- [ ] **Step 1: Add the failing tests** (append to `test_rfcode.c`, and call them from `main`)

```c
/* Builds a capture: `reps` copies of `frame` (len), each followed by a long gap. */
static uint16_t build(uint16_t *buf, const uint16_t *frame, uint16_t len, uint16_t reps, uint16_t gap) {
    uint16_t w = 0;
    for (uint16_t r = 0; r < reps; r++) {
        memcpy(&buf[w], frame, len * sizeof(uint16_t));
        w += len;
        buf[w++] = gap;
    }
    return w;
}

static void learns_a_repeating_frame(void) {
    uint16_t frame[] = {300, 900, 300, 900, 600, 300};
    uint16_t buf[256];
    uint16_t n = build(buf, frame, 6, 5, 10000);
    yg_rfcode_t out = {0};
    CHECK(yg_rfcode_learn(buf, n, true, 3, &out));
    CHECK(out.learned);
    CHECK_INT(out.count, 6);
    CHECK(yg_rfcode_frames_match(out.edges, frame, 6));
    CHECK(out.gap_us >= 9000 && out.gap_us <= 11000);
}

static void rejects_too_few_repeats(void) {
    uint16_t frame[] = {300, 900, 300, 900};
    uint16_t buf[64];
    uint16_t n = build(buf, frame, 4, 2, 10000);
    yg_rfcode_t out = {0};
    CHECK(!yg_rfcode_learn(buf, n, true, 3, &out));
}

static void rejects_rolling_code(void) {
    /* Five different frames: replay could never open the gate. */
    uint16_t buf[256];
    uint16_t w = 0;
    for (uint16_t r = 0; r < 5; r++) {
        uint16_t frame[] = {300, (uint16_t)(600 + r * 200), 300, 900};
        memcpy(&buf[w], frame, sizeof(frame)); w += 4;
        buf[w++] = 10000;
    }
    yg_rfcode_t out = {0};
    CHECK(!yg_rfcode_learn(buf, w, true, 3, &out));
}

static void tolerates_jitter(void) {
    uint16_t f1[] = {300, 900, 300, 900};
    uint16_t f2[] = {320, 880, 290, 915};
    uint16_t f3[] = {290, 910, 310, 890};
    uint16_t buf[64]; uint16_t w = 0;
    memcpy(&buf[w], f1, sizeof(f1)); w += 4; buf[w++] = 10000;
    memcpy(&buf[w], f2, sizeof(f2)); w += 4; buf[w++] = 10000;
    memcpy(&buf[w], f3, sizeof(f3)); w += 4; buf[w++] = 10000;
    yg_rfcode_t out = {0};
    CHECK(yg_rfcode_learn(buf, w, true, 3, &out));
    CHECK_INT(out.count, 4);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `python test/host/run_tests.py rfcode`
Expected: build FAIL, `yg_rfcode_learn` not declared.

- [ ] **Step 3: Declare in the header**

```c
/* Add to yg_rfcode.h */
#define YG_RF_MAX_FRAMES  64u
#define YG_RF_GAP_MIN_US  3000u  /* an OFF period this long separates frames */

/* Split a raw capture into frames at long OFF gaps, and return a frame that repeats
 * at least `min_repeats` times within tolerance. first_high is true if durations[0]
 * is carrier-on. Returns false if too few frames agree, which is what a rolling or
 * noisy code looks like. */
bool yg_rfcode_learn(const uint16_t *durations, uint16_t n, bool first_high,
                     uint8_t min_repeats, yg_rfcode_t *out);
```

- [ ] **Step 4: Implement** (add to `yg_rfcode.c`)

```c
bool yg_rfcode_learn(const uint16_t *durations, uint16_t n, bool first_high,
                     uint8_t min_repeats, yg_rfcode_t *out) {
    uint16_t start[YG_RF_MAX_FRAMES], len[YG_RF_MAX_FRAMES], gap[YG_RF_MAX_FRAMES];
    uint16_t frames = 0, span = 0;
    bool high = first_high;
    for (uint16_t i = 0; i < n; i++) {
        if (!high && durations[i] >= YG_RF_GAP_MIN_US) {       /* a gap ends a frame */
            if (i > span && frames < YG_RF_MAX_FRAMES) {
                start[frames] = span; len[frames] = i - span; gap[frames] = durations[i];
                frames++;
            }
            span = i + 1;                                       /* next frame starts high */
        }
        high = !high;
    }
    if (frames < min_repeats) return false;
    for (uint16_t f = 0; f + min_repeats <= frames; f++) {
        if (len[f] == 0 || len[f] > YG_RF_MAX_EDGES) continue;
        uint16_t agree = 1;
        for (uint16_t g = f + 1; g < frames; g++) {
            if (len[g] == len[f] &&
                yg_rfcode_frames_match(&durations[start[f]], &durations[start[g]], len[f])) {
                agree++;
            }
        }
        if (agree >= min_repeats) {
            out->learned = true;
            out->count = len[f];
            memcpy(out->edges, &durations[start[f]], len[f] * sizeof(uint16_t));
            out->gap_us = gap[f];
            return true;
        }
    }
    return false;
}
```

- [ ] **Step 5: Run to verify they pass** — `python test/host/run_tests.py rfcode`, expect 0 failures.
- [ ] **Step 6: Commit** — `git commit -am "Learn a stable OOK frame and reject rolling codes"`

---

## Task 3: Build the replay stream

**Files:** Modify `components/yg_core/include/yg_rfcode.h`, `components/yg_core/yg_rfcode.c`, `test/host/test_rfcode.c`.

- [ ] **Step 1: Add the failing test**

```c
static void replays_for_the_press_duration(void) {
    yg_rfcode_t code = { .learned = true, .count = 4, .gap_us = 10000 };
    uint16_t f[] = {300, 900, 300, 900}; memcpy(code.edges, f, sizeof(f));
    uint16_t out[512];
    /* frame = 2400us, cycle = 12400us; 300ms budget -> about 24 frames */
    uint16_t w = yg_rfcode_replay(&code, 300, out, 512);
    CHECK(w > 0);
    CHECK_INT(out[0], 300);
    CHECK(w >= 4 * 20);                 /* at least ~20 frames fit */
    uint16_t cap = yg_rfcode_replay(&code, 300, out, 10);  /* respect max */
    CHECK(cap <= 10);
    yg_rfcode_t none = {0};
    CHECK_INT(yg_rfcode_replay(&none, 300, out, 512), 0);
}
```

- [ ] **Step 2: Run, expect FAIL** (`yg_rfcode_replay` not declared).

- [ ] **Step 3: Declare**

```c
/* Add to yg_rfcode.h: write the on/off durations to transmit for `ms`, repeating
 * frame+gap. Returns the count written (<= max). out[0] is carrier-on. */
uint16_t yg_rfcode_replay(const yg_rfcode_t *code, uint32_t ms, uint16_t *out, uint16_t max);
```

- [ ] **Step 4: Implement**

```c
uint16_t yg_rfcode_replay(const yg_rfcode_t *code, uint32_t ms, uint16_t *out, uint16_t max) {
    if (!code->learned || code->count == 0) return 0;
    uint32_t frame_us = 0;
    for (uint16_t i = 0; i < code->count; i++) frame_us += code->edges[i];
    uint32_t cycle_us = frame_us + code->gap_us;
    uint32_t budget_us = ms * 1000u, elapsed = 0;
    uint16_t w = 0;
    while (w + code->count <= max) {
        for (uint16_t i = 0; i < code->count; i++) out[w++] = code->edges[i];
        elapsed += cycle_us;
        if (elapsed >= budget_us) break;
        if (w < max) out[w++] = code->gap_us;
    }
    return w;
}
```

- [ ] **Step 5: Run, expect PASS.**
- [ ] **Step 6: Commit** — `git commit -am "Generate the OOK replay stream for a press"`

---

## Task 4: Engine learn orchestration and status

**Files:** Modify `components/yg_core/include/yg_engine.h`, `components/yg_core/yg_engine.c`, and `test/host/test_engine.c` (follow its existing fake-ops pattern).

**Design:** No separate state-machine module (YAGNI). The engine gains a thin learn state: a `learning` flag with a start time for timeout, a learned `yg_rfcode_t`, and:
- `void yg_engine_rf_learn_start(e, now)` — sets `learning`, calls `ops->rf_listen(ctx, true)`.
- `void yg_engine_rf_learn_cancel(e, now)`.
- `void yg_engine_on_rf_capture(e, now, durations, n, first_high)` — if `learning`, run `yg_rfcode_learn`; on success store the code, call `ops->save_rf_code`, buzz/LED the pairing "saved" cue, clear `learning`; on failure buzz/LED the "rejected" cue and keep listening until timeout.
- `void yg_engine_rf_forget(e, now)` — clears the code, calls `ops->save_rf_code` with an unlearned code.
- `yg_engine_tick` times out learning after the pairing timeout and buzzes the timeout cue.
- Two new ops in `yg_engine_ops_t`: `void (*save_rf_code)(void *ctx, const yg_rfcode_t *c)` and `void (*rf_listen)(void *ctx, bool on)`. On the MOSFET build these are left NULL and never called (learning is never started there).
- Status JSON: generalise the `remote` field to mean "actuator asserted now" and add `"rf_learned": true/false` on the RF build.

- [ ] **Step 1:** Add failing engine tests: start learn, feed a good capture (fake ops record `save_rf_code` and the buzz cue), assert the code is stored and `learning` clears; feed a rolling capture, assert rejected and still learning; assert timeout clears learning; assert `rf_learned` appears in `yg_engine_status_json`. Follow the fake-ops harness already in `test_engine.c`.
- [ ] **Step 2:** Run `python test/host/run_tests.py engine`, expect FAIL (new symbols).
- [ ] **Step 3:** Add the ops, fields, API, and status as above. Include `yg_rfcode.h` from `yg_engine.h`.
- [ ] **Step 4:** Run, expect PASS. Run the whole suite `python test/host/run_tests.py` to confirm nothing else broke.
- [ ] **Step 5:** Commit — `git commit -am "Engine: learn, store, forget an RF code behind the ops seam"`

---

## Task 5: Store the RF code in NVS

**Files:** Modify `components/yg_settings_nvs/include/yg_nvs.h`, `components/yg_settings_nvs/yg_nvs.c`.

- [ ] Add `bool yg_nvs_load_rf_code(yg_rfcode_t *c)` and `void yg_nvs_save_rf_code(const yg_rfcode_t *c)`, storing the struct as one blob under a new NVS key (<=15 chars, e.g. `rf_code`). Load it in `yg_nvs_load` alongside the beacon. Keep it on `yg_nvs_save_settings`/reset-to-defaults (it is a separate key, like the beacon), and let `yg_nvs_factory_reset` erase it with everything else.
- [ ] This component is ESP-IDF, so it is not in the host build; its correctness is covered on the bench (Task 14) and by the engine ops fake (Task 4).
- [ ] Commit — `git commit -am "Persist the learned RF code in NVS, like the beacon"`

---

## Task 6: Build variants (Kconfig and wiring)

**Files:** Modify `main/Kconfig.projbuild`, `main/main.c`, `main/CMakeLists.txt`, component `CMakeLists.txt`, `components/yg_io/*`.

- [ ] Add a `choice YG_GATE_DRIVER` to `Kconfig.projbuild` with `YG_GATE_DRIVER_MOSFET` (default) and `YG_GATE_DRIVER_RF433`, exposing `CONFIG_YG_GATE_DRIVER_RF433`.
- [ ] In `main/main.c`, initialise `yg_io_remote_init()` on MOSFET and `yg_rf_init()` on RF (`#if CONFIG_YG_GATE_DRIVER_RF433`).
- [ ] Guard `yg_io_press`/`yg_io_remote_level` and the MOSFET GPIO setup under the MOSFET build; keep LED, buzzer, button for both.
- [ ] Make `yg_rf` compile only on the RF build (CMake condition on `CONFIG_YG_GATE_DRIVER_RF433`).
- [ ] Verify both images configure: `idf.py set-target esp32c6 reconfigure` for each `YG_GATE_DRIVER` value (build needs ESP-IDF; copy the sources elsewhere per AGENTS.md, do not touch the shared `build/`).
- [ ] Commit — `git commit -am "Select the gate driver at build time"`

---

## Task 7: CC1101 driver (yg_rf) — needs the module to fully verify

**Files:** Create `components/yg_rf/include/yg_rf.h`, `components/yg_rf/yg_rf.c`, `components/yg_rf/CMakeLists.txt`.

**Interface (mirrors `yg_io`'s thinness):**
```c
void yg_rf_init(void);                 /* SPI up, CC1101 to idle, CSN high first */
bool yg_rf_present(void);              /* reads the CC1101 version register */
void yg_rf_transmit(const uint16_t *edges, uint16_t n);  /* async OOK via RMT TX */
void yg_rf_listen(bool on, yg_rf_capture_cb_t cb);       /* async OOK via RMT RX */
```
- [ ] Confirm the carrier frequency and timing from a HackRF capture of the real remote before fixing register values (spec section 8, test plan).
- [ ] SPI init and reset; put the CC1101 in async OOK; set the frequency; drive CSN high at the very first step (mirror `yg_io_remote_init` driving the MOSFET low first).
- [ ] `yg_rf_present` reads the version register and feeds a module-health status like the motion-sensor health.
- [ ] RX: RMT captures edge durations on GDO0 into a buffer, calls back a completed capture (fed to `yg_engine_on_rf_capture`).
- [ ] TX: `yg_rfcode_replay` builds the stream; RMT plays it on GDO0; the CC1101 keys the carrier.
- [ ] Bench verify with the HackRF (Task 14).
- [ ] Commit — `git commit -am "Add the CC1101 driver for learn and replay"`

---

## Task 8: Bind the actuator per variant

**Files:** Modify `components/yg_app/yg_app.c`.
- [ ] On MOSFET, `op_press` stays `yg_io_press`. On RF, `op_press` calls `yg_rf_transmit` with the stream from `yg_rfcode_replay` for `ms`, and `op_rf_listen`/`op_save_rf_code` and the boot-time `yg_nvs_load_rf_code` are wired (`#if CONFIG_YG_GATE_DRIVER_RF433`). The RX callback calls `yg_engine_on_rf_capture` through `yg_app_call`, like other engine entry points.
- [ ] Commit — `git commit -am "Wire the RF actuator and learn ops into the app"`

---

## Task 9: API and status

**Files:** Modify `components/yg_net/yg_http.c`, `tools/mock_api.py`, `test/bench/api_check.py`, spec section 8.3.
- [ ] Generalise the `/api/status` `remote` field to the actuator state and add `rf_learned` on RF. Add `POST /api/rf/learn` (start), `POST /api/rf/forget`, surfaced only on the RF build.
- [ ] Update `mock_api.py` to serve the RF status and endpoints so `page_check.py` can drive the card with no board.
- [ ] Update `api_check.py` for the RF variant (it must not press blindly: on RF, "press" transmits, so gate it the same way it gates the MOSFET press).
- [ ] Keep spec section 8.3 in step.
- [ ] Commit — `git commit -am "Expose the RF actuator state and learn endpoints"`

---

## Task 10: Page "Gate remote" card

**Files:** Modify `web/index.html`, `test/bench/page_check.py`, `atlas.html` (phone picture).
- [ ] Add an Advanced-mode "Gate remote" card shown when status says the driver is RF: learned/not-learned, Learn, Forget, Test open. Key the card off the status driver field. Edit `index.html` by exact string match (long embedded lines).
- [ ] Extend `page_check.py` for the card against the RF mock.
- [ ] Update the atlas phone picture per the AGENTS.md page row.
- [ ] Commit — `git commit -am "Add the Gate remote card for the RF variant"`

---

## Task 11: Hardware docs and schematic

**Files:** Modify `hardware/design.md`, `hardware/board-layout.html`, `hardware/check_layout.py`, `README.md`, `atlas.html` (WIRES, pin table), `main/Kconfig.projbuild` (pins).
- [ ] Finalise the CC1101 pin map against the module the owner ordered, honouring: CSN idles high at reset, keep CSN/GDO0 off GPIO18-23 and the strapping pins, GPIO10 and the resistors are freed.
- [ ] Add the RF schematic and pin table; run `python hardware/check_layout.py`.
- [ ] Commit — `git commit -am "Document the RF board layout and CC1101 wiring"`

---

## Task 12: Case variant

**Files:** Modify `hardware/case/build_case.py`, `hardware/case/README.md`, `atlas.html` (CASE model).
- [ ] An RF case variant: no BFT remote cavity, an antenna channel for a 17.3cm quarter-wave whip, CC1101 module space. Keep the manual's folded size fitting under the top.
- [ ] Commit — `git commit -am "Add the RF case variant"`

---

## Task 13: Technician docs in step

**Files:** Modify `manual.html` (RF variant), `web/index.html` (Help tab), `atlas.html` (status strip, "Not proven yet"), `README.md`, `hardware/README.md`, `hardware/case/README.md`.
- [ ] RF manual variant and Help tab gain the "learn the remote" steps. Edit by exact string match. Keep the LED out of the manual and Help (the LED-is-for-developers rule).
- [ ] Add the RF variant's untested items to the "Not proven yet" lists and the dated status strip.
- [ ] Commit — `git commit -am "Document RF setup for technicians and mark what is unproven"`

---

## Task 14: Full verification pass

- [ ] `python test/host/run_tests.py` — all logic tests and docs check pass (the manual page-count step needs a working headless-print environment).
- [ ] `python test/bench/page_check.py` — the page against the RF mock.
- [ ] `python tools/check_docs.py` — docs against firmware.
- [ ] Build both variants with ESP-IDF in a copied tree.
- [ ] On the bench with the CC1101 and the HackRF: `yg_rf_present` true; learn the real remote; replay; capture the replay on the HackRF and compare to the golden reference; then the real-gate open from the parking spot. Record results in the status strip.
- [ ] Commit any doc updates — `git commit -am "Record the RF bench and gate results"`

---

## Self-review

- **Spec coverage:** variant selection (Task 6), actuator seam (Tasks 4, 8), learn flow and stability/rolling guard (Tasks 2, 4), storage (Task 5), page/Help/manual (Tasks 10, 13), pins/schematic/case (Tasks 11, 12), safety (CSN-high and idle-at-boot in Task 7, no trigger change anywhere), tests incl. HackRF (Task 14). Covered.
- **Scope:** one variant, one plan. The host-testable core (Tasks 1–4) stands alone and is finishable now.
- **Open values** (carrier freq, pins, footprint) are explicit confirmation steps in Tasks 7 and 11, not baked-in guesses.
- **Type consistency:** `yg_rfcode_t`, `yg_rfcode_learn`, `yg_rfcode_replay`, `yg_rfcode_frames_match`, `yg_engine_on_rf_capture`, `yg_nvs_save_rf_code`, `save_rf_code`/`rf_listen` ops used consistently across tasks.
