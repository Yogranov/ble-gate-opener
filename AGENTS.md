# Working on YogiGate

Rules for anyone changing this repo, people and coding agents alike. YogiGate opens a gate without anyone touching a remote: a beacon sits on the gate, and a unit in the car presses the button of a paired remote when the car has moved and can hear the beacon.

## Keep everything in step

The same facts live in several places, and nothing generates one from another. **A change is not finished until every place in its row is updated.** Then run `python tools/check_docs.py`, which catches what it can. It does not read prose, so the table still matters.

| If you change | Also update |
|---|---|
| A setting: add, remove, rename, default or range | `components/yg_core/yg_settings.c`, `web/index.html` (`LABELS`, `GROUPS`, and `SIMPLE` if it belongs in simple mode), `tools/mock_api.py`, `test/host/test_settings.c`, spec section 5.7, `atlas.html` settings table |
| A sound | `components/yg_core/yg_sound.c`, `web/index.html` (`SOUNDS`, and the Help tab), `test/bench/api_check.py`, spec section 8.5, `atlas.html` sound strips, `manual.html` sound table |
| When a sound plays | `components/yg_core/yg_engine.c` and its test, spec section 8.5, `atlas.html`, `manual.html`, the page's Help tab |
| An LED pattern | `components/yg_io/yg_io.c`, `components/yg_core/include/yg_engine.h`, spec section 8.5, `atlas.html` LED table, `manual.html` colour table, the page's Help tab |
| The press rule, or anything in `yg_trigger.c` | Its tests, spec section 5, and the port of it in `atlas.html`, between `SIM-START` and `SIM-END` |
| Button gestures | `components/yg_core/yg_engine.c`, spec section 8.4, README, `atlas.html`, `manual.html`, the page's Help tab |
| Hotspot behaviour or the default password | `yg_hotspot_policy.c` or `yg_settings.h`, spec section 8.1, README, `atlas.html` (including its hotspot chart), `manual.html`, the page's Help tab |
| The page: a tab, a card, or wording quoted elsewhere | `web/index.html`, `test/bench/page_check.py`, `tools/mock_api.py` if the API changed, and the phone picture in `atlas.html` |
| The API or `/api/status` | `components/yg_net/yg_http.c` or `yg_engine.c`, `tools/mock_api.py`, `test/bench/api_check.py`, spec section 8.3 |
| The board layout or a GPIO | `hardware/board-layout.html`, `hardware/design.md`, and `python hardware/check_layout.py`, `atlas.html` (`WIRES`, the pin table), README, `main/Kconfig.projbuild` |
| A case size | `hardware/case/build_case.py`, `hardware/case/README.md`, the `CASE` model in `atlas.html`, and the manual's folded size if it no longer fits under the top |
| The scan's timing, or anything else that uses the radio | `components/yg_ble/yg_ble.c`, spec sections 8.1 and 10.3, then a real phone on the page counting requests, because the mock API cannot show this |
| Log records | `components/yg_core/yg_record.c`, `docs/log-format.md`, `tools/yg_logs.py` |
| The logo | `logo.png`, then the small copies embedded in `web/index.html` and the phone picture in `atlas.html` (240 px wide WebP, under 8 kB, because the page lives in the firmware), and the copies in `atlas.html` and `manual.html` |
| How setup goes, or what a technician sees or hears | `manual.html` first, then the page's Help tab, which says the same, then `atlas.html` and README |
| Something tested for the first time, such as the first car test | The "Not proven yet" lists in `atlas.html`, `hardware/README.md` and `hardware/case/README.md`, and the dated status strip at the top of `atlas.html` |

If you decide something new about how the product behaves, add it to the design spec, and to the rules below if it should stand.

## Who each document is for

| File | Reader | Keep it |
|---|---|---|
| `manual.html` | A technician fitting a unit | **Both sides of one A4 sheet, cut to 120 × 132 mm and folded to 60 × 33 mm to go inside the case.** How to set up, and what each sound and colour means. No theory, and nothing a technician does not need on the job |
| The Help tab in `web/index.html` | The same technician, once the printed page is gone | The same words as `manual.html`, because the page ships inside the firmware. It can only help someone who already reached the hotspot |
| `atlas.html` | Anyone who wants the whole picture | Everything about how it works and how it is built, with its drawings and trip panel |
| `README.md` | A developer | Wiring, building, tests and code layout |
| `docs/superpowers/specs/` | Whoever changes the design | The decisions and the reasons for them |
| `hardware/` | Whoever solders or prints | Layout, parts and case |

Both HTML files must work with no internet, because a phone joined to the unit's hotspot has none. They embed their fonts and logo, and load nothing from the network, and neither does `web/index.html`. Those embedded lines are very long, so edit these files by exact string match, and do not read them whole.

## Product rules

These are the owner's decisions. Change them only when the owner asks, and then update this list.

- **Never open the gate because the car started.** A press needs motion in the last minute and the beacon heard.
- **The buzzer is on by default, and answers people.** It sounds when someone does something on the unit, or when something is wrong. It stays quiet when the gate opens by itself, and at a normal start. It must never become annoying.
- **The hotspot starts only when asked,** by a triple-click or the USB `wifi` command. The one exception is the first start after a firmware update, which needs it for its self-check.
- **While the hotspot is on, the scan listens a quarter of the time.** At full time a phone on the page gets nothing through at all, and that was measured on a real unit. With the hotspot off, the scan has the whole radio, because the gate depends on it.
- **Holding the button for 15 s is a factory reset,** with its own sound, so a unit can start from scratch with no phone. A button already down at power-on does not count, so a stuck one cannot wipe the unit at every start.
- **The default hotspot password is `password`.** Changing it is an option on the page, and the page does not nag about it.
- **The page is simple by default.** Everything else lives behind Advanced mode, which is remembered per phone. A new setting or action is advanced unless a technician or owner needs it routinely.
- **Every setting and action has a place on the page,** except starting the hotspot, which needs the button or USB.
- **No clock, no cloud, no internet.** Trips are numbered, and logs leave the unit only when someone downloads them.

## Rules for the code

- All decisions live in `components/yg_core`, which has no ESP-IDF dependency and is tested on a PC. Hardware layers stay thin.
- Write the test first and watch it fail.
- Motion thresholds are first guesses until real captures exist. Prefer changes justified by logged data over more logic. The review history is in the spec.
- The remote's GPIO has one owner, `yg_io`, with a hard time limit on a press. Do not use GPIO 18 to 23 for it, because they are pulled up during reset and could press the remote at every ignition.
- Plain C, formatted with `.clang-format`, with a `yg_` prefix on public names. Match the code around you.

## Safety and privacy

- **The repo is public.** Do not commit logs, which show when the owner comes and goes (`logs/` is ignored), and keep personal details out of the docs. The default password being public is accepted, because the hotspot is only on after someone presses the button.
- **The bench checks can act on real hardware.** `api_check.py` presses the remote output, so check whether a remote is connected and near its gate, and it also forgets the beacon and resets the settings. `ota_check.py`, `soak_check.py` and `log_check.py` erase logs, and `log_check.py` also erases settings and the beacon.

## Shared working tree

Several sessions can work in this tree at once, on one branch.

- Run `git status` and check for recent changes before trusting a failure, because it may be someone else's half-finished edit.
- Commit only what you changed, and only when asked. Branch first if you are on `master`.
- Do not build in the shared `build/` while another session might be using it. Copy the sources somewhere else and build there. If you reuse a copy after changing files in it, update their timestamps first, because a tool that keeps old timestamps makes the build reuse stale objects.

## Checks

```powershell
python test/host/run_tests.py        # Logic tests and the docs check. Needs clang or gcc, and no board
python test/bench/page_check.py      # The page in headless Edge against the mock API
python tools/check_docs.py           # Docs against firmware, and whether the manual still fits its paper and the case
python hardware/check_layout.py      # After changing the board layout
```

Firmware, in a terminal with ESP-IDF v6.1 active:

```powershell
idf.py build
idf.py -p COM3 flash
```

A flash keeps the settings stored on the unit, so a changed default only reaches a unit after Reset to defaults on the page, which keeps the beacon, logs and saved gyro zero. Factory reset erases those too. When you report back, say what ran and what did not, especially anything not yet tried on a real unit or in a car.
