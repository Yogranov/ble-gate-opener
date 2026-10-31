# YogiGate case

A two-part printed case for the car unit that snaps together with no screws, black with a white logo, about 75 × 64 × 46 mm. See [design.md](design.md) for why it is shaped this way.

**Status: designed, not printed yet.** See [Not proven yet](#not-proven-yet).

| File | What |
|---|---|
| `print/body-and-logo.3mf` | The Body and logo, ready to slice: already upside down, with the logo on filament 2 |
| `print/cover.3mf` | The Cover, ready to slice |
| `print/body.stl`, `logo.stl`, `cover.stl` | The same parts as STL, each the way it prints. Load the Body and logo together as one object, and set the logo's filament yourself. `build_case.py` makes these, and they are not kept in git |
| `yogigate-case.sc` | The design, to open in SolidCraft. The bodies named `Ref` are mock-ups of the real parts, and are not printed |
| [build_case.py](build_case.py) | Builds and checks all of the above from the sizes at its top |
| [trace_logo.py](trace_logo.py), [logo.svg](logo.svg) | Traces the logo template into the outline the case uses |

## Measure first

These sizes are assumed, and a wrong one can stop the case fitting. Measure them on the real parts, then change them at the top of `build_case.py` and rebuild.

| Parameter | What to measure | Assumed (mm) |
|---|---|---|
| `board_t` | Matrix board thickness, because the board is clamped at this size | 1.6 |
| `hole_inset` | Board's corner holes, in from the edges, where the posts and legs meet | 2 |
| `header_h` | Female header plus male header plastic, with the C6 plugged in | 11 |
| `c6_top` | Top of the C6's module and USB ports, above its board | 3.2 |
| `plug_w`, `plug_h` | Body of the USB plug you will use | 12, 7 |
| `btn_h` | Tactile button, from the board to the top of its stem. If it is taller than this, the case holds it pressed all the time | 5 |

The remote's size is measured, but check it fits the clips, including any rounded edges.

## Printing

Print in **PETG or ASA, not PLA**, which can soften in a parked car. Avoid carbon-filled filament, which could weaken the signal.

1. Load black as filament 1 and white as filament 2.
2. Open `body-and-logo.3mf` in Bambu Studio. It opens with the top on the bed, the colours assigned and 4 wall loops set.
3. Use a Textured PEI plate, because the slicer refuses PETG on the wrong plate. The top takes on the plate's texture.
4. Use 0.2 mm layers, so that the logo takes 4 layers. Leave supports off.
5. Print the Body, and then `cover.3mf` in black.

| Part | Time | Filament | Colour changes | Supports |
|---|---|---|---|---|
| Body and logo | 1 h 40 min | 42 g black, 2 g white | 4, all in the first few layers | None |
| Cover | 47 min | 17 g black | None | None |

These come from slicing in Bambu Studio 2.7, for a P1S with a 0.4 mm nozzle and Bambu PETG Basic.

## What else you need

A 6 × 6 mm through-hole tactile button, 5 mm tall, which goes on the board. A piece of Velcro or Dual Lock holds the case in the glove box. There are no screws, and no foam or tape.

## Assembly

1. Solder the board as in [board-layout.html](../board-layout.html), including the tactile button, but leave the remote's wires about 12 cm long, so the Cover can come off with the remote still connected. Keep the hot glue on the headers.
2. Press the remote into the Cover's clips, with its open button side down and its LED end toward the row X end of the board.
3. Turn the Body upside down, and lower the board in, parts side first, with the USB ports facing their openings. It rests on the 4 posts, inside the stops, with the button under the tab's post.
4. Fold the wires into the space along the front wall, clear of the hooks. Press the Cover on until all 6 hooks click. The 4 legs and the ledge now hold the board against the posts.
5. Check the button: press the tab and feel it click. If the unit acts as if the button is held down when you let go, the tab's post is pressing on it, so make `stem_gap` larger or check `btn_h`.
6. Stick the Velcro to the Cover.

**To change the battery**, pull the case off its Velcro and turn it upside down. Lever the Cover off with a fingernail or a coin at the notch at either end. Lift the Cover away with the remote on it, and spread the clips to take the remote out. Keep the Body upside down while the Cover is off, because only the Cover holds the board in.

## Changing the case

Change the sizes at the top of `build_case.py`, then run:

```powershell
python hardware/case/build_case.py
```

It takes about 10 minutes, then reports the fit and overhang checks, and writes the design and print files. To watch it build in the open SolidCraft window instead, add `--live`. That only builds the model there, so run it without `--live` to get the design and print files. It needs [SolidCraft](https://github.com/bherbruck/solidcraft), and only Python's standard library.

After changing the logo template, or `WIDTH_MM` in `trace_logo.py` along with the width in `build_case.py`, run this first:

```powershell
pip install numpy opencv-python-headless
python hardware/case/trace_logo.py
```

## Not proven yet

Remove each of these once it has been tested.

- The case has not been printed, and has not held the real parts. The fit checks use mock-ups built from the sizes above.
- **Snap hooks:** how firmly they hold, how easily they open, and how many times they can flex are calculated, not measured. If they are too tight or too loose, change `hook_w` or `hook_z`, or `TOOTH` in `build_case.py`.
- **Board clamp:** the board has to sit firmly between the posts and legs without rattling, which depends on the real board's thickness and on print accuracy. The motion sensor needs it held firmly.
- **Slicing:** the files were sliced from Bambu Studio's command line, not its window. Look at the preview before printing, especially the logo's first layers.
- **Button tab:** how firmly it presses and how it feels are calculated, not tested. The gap to the button depends on the real button's height and on print accuracy. Make `tab_t` thinner or `tab_len` longer to press more easily, or do the opposite to make it firmer.
- **Signal:** compare the beacon's signal with the case and without it, in the car.
- **Remote range:** if the remote has to move off the board, as in the [board design](../README.md#not-proven-yet), the Cover needs an opening for its wires, and the case can be about 20 mm shorter.
- **Heat:** PETG in a glove box through a summer, especially the snap hooks.
