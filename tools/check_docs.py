#!/usr/bin/env python3
"""Fails when the docs, the page or the mock no longer match the firmware. AGENTS.md says
what to update when something changes, and this catches what it can of the rest.

    python tools/check_docs.py
"""
import argparse
import ast
import json
import os
import re
import subprocess
import sys
import tempfile

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)


def read(root, path):
    with open(os.path.join(root, path), encoding="utf8") as f:
        return f.read()


def load_settings(root):
    """The table in yg_settings.c, as key: (min, max, default)."""
    text = read(root, "components/yg_core/yg_settings.c")
    rows = re.findall(r'\{ "(\w+)", "\w+", YG_SET_(\w+), FIELD\(\w+\), ([^,]+), ([^,]+), ([^}]+?) \}', text)
    named = {"YG_SET_POINT_ANY": 0, "YG_BUZZER_PASSIVE": 0, "YG_BUZZER_ACTIVE": 1, "YG_SSID_MAX": 32, "YG_PASSWORD_MIN": 8, "YG_PASSWORD_MAX": 63}
    number = lambda s: named[s] if s in named else int(s)
    return {key: {"type": kind, "min": number(lo), "max": number(hi), "def": number(default)} for key, kind, lo, hi, default in rows}


def shown_default(setting):
    """How the atlas and the spec write this setting's default, as text without units."""
    kind, value = setting["type"], setting["def"]
    if kind == "DBM_OR_ANY":
        return "any"
    if kind == "BOOL":
        return "on" if value else "off"
    if kind == "BUZZER_TYPE":
        return "active" if value else "passive"
    return str(value)


def clean(cell):
    cell = re.sub(r"<[^>]+>", "", cell).replace("&nbsp;", " ").replace("&minus;", "-").replace("−", "-").replace("&deg;", "").replace("`", "").replace("*", "")
    return cell.strip().lower()


def check_settings(root, settings):
    check(len(settings) >= 20, f"could not read the settings table, found {len(settings)}")
    atlas = read(root, "atlas.html")
    page = read(root, "web/index.html")
    mock = read(root, "tools/mock_api.py")
    spec = read(root, "docs/superpowers/specs/2026-10-31-esp-idf-rewrite-design.md")
    keys_in_atlas = re.findall(r"<small>(\w+)</small>", atlas[atlas.index('id="settings"'):atlas.index('id="logs"')])
    check(sorted(keys_in_atlas) == sorted(settings), f"atlas settings table does not list exactly the firmware's settings: {sorted(set(keys_in_atlas) ^ set(settings))}")
    mock_rows = re.findall(r'\{"key": "(\w+)", "type": "\w+"(.*?)\},?\n', mock)
    mock_keys = [key for key, _ in mock_rows]
    check(sorted(mock_keys) == sorted(settings), f"mock_api.py does not list exactly the firmware's settings: {sorted(set(mock_keys) ^ set(settings))}")
    mock_by_key = dict(mock_rows)
    for key, s in settings.items():
        kind = s["type"]
        expected = shown_default(s)
        # The atlas table.
        row = re.search(r"<small>" + key + r'</small></td><td class="n"[^>]*>(.*?)</td>', atlas)
        if row and kind not in ("STR", "PASSWORD"):
            found = clean(row.group(1))
            check(re.match(r"-?[\w]+", found).group(0) == expected, f"atlas shows {key} default as '{found}', firmware says {expected}")
        # The spec's settings table.
        row = re.search(r"^\| `" + key + r"` \| (.*?) \|", spec, re.M)
        check(row is not None, f"spec section 5.7 does not list {key}")
        if row and kind not in ("STR", "PASSWORD"):
            found = clean(row.group(1))
            check(found.split()[0] == expected, f"spec shows {key} default as '{found}', firmware says {expected}")
        # The page has a label for it. The password has its own page.
        if kind != "PASSWORD":
            check(re.search(r"^\s*" + key + r": \[", page, re.M) is not None, f"web/index.html has no label for {key}")
        # The mock has the same limits and default. Its debug flag is on, to show the debug tools.
        rest = mock_by_key.get(key, "")
        if kind in ("U8", "U16", "I8", "DBM_OR_ANY"):
            lo, hi = re.search(r'"min": (-?\d+)', rest), re.search(r'"max": (-?\d+)', rest)
            check(lo and hi and int(lo.group(1)) == s["min"] and int(hi.group(1)) == s["max"], f"mock_api.py has different limits for {key}")
        if kind in ("U8", "U16", "I8"):
            value = re.search(r'"value": (-?\d+)', rest)
            check(value and int(value.group(1)) == s["def"], f"mock_api.py has a different default for {key}")
        if kind == "BOOL" and key != "debug":
            check(f'"value": {"True" if s["def"] else "False"}' in rest, f"mock_api.py has a different default for {key}")
        if kind == "BUZZER_TYPE":
            check(f'"value": "{expected}"' in rest, f"mock_api.py has a different default for {key}")
    # The default password, wherever people are told it.
    default = re.search(r'#define YG_PASSWORD_DEFAULT "(\w+)"', read(root, "components/yg_core/include/yg_settings.h")).group(1)
    for path in ("manual.html", "atlas.html", "README.md", "web/index.html"):
        check(f"<code>{default}</code>" in read(root, path) or f"`{default}`" in read(root, path), f"{path} does not give the default password")


def check_sounds(root):
    text = read(root, "components/yg_core/yg_sound.c")
    header = read(root, "components/yg_core/include/yg_sound.h")
    arrays = {m.group(1): [int(n) for n in re.findall(r"\d+", m.group(2))] for m in re.finditer(r"static const uint16_t (\w+)\[\] = \{(.*?)\};", text, re.S)}
    table = dict(re.findall(r'\[(YG_BUZZ_\w+)\] = \{ "\w+", (\w+),', text))
    names = dict(re.findall(r'\[(YG_BUZZ_\w+)\] = \{ "(\w+)",', text))
    order = [e for e in re.findall(r"^\s+(YG_BUZZ_\w+),", header, re.M) if e != "YG_BUZZ_COUNT"]
    check(sorted(order) == sorted(table), "yg_sound.c does not have one sound per event")

    def segments(event):
        at, out = 0, []
        for i, ms in enumerate(arrays[table[event]]):
            if i % 2 == 0:
                out.append(f"{at}-{at + ms}")
            at += ms
        return ",".join(out)

    wanted = [segments(e) for e in order]
    atlas = read(root, "atlas.html")
    block = atlas[atlas.index('id="soundRows"'):atlas.index("</figure>", atlas.index('id="soundRows"'))]
    check(re.findall(r'data-seg="([\d,-]+)"', block) == wanted, "atlas sound strips do not match yg_sound.c")
    manual = read(root, "manual.html")
    for event in order:
        found = re.search(r'data-sound="' + names[event] + r'" data-seg="([\d,-]+)"', manual)
        check(found and found.group(1) == segments(event), f"manual.html's '{names[event]}' sound does not match yg_sound.c")
    page = read(root, "web/index.html")
    for event in order:
        found = re.search(r'data-sound="' + names[event] + r'"><span class="r" data-seg="([\d,-]+)"', page)
        check(found and found.group(1) == segments(event), f"the page's Help tab has a different '{names[event]}' sound from yg_sound.c")
    listed = re.search(r"const SOUNDS = \[(.*?)\];", page, re.S)
    check(listed and re.findall(r'\["(\w+)",', listed.group(1)) == [names[e] for e in order], "the page's sound list does not match yg_sound.c")
    check(all(f'"{names[e]}"' in read(root, "test/bench/api_check.py") for e in order), "api_check.py does not try every sound")


def check_leds(root):
    """Only the atlas lists the LED's patterns. The LED is inside the case, where a technician
    never sees it, so the manual and the page's Help tab leave it out."""
    header = read(root, "components/yg_core/include/yg_engine.h")
    block = header[header.index("YG_LED_OFF,"):header.index("} yg_led_event_t")]
    colours = len(re.findall(r"YG_LED_\w+,", block)) - 1   # Off is not a pattern.
    atlas = read(root, "atlas.html")
    rows = atlas[atlas.index('id="ledRows"'):atlas.index("</figure>", atlas.index('id="ledRows"'))]
    check(rows.count('class="led"') == colours, f"atlas LED table has {rows.count('class=' + chr(34) + 'led' + chr(34))} rows, firmware has {colours} patterns")


def check_rule_constants(root, settings):
    """The atlas's trip panel is a port of yg_trigger.c, using the default settings."""
    atlas = read(root, "atlas.html")
    found = re.search(r"const MOVED_WINDOW = (\d+), GONE = (\d+), STILL = (\d+), PRESS = (\d+)", atlas)
    check(found is not None, "atlas trip panel constants not found")
    if found:
        wanted = (settings["moved_window_s"]["def"], settings["gone_s"]["def"], settings["still_s"]["def"], settings["press_ms"]["def"] // 1000)
        check(tuple(int(n) for n in found.groups()) == wanted, f"atlas trip panel uses {found.groups()}, firmware defaults are {wanted}")


def check_drawings(root):
    wires = re.compile(r"const WIRES = \[(.*?)\n\];", re.S)
    layout, atlas = wires.search(read(root, "hardware/board-layout.html")), wires.search(read(root, "atlas.html"))
    check(layout and atlas and layout.group(1) == atlas.group(1), "the WIRES list in atlas.html differs from hardware/board-layout.html")
    sizes = case_sizes(root)
    found = re.search(r"const L = ([\d.]+), W = ([\d.]+), H = ([\d.]+), wall = ([\d.]+), coverT = ([\d.]+), zCeil = ([\d.]+), x0 = (-?[\d.]+), y0 = (-?[\d.]+), zBoard = ([\d.]+), zParts = ([\d.]+), zC6 = ([\d.]+), zC6Top = ([\d.]+)", read(root, "atlas.html"))
    check(found is not None, "atlas 3D case sizes not found")
    if found:
        names = ["L", "W", "H", "wall", "cover_t", "z_ceil", "x0", "y0", "z_board", "z_parts", "z_c6", "z_c6_top"]
        for name, shown in zip(names, found.groups()):
            check(abs(float(shown) - sizes[name]) < 0.051, f"atlas 3D case has {name} = {shown}, build_case.py says {sizes[name]:.2f}")


def check_links(root):
    for page in ("atlas.html", "manual.html"):
        text = read(root, page)
        for link in sorted(set(re.findall(r'<a [^>]*href="([^"#:]+)"', text))):
            check(os.path.exists(os.path.join(root, link)), f"{page} links to a missing file: {link}")
        for anchor in sorted(set(re.findall(r'href="#([\w-]+)"', text))):
            check(f'id="{anchor}"' in text, f"{page} links to a missing section: #{anchor}")
    check("manual.html" in read(root, "atlas.html"), "atlas.html should point to the manual")


def case_sizes(root):
    text = read(root, "hardware/case/build_case.py")
    params = ast.literal_eval(text[text.index("PARAMS = [") + len("PARAMS = "):text.index("\n]\n", text.index("PARAMS = [")) + 2])
    sizes = {}
    for name, value, _ in params:
        sizes[name] = eval(value, {}, sizes) if isinstance(value, str) else value
    return sizes


def check_folded_fit(root):
    """The manual is cut out, folded in half three times, and taped under the case's top."""
    found = re.search(r"--cut-w:([\d.]+)mm; --cut-h:([\d.]+)mm", read(root, "manual.html"))
    check(found is not None, "manual.html does not state the size it is cut to")
    if not found:
        return
    long_side, short_side = float(found.group(1)) / 2, float(found.group(2)) / 4
    case = case_sizes(root)
    check(long_side <= 2 * case["x_in"], f"folded, the manual is {long_side} mm long, and the case is {2 * case['x_in']:.1f} mm inside")
    # Laid in the middle of the top, it has to stay off the antenna, the tab and the corner posts.
    antenna_end = case["y0"] + 2.21
    tab_edge = case["tab_y0"]
    check(-short_side / 2 >= antenna_end, f"folded, the manual reaches over the antenna, which ends at {antenna_end:.1f} mm")
    check(short_side / 2 <= tab_edge, f"folded, the manual reaches under the button tab, which starts at {tab_edge:.1f} mm")
    check(short_side / 2 <= case["y0"] + case["board_w"] - case["hole_inset"] - case["post_d"] / 2, "folded, the manual reaches a corner post")
    # 8 layers of ordinary 0.1 mm paper, which a crease roughly doubles until it is pressed flat.
    check(8 * 0.1 * 2 <= case["top_gap"], f"folded, the manual is thicker than the {case['top_gap']} mm gap above the C6")


def check_print(root):
    """Skipped with a note where Edge is not installed."""
    if not os.path.exists(EDGE):
        print("note: Edge not found, so the manual's printed layout was not checked")
        return
    with tempfile.TemporaryDirectory() as folder:
        pdf = os.path.join(folder, "manual.pdf")
        url = "file:///" + os.path.abspath(os.path.join(root, "manual.html")).replace("\\", "/")
        subprocess.run([EDGE, "--headless=new", "--disable-gpu", "--no-pdf-header-footer", f"--user-data-dir={os.path.join(folder, 'profile')}", f"--print-to-pdf={pdf}", url],
                       capture_output=True, timeout=120)
        if not os.path.exists(pdf):
            check(False, "could not print manual.html to count its pages")
            return
        with open(pdf, "rb") as f:
            pages = len(re.findall(rb"/Type\s*/Page(?![a-zA-Z])", f.read()))
        check(pages == 2, f"manual.html prints on {pages} pages, and it must be exactly 2, the two sides of one sheet")

        # A side with too much on it would be cut off with no sign in the page count, so
        # lay it out as print does and measure it.
        test = read(root, "manual.html").replace("@media print", "@media all")
        probe = """<pre id="probe"></pre><script>document.fonts.ready.then(() => { const out = [...document.querySelectorAll(".panel")].map(p => { const stripe = p.querySelector(".boom"); return [p.scrollHeight, p.clientHeight, p.scrollWidth, p.clientWidth, stripe ? stripe.getBoundingClientRect().bottom - p.getBoundingClientRect().top : 0]; }); document.getElementById("probe").textContent = JSON.stringify(out); document.getElementById("probe").style.display = "none"; });</script>"""
        path = os.path.join(folder, "probe.html")
        with open(path, "w", encoding="utf8") as f:
            f.write(test.replace("</body>", probe + "</body>"))
        dom = subprocess.run([EDGE, "--headless=new", "--disable-gpu", f"--user-data-dir={os.path.join(folder, 'profile2')}", "--window-size=900,1400", "--virtual-time-budget=6000", "--dump-dom",
                              "file:///" + path.replace("\\", "/")], capture_output=True, text=True, encoding="utf8", errors="replace", timeout=120).stdout
        found = re.search(r'<pre id="probe"[^>]*>(\[.*?\])</pre>', dom, re.S)
        check(found is not None, "could not measure the manual's two sides")
        if found:
            sides = json.loads(found.group(1))
            check(len(sides) == 2, f"manual.html has {len(sides)} sides, and it must have 2")
            for number, (tall, room, wide, across, cover) in enumerate(sides, 1):
                check(tall <= room and wide <= across, f"side {number} of the manual does not fit: it needs {tall} px of height in {room}, and {wide} px of width in {across}")
                # The first fold comes a quarter of the way down, and the logo block has to stay above it, on the outside.
                check(cover <= room / 4, f"the manual's logo block ends {cover:.0f} px down, past the first fold at {room / 4:.0f} px")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
    parser.add_argument("--no-print", action="store_true", help="skip printing the manual to check its layout")
    args = parser.parse_args()
    settings = load_settings(args.root)
    check_settings(args.root, settings)
    check_sounds(args.root)
    check_leds(args.root)
    check_rule_constants(args.root, settings)
    check_drawings(args.root)
    check_links(args.root)
    check_folded_fit(args.root)
    if not args.no_print:
        check_print(args.root)
    for failure in failures:
        print("FAIL " + failure)
    print(f"{len(failures)} problems" if failures else "The docs match the firmware.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
