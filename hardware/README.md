# YogiGate hardware

The car unit is hand-built on a matrix board: an ESP32-C6, a motion sensor, and a MOSFET that presses the button of a gate remote.

**Status: built and bench-tested over USB, with the remote not yet connected, and never in a car.** See [Not proven yet](#not-proven-yet).

To build it, open [board-layout.html](board-layout.html) in a browser. It needs no internet.

| File | What |
|---|---|
| [board-layout.html](board-layout.html) | Drawings of both sides, a side view, checks, build order and tests |
| [design.md](design.md) | Why each part and position was chosen, details of the real parts, and the layout as tables |
| [check_layout.py](check_layout.py) | Checks the layout's wiring and fit, without needing a board |
| [case/](case/README.md) | The 3D-printed case, with its print files and assembly steps. In the case, the remote sits in the cover and is not taped to the board |

## Parts

| Part | Size (mm) | Status |
|---|---|---|
| Matrix board, rows A-X, columns 01-18 | 70 × 50 | Have |
| MuseLab ESP32-C6 board | 53 × 28 | Have |
| MPU6050 module | 21 × 16 | Have |
| P30N06LE MOSFET | | Have |
| BFT remote, 2 buttons, 12 V 23A battery | 64 × 40 × 17 | Have |
| Active buzzer | About 12.5 across | Have |
| Tactile button, through-hole | 6.5 × 6, 6 tall | Have |
| 220 Ω and 10 kΩ resistors, 1 of each | | Have |
| 15-pin female headers | | 2 needed |
| 15-pin male headers, for the C6 | | 2 needed |
| 8-pin male header, for the MPU6050 | | 1 needed |
| Thin insulated wire, hot glue | | Needed |

The GPIO for each function is in the [main README](../README.md#hardware).

## Changing the layout

The layout is in two places: `board-layout.html`, whose drawings come from the lists in its script, and the tables in `design.md`. Change both, then run:

```powershell
python hardware/check_layout.py
```

If the part's position changes, update the sizes at the top of the script too. If a GPIO changes, update `main/Kconfig.projbuild` and the table in the main README as well.

## Not proven yet

Remove each of these once it has been tested.

- The remote has not been connected, so a real press has not been tested. Nor has holding the external button for 3 s or 15 s.
- **Remote range:** being against the board could shorten it. If the gate does not open reliably, the remote goes on longer wires, off the board.
- **Remote:** its button circuit and current are unknown, so only a real press from the unit proves the MOSFET works it.
- **Buzzer:** it is active and beeps clearly on GPIO11 on the bench, but its loudness in the closed glove box and its current are not measured. If it is too quiet, the firmware can raise the pin's drive to 40 mA. If that is not enough, it needs a MOSFET and 5 V, and the layout changes.- **C6 position:** how far the pins sit from each end of the C6 comes from a photo, so the distances at each edge could be off by about 1 mm.
- **Assumed sizes:** [design.md](design.md#details-of-the-real-parts) lists every size that was assumed rather than measured. Measure those parts before printing the case.
