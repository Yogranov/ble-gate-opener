#!/usr/bin/env python3
"""Checks the board layout without needing a board. Run it after changing the layout.

    python hardware/check_layout.py

It tests that the wires on board-layout.html make the circuit, that the parts fit on
the board and stay out from under the C6, and that design.md lists the same pins and
wires as the page. It cannot tell if a part was measured wrongly.
"""
import itertools
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MM = 2.54  # Hole spacing. Positions below are in holes, from column 1 and row 1.

# Sizes in mm, measured on the real parts. The buzzer is estimated from a photo.
BOARD = (1 - 3.41 / MM, 1 - 5.79 / MM, 18 + 3.41 / MM, 24 + 5.79 / MM)
PARTS = {
    "MPU6050": (5 - 1.61 / MM, 5 + 1.1 / MM - 16 / MM, 12 + 1.61 / MM, 5 + 1.1 / MM),
    "MOSFET": (8 - 19.1 / MM, 21 - 5.1 / MM, 8 - 3.5 / MM, 21 + 5.1 / MM),
    "220 ohm": (9 - 3.15 / MM, 19 - 1.25 / MM, 9 + 3.15 / MM, 19 + 1.25 / MM),
    "10 kohm": (9 - 1.25 / MM, 22 - 3.15 / MM, 9 + 1.25 / MM, 22 + 3.15 / MM),
    "Buzzer": (13.5 - 6 / MM, 22 - 6 / MM, 13.5 + 6 / MM, 22 + 6 / MM),
    "Top header": (0.5, 6.5, 15.5, 7.5),
    "Bottom header": (0.5, 16.5, 15.5, 17.5),
}
# From the main board's end past GND to the USB ports past 5V, and 1.3 mm past each pin row.
C6 = (1 - 1.2 / MM, 7 - 1.3 / MM, 15 + 9.4 / MM, 17 + 1.3 / MM)

# Holes that must end up joined. G1 and Q1 are both GND, but only join on the C6 itself.
NETS = {
    "GPIO10 to the 220 ohm": ["Q11", "S11"],
    "Gate": ["S7", "T8", "T9"],
    "Source and GND": ["Q1", "V8", "W8", "X8", "X9", "V12", "remote −", "button2"],
    "Drain": ["U8", "remote +"],
    "GPIO11": ["Q12", "V15"],
    "GPIO7": ["Q7", "R2", "button1"],
    "SDA": ["G2", "E8"],
    "SCL": ["G3", "E7"],
    "MPU6050 GND": ["G1", "E6"],
    "3V3": ["Q2", "E5"],
}
PINS = {"Q11": "10", "Q12": "11", "Q7": "7", "Q1": "GND", "Q2": "3V3", "G2": "2", "G3": "3", "G1": "GND"}
MPU_HOLES = {"VCC": "E5", "GND": "E6", "SCL": "E7", "SDA": "E8"}
# Power, USB, reset and strapping pins, which no wire may reach.
FORBIDDEN = {"5V", "12", "13", "RST", "9", "8", "4", "5", "15"}

failures = []


def fail(message):
    failures.append(message)


def read(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return f.read()


def json_const(page, name):
    return json.loads(re.search(rf"const {name} = (\[.*?\]);", page).group(1))


def read_wires(page):
    """Returns (holes, end label) for each wire on the page."""
    wires = []
    for line in re.search(r"const WIRES = \[(.*?)\n\];", page, re.S).group(1).splitlines():
        points = re.search(r"pts:\[(.*)\]", line)
        if points:
            end = re.search(r'end:"(.*?)"', line)
            wires.append((re.findall(r'"([A-X]\d+)"', points.group(1)), end.group(1) if end else None))
    return wires


def check_circuit(wires, row_g, row_q):
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    buttons = 0
    for holes, end in wires:
        for a, b in zip(holes, holes[1:]):
            parent[find(a)] = find(b)
        if end == "to button":
            buttons += 1
            end = f"button{buttons}"
        if end:
            parent[find(holes[-1])] = find(end)

    roots = {}
    for name, members in NETS.items():
        found = {find(m) for m in members}
        if len(found) != 1:
            fail(f"{name} is not fully connected: {members}")
        roots[name] = next(iter(found))
    for a, b in itertools.combinations(NETS, 2):
        if roots[a] == roots[b]:
            fail(f"{a} is shorted to {b}")

    pin = {f"G{i + 1}": n for i, n in enumerate(row_g)}
    pin.update({f"Q{i + 1}": n for i, n in enumerate(row_q)})
    for hole, name in PINS.items():
        if pin.get(hole) != name:
            fail(f"{hole} should be pin {name}, but is {pin.get(hole)}")
    for holes, _ in wires:
        for hole in holes:
            if pin.get(hole) in FORBIDDEN:
                fail(f"A wire reaches {hole}, which is pin {pin[hole]}")


def check_fit():
    for name, p in PARTS.items():
        if p[0] < BOARD[0] or p[1] < BOARD[1] or p[2] > BOARD[2] or p[3] > BOARD[3]:
            fail(f"{name} is off the board")
    for (a, pa), (b, pb) in itertools.combinations(PARTS.items(), 2):
        if min(pa[2], pb[2]) > max(pa[0], pb[0]) and min(pa[3], pb[3]) > max(pa[1], pb[1]):
            fail(f"{a} overlaps {b}")
    for name, p in PARTS.items():
        if "header" not in name and min(p[2], C6[2]) > max(p[0], C6[0]) and min(p[3], C6[3]) > max(p[1], C6[1]):
            fail(f"{name} is under the C6")
    if C6[2] > BOARD[2] + 1 / MM:
        fail("The USB ports stick out more than 1 mm past the board's edge")
    if BOARD[2] - C6[2] > 3 / MM:
        fail("The USB ports are more than 3 mm inside the board's edge")


def check_design_doc(design, wires, row_g, row_q):
    """The page and design.md both list the layout, so they could drift apart."""
    for label, pins in (("Row G", row_g), ("Row Q", row_q)):
        row = re.search(rf"^\| {label} \|(.*)\|\s*$", design, re.M)
        cells = [c.strip() for c in row.group(1).split("|")] if row else None
        if cells != pins:
            fail(f"design.md's {label} is {cells}, but the page has {pins}")

    section = design.split("## 6. Solder side")[1].split("## 7.")[0]
    rows = []
    for line in section.splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 4 and re.fullmatch(r"[A-X]\d+", cells[0]):
            rows.append((cells[0], cells[1]))
    if len(rows) != len(wires):
        fail(f"design.md lists {len(rows)} wires, but the page has {len(wires)}")
    for holes, end in wires:
        if end == "to button":
            wanted = (holes[0], f"Button, through {holes[-1]}")
        elif end:
            wanted = (holes[0], "Remote (+)" if end.endswith("+") else "Remote (−)")
        else:
            wanted = (holes[0], holes[-1])
        if wanted not in rows:
            fail(f"design.md has no row for the wire {wanted[0]} to {wanted[1]}")


def main():
    sys.stdout.reconfigure(errors="replace")
    page = read("board-layout.html")
    wires = read_wires(page)
    row_q, row_g = json_const(page, "J1"), json_const(page, "J3")
    if len(row_q) != 15 or len(row_g) != 15:
        fail("Each C6 pin row should have 15 pins")
    mpu = json_const(page, "MPU")
    for name, hole in MPU_HOLES.items():
        if f"E{5 + mpu.index(name)}" != hole:
            fail(f"MPU6050 {name} should be at {hole}")

    check_circuit(wires, row_g, row_q)
    check_fit()
    check_design_doc(read("design.md"), wires, row_g, row_q)

    for message in failures:
        print("FAIL:", message)
    if failures:
        sys.exit(1)
    print(f"Layout OK: {len(wires)} wires make the circuit, the parts fit, and design.md matches.")


if __name__ == "__main__":
    main()
