# YogiGate matrix board design

**Date:** 2026-10-31
**Scope:** Part choices and the hole-by-hole layout of the car unit on a 50 × 70 mm matrix board. The case is not covered. GPIO numbers come from section 4.2 of [2026-10-31-esp-idf-rewrite-design.md](../docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md). [board-layout.html](board-layout.html) draws this layout and gives the build order.

## 1. Parts

Sizes are the owner's measurements, except the buzzer, which is estimated from a photo. Parts are named in full here, because a reference like Q1 would read the same as hole Q1.

| Part | Size (mm) | Job |
|---|---|---|
| Matrix board, rows A-X, columns 01-18 | 70 × 50 | |
| MuseLab ESP32-C6 board on 15-pin female headers | 53 × 28 | Main board |
| MPU6050 module, soldered directly | 21 × 16 | Motion sensor |
| P30N06LE MOSFET | | Presses the remote button |
| 220 Ω resistor | | Gate series resistor |
| 10 kΩ resistor | | Gate pull-down |
| Active buzzer | About 12 across | Beeps, off by default |
| Momentary panel button | 16 across, 25 long, 20 pressed | Same gestures as BOOT |
| BFT remote, on its own battery | 64 × 40 × 17 | Sends the gate signal |

### Details of the real parts

The layout depends on these. They were read from close-up photos of the owner's parts, which are not kept in the repo. Distances marked "about" are estimates from those photos.

**ESP32-C6 board.** Marked MUSE LAB, with an ESP32-C6 module, an RGB LED, and two USB-C ports marked ESP32C6 and CH343. No pins were fitted.

- Two rows of 15 pins at 2.54 mm, with 25.4 mm (11 holes) between the rows, and each row 1.3 mm in from its long edge.
- Both rows start with a square GND pad at the antenna end. Section 4 lists every pin.
- Past the pin at the antenna end, the main board ends after about 1.2 mm and the antenna tip after about 8 mm. Past the pin at the USB end, the main board ends after about 8.3 mm and the USB ports after about 9.4 mm. With the 35.56 mm between the end pins, that adds up to the measured 53 mm.
- The module is about 18 mm wide and centred between the rows.
- The port marked ESP32C6 and the RST button are on the same side as the row starting GND, 2, 3. The port marked CH343 and the BOOT button are on the same side as the row starting GND, 3V3, RST.
- Each USB port is about 8 mm wide, and starts about 1 mm in from its pin row.
- Each button is about 3.3 mm from the pin at the USB end, along the board, and about 4.3 mm in from its pin row.

**MPU6050 module.** Marked HC, with no pins fitted. Its 8 pins run along one 21 mm edge, about 1.1 mm in from it. With the chip up and the pins on the left, they read from the top: VCC, GND, SCL, SDA, XDA, XCL, AD0, INT. Two mounting holes are at the far edge.

**MOSFET.** Marked P30N06LE and 1H25AL, in a TO-220 package with its metal tab joined to the drain. With the print facing you and the legs down, the legs are G, D, S from the left. Each leg has a wide part about 3.5 mm long next to the body. The drawings assume a typical TO-220 body, 10.2 mm wide and 15.6 mm long including the tab.

**Remote.** BFT 36015, 12 V, type B RCB, with 2 buttons and a 12 V 23A battery. With the rubber keypad removed, the buttons are exposed in a well near the LED end, and two wires are soldered to the button that is used. A key-ring slot is at the other end. In the old build, its case was taped.

**Button.** Momentary panel button with a white cap and two spade terminals.

**Buzzer.** Round, black, with two pins on the underside. The owner has active and passive ones, and this layout needs an active one.

**Matrix board.** 18 columns and 24 rows of holes at 2.54 mm, with a mounting hole at each corner. The first hole is 3.41 mm in from a long edge and 5.79 mm in from a short edge. A row of long oval pads, one per column, runs along each short edge. The long edges have only printed labels.

**Sizes assumed, not measured.** These come from typical parts or from estimates on the photos, so measure the real parts before relying on them for a case or drilling a hole. The owner measured the button as 16 across, 25 long and 20 pressed. This table assumes that 16 is its widest part, and that 25 and 20 leave out the terminals.

| Item | Size (mm) |
|---|---|
| Matrix board thickness | 1.6 |
| Female header height | 8.5 |
| Male header plastic, between the female header and the C6 | 2.5 |
| C6 board thickness | 1.6 |
| C6 module and USB ports, above its board | 3.2 |
| MPU6050 height above the matrix board, including pin tips | 5.5 |
| MOSFET height, lying flat | 5 |
| Buzzer height | 9.5 |
| Resistor body, 220 Ω and 10 kΩ | 6.3 long, 2.5 across |
| USB plug thickness | 7 |
| USB plug width | 12 |
| USB plug length, out of the port | 30 |
| Foam tape thickness | 3 |
| Matrix board corner holes | 2 across, with centres 2 in from each edge, so 46 × 66 apart |
| MPU6050 mounting holes | 3.3 across, 16.5 apart, with centres 2 in from the far edge |
| Buzzer pin spacing | 7.6 |
| Button threaded body, and the panel hole for it | 12 across |
| Button nut | 16 across at its widest, 3 thick |
| Button flange | 15 across, 2 thick |
| Button cap above the flange | 5 |
| Button body behind the flange | 20, plus 4.5 for the terminals |

The stack is about 38 mm high: 17 for the remote, 3 for the tape, 1.6 for the board, 11 for the headers, 1.6 for the C6 board and 3.2 for its module.

## 2. Decisions

**The C6 lies across the board, with its USB ports at an edge.** USB is the unit's power source and its flash and debug port, so both ports must stay easy to reach. The C6 is 53 mm long and the board is 50 mm wide, so with its pins in columns 01-15 the USB ports end about 1.6 mm inside the column 18 edge, and the antenna reaches about 4.6 mm past the column 01 edge. Only about 2.2 mm of the antenna lies over the board, above the bare margin, so nothing needs cutting.

**The layout follows the owner's board, not the official one.** The owner's board is marked MUSE LAB, and is not an ESP32-C6-DevKitC-1. It has 15 pins per row instead of 16, 25.4 mm between rows instead of 22.86 mm, and a different pin order. SDA and SCL are on the opposite row from GPIO10 and GPIO11. The GPIO numbers do not change, so the firmware does not either.

**P30N06LE for the remote.** The gate is driven at 3.3 V.

| Part | Gate threshold | At 3.3 V |
|---|---|---|
| IRF520 | 2-4 V | Not guaranteed to turn on |
| IRF540N | 2-4 V | Not guaranteed to turn on |
| IRLB8743 | 2.35 V max | Works |
| P30N06LE | 1-2 V | Works |

The remote's current is not published, and is estimated at tens of mA. At that level the IRLB8743's 3.2 mΩ gains nothing over the P30N06LE's 47 mΩ at 5 V, and the P30N06LE has about a quarter of the input capacitance (1350 pF against 5110 pF) and a 60 V rating. Neither datasheet gives a resistance at 3.3 V, so the bench test in section 7 measures it.

**10 kΩ pull-down, not 10-100 kΩ.** A logic-level gate can start to turn on at 1 V, and lower when hot. Against a roughly 45 kΩ internal pull-up, 100 kΩ leaves about 2.3 V on the gate and 10 kΩ leaves about 0.6 V. GPIO10 has no pull-up during reset, so this is a margin against the one failure the unit must never have: pressing at ignition.

**The remote keeps its own battery.** It then floats, with the MOSFET as its only link to the unit. This makes the circuit work however the remote wires its button internally, and car voltage spikes cannot reach the MOSFET. The firmware cannot see a dead battery, so it is replaced every year.

**The remote goes under the board.** At 64 × 40 mm it fits inside the board's outline, and the whole unit becomes one block of about 70 × 50 × 38 mm. Foam tape holds it, so it peels off for the battery. Its open button side faces away from the board, so the exposed contacts cannot touch the solder joints. Being close to the board could cost it some range, so this is tested before anything is stuck down. If the gate does not open reliably, the remote stays off the board on longer wires, and nothing else changes.

**No zip ties through the board.** The holes are about 1 mm, and even a small zip tie is about 2.5 mm wide. The button wires pass through free holes instead, so a pull cannot reach their joints.

**External button on GPIO7, not GPIO9.** GPIO9 is a strapping pin. If something in the glove box holds a button on it at ignition, the chip stays in its download mode for that whole drive. The firmware reads GPIO7 and GPIO9 together, so BOOT still works and nothing changes if the external button is not fitted. The button is too large for the board, so it stays on two wires, in a panel hole assumed to be 12 mm.

**Active buzzer straight on GPIO11, with no MOSFET and no diode.** An active buzzer has its own driver, so the pin only supplies its power. The pin's default drive is about 20 mA. This is quieter than the buzzer would be on 5 V, and it needs a bench check first: under about 20 mA at 3.3 V, and loud enough with the glove box closed. If it fails that check, the buzzer needs a MOSFET and 5 V, and this layout has to change. A passive buzzer cannot use this wiring. The `buzzer_type` setting must be set to active on the page when the buzzer is turned on.

**Nothing under the C6 except its headers.** The owner's choice. The C6 sits across the middle of the board, leaving room above and below it, and it can be removed without disturbing anything. Wires may still pass under it on the solder side.

**MPU6050 above the C6.** SDA and SCL are on row G, so its wires stay short. It starts at column 05 rather than 01 to keep it about 12.5 mm from the antenna.

**MOSFET and resistors below the C6, next to GPIO10.** The resistors' own leads make the links from GPIO10 to the gate, so that path has no separate wires to break. Each resistor spans four holes, because a body of about 6.3 mm does not fit flat across two.

**MOSFET lies flat with the print facing the board.** The metal tab is the drain, so it must not rest on the pads. Lying flat also keeps it low and stops vibration working on its legs.

## 3. Circuit

```
GPIO10 ──[220 Ω]──┬────── G
                  │       D ────── remote button wire (+)
               [10 kΩ]      P30N06LE
                  │
GND ──────────────┴────── S ────── remote button wire (−)


GPIO11 ─────────────────────────── buzzer +
GND ────────────────────────────── buzzer −

GPIO7  ─────────────────────────── button ── GND
```

To find the remote's polarity, put a meter across the two button wires with the battery in. The wire under the red probe when the reading is positive is (+). Reversed, the MOSFET's internal diode conducts and the remote acts as if the button is held.

## 4. Coordinates

Hold the board with row A at the top and column 01 at the left. That face is the parts side. Holes are named by row and column, so Q11 is row Q, column 11.

The C6's pin rows are 11 holes apart, counted by the owner, so they sit in rows G and Q. Its antenna points past the column 01 edge and its USB ports face the column 18 edge. The pin order below runs from the antenna end to the USB end, as labelled on the C6.

| Column | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Row G | GND | 2 | 3 | TX | RX | 15 | 23 | 22 | 21 | 20 | 19 | 18 | 9 | GND | 5V |
| Row Q | GND | 3V3 | RST | 4 | 5 | 6 | 7 | 0 | 1 | 8 | 10 | 11 | 12 | 13 | 5V |

The antenna lies between rows I and O. Keep parts and wires out of the margin beside it.

Of the two USB ports, the one marked ESP32C6, near row G, is the native USB port. The one marked CH343 is near row Q.

## 5. Parts side

| Part | Holes | Notes |
|---|---|---|
| Female headers | Rows G and Q, columns 01-15 | |
| MPU6050 | Row E: VCC E5, GND E6, SCL E7, SDA E8, XDA E9, XCL E10, AD0 E11, INT E12 | Chip side up, body toward row A. Long pin ends down, trimmed after soldering. Solder all 8 pins for strength. A dab of glue under the far edge |
| 220 Ω resistor | S7 to S11 | Lying flat |
| MOSFET | G T8, D U8, S V8 | Print facing the board, legs toward column 08, body toward column 01. Glue it down |
| 10 kΩ resistor | T9 to X9 | Lying flat |
| Buzzer | + V15, − V12 | |

Mark the gate leg with a pen before bending, just past the wide part of the legs. With the print facing you and the legs down, the order is G, D, S from the left.

## 6. Solder side

Use thin insulated wire, except where bare offcuts or part leads are noted.

| From | To | Net | Notes |
|---|---|---|---|
| G2 | E8 | SDA | Crosses SCL, which is fine |
| G3 | E7 | SCL | |
| G1 | E6 | MPU GND | |
| Q2 | E5 | MPU VCC | Up under the C6, between columns 02 and 03 |
| S11 | Q11 | GPIO10 to the 220 Ω | The resistor's lead. Do not touch Q10 or Q12 |
| S7 | T8 | The 220 Ω to the gate | The resistor's lead |
| T9 | T8 | The 10 kΩ to the gate | The resistor's lead |
| X9 | X8 | The 10 kΩ to ground | The resistor's lead |
| V8 | X8 | Ground, through W8 | Bare offcut, soldered at all three holes |
| Q1 | V8 | GND to the source | |
| Q12 | V15 | GPIO11 to buzzer + | |
| V12 | W8 | Buzzer − to GND | |
| Q7 | Button, through R2 | GPIO7 to button | Leaves by the column 01 edge |
| Q1 | Button, through R1 | GND to button | Leaves by the column 01 edge |
| U8 | Remote (+) | MOSFET drain | Leaves by the bottom edge |
| X8 | Remote (−) | MOSFET source | Leaves by the bottom edge |

Tape the remote's back to the solder side after the tests in section 7, centred on the board, with its LED end toward row X. It covers about columns 02-17 and all the rows. Use 2 to 3 mm double-sided foam tape on the two clear areas: columns 05-14, rows I-O, and columns 10-17, rows W-X. Trim any tall pin ends first.

Add a dab of hot glue at two header corners, or the USB cable and vibration will slowly pull the C6 out. Keep it clear of BOOT and RST.

## 7. Tests

**Before soldering:**

- The C6 fits: pin rows in rows G and Q, columns 01-15, and row Q reads as in section 4.
- With the C6 on its headers, the USB cable plugs into both ports without touching anything.
- The buzzer draws under about 20 mA at 3.3 V and is loud enough, and its pins fit V12 and V15.
- Held against the back of the board, in the glove box, the remote opens the gate reliably from the parking spot.

**Before plugging in the C6, with a meter:**

- Q2 to Q1 (3V3 to GND) is not shorted.
- Q15 to Q1 (5V to GND) is not shorted.
- E5 to E6 (MPU6050 VCC to GND) is not shorted.
- T8 to V8 reads about 10 kΩ.
- V8 to Q1 reads about 0 Ω.
- With the remote connected and its battery in, its LED is off. If it is on, (+) and (−) are swapped.

**With the C6 plugged in:**

- Power cycle the unit several times. The remote LED must stay off at every start.
- Press the remote from the page. Its LED lights for the press, and U8 to V8 reads under about 50 mV.
- Turn the buzzer on, set its type to active, and use the Beep button on the page.
- Click the external button once. It presses the remote.
- The page shows the motion sensor as found.
- After the unit is in the car, check that the signal from the beacon has not dropped compared with the C6 alone.

## 8. Firmware and settings

- `YG_PIN_BUTTON_EXT` defaults to GPIO7 and is read together with `YG_PIN_BUTTON`.
- No other pin changes. The buzzer already uses GPIO11, and the MPU6050 already uses GPIO2 and GPIO3.
- `buzzer_type` keeps its passive default, because the setting is chosen on the page when the buzzer is turned on.
