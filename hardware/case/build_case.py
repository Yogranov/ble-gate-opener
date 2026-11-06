#!/usr/bin/env python3
"""Builds the case in SolidCraft from the sizes below. Run it after changing a size.

    python hardware/case/build_case.py          Saves yogigate-case.sc next to this file
    python hardware/case/build_case.py --live   Builds in the open SolidCraft window instead

X runs along the board's rows, from row A to the button end. Y runs along its columns,
from the antenna to the USB ports. Z is up, from the bottom of the cover. Hole and part
positions come from ../design.md, measured from the board's column 01 and row A edges.
"""
import json
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import zipfile

HERE =os.path.dirname(os.path.abspath(__file__))
CLI = os.environ.get("SOLIDCRAFT_CLI", r"G:\Program Files\SolidCraft\solidcraft-cli.exe")
LIVE_PORT = os.environ.get("SOLIDCRAFT_PORT", "27878")
OUT = os.path.join(HERE, "yogigate-case.sc")
PRINT_DIR = os.path.join(HERE, "print")

BLACK, WHITE = "#161616", "#f4f4f4"
MAX_OVERHANG_DEG = 50  # From vertical. Steeper than this counts as needing support, unless it can bridge.
MAX_BRIDGE_MM = 15  # Along a flat patch.
MAX_SPECK_MM2 = 1.0
LOGO_WIDTH_MM = 66  # The same as WIDTH_MM in trace_logo.py.
WALL_LOOPS = 4  # The slicer's default of 2 is thin for snap hooks and for walls with grooves in them.

# Name, value in mm or an expression, comment shown in SolidCraft's parameter table.
PARAMS = [
    # The parts. hardware/design.md says which of these are measured and which are assumed.
    ("board_l", 70, "Matrix board, along its rows. Measured"),
    ("board_w", 50, "Matrix board, along its columns. Measured"),
    ("board_t", 1.5, "Matrix board thickness. Measured, and the Cover clamps the board at this"),
    ("hole_inset", 2, "Board's corner holes, in from each edge. Assumed"),
    ("remote_l", 64, "Remote length. Measured"),
    ("remote_w", 40, "Remote width. Measured"),
    ("remote_h", 17, "Remote thickness. Measured"),
    ("under_gap", 3, "Gap between the remote and the board, for solder joints and wires"),
    ("header_h", 11, "Female header plus male header plastic. Assumed"),
    ("c6_t", 1.6, "C6 board thickness. Assumed"),
    ("c6_top", 3.2, "C6 module and USB ports, above its board. Assumed"),
    ("ant_out", 4.6, "Antenna tip, past the board's column 01 edge. Estimated"),
    ("plug_w", 12, "USB plug width. Assumed"),
    ("plug_h", 7, "USB plug thickness. Assumed"),
    ("btn_h", 6.5, "Tactile button as soldered, from the board's top to the top of its stem. Measured"),
    ("solder_tail", 2.5, "How far legs and joints stick out under the board. Trim anything longer"),
    # The case.
    ("wall", 2.4, "Side walls. Keep to a multiple of the 0.4 mm line width"),
    ("top_t", 2, "Top, including the logo's inlay"),
    # With the board's 1.5 mm, these two put the plate's top and the legs' tips on whole 0.2 mm layers.
    # The slicer rounds anything else, and at this size that could wipe out most of the clamp.
    # Plastic barely compresses, so any clamp has to come from the Cover bending. Keep it small: tape on
    # a leg's tip adds pressure easily, and a Cover that can't close is the worse failure.
    ("cover_t", 2.1, "Cover plate, up to where the Body starts, including the clamp"),
    ("clamp", 0.1, "How far each leg would reach into the board's space once the case is closed"),
    ("clr", 0.3, "Gap between the board's edges and the walls"),
    ("ant_air", 4, "Air between the antenna tip and the front wall"),
    ("top_gap", 2.6, "Gap above the C6, which also stops it leaving its headers"),
    ("rail_h", 1.5, "Rails lifting the remote, leaving room for its wires"),
    ("corner_r", 5, "Outer corner radius"),
    ("inner_r", 0.9, "Inner corner radius, which must clear the board's square corner"),
    ("post_d", 5, "Posts from the top, pressing on the board's parts side"),
    ("leg_d", 4.2, "Legs from the Cover, which the board rests on. The walls limit this"),
    ("hook_w", 10, "Snap hooks, wide. Wider holds harder"),
    ("hook_z", 16.5, "Where a snap hook's tooth starts. Higher makes a longer, gentler arm"),
    ("inlay", 0.8, "Logo depth, which is 4 layers at 0.2 mm"),
    ("tab_w", 9, "Flexible tab in the top that presses the button, wide"),
    ("tab_len", 16, "Flexible tab, from its fixed end to its tip. Longer presses more easily"),
    ("tab_t", 1.2, "Flexible tab, thick. Thinner presses more easily"),
    ("tab_slit", 1, "Slit around the flexible tab"),
    ("stem_gap", 0.5, "Gap between the tab's post and the button's stem. A bigger gap means a longer press"),
    # Worked out from the above.
    ("L", "2*wall + 2*clr + board_l", "Outside length"),
    ("W", "2*wall + ant_out + ant_air + board_w + clr", "Outside width"),
    ("z_board", "cover_t + rail_h + remote_h + under_gap", "Underside of the board"),
    ("z_parts", "z_board + board_t", "Top of the board"),
    ("z_c6", "z_parts + header_h", "Underside of the C6"),
    ("z_c6_top", "z_c6 + c6_t", "Top of the C6's board"),
    ("z_usb", "z_c6_top + c6_top/2", "Centre of the USB ports"),
    ("z_ceil", "z_c6_top + c6_top + top_gap", "Underside of the top"),
    ("H", "z_ceil + top_t", "Outside height"),
    ("x0", "wall + clr - L/2", "Board's row A edge"),
    ("y0", "wall + ant_out + ant_air - W/2", "Board's column 01 edge"),
    ("x_in", "L/2 - wall", "Inside face of the end walls"),
    ("y_in", "W/2 - wall", "Inside face of the front and back walls"),
    ("rx0", "x0 + (board_l - remote_l)/2", "Remote's key ring end"),
    ("ry0", "y0 + (board_w - remote_w)/2", "Remote's front edge"),
    ("z_remote", "cover_t + rail_h + remote_h", "Top of the remote"),
    ("btn_x", "x0 + 52.78", "Button's centre, between rows R and U"),
    ("btn_y", "y0 + 44.05", "Button's centre, on column 17"),
    ("tab_tip", "btn_x - 4", "Flexible tab's free end"),
    ("tab_root", "tab_tip + tab_len", "Flexible tab's fixed end"),
    ("tab_y0", "btn_y - tab_w/2", "Flexible tab's front edge"),
    ("tab_y1", "btn_y + tab_w/2", "Flexible tab's back edge"),
]

# Numbers for where SolidCraft needs them as plain values, such as edge picks.
V = {}
for _name, _value, _ in PARAMS:
    V[_name] = eval(_value, {}, V) if isinstance(_value, str) else _value


def value(expression):
    return eval(str(expression), {}, V)


def hole_x(row):
    """X of a hole in this row, where row A is 1."""
    return f"x0 + {5.79 + (row - 1) * 2.54:.3f}"


def hole_y(column):
    return f"y0 + {3.41 + (column - 1) * 2.54:.3f}"


# Holes and parts on the board, from hardware/design.md.
ROW_G, ROW_Q = 7, 17
C6_X0, C6_X1 = 21.03 - 1.3, 46.43 + 1.3  # From the row A edge: 1.3 past each pin row.
C6_Y0, C6_Y1 = 3.41 - 1.2, 38.97 + 8.3  # From the column 01 edge: the board without the antenna.
PORT_X = (26.03, 41.43)  # Port centres: each starts 1 mm in from its pin row and is 8 wide.
PORT_Y0, PORT_Y1 = 39.0, 38.97 + 9.4
# Only the native port, marked ESP32C6, gets an opening: it powers the unit and carries the console.
# The CH343 port stays inside, unused.
OPEN_PORTS = (26.03,)
# Slots in the back wall beside the buzzer. 0 leaves its sound to the USB opening and the tab's slit;
# 4 puts back the slots of the second print.
BUZZER_SLOTS = 0
MODULE_X0, MODULE_X1 = 24.73, 42.73  # 18 wide, centred between the pin rows.
MODULE_Y0, MODULE_Y1 = -4.6, 20.9

# Where the board is held, at its corners. A post above and a leg below meet at each one, so
# the board is clamped without being bent. They stay clear of the remote, which carries no load.
CORNERS = [
    (f"x0 + hole_inset{dx}", f"y0 + hole_inset{dy}")
    for dx in ("", " + board_l - 2*hole_inset")
    for dy in ("", " + board_w - 2*hole_inset")
]

# Snap hooks on the Cover: the side of the case, the centre along that side, and the arm's thickness.
# The end hooks are thinner, because bending in they must stay clear of the remote's ends.
# The back left hook sits off the middle, clear of the USB ledge and a remote clip.
HOOKS = [
    ("front", "-14 mm", 2.0),
    ("front", "14 mm", 2.0),
    ("back", "-23 mm", 2.0),
    ("back", "14 mm", 2.0),
    ("left", "0 mm", 1.6),
    ("right", "0 mm", 1.6),
]
# Across a hook, measured outward from the inside face of the wall.
HOOK_GAP, TOOTH = 0.2, 1.2
# A 45 degree fillet where an arm meets the plate, which spreads the bending there.
ROOT = 0.8
# The catch rises this much per mm outward, which is 30 degrees from flat. A flatter catch holds harder and
# snaps more crisply, and at about 1 mm wide it still prints without support, but flat would need a tool to open.
CATCH = 0.577
# How far the groove's catch sits above the tooth's once the case is closed. The arm stays bent, which
# keeps pulling the two halves together and takes up any play. The legs pressing the board push the Cover
# back by up to the clamp, so this has to be well over it: at 0.15 the second print felt loose with
# the parts inside.
BITE = 0.35
# The volume the teeth overlap the grooves in the model, because the model cannot show the arms bent.
HOOK_OVERLAP_MM3 = 45
# Across a remote clip, measured outward from the remote's edge. Each clip's tooth reaches 0.2 mm into
# the remote's top edge, so it stays bent and holds the remote down without play.
CLIP_FIT, CLIP_T, CLIP_BITE = 0.15, 1.8, 0.2
CLIP_OVERLAP_MM3 = 3


class Script:
    def __init__(self):
        self.commands = []

    def add(self, command, **params):
        self.commands.append({"command": command, "params": params})

    def box(self, name, x, y, z, operation="new", target=None):
        """Each of x, y and z is (from, to)."""
        params = dict(
            name=name,
            length=f"({x[1]}) - ({x[0]})",
            width=f"({y[1]}) - ({y[0]})",
            height=f"({z[1]}) - ({z[0]})",
            center=[f"(({x[0]}) + ({x[1]}))/2", f"(({y[0]}) + ({y[1]}))/2", f"(({z[0]}) + ({z[1]}))/2"],
            operation=operation,
        )
        if target:
            params["targets"] = [target]
        else:
            params["body_name"] = name
        self.add("solid.box", **params)

    def cylinder(self, name, x, y, z0, height, diameter, operation="new", target=None):
        params = dict(name=name, diameter=diameter, height=height, base=[x, y, z0], operation=operation)
        if target:
            params["targets"] = [target]
        else:
            params["body_name"] = name
        self.add("solid.cylinder", **params)

    def sketch(self, name, plane, offset):
        self.add("sketch.create", name=name, plane=plane, offset=offset)

    def wall_profile(self, name, side, centre, width, points, operation, target):
        """Extrudes a profile along a wall. Points are (outward from the wall's inside face, z)."""
        sign = {"front": "-", "back": "", "left": "-", "right": ""}[side]
        face = "y_in" if side in ("front", "back") else "x_in"
        sketch = f"{name} profile"
        if side in ("front", "back"):
            self.sketch(sketch, "YZ", f"({centre}) - {width}/2")
            points = [[f"{sign}({face} + {d})", z] for d, z in points]
        else:
            self.sketch(sketch, "XZ", f"({centre}) - {width}/2")
            points = [[f"{sign}({face} + {d})", f"-({z})"] for d, z in points]
        self.add("sketch.line", closed=True, points=points)
        self.add("sketch.finish")
        self.add("solid.extrude", name=name, sketch=sketch, distance=width, operation=operation, targets=[target])

    def wall_opening(self, x0, x1, z0, z1):
        """A rectangle on a sketch of plane XZ, where sketch y is -Z. Lines take expressions, unlike rectangles."""
        points = [[x0, f"-({z0})"], [x1, f"-({z0})"], [x1, f"-({z1})"], [x0, f"-({z1})"]]
        self.add("sketch.line", closed=True, points=points)


def add_parameters(s):
    for name, val, comment in PARAMS:
        expression = val if isinstance(val, str) else f"{val} mm"
        s.add("parameters.add", name=name, expression=expression, comment=comment)


def build_shell(s):
    s.box("Body", ("-L/2", "L/2"), ("-W/2", "W/2"), ("cover_t", "H"))
    corners = [[sx * V["L"] / 2, sy * V["W"] / 2, (V["H"] + V["cover_t"]) / 2] for sx in (-1, 1) for sy in (-1, 1)]
    s.add("solid.fillet", name="Outer corners", edges=corners, radius="corner_r")
    # On the bed, and at the seam. A chamfer prints where a round would need support.
    s.add("solid.chamfer", name="Top edge", edges=[[0, V["W"] / 2, V["H"]]], distance=0.8)
    s.add("solid.chamfer", name="Seam edge", edges=[[0, V["W"] / 2, V["cover_t"]]], distance=0.4)

    s.box("Inside", ("-x_in", "x_in"), ("-y_in", "y_in"), ("cover_t - 1", "z_ceil"), "cut", "Body")
    inner = [[sx * V["x_in"], sy * V["y_in"], (V["z_ceil"] + V["cover_t"]) / 2] for sx in (-1, 1) for sy in (-1, 1)]
    s.add("solid.fillet", name="Inner corners", edges=inner, radius="inner_r")


def build_supports(s):
    """Everything that joins the Body comes first, because a later join would fill the cuts."""
    for i, (x, y) in enumerate(CORNERS, 1):
        s.cylinder(f"Post {i}", x, y, "z_parts", "z_ceil - z_parts + 1", "post_d", "join", "Body")

    # The walls stop the board at its back and left edges. These stop it at the other two,
    # so pushing a plug in cannot slide it, and they stay out of its way as it goes in.
    z = ("z_board + 0.3", "z_ceil + 1")
    edge_y = ("y0 - clr - 1.6", "y0 - clr")
    edge_x = ("x0 + board_l + clr", "x0 + board_l + clr + 1.6")
    near_x, far_x = "x0 + hole_inset", "x0 + board_l - hole_inset"
    near_y, far_y = "y0 + hole_inset", "y0 + board_w - hole_inset"
    s.box("Stop front 1", (f"{near_x} - 2.5", f"{near_x} + 2.5"), edge_y, z, "join", "Body")
    s.box("Stop front 2", (f"{far_x} - 2.5", f"{far_x} + 2.5"), edge_y, z, "join", "Body")
    s.box("Stop end 1", edge_x, (f"{near_y} - 2.5", f"{near_y} + 2.5"), z, "join", "Body")
    s.box("Stop end 2", edge_x, (f"{far_y} - 2.5", f"{far_y} + 2.5"), z, "join", "Body")


def build_button_tab(s):
    """A tab cut into the top, whose post presses the tactile button below.

    It sits flush, so anything flat rests on the top around it and cannot push it in.
    It stays one piece with the Body, and bends along the print's layers, its strong direction.
    """
    s.box("Tab underside", ("tab_tip", "tab_root"), ("tab_y0", "tab_y1"), ("z_ceil - 1", "H - tab_t"), "cut", "Body")
    tip = "z_parts + btn_h + stem_gap"
    s.cylinder("Tab post", "btn_x", "btn_y", tip, f"H - tab_t - ({tip}) + 0.5", "4.5 mm", "join", "Body")
    # One closed outline for the U-shaped slit, with the corners at the tip cut off.
    c, o = 1.5, 1.5
    s.sketch("Tab slit", "XY", "z_ceil - 1")
    s.add(
        "sketch.line",
        closed=True,
        points=[
            ["tab_root", "tab_y0 - tab_slit"],
            [f"tab_tip - tab_slit + {o}", "tab_y0 - tab_slit"],
            ["tab_tip - tab_slit", f"tab_y0 - tab_slit + {o}"],
            ["tab_tip - tab_slit", f"tab_y1 + tab_slit - {o}"],
            [f"tab_tip - tab_slit + {o}", "tab_y1 + tab_slit"],
            ["tab_root", "tab_y1 + tab_slit"],
            ["tab_root", "tab_y1"],
            [f"tab_tip + {c}", "tab_y1"],
            ["tab_tip", f"tab_y1 - {c}"],
            ["tab_tip", f"tab_y0 + {c}"],
            [f"tab_tip + {c}", "tab_y0"],
            ["tab_root", "tab_y0"],
        ],
    )
    s.add("sketch.finish")
    s.add("solid.extrude", name="Tab cut", sketch="Tab slit", distance="top_t + 2", operation="cut", targets=["Body"])


def build_cuts(s):
    # Grooves in the walls for the snap hooks. They stop inside the wall, so nothing shows outside.
    # The lower face is the catch the tooth pulls against, and it matches the tooth's angle.
    def catch(d):
        return f"hook_z + clamp + {BITE + (d + HOOK_GAP) * CATCH:.3f}"

    for side, centre, _ in HOOKS:
        points = [
            (-0.5, catch(-0.5)),
            (TOOTH + 0.2, catch(TOOTH + 0.2)),
            (TOOTH + 0.2, "hook_z + 4.6"),
            (-0.5, "hook_z + 4.6"),
        ]
        s.wall_profile(f"Groove {side} {centre}", side, centre, "(hook_w + 1)", points, "cut", "Body")

    # The USB opening is the size of the plug, because the port sits inside the board's edge.
    s.sketch("USB openings", "XZ", "y_in - 0.5")
    for port in OPEN_PORTS:
        s.wall_opening(
            f"x0 + {port} - (plug_w + 1)/2",
            f"x0 + {port} + (plug_w + 1)/2",
            "z_usb - (plug_h + 1)/2",
            "z_usb + (plug_h + 1)/2",
        )
    s.add("sketch.finish")
    s.add("solid.extrude", name="USB cut", sketch="USB openings", distance="wall + 1", operation="cut", targets=["Body"])
    edges = []
    y = V["W"] / 2 - V["wall"] / 2
    for port in OPEN_PORTS:
        for sx in (-1, 1):
            for sz in (-1, 1):
                edges.append(
                    [
                        V["x0"] + port + sx * (V["plug_w"] + 1) / 2,
                        y,
                        V["z_usb"] + sz * (V["plug_h"] + 1) / 2,
                    ]
                )
    s.add("solid.fillet", name="USB corners", edges=edges, radius=1.5)

    # The front wall has no slots: plastic this thin barely affects the antenna, and the unit makes too
    # little heat to need vents.
    if not BUZZER_SLOTS:
        return
    s.sketch("Buzzer slots sketch", "XZ", "y_in - 0.5")
    for i in range(BUZZER_SLOTS):
        x = 10.87 + (i - (BUZZER_SLOTS - 1) / 2) * 3.2
        s.wall_opening(f"x0 + {x - 0.8:.2f}", f"x0 + {x + 0.8:.2f}", "z_ceil - 15", "z_ceil - 2")
    s.add("sketch.finish")
    s.add(
        "solid.extrude",
        name="Buzzer slots",
        sketch="Buzzer slots sketch",
        distance="wall + 1",
        operation="cut",
        targets=["Body"],
    )


def build_logo(s):
    with open(os.path.join(HERE, "logo.svg"), encoding="utf-8") as f:
        svg = f.read()
    s.sketch("Logo outline", "XY", "H - inlay")
    s.add("sketch.insert_svg", text=svg, width=LOGO_WIDTH_MM, align="center", at=[0, 0])
    s.add("sketch.finish")
    s.add("solid.extrude", name="Logo cut", sketch="Logo outline", distance="inlay + 1", operation="cut", targets=["Body"])
    s.add("solid.extrude", name="Logo", sketch="Logo outline", distance="inlay", operation="new", body_name="Logo")
    # The islands of the logo come out as separate bodies. Joined, the slicer sees one part to colour.
    islands = int(re.search(r'data-islands="(\d+)"', svg).group(1))
    tools = [f"Logo ({i})" for i in range(2, islands + 1)]
    s.add("solid.combine", name="Logo joined", target="Logo", tools=tools, operation="join")


def build_cover(s):
    # The model shows the Cover with its legs just touching the board and a gap at the seam. Closing the
    # case lifts it by the clamp, so the legs press the board, and the Cover's corners flex to allow it.
    top = V["cover_t"] - V["clamp"]
    s.box("Cover", ("-L/2", "L/2"), ("-W/2", "W/2"), ("0 mm", "cover_t - clamp"))
    corners = [[sx * V["L"] / 2, sy * V["W"] / 2, top / 2] for sx in (-1, 1) for sy in (-1, 1)]
    s.add("solid.fillet", name="Cover corners", edges=corners, radius="corner_r")
    s.add("solid.chamfer", name="Cover bottom edge", edges=[[0, V["W"] / 2, 0]], distance=0.8)
    s.add("solid.chamfer", name="Cover seam edge", edges=[[0, V["W"] / 2, top]], distance=0.4)

    # A rim that locates the Cover inside the Body and stiffens it, with gaps for the hooks.
    gap, thick = 0.2, 1.2
    z = ("cover_t - 1", "cover_t + 3")
    for name, a, b in (("1", "-30 mm", "-20 mm"), ("2", "-8 mm", "8 mm"), ("3", "20 mm", "30 mm")):
        s.box(f"Rim front {name}", (a, b), (f"-y_in + {gap}", f"-y_in + {gap + thick}"), z, "join", "Cover")
    for name, a, b in (("1", "-17 mm", "8 mm"), ("2", "20 mm", "30 mm")):
        s.box(f"Rim back {name}", (a, b), (f"y_in - {gap + thick}", f"y_in - {gap}"), z, "join", "Cover")
    for name, a, b in (("1", "-25 mm", "-6 mm"), ("2", "6 mm", "25 mm")):
        s.box(f"Rim left {name}", (f"-x_in + {gap}", f"-x_in + {gap + thick}"), (a, b), z, "join", "Cover")
        s.box(f"Rim right {name}", (f"x_in - {gap + thick}", f"x_in - {gap}"), (a, b), z, "join", "Cover")

    # Snap hooks. The arm flexes inward as the Cover goes on, then the tooth clicks into its groove.
    tip = (TOOTH + HOOK_GAP) * CATCH
    for side, centre, thick in HOOKS:
        outer, inner = -HOOK_GAP, -HOOK_GAP - thick
        points = [
            (inner - ROOT, "cover_t - 1"),
            (outer, "cover_t - 1"),
            (outer, "hook_z"),
            (TOOTH, f"hook_z + {tip:.3f}"),
            (TOOTH, f"hook_z + {tip + 0.6:.3f}"),
            (outer, f"hook_z + {tip + 3.6:.3f}"),
            (inner, f"hook_z + {tip + 3.6:.3f}"),
            (inner, f"cover_t - clamp + {ROOT}"),
            (inner - ROOT, "cover_t - clamp"),
        ]
        s.wall_profile(f"Hook {side} {centre}", side, centre, "hook_w", points, "join", "Cover")

    # Legs that the board rests on, standing beside the remote, not on it.
    for i, (x, y) in enumerate(CORNERS, 1):
        s.cylinder(f"Leg {i}", x, y, "cover_t - 1", "z_board - cover_t + 1", "leg_d", "join", "Cover")
    # A short ledge along the board's edge under the USB port, where a plug pushes down. It stops just
    # below the board, because touching it would take the pressure off the corners.
    s.box(
        "USB ledge",
        (f"x0 + {OPEN_PORTS[0] - 1.5:.2f}", f"x0 + {OPEN_PORTS[0] + 3.5:.2f}"),
        ("y0 + board_w - 2.2", f"y_in - {gap}"),
        ("cover_t - 1", "z_board - clamp - 0.1"),
        "join",
        "Cover",
    )

    # Rails under the remote's long edges, leaving its buttons and wires clear.
    z = ("cover_t - 1", "cover_t + rail_h")
    s.box("Rail front", ("rx0", "rx0 + remote_l"), ("ry0", "ry0 + 3"), z, "join", "Cover")
    s.box("Rail back", ("rx0", "rx0 + remote_l"), ("ry0 + remote_w - 3", "ry0 + remote_w"), z, "join", "Cover")

    # Stops at the remote's ends, leaving the middle open for its wires.
    fit = 0.2
    z = ("cover_t - 1", "cover_t + rail_h + 6")
    middle = "ry0 + remote_w/2"
    for name, x in (("key ring", ("rx0 - 2", f"rx0 - {fit}")), ("LED", (f"rx0 + remote_l + {fit}", "rx0 + remote_l + 2"))):
        s.box(f"Remote stop {name} 1", x, (f"{middle} - 14", f"{middle} - 10"), z, "join", "Cover")
        s.box(f"Remote stop {name} 2", x, (f"{middle} + 10", f"{middle} + 14"), z, "join", "Cover")

    # Clips that hold the remote down and keep its case shut. Each flexes outward to let it in.
    # Their tops come close to the board, so they stay out from under its solder joints.
    clips = (
        ("front 1", lambda d: f"ry0 - ({d})", "rx0 + 2", 8),
        ("front 2", lambda d: f"ry0 - ({d})", "rx0 + 53", 8),
        ("back 1", lambda d: f"ry0 + remote_w + ({d})", "x0 + 17", 7),
        ("back 2", lambda d: f"ry0 + remote_w + ({d})", "x0 + 59", 6),
    )
    # The catch is at 45 degrees and passes CLIP_BITE below the remote's top edge.
    catch = f"z_remote - {CLIP_BITE + CLIP_FIT:.2f}"
    for name, y, start, clip_w in clips:
        sketch = f"Remote clip {name} profile"
        s.sketch(sketch, "YZ", start)
        points = [
            (CLIP_FIT, "cover_t - 1"),
            (CLIP_FIT + CLIP_T, "cover_t - 1"),
            (CLIP_FIT + CLIP_T, f"{catch} + 2.4"),
            (CLIP_FIT, f"{catch} + 2.4"),
            (CLIP_FIT - 1, f"{catch} + 1.2"),
            (CLIP_FIT - 1, f"{catch} + 1"),
            (CLIP_FIT, catch),
        ]
        s.add("sketch.line", closed=True, points=[[y(d), z_] for d, z_ in points])
        s.add("sketch.finish")
        s.add(
            "solid.extrude",
            name=f"Remote clip {name}",
            sketch=sketch,
            distance=clip_w,
            operation="join",
            targets=["Cover"],
        )

    # Notches at the seam, for a fingernail or a coin to open the case. Each is right beside a hook.
    for name, x in (("left", ("-L/2 - 1", "-L/2 + 2.5")), ("right", ("L/2 - 2.5", "L/2 + 1"))):
        s.box(f"Pry notch {name}", x, ("-8 mm", "8 mm"), ("0.8 mm", "cover_t"), "cut", "Cover")


def build_reference_parts(s):
    """Mock-ups of the real parts, for checking the fit. They are never printed."""
    x, y = lambda v: f"x0 + {v}", lambda v: f"y0 + {v}"
    s.box("Ref board", ("x0", "x0 + board_l"), ("y0", "y0 + board_w"), ("z_board", "z_parts"))
    s.box("Ref remote", ("rx0", "rx0 + remote_l"), ("ry0", "ry0 + remote_w"),
          ("cover_t + rail_h", "cover_t + rail_h + remote_h"))
    s.box("Ref header G", (x(21.03 - 1.27), x(21.03 + 1.27)), (y(2.14), y(40.24)), ("z_parts", "z_c6"))
    s.box("Ref header Q", (x(46.43 - 1.27), x(46.43 + 1.27)), (y(2.14), y(40.24)), ("z_parts", "z_c6"))
    s.box("Ref C6", (x(C6_X0), x(C6_X1)), (y(C6_Y0), y(C6_Y1)), ("z_c6", "z_c6_top"))
    s.box("Ref module", (x(MODULE_X0), x(MODULE_X1)), (y(MODULE_Y0), y(MODULE_Y1)), ("z_c6_top - 0.5", "z_c6_top + c6_top"))
    for i, port in enumerate(PORT_X, 1):
        s.box(f"Ref port {i}", (x(port - 4), x(port + 4)), (y(PORT_Y0), y(PORT_Y1)), ("z_c6_top", "z_c6_top + c6_top"))
    for port in OPEN_PORTS:
        s.box(
            "Ref plug",
            (f"x0 + {port} - plug_w/2", f"x0 + {port} + plug_w/2"),
            (y(PORT_Y1), f"y0 + {PORT_Y1} + 30"),
            ("z_usb - plug_h/2", "z_usb + plug_h/2"),
        )
    s.box("Ref MPU6050", (x(1.05), x(17.05)), (y(9.42), y(30.42)), ("z_parts", "z_parts + 5.5"))
    s.box("Ref MOSFET", (x(51.49), x(61.69)), (y(2.83), y(18.32)), ("z_parts", "z_parts + 5"))
    s.cylinder("Ref buzzer", x(10.87), y(40.24), "z_parts", "9.5 mm", "12.5 mm")

    s.box("Ref button", ("btn_x - 3.25", "btn_x + 3.25"), ("btn_y - 3", "btn_y + 3"), ("z_parts", "z_parts + 3.5"))
    s.cylinder("Ref button stem", "btn_x", "btn_y", "z_parts + 3.5", "btn_h - 3.5", "3.5 mm")

    # What sticks out under the board, so nothing on the Cover may come up into it.
    # The holes used, from ../design.md: headers and MPU6050 as bars, and each other leg on its own.
    # The first unit keeps the earlier layout, so these cover both it and the later units' holes.
    z = ("z_board - solder_tail", "z_board")
    bars = ((7, 1, 15), (17, 1, 15), (5, 4, 12), (24, 9, 13))
    legs = (
        (3, 14), (3, 15), (3, 17), (3, 18), (18, 11), (18, 16), (18, 18), (19, 8), (19, 12), (20, 9),
        (20, 10), (21, 9), (21, 16), (21, 18), (22, 9), (22, 11), (23, 9), (23, 13),
    )
    first = True
    for row, col0, col1 in bars + tuple((r, c, c) for r, c in legs):
        area = (
            (f"{hole_x(row)} - 1.2", f"{hole_x(row)} + 1.2"),
            (f"{hole_y(col0)} - 1.2", f"{hole_y(col1)} + 1.2"),
        )
        s.box(f"Ref solder {row} {col0}" if not first else "Ref solder", *area, z, "new" if first else "join", None if first else "Ref solder")
        first = False


REFS = {
    "Ref board": "#2f7d4f",
    "Ref remote": "#3a5f9e",
    "Ref header G": "#222222",
    "Ref header Q": "#222222",
    "Ref C6": "#20486e",
    "Ref module": "#b8b8b8",
    "Ref port 1": "#c9c9c9",
    "Ref port 2": "#c9c9c9",
    "Ref plug": "#555555",
    "Ref MPU6050": "#7a3f9e",
    "Ref MOSFET": "#333333",
    "Ref buzzer": "#111111",
    "Ref button": "#888888",
    "Ref button stem": "#dddddd",
    "Ref solder": "#c9c9c9",
}


def build_appearance(s):
    s.add("appearance.assign", bodies=["Body", "Cover"], color=BLACK)
    s.add("appearance.assign", bodies=["Logo"], color=WHITE)
    for name, color in REFS.items():
        s.add("appearance.assign", bodies=[name], color=color)
    # It sticks out of the back and is not printed, so it stays out of the way unless asked for.
    s.add("browser.visibility", visible=False, items=["Ref plug"])
    # Its marker shows through the top face, and looks like part of the case.
    s.add("browser.visibility", visible=False, folder="origin")


def add_path_check(s):
    """The board goes into the Body from below, so anything standing under it would block it, even if it fits once in place."""
    s.box("Path board", ("x0", "x0 + board_l"), ("y0", "y0 + board_w"), ("0 mm", "z_parts"))
    s.box(
        "Path antenna",
        (f"x0 + {MODULE_X0}", f"x0 + {MODULE_X1}"),
        (f"y0 + {MODULE_Y0}", "y0"),
        ("0 mm", "z_c6_top + c6_top"),
    )
    s.add("inspect.interference", bodies=["Body", "Path board", "Path antenna"])
    s.add("edit.undo")
    s.add("edit.undo")


def script(folder=None):
    """With a folder, this also checks the fit and exports the printed parts there."""
    s = Script()
    add_parameters(s)
    build_shell(s)
    build_supports(s)
    build_cuts(s)
    build_button_tab(s)
    build_cover(s)
    build_reference_parts(s)
    if folder:
        # Before the logo, because the logo only removes material, and its many faces make this slow.
        s.add("inspect.interference", bodies=["Body", "Cover", *REFS])
        add_path_check(s)
    build_logo(s)
    build_appearance(s)
    if folder:
        s.add("document.inspect", measure=True, sketches=False, joints=False)
        os.makedirs(PRINT_DIR, exist_ok=True)
        for body in ("Body", "Logo", "Cover"):
            s.add("file.export", path=os.path.join(PRINT_DIR, f"{body.lower()}.stl"), bodies=[body], format="stl")
        s.add("file.export", path=os.path.join(PRINT_DIR, "body-and-logo.3mf"), bodies=["Body", "Logo"], format="3mf")
        s.add("file.export", path=os.path.join(PRINT_DIR, "cover.3mf"), bodies=["Cover"], format="3mf")
    return s.commands


def report_fit(results):
    """The printed parts may not overlap anything. Overlaps among the mock-ups are listed, but allowed."""
    failed = 0
    for found in (r for r in results if "interferences" in r):
        for hit in found["interferences"]:
            printed = {"Body", "Cover"} & {hit["a"], hit["b"]}
            pair = {hit["a"], hit["b"]}
            hooks = pair == {"Body", "Cover"} and hit["volume_mm3"] <= HOOK_OVERLAP_MM3
            clips = pair == {"Cover", "Ref remote"} and hit["volume_mm3"] <= CLIP_OVERLAP_MM3
            bad = bool(printed) and not (hooks or clips)
            failed += bad
            print(f"  {'FAIL' if bad else 'note'}: {hit['a']} overlaps {hit['b']} by {hit['volume_mm3']:.3f} mm3")
        for item in found.get("unchecked", []):
            failed += 1
            print(f"  FAIL: {item['a']} and {item['b']} were not checked: {item['reason']}")
        print(f"Fit: {found.get('checked')} bodies checked")
    print(f"Fit: {failed} problems")
    return failed


def report_design(results):
    found = next(r for r in results if "timeline" in r)
    failed = 0
    for feature in found["timeline"]:
        if feature.get("error"):
            failed += 1
            print(f"  FAIL: {feature['name']}: {feature['error']}")
    for body in found["bodies"]:
        if not body["name"].startswith("Ref"):
            size = [b - a for a, b in zip(body["bbox"]["min"], body["bbox"]["max"])]
            print(f"  {body['name']}: {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} mm, {body['volume_mm3'] / 1000:.1f} cm3")
    return failed


def read_stl(path):
    with open(path, "rb") as f:
        f.seek(80)
        count = struct.unpack("<I", f.read(4))[0]
        return [struct.unpack("<12x9f2x", f.read(50)) for _ in range(count)]


def overhang_patches(path, upside_down, ignore_z=None):
    """Finds surfaces too flat to print unsupported. Returns the size of each patch, as printed."""
    patches, seen, parent = [], {}, {}

    def find(a):
        parent.setdefault(a, a)
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    triangles = read_stl(path)
    sign = -1 if upside_down else 1
    bed = min(sign * t[k] for t in triangles for k in (2, 5, 8))
    flagged = {}
    for i, t in enumerate(triangles):
        a, b, c = t[0:3], t[3:6], t[6:9]
        u, v = [b[k] - a[k] for k in range(3)], [c[k] - a[k] for k in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        length = math.sqrt(sum(x * x for x in n))
        if length < 2e-5:
            continue
        angle = math.degrees(math.asin(max(-1, min(1, -sign * n[2] / length))))
        height = max(sign * a[2], sign * b[2], sign * c[2]) - bed
        # A surface in the logo's layer is printed on top of the white, so nothing overhangs there.
        if angle > MAX_OVERHANG_DEG and height > 0.01 and not (ignore_z and abs(a[2] - ignore_z) < 0.01):
            flagged[i] = (a, b, c, length / 2)
            for p in (a, b, c):
                key = tuple(round(x * 200) for x in p)
                parent[find(i)] = find(seen.setdefault(key, i))
    groups = {}
    for i in flagged:
        groups.setdefault(find(i), []).append(i)
    for members in groups.values():
        points = [p for i in members for p in flagged[i][:3]]
        spans = sorted(max(p[k] for p in points) - min(p[k] for p in points) for k in range(2))
        area = sum(flagged[i][3] for i in members)
        patches.append((area, spans[0], spans[1], [round(sum(p[k] for p in points) / len(points), 1) for k in range(3)]))
    return patches


def report_overhangs():
    """A flat patch prints as a bridge if it is short enough, taking its long side to be safe. A speck does not matter."""
    failed = 0
    jobs = (("Body", "body.stl", True, V["H"] - V["inlay"]), ("Cover", "cover.stl", False, None))
    for name, stl, upside_down, ignore_z in jobs:
        patches = overhang_patches(os.path.join(PRINT_DIR, stl), upside_down, ignore_z)
        bad = [p for p in patches if p[0] > MAX_SPECK_MM2 and p[2] > MAX_BRIDGE_MM]
        longest = max((p[2] for p in patches if p[0] > MAX_SPECK_MM2), default=0)
        print(f"Overhangs on the {name}: {len(patches)} patches, longest bridge {longest:.1f} mm, {len(bad)} problems")
        for area, short, long, centre in bad:
            print(f"  FAIL: {area:.1f} mm2, {short:.1f} x {long:.1f} mm, at {centre}")
        failed += len(bad)
    return failed


def make_print_ready(path, name, flip, extruders):
    """Rewrites a 3MF so that Bambu Studio opens it as one object, lying the right way, with each part's filament set.

    extruders gives the filament number for each body. flip turns the object over, onto its top face.
    Keep the Application as it is. Bambu Studio crashes on a file that claims to be its own project without being one.
    """
    with zipfile.ZipFile(path) as z:
        files = {n: z.read(n) for n in z.namelist()}
    model = files["3D/3dmodel.model"].decode("utf-8")

    objects = re.findall(r'<object id="(\d+)" type="model" name="([^"]+)"[^>]*>\s*(<mesh>.*?</mesh>)\s*</object>', model, re.S)
    assembly = max(int(i) for i, _, _ in objects) + 1
    meshes = "\n".join(f'  <object id="{i}" type="model">\n{mesh}\n  </object>' for i, _, mesh in objects)
    components = "\n".join(f'    <component objectid="{i}"/>' for i, _, _ in objects)
    height = V["H"] if flip else 0
    rotation = "1 0 0 0 -1 0 0 0 -1" if flip else "1 0 0 0 1 0 0 0 1"
    files["3D/3dmodel.model"] = f"""<?xml version="1.0" encoding="UTF-8"?>
<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">
 <metadata name="Application">SolidCraft</metadata>
 <resources>
{meshes}
  <object id="{assembly}" type="model">
   <components>
{components}
   </components>
  </object>
 </resources>
 <build>
  <item objectid="{assembly}" transform="{rotation} 128 128 {height:.3f}" printable="1"/>
 </build>
</model>
""".encode("utf-8")

    parts = "\n".join(
        f'    <part id="{i}" subtype="normal_part">\n'
        f'      <metadata key="name" value="{body}"/>\n'
        f'      <metadata key="extruder" value="{extruders[body]}"/>\n'
        f"    </part>"
        for i, body, _ in objects
    )
    files["Metadata/model_settings.config"] = f"""<?xml version="1.0" encoding="UTF-8"?>
<config>
  <object id="{assembly}">
    <metadata key="name" value="{name}"/>
    <metadata key="extruder" value="1"/>
    <metadata key="wall_loops" value="{WALL_LOOPS}"/>
{parts}
  </object>
</config>
""".encode("utf-8")

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for filename, data in files.items():
            z.writestr(filename, data)


def turn_stl_over(path):
    """Turns a binary STL upside down, onto the case's top face, the way it prints."""
    with open(path, "rb") as f:
        header, count = f.read(80), f.read(4)
        data = f.read()
    records = []
    for i in range(0, len(data), 50):
        v = struct.unpack("<12f", data[i : i + 48])
        turned = [v[0], -v[1], -v[2]]
        for k in range(3, 12, 3):
            turned += [v[k], -v[k + 1], V["H"] - v[k + 2]]
        records.append(struct.pack("<12f", *turned) + data[i + 48 : i + 50])
    with open(path, "wb") as f:
        f.write(header + count + b"".join(records))


def make_print_files():
    make_print_ready(os.path.join(PRINT_DIR, "body-and-logo.3mf"), "YogiGate body", True, {"Body": 1, "Logo": 2})
    make_print_ready(os.path.join(PRINT_DIR, "cover.3mf"), "YogiGate cover", False, {"Cover": 1})
    turn_stl_over(os.path.join(PRINT_DIR, "body.stl"))
    turn_stl_over(os.path.join(PRINT_DIR, "logo.stl"))


def run_headless():
    with tempfile.TemporaryDirectory() as folder:
        commands = script(folder)
        print(f"{len(commands)} steps, {V['L']:.1f} x {V['W']:.1f} x {V['H']:.1f} mm")
        path = os.path.join(folder, "case-script.json")
        out = os.path.join(folder, "results.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump({"commands": commands}, f, indent=1)
        result = subprocess.run(
            [CLI, "run", path, "--save", OUT, "--results", "--json-out", out, "--quiet"],
            capture_output=True,
            text=True,
            encoding="utf-8",
        )
        if result.returncode:
            print(result.stdout[-3000:])
            print(result.stderr[-3000:])
            return result.returncode
        with open(out, encoding="utf-8") as f:
            results = json.load(f)
        results = [r.get("result", r) if isinstance(r, dict) else r for r in results]
        results = [r for r in results if isinstance(r, dict)]
        problems = report_fit(results) + report_design(results) + report_overhangs()
    make_print_files()
    print("Case OK" if not problems else f"{problems} problems")
    return 1 if problems else 0


def run_live(commands):
    """Talks to the open app the way an MCP client does, because the script is too long to send by hand."""
    proc = subprocess.Popen(
        [CLI, "mcp", "--connect", f"127.0.0.1:{LIVE_PORT}"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    ids = iter(range(1, 100000))

    def call(method, params):
        proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": next(ids), "method": method, "params": params}) + "\n")
        proc.stdin.flush()
        return json.loads(proc.stdout.readline())

    def tool(name, arguments):
        reply = call("tools/call", {"name": name, "arguments": arguments})
        return reply["result"]["content"][0]["text"], reply["result"].get("isError", False)

    call("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "build_case", "version": "1"}})
    proc.stdin.write(json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n")

    state, _ = tool("inspect_design", {"measure": False, "sketches": False, "joints": False})
    if json.loads(state)["dirty"]:
        print("The open design has unsaved changes, and building here would discard them. Save it first.")
        return 1
    tool("new_design", {"name": "YogiGate case"})
    text, failed = tool("batch", {"commands": commands, "verbose": False})
    # A long build outlasts the call. It is still running, and pending waits for its result.
    while text.startswith("still running") or '"done": false' in text:
        text, failed = tool("pending", {})
    proc.stdin.close()
    proc.wait(timeout=30)
    print(text[-600:] if failed else "Built in the open window")
    return 1 if failed else 0


def main():
    sys.exit(run_live(script()) if "--live" in sys.argv else run_headless())


if __name__ == "__main__":
    main()
