# YogiGate case design

**Date:** 2026-11-01
**Scope:** The 3D-printed case for the car unit in [../design.md](../design.md). This file covers why the case is shaped this way. [README.md](README.md) covers printing and assembly.

## 1. What it is

Two printed parts that snap together with no screws, about 75 × 64 × 46 mm when closed, printed without supports.

| Part | Holds | Printed |
|---|---|---|
| Body: top and walls | Flexible button tab, white logo, posts above the board | Upside down, with the top on the bed |
| Cover: bottom plate | Remote, legs under the board, snap hooks | Flat, with its outside on the bed |

Inside, from the bottom up: Cover, rails, remote, a 3 mm gap, matrix board, headers, C6. This is the stack in the [board design](../design.md), with rails under the remote and a gap above the C6.

## 2. Decisions

**The board stands on legs, and the remote carries no load.** The remote is not a solid block: its keypad is off, wires leave its open side, and its case comes apart. So 4 legs rise from the Cover beside the remote to the board's corners, and 4 posts come down from the top to meet them. Each post and leg are directly opposite each other, so the board is clamped without being bent. A short ledge under the USB ports takes the push from a plug.

**Stops keep the board in place sideways.** The walls stop its back and left edges, and small stops on the Body stop the other two. Pushing a plug in pushes the board against the front stops. They locate the board by its edges, because its edges are measured and the spacing of its corner holes is not.

**The two parts snap together, with no screws.** Six hooks on the Cover click into grooves in the Body's walls. The grooves stop inside the wall, so nothing shows on the outside, and the wall stays 1.4 mm thick there. A hook's tooth is 0.8 mm deep and its arm is about 13 mm long, which keeps the bending low enough to open and close the case many times.

**The hooks grip at 45°.** That is the steepest angle both parts can print without support. It also makes each hook pull the two halves together, which takes up print tolerances and stops rattling. A 90° catch would hold harder, but it would need support or a visible window through the wall, and it could not be opened by pulling. Each hook is estimated to hold about 12 N, so about 70 N in all.

**Opening takes a fingernail or a coin.** A notch at the seam at each end sits right beside a hook. Levering there releases that hook, and the rest follow.

**The Body keeps the board, and the Cover keeps the remote.** The remote's battery is changed every year. Turned upside down with the Cover off, the Body is a bowl with the board resting on its posts, so the remote can come away on its wires while the board stays put. The board is only held in by the Cover, so it falls out if the open Body is turned the other way up. Clips on the board would prevent that, but they would add four more parts to get right, for something that happens once a year.

**Clips hold the remote.** Four flexible clips on the Cover hold it down, and stops at its ends locate it. They also keep its case shut, which used to need tape. The rails leave a 1.5 mm gap under it for the wires on its open button side.

**The logo is a flush inlay, 0.8 mm deep.** Printing the top face down on the bed gives it an even finish, with the two colours level. Only the first 4 layers hold both colours, and the rest of the print is black, which comes to 4 colour changes when sliced. A glued-on badge would need a single colour change, but it adds a seam and a gluing step. Below about 0.6 mm, the black behind the white starts to show.

**The logo is simplified for a 0.4 mm nozzle.** Gaps narrower than about 0.5 mm are widened rather than filled, so the letters stay separate. Lines thinner than 0.5 mm are removed. The padlocks on the remote icon were about 1.5 mm across and would print as blobs, so they are plain dots. [trace_logo.py](trace_logo.py) makes [logo.svg](logo.svg) from the template in the repo's root.

**A flexible tab in the top presses a tactile button on the board.** A U-shaped slit cuts a 9 × 16 mm tab out of the top, and a post under its free end reaches down to the button's stem. It takes no extra part or wires, so the case stays two pieces, and it removes the 19 mm of extra length a panel button needed. The tab is printed flat, so it bends along the layers, its strong direction.

**The tab is flush, which is what protects it.** A single click opens the gate, so nothing in the glove box may press it by accident. Anything flat or long rests on the rigid top around the tab, so a book, a pile of papers or a pen lying across it cannot push it in. Only something smaller than the tab, pressing on it with about 0.5 kg, could trigger it. That force is calculated from the tab's bending and the button's own force. Making `tab_t` thicker or `tab_len` shorter makes it firmer. A recessed tab would protect it further, but the top prints on the bed, so a recess would need support.

**The tab sits at the back, near the end, above the gate arm's tip.** This is where the free spot on the board is, in columns 16-18 and rows R-U, past the C6's edge. The logo's top right corner is empty, so the tab doesn't touch it, and the logo can sit in the middle of the top, 66 mm wide.

**The USB openings are the size of the plug, 13 × 8 mm.** The ports end about 1.6 mm inside the board's edge, with the wall beyond them, so a plug's body has to enter the wall to seat fully. Each opening has 0.5 mm of clearance around an assumed 12 × 7 mm plug. The 2.4 mm between them stays as a rib.

**At least 4 mm of air around the antenna, with slots beside it.** The signal is already weak in the glove box, so the wall in front of the antenna is mostly slots, and there is no metal in the case's parts. Carbon-filled filament could absorb signal, so the case should be plain PETG or ASA.

**The gap above the C6 holds it in its headers.** It can lift only about 2.6 mm before it reaches the top, which is less than its pins' depth in the headers. The hot glue in the board design is still worth keeping, against sideways force from the USB cable.

**Inner corners are small.** The board's square corner has to fit in the case's corner, which limits the inner radius to 0.9 mm and the outer radius to 5 mm. The wall at that corner is about 1.7 mm.

**PETG or ASA, not PLA.** A parked car's glove box can get hot enough to soften PLA, and the snap hooks would be the first thing to suffer. PETG needs no enclosure on a Bambu printer, so it is the easy choice.

**The status LED does not show yet.** Its position on the C6 was not recorded. Once it is measured, a small hole behind a white part of the logo could let it glow through.

## 3. How it prints without supports

The Body prints top-down, so a surface facing up on the finished case becomes an overhang in the printer.

| Feature | Why it needs no support |
|---|---|
| Top edge | 0.8 mm chamfer at 45° |
| Button tab | Lies flat on the bed, and its post grows straight up from it |
| Hook grooves in the walls | The face that overhangs is at 45° |
| Hook teeth and remote clips | The face that overhangs is at 45° |
| USB openings | About a 12 mm bridge across each |
| Slots | A 1.6 mm bridge across each |

The hook arms bend across the print's layers, which is plastic's weak direction. The bending works out at about 1.1%, within what PETG should take, but only a real print can prove it.

## 4. Checks

[build_case.py](build_case.py) runs these on every build, and exits with an error if one fails.

- **Fit:** the Body and Cover may not overlap each other or any mock-up of a real part.
- **Way in:** nothing on the Body may stand in the path of the board and antenna as they go in. The fit check alone cannot catch this.
- **Overhangs:** every surface more than 50° from vertical in the print orientation has to be a bridge of 15 mm or less. The logo's pockets don't count, because the white is printed under them.

The mock-ups are the bodies named `Ref` in the design. Their sizes are only as good as the parameters, so the checks cannot catch a wrongly measured part.

## 5. Sizes

Every main size is a named parameter at the top of [build_case.py](build_case.py), and in SolidCraft's parameter table. The board design lists which part sizes are [measured and which are assumed](../design.md#details-of-the-real-parts).
