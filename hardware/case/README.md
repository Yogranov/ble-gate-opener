# YogiGate case

A two-part printed case for the car unit that snaps together with no screws, black with a white logo, about 75 × 64 × 46 mm. See [design.md](design.md) for why it is shaped this way.

**Status: the second print fitted, but its button press felt too long, and its hooks and clips were a little loose with the parts inside. This version fixes those, keeps only the USB opening in use and drops the slots. It has not been printed yet.** See [Not proven yet](#not-proven-yet).

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
| `hole_inset` | Board's corner holes, in from the edges, where the posts and legs meet | 2 |
| `header_h` | Female header plus male header plastic, with the C6 plugged in | 11 |
| `c6_top` | Top of the C6's module and USB ports, above its board | 3.2 |
| `plug_w`, `plug_h` | Body of the USB plug you will use | 12, 7 |
| `solder_tail` | The longest leg or joint under the board. Trim anything longer, or it can reach the remote | 2.5 |

Already measured: the board is 1.5 mm thick (`board_t`), and the board and released button are about 8 mm together, so the stem's top is 6.5 mm above the board (`btn_h`). The whole stack, from the remote to the top of the C6, is about 36.5 mm. The remote's size is measured, but check it fits the clips, including any rounded edges.

The case fits both board layouts: the first unit's, and the one of 2026-11-05 for the next units. Its check keeps clear of the solder joints of both.

## Printing

Print the car's case in **ASA**, which keeps its shape in a parked car's heat. PETG is fine for test prints, but through a hot summer it slowly lets the snap and the clamp loosen, and PLA softens outright. Avoid carbon-filled filament, which could weaken the signal.

1. Load black as filament 1 and white as filament 2.
2. Open `body-and-logo.3mf` in Bambu Studio. It opens with the top on the bed, the colours assigned and 4 wall loops set.
3. Use a Textured PEI plate. The slicer takes both ASA and PETG on it, and refuses PETG on some other plates. The top takes on the plate's texture.
4. Use 0.2 mm layers for both parts. The logo takes 4 layers, and the heights that clamp the board fall on whole layers. Leave supports off.
5. Print the Body, and then `cover.3mf` in black.

| Part | In ASA | In PETG | Colour changes | Supports |
|---|---|---|---|---|
| Body and logo | 1 h 32 min, 36 g black, 2 g white | 1 h 35 min, 43 g black, 2 g white | 4, all in the first few layers | None |
| Cover | 46 min, 14 g black | 48 min, 17 g black | None | None |

These come from slicing in Bambu Studio 2.7, for a P1S with a 0.4 mm nozzle, Bambu ASA and Bambu PETG Basic. ASA shrinks a little more than PETG, and the fits were tuned on PETG prints, so check the snap and the clamp on the first ASA print.

## What else you need

A 6.5 × 6 mm through-hole tactile button, 6 mm tall, which goes on the board. Velcro or Dual Lock with acrylic adhesive, like that on 3M's VHB tapes, holds the case in the glove box. The rubber adhesive on ordinary Velcro can let go in a hot car, and a unit sliding about can look like driving. There are no screws. Foam tape is optional, to damp vibration, and it needs acrylic adhesive too.

## Assembly

1. Solder the board as in [board-layout.html](../board-layout.html), including the tactile button, but leave the remote's wires about 12 cm long, so the Cover can come off with the remote still connected. Keep the hot glue on the headers.
2. Press the remote firmly into the Cover's clips, with its open button side down and its LED end toward the row X end of the board. It should then sit without play.
3. Turn the Body upside down. If the unit gets a [printed manual](../../manual.html), fold it to 60 × 33 mm and tape it flat to the inside of the top, in the middle, with its long side along the case's long side. That keeps it off the antenna and the button tab. Then lower the board in, parts side first, with the USB port marked ESP32C6 facing the opening. The C6's other port, marked CH343, stays inside, unused. The board rests on the 4 posts, inside the stops, with the button under the tab's post.
4. Trim anything under the board longer than 2.5 mm. Fold the wires into the space along the front wall, clear of the hooks. If you want foam, add 2 to 3 mm tape on the remote's back in the area under the C6, or on top of the C6's module if there is no printed manual, because they would share the same gap. Press the Cover on firmly until all 6 hooks snap. The 4 legs now press the board against the 4 posts.
5. Check the board: with the Cover on, shake the case, and it should not rattle. The clamp is small on purpose, so if a corner is loose, stick a layer of tape on that leg's tip. If the Cover won't close fully, sand the leg tips a little.
6. Check the button: press the tab and feel it click. It has a 0.5 mm gap before it reaches the button. If the unit acts as if the button is held down when you let go, the tab's post is pressing on it, so check `btn_h`.
7. Stick the Velcro to the Cover.

**To change the battery**, pull the case off its Velcro and turn it upside down. Lever the Cover off with a coin at the notch at either end. Lift the Cover away with the remote on it, and spread the clips to take the remote out. Keep the Body upside down while the Cover is off, because only the Cover holds the board in.

## Changing the case

Change the sizes at the top of `build_case.py`, then run:

```powershell
python hardware/case/build_case.py
```

It takes about a minute, then reports the fit and overhang checks, and writes the design and print files. To watch it build in the open SolidCraft window instead, add `--live`. That only builds the model there, so run it without `--live` to get the design and print files. It needs [SolidCraft](https://github.com/bherbruck/solidcraft), and only Python's standard library.

After changing the logo template, or `WIDTH_MM` in `trace_logo.py` along with the width in `build_case.py`, run this first:

```powershell
pip install numpy opencv-python-headless
python hardware/case/trace_logo.py
```

## Not proven yet

Remove each of these once it has been tested.

- **This version** has not been printed. The fit checks use mock-ups built from the sizes above.
- **Snap hooks:** the deeper teeth, thicker arms and 0.35 mm bite have not been printed. How firmly they hold, how easily they open, and how many times they can flex are calculated, not measured. If they are too tight or too loose, change `hook_w` or `hook_z`, or `TOOTH`, `CATCH` or `BITE` in `build_case.py`.
- **Remote clips:** the thicker clips and their 0.2 mm bite into the remote have not been printed. If the remote won't go in, make `CLIP_FIT` larger or `CLIP_BITE` smaller in `build_case.py`.
- **Board clamp:** the file leaves 1.4 mm for the 1.5 mm board at all 4 corners, but a print's error of about 0.1 mm is the same size as that squeeze. Whether all 4 press firmly can only be checked on the print. With a 1.5 mm board, `clamp` only stays on whole layers at odd tenths, such as 0.1 and 0.3.
- **Button tab:** the 0.5 mm gap and a press of about 0.6 kg are calculated, not tested. Make `tab_t` thinner or `tab_len` longer to press more easily, or do the opposite to make it firmer.
- **Buzzer:** with no slots, whether it is loud enough in a closed glove box. On the second print, tape over the buzzer slots, plug the cable in, and use Beep on the page. If it is too quiet, set `BUZZER_SLOTS = 4` in `build_case.py`.
- **ASA:** the fits were tuned on PETG prints, and ASA shrinks a little more.
- **Slicing:** the files were sliced from Bambu Studio's command line, not its window. Look at the preview before printing, especially the logo's first layers.
- **Printed manual:** the fit of the folded sheet under the top is calculated, and has not been tried. Nor has the effect of folded paper that close to the C6 on its signal, though it stays off the antenna.
- **Signal:** compare the beacon's signal with the case and without it, in the car.
- **Remote range:** if the remote has to move off the board, as in the [board design](../README.md#not-proven-yet), the Cover needs an opening for its wires, and the case can be about 20 mm shorter.
- **Heat:** ASA in a glove box through a summer, especially the snap hooks and the clamp, which heat slowly relaxes. The sensor already read 48 °C after one parked hour in November.
