# YogiGate case design

**Date:** 2026-11-01, updated 2026-11-05 after the second print
**Scope:** The 3D-printed case for the car unit in [../design.md](../design.md). This file covers why the case is shaped this way. [README.md](README.md) covers printing and assembly.

## 1. What it is

Two printed parts that snap together with no screws, about 75 × 64 × 46 mm when closed, printed without supports.

| Part | Holds | Printed |
|---|---|---|
| Body: top and walls | Flexible button tab, white logo, posts above the board | Upside down, with the top on the bed |
| Cover: bottom plate | Remote, legs under the board, snap hooks | Flat, with its outside on the bed |

Inside, from the bottom up: Cover, rails, remote, a 3 mm gap, matrix board, headers, C6. This is the stack in the [board design](../design.md), with rails under the remote and a gap above the C6.

## 2. Decisions

**The board stands on legs, and the remote carries no load.** The remote is not a solid block: its keypad is off, wires leave its open side, and its case comes apart. So 4 legs rise from the Cover beside the remote to the board's corners, and 4 posts come down from the top to meet them. Each post and leg are directly opposite each other, so the board is clamped without being bent. A short ledge under the USB port takes the push from a plug, and stops 0.1 mm below the board so it doesn't take pressure off the corners.

**The legs press the board at all 4 corners, with a small clamp.** The motion sensor needs the board held firmly. With the case closed, 23.0 mm separates the Body's rim from each post's tip, and each leg stands 21.6 mm above the Cover's plate, which leaves 1.4 mm for the 1.5 mm board. That 0.1 mm squeeze is deliberately small. Plastic barely compresses, so a clamp can only come from the Cover bending, and too much would hold it open, which is the worse failure. A print's error of about 0.1 mm could also leave a corner with no pressure, so a layer of tape on that leg's tip adds to it. The `clamp` parameter changes all four, but with a 1.5 mm board only odd tenths, such as 0.1 and 0.3, keep the heights on whole 0.2 mm layers, where the slicer doesn't round them. So print both parts at 0.2 mm.

**Stops keep the board in place sideways.** The walls stop its back and left edges, and small stops on the Body stop the other two. Pushing a plug in pushes the board against the front stops. They locate the board by its edges, because its edges are measured and the spacing of its corner holes is not.

**The two parts snap together, with no screws.** Six hooks on the Cover click into grooves in the Body's walls. The grooves stop inside the wall, so nothing shows on the outside, and the wall stays 1.0 mm thick there. A hook's tooth is 1.2 mm deep, and its arm is 2 mm thick and 14.5 mm long. The two hooks at the ends are 1.6 mm thick, because as they bend in, they must stay clear of the remote's ends. A 45° fillet where each arm meets the plate spreads the bending there. The back hook nearest the USB port sits off the middle, clear of the USB ledge and a remote clip.

**The hooks catch at 30° from flat, and stay bent when closed.** A flatter catch holds harder and snaps more crisply, and being narrow, it still prints without support. A flat catch would hold harder still, but it could not be opened without a tool. The groove's catch sits 0.35 mm above the tooth's once the case is closed, so the arm never fully relaxes, and keeps pulling the two halves together. That takes up print tolerances and stops rattling. It has to be well over the clamp, because the legs pressing the board push the Cover back by up to 0.1 mm. The first print had a gap at the catch instead. The second print's 0.15 mm was mostly used up by the clamp, and it still felt a little loose with the parts inside. Pressing it closed is estimated to take about 60 N in all, and each hook on a long side to hold about 60 N before it lets go.

**Opening takes a coin.** A notch at the seam at each end sits right beside a hook. Levering there releases that hook, and the rest follow.

**The Body keeps the board, and the Cover keeps the remote.** The remote's battery is changed every year. Turned upside down with the Cover off, the Body is a bowl with the board resting on its posts, so the remote can come away on its wires while the board stays put. The board is only held in by the Cover, so it falls out if the open Body is turned the other way up. Clips on the board would prevent that, but they would add four more parts to get right, for something that happens once a year.

**Clips hold the remote without play, clear of the solder joints.** Four clips on the Cover hold it down, and stops at its ends locate it, 0.2 mm off. Each clip is 1.8 mm thick and sits 0.15 mm off the remote's side, and its tooth reaches 0.2 mm into the remote's top edge, so it stays slightly bent and holds the remote down. The second print's thinner clips had 0.3 mm of play, which let the remote move. The clips also keep the remote's case shut, which used to need tape. The rails leave a 1.5 mm gap under it for the wires on its open button side. The clips' tops come within about 1 mm of the board, so they sit where no leg comes through it. On the first print, the two back clips sat under the buzzer's and button's legs, which stopped the Cover closing.

**The logo is a flush inlay, 0.8 mm deep.** Printing the top face down on the bed gives it an even finish, with the two colours level. Only the first 4 layers hold both colours, and the rest of the print is black, which comes to 4 colour changes when sliced. A glued-on badge would need a single colour change, but it adds a seam and a gluing step. Below about 0.6 mm, the black behind the white starts to show.

**The logo is simplified for a 0.4 mm nozzle.** Gaps narrower than about 0.5 mm are widened rather than filled, so the letters stay separate. Lines thinner than 0.5 mm are removed. The padlocks on the remote icon were about 1.5 mm across and would print as blobs, so they are plain dots. [trace_logo.py](trace_logo.py) makes [logo.svg](logo.svg) from the template in the repo's root.

**A flexible tab in the top presses a tactile button on the board.** A U-shaped slit cuts a 9 × 16 mm tab out of the top, and a post under its free end reaches down to the button's stem. It takes no extra part or wires, so the case stays two pieces, and it removes the 19 mm of extra length a panel button needed. The tab is printed flat, so it bends along the layers, its strong direction.

**The post stops 0.5 mm short of the button.** As soldered, the board and released button measure about 8 mm together, so the stem's top is about 6.5 mm above the 1.5 mm board. The button travels about 0.5 mm. The post ends 7.0 mm above the board, which leaves 0.5 mm of gap. The second print left 1 mm, which made the press feel too long, and the first print assumed a 5 mm button, so its post held the button down. The tab moves about 1 mm to press it, and it is 1.2 mm thick to keep that easy, at about 0.6 kg by calculation.

**The tab is flush, which is what protects it.** A single click opens the gate, so nothing in the glove box may press it by accident. Anything flat or long rests on the rigid top around the tab, so a book, a pile of papers or a pen lying across it cannot push it in. Only something smaller than the tab, pressing on it with about 0.5 kg, could trigger it. That force is calculated from the tab's bending and the button's own force. Making `tab_t` thicker or `tab_len` shorter makes it firmer. A recessed tab would protect it further, but the top prints on the bed, so a recess would need support.

**The tab sits at the back, near the end, above the gate arm's tip.** This is where the free spot on the board is, in columns 16-18 and rows R-U, past the C6's edge. The logo's top right corner is empty, so the tab doesn't touch it, and the logo can sit in the middle of the top, 66 mm wide.

**One USB opening, the size of the plug, 13 × 8 mm.** Only the native port, marked ESP32C6, is open: it powers the unit and carries the console. The C6's other port, marked CH343, stays inside, unused. The port ends about 1.6 mm inside the board's edge, with the wall beyond it, so a plug's body has to enter the wall to seat fully. The opening has 0.5 mm of clearance around an assumed 12 × 7 mm plug.

**No slots.** The first two prints had slots in front of the antenna and beside the buzzer. A 2.4 mm plastic wall barely affects a 2.4 GHz antenna, so the front slots did little for the signal, and the unit makes too little heat to need vents. The buzzer's sound now leaves through the USB opening, which a plugged-in cable mostly fills, and through the button tab's slit. That is about 60 mm² in all, against the 80 mm² the buzzer slots added, so the buzzer will be somewhat quieter. `BUZZER_SLOTS` in [build_case.py](build_case.py) puts 4 back.

**At least 4 mm of air around the antenna.** The signal is already weak in the glove box, so nothing goes near the antenna, and there is no metal in the case's parts. Carbon-filled filament could absorb signal, so the case is plain plastic.

**Foam is optional, and only damps vibration.** The board is held by the legs and posts, so foam never carries it. A 2 to 3 mm foam tape fits on top of the C6's module, under the top, or on the remote's back in the area under the C6, between rows I and O, where no joints come through. A folded printed manual uses the gap above the C6's module too, so with one in place, only the remote's back takes foam.

**The gap above the C6 holds it in its headers.** It can lift only about 2.6 mm before it reaches the top, which is less than its pins' depth in the headers. The hot glue in the board design is still worth keeping, against sideways force from the USB cable. Hot glue softens in a hot car, and the gap still holds the C6 in when it does.

**Inner corners are small.** The board's square corner has to fit in the case's corner, which limits the inner radius to 0.9 mm and the outer radius to 5 mm. The wall at that corner is about 1.7 mm.

**ASA for the car, PETG for test prints, never PLA.** The sensor read 48 °C after one parked hour in November, so a summer glove box will get much hotter. PETG softens at about 70 °C, and well before that it creeps under a steady load. Here that load is the bent hooks and the clamp, so a PETG case would slowly loosen through a summer. ASA keeps its shape to about 95 °C. The P1S is enclosed, so it prints ASA, and the slicer takes it with the same settings on the same plate. ASA shrinks a little more than PETG, so check the snap and the clamp on the first ASA print.

**No vents.** The unit runs on ignition power, so it is off while the car is parked and adds no heat to a hot glove box. Driving, it uses about half a watt, which warms a case this size by a couple of degrees. Vents could not bring it below the glove box's own temperature anyway.

**Heat-proof adhesive.** The Velcro or Dual Lock under the case, and any foam tape, need an acrylic adhesive, like that on 3M's VHB tapes. The rubber adhesive on ordinary Velcro can let go in a hot car, and a unit sliding about can look like driving.

**The status LED does not show yet.** Its position on the C6 was not recorded. Once it is measured, a small hole behind a white part of the logo could let it glow through.

## 3. How it prints without supports

The Body prints top-down, so a surface facing up on the finished case becomes an overhang in the printer.

| Feature | Why it needs no support |
|---|---|
| Top edge | 0.8 mm chamfer at 45° |
| Button tab | Lies flat on the bed, and its post grows straight up from it |
| Hook grooves and teeth | The catch overhangs by only about 1.4 mm, at 30° from flat |
| Hook roots | A 45° fillet |
| Remote clips | The face that overhangs is at 45° |
| USB opening | About a 12 mm bridge |

The hook arms bend across the print's layers, which is plastic's weak direction. While the case closes, the hooks on the long sides bend about 1.7% at their root, and the end hooks about 1.4%. Once closed, they stay bent at about 0.9% with no board in and 0.6% with one, which heat slowly relaxes. The first two prints' hooks bent about 1.1% and 1.6% without breaking, so only a new print can prove these.

## 4. Checks

[build_case.py](build_case.py) runs these on every build, and exits with an error if one fails.

- **Fit:** the Body and Cover may not overlap each other or any mock-up of a real part, including the legs and joints under the board, and the button. The only overlaps allowed are the hooks' teeth biting into their grooves, up to 45 mm³ in all, and the remote clips biting into the remote, up to 3 mm³, because the model cannot show the arms bent.
- **Way in:** nothing on the Body may stand in the path of the board and antenna as they go in. The fit check alone cannot catch this.
- **Overhangs:** every surface more than 50° from vertical in the print orientation has to be a bridge of 15 mm or less. The logo's pockets don't count, because the white is printed under them.

The mock-ups are the bodies named `Ref` in the design. Their sizes are only as good as the parameters, so the checks cannot catch a wrongly measured part.

## 5. Sizes

Every main size is a named parameter at the top of [build_case.py](build_case.py), and in SolidCraft's parameter table. The board design lists which part sizes are [measured and which are assumed](../design.md#details-of-the-real-parts).
