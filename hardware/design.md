# YogiGate matrix board design

**Date:** 2026-10-31
**Scope:** Part choices and the hole-by-hole layout of the car unit on a 50 × 70 mm matrix board. The case has [its own design](case/design.md). GPIO numbers come from section 4.2 of [2026-10-31-esp-idf-rewrite-design.md](../docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md). [board-layout.html](board-layout.html) draws this layout and gives the build order.

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
| Active buzzer | About 12.5 across | Beeps, on by default |
| Tactile button, through-hole | 6.5 × 6, 6 tall | Same gestures as BOOT |
| BFT MITTO B RCB02 remote, on its own battery | 64 × 40 × 17 | Sends the gate signal |

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

**MOSFET.** Marked P30N06LE and 1H25AL, in a TO-220 package with its metal tab joined to the drain. With the print facing you and the legs down, the legs are G, D, S from the left. Each leg has a wide part about 4.3 mm long next to the body, so its holes sit about 5.4 mm from the body. A dry fit on the board shows a body 10 mm wide and 15.5 mm long, including the tab.

**Remote.** BFT 36015, 12 V, type B RCB, with 2 buttons and a 12 V 23A battery. With the rubber keypad removed, the buttons are exposed in a well near the LED end, and two wires are soldered to the button that is used. On the owner's remote, its black wire is (+), about 10 V above its red wire, so its black wire goes to the MOSFET's drain and its red wire to ground. Matching the wire colours to the board's presses the remote, even with the board unpowered, through the MOSFET's internal diode. A key-ring slot is at the other end. In the old build, its case was taped.

**Button.** A 6.5 × 6 mm tactile button with 4 legs and a short stem, 6 mm tall loose, and 5.5 mm pressed. As soldered, the board and released button measure about 8 mm together, so the stem's top is about 6.5 mm above the board. On a dry fit, its legs span 4 holes one way, with 2 holes between them, and 3 holes the other way, with 1 between them.

**Buzzer.** Round, black, with two pins on the underside, about 12.5 mm across on a dry fit of the board. The owner has active and passive ones, and this layout needs an active one. The one fitted is active, because it gives clear beeps from short pulses on GPIO11. Its pins fit holes with 2 holes between them.

**Matrix board.** 18 columns and 24 rows of holes at 2.54 mm, with a mounting hole at each corner. The first hole is 3.41 mm in from a long edge and 5.79 mm in from a short edge. A row of long oval pads, one per column, runs along each short edge. The long edges have only printed labels.

**Sizes assumed, not measured.** These come from typical parts or from estimates on the photos, so measure the real parts before relying on them for the case.

| Item | Size (mm) |
|---|---|
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
| Gap between the remote and the board | 3 |
| Matrix board corner holes | 2 across, with centres 2 in from each edge, so 46 × 66 apart |
| MPU6050 mounting holes | 3.3 across, 16.5 apart, with centres 2 in from the far edge |
| Buzzer pin spacing | 7.6 |
| Button body | 6.5 × 6, 3.5 tall |
| Button, from the board to the top of its stem | 5 |
| Button legs | 6.5 × 4.5 apart |

The stack was planned at about 38 mm high: 17 for the remote, 3 for the gap, 1.5 for the board, 11 for the headers, 1.6 for the C6 board and 3.2 for its module. As built, the owner measured about 36.5 mm from the remote to the top of the C6. The matrix board measures 1.5 mm thick.

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

**The remote keeps its own battery.** It then takes no power from the unit, and its only link to the unit is the two wires across the MOSFET. This makes the circuit work however the remote wires its button internally, as long as its (+) and (−) are found with a meter. The firmware cannot see a dead battery, so it is replaced every year.

**The remote goes under the board.** At 64 × 40 mm it fits inside the board's outline, and the whole unit becomes one block of about 70 × 50 × 38 mm. It clips into the case's cover and carries no load, because the board stands on legs beside it. Its open button side faces away from the board, so the exposed contacts cannot touch the solder joints. Being close to the board could cost it some range, so this is tested before anything is stuck down. If the gate does not open reliably, the remote stays off the board on longer wires, and nothing else changes.

**External button on GPIO7, not GPIO9.** GPIO9 is a strapping pin. If something in the glove box holds a button on it at ignition, the chip stays in its download mode for that whole drive. The firmware reads GPIO7 and GPIO9 together, so BOOT still works and nothing changes if the external button is not fitted.

**A tactile button on the board, not a panel button.** A panel button is 24.5 mm long behind its flange, which fits nowhere above or below the board, so it needed 19 mm of extra case and two loose wires. A 6 × 6 mm tactile button solders straight to the board, and a flexible tab in the case's top presses it. It sits in columns 16-18, rows R-U, past the C6's edge, where the tab's post can reach it. That is the one free spot both clear of the C6 and away from where the logo sits on top. Its legs span 4 holes one way and 3 the other. The 4-hole span runs from row R to row U, because the tab's post is centred on that position. It is wired from diagonally opposite legs, because which legs connect differs between makers.

**Active buzzer straight on GPIO11, with no MOSFET and no diode.** An active buzzer has its own driver, so the pin only supplies its power. The pin's default drive is about 20 mA. This is quieter than the buzzer would be on 5 V, and it needs a bench check first: under about 20 mA at 3.3 V, and loud enough with the glove box closed. If it is too quiet or draws more than that, the firmware can first raise the pin's drive to 40 mA, the C6's limit. If that is not enough, the buzzer needs a MOSFET and 5 V, and this layout has to change. The diode is left out because a buzzer's coil stores very little energy, which is a judgement and not a maker's statement. A passive buzzer cannot use this wiring. The `buzzer_type` setting defaults to active, to match.

**Nothing under the C6 except its headers.** The owner's choice. The C6 sits across the middle of the board, leaving room above and below it, and it can be removed without disturbing anything. Wires may still pass under it on the solder side.

**MPU6050 and buzzer above the C6.** SDA and SCL are on row G, so the MPU6050's wires stay short. It starts at column 05 rather than 01 to keep it about 12.5 mm from the antenna, which leaves room for the buzzer to its right, with a gap of about 3.6 mm. This gives the parts below the C6 more room, and the buzzer takes its ground from G14 beside it. Its signal wire is longer, because GPIO11 is on row Q. Being this close to the motion sensor should not matter, because the buzzer's tone is far above the sensor's 10 Hz filter.

**MOSFET and resistors below the C6, next to GPIO10.** The resistors' own leads make the links from GPIO10 to the gate, so that path has no separate wires to break. Each resistor spans four holes, because a body of about 6.3 mm does not fit flat across two. The MOSFET's legs need about 4.3 mm before they can bend, so their holes are in column 09. These positions follow a dry fit on the real board.

**The two resistors must not be swapped.** They form a divider on the gate. With 220 Ω in series and 10 kΩ to ground, the gate reaches about 3.2 V. Swapped, it reaches under 0.1 V, and with 10 kΩ in both places it reaches about 1.65 V, so the remote will not press reliably. Use a meter to tell them apart. The owner's beige resistor with 4 bands is 10 kΩ. The blue one with 5 bands reads as 220 Ω from one end and 10 kΩ from the other, and measured about 220 Ω on the meter.

**MOSFET lies flat with the print facing the board.** The metal tab is the drain, so it must not rest on the pads. With the print up, G and S would swap in these holes, and the remote would not press. Lying flat also keeps it low and stops vibration working on its legs.

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

GPIO2  ─────────────────────────── MPU6050 SDA
GPIO3  ─────────────────────────── MPU6050 SCL
3V3    ─────────────────────────── MPU6050 VCC
GND    ─────────────────────────── MPU6050 GND
```

To find the remote's polarity, put a meter across the two button wires with the battery in. The wire under the red probe when the reading is positive is (+). Reversed, the MOSFET's internal diode conducts and the remote acts as if the button is held.

## 4. Coordinates

Hold the board with row A at the top and column 01 at the left. That face is the parts side. Holes are named by row and column, so Q11 is row Q, column 11.

Hole names always come from the parts side. The back of the board is printed so that it reads the same way up, so its letters run the other way: A on the back is X on the parts side, B is W, H is Q and R is G. The column numbers match on both sides.

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
| 220 Ω resistor | S8 to S12 | Lying flat |
| MOSFET | G T9, D U9, S V9 | Print facing the board, legs toward column 09, body toward column 01. Glue it down |
| 10 kΩ resistor | T10 to X10 | Lying flat |
| Buzzer | + C18, − C15 | The + is on the outside, nearest the board's edge |
| Button | R16, R18, U16, U18 | Stem up. 4 holes from row R to row U, and 3 holes from column 16 to column 18 |

Mark the gate leg with a pen before bending, just past the wide part of the legs. With the print facing you and the legs down, the order is G, D, S from the left. As fitted, you see its metal back and cannot read the print, and G is the leg nearest the C6. With the print up, G and S swap in these holes, G lands on V9, which is ground, and the remote will not press.

## 6. Solder side

Use thin insulated wire, except where bare offcuts or part leads are noted.

| From | To | Net | Notes |
|---|---|---|---|
| G2 | E8 | SDA | Crosses SCL, which is fine |
| G3 | E7 | SCL | |
| G1 | E6 | MPU GND | |
| Q2 | E5 | MPU VCC | Up under the C6, between columns 02 and 03 |
| S12 | Q11 | GPIO10 to the 220 Ω | The resistor's lead. Do not touch Q10 or Q12 |
| S8 | T9 | The 220 Ω to the gate | The resistor's lead |
| T10 | T9 | The 10 kΩ to the gate | The resistor's lead |
| X10 | X9 | The 10 kΩ to ground | The resistor's lead |
| V9 | X9 | Ground, through W9 | Bare offcut, soldered at all three holes |
| Q1 | V9 | GND to the source | |
| Q12 | C18 | GPIO11 to buzzer + | Up under the C6 and past the end of row G |
| G14 | C15 | Buzzer − to GND | Do not touch G15, which is 5V |
| Q7 | R16 | GPIO7 to button | Along the gap between rows Q and R |
| U18 | W9 | Button to GND | R16 and U18 are diagonally opposite legs |
| U9 | Remote (+) | MOSFET drain | Through the oval pad at column 07, by the bottom edge. Those pads have no hole, so tack the wire down with glue |
| X9 | Remote (−) | MOSFET source | Through the oval pad at column 09, by the bottom edge. Same glue |

The remote goes in the case after the tests in section 7, centred under the board, with its LED end toward row X. It covers about columns 02-17 and all the rows. Trim anything under the board longer than 2.5 mm first. [case/README.md](case/README.md) has the assembly steps.

Add a dab of hot glue at two header corners, or the USB cable and vibration will slowly pull the C6 out. Keep it clear of BOOT and RST.

## 7. Tests

**Before soldering:**

- The C6 fits: pin rows in rows G and Q, columns 01-15, and row Q reads as in section 4.
- With the C6 on its headers, the USB cable plugs into both ports without touching anything.
- On 3.3 V, the buzzer beeps steadily, which shows it is active, and draws under about 20 mA. Jumpered to GPIO11, it is loud enough with the glove box closed. Its pins fit C15 and C18.
- The button's legs fit R16, R18, U16 and U18, and once soldered, it measures about 6.5 mm from the board's top to the top of its stem. If not, change `btn_h` in [the case](case/README.md).
- A meter confirms the resistors: about 220 Ω for row S, and about 10 kΩ for column 10.
- Before it is glued, the meter proves the MOSFET's legs. Touch all three together, then hold it with the print facing you and the legs down. On diode test, red on the right leg and black on the middle leg reads about 0.5 to 0.7 V, and the left leg reads open to the middle leg both ways. That makes the left leg G.
- With 3.3 V on G and GND on S, D to S reads about 0 Ω. With G joined to S, it reads open. The meter's red probe goes on D and black on S.
- Held against the back of the board, in the glove box, the remote opens the gate reliably from the parking spot.

**Before plugging in the C6, with a meter:**

- G14 to G15 (GND to 5V) is not shorted. This is the only place 5V sits next to GND.
- Q1 to Q2 (GND to 3V3) is not shorted.
- Q10 to Q11 is not shorted. GPIO8 is pulled up on the C6, so a bridge here could press the remote at every start.
- These neighbours are not shorted either: G1 to G2, G2 to G3, G3 to G4, G13 to G14, Q2 to Q3, Q6 to Q7, Q7 to Q8, Q11 to Q12 and Q12 to Q13.
- E5 to E6 (MPU6050 VCC to GND) is not shorted.
- S8 to S12 reads about 220 Ω.
- T9 to V9 reads about 10 kΩ.
- V9 to Q1 reads about 0 Ω.
- With the remote connected and its battery in, its LED is off. If it is on, (+) and (−) are swapped.

**With the C6 plugged in:**

- Before connecting USB, Q15 to Q1 (5V to GND) is not shorted.
- Flash the current firmware, because older builds don't read the button on GPIO7. An old build folder needs `idf.py reconfigure` once.
- Power cycle the unit several times. The remote LED must stay off at every start.
- Press the remote from the page. Its LED lights for the press, and U9 to V9 reads under about 50 mV.
- Use the Beep button on the page. The buzzer is on and set to active by default. If it ever beeped as the passive type, restart the unit first, or it stays silent.
- Click the external button once. It presses the remote.
- The page shows the motion sensor as found.
- After the unit is in the car, check that the signal from the beacon has not dropped compared with the C6 alone.

## 8. Firmware and settings

- `YG_PIN_BUTTON_EXT` defaults to GPIO7 and is read together with `YG_PIN_BUTTON`.
- No other pin changes. The buzzer already uses GPIO11, and the MPU6050 already uses GPIO2 and GPIO3.
- `buzzer_type` defaults to active, because this layout only works with an active buzzer.
