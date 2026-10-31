#!/usr/bin/env python3
"""Fetch and analyse YogiGate logs. Log files stay in logs/, which git ignores.

    python tools/yg_logs.py fetch --port COM3          over USB
    python tools/yg_logs.py fetch --host 192.168.4.1   over the hotspot
    python tools/yg_logs.py summary logs/some.ndjson   one line per trip, then totals
    python tools/yg_logs.py replay logs/some.ndjson --trip 12 --set motion_accel_mg=60

Replay runs a trip through the current logic with changed settings. Build it first
with: python test/host/run_tests.py
"""
import argparse
import base64
import json
import os
import statistics
import struct
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ACCEL_LSB_PER_G = 16384.0
GYRO_LSB_PER_DPS = 131.0
SAMPLE_MS = 20
TICK_MS = 100


def default_output():
    os.makedirs(os.path.join(ROOT, "logs"), exist_ok=True)
    return os.path.join(ROOT, "logs", time.strftime("yogigate-%Y%m%d-%H%M%S.ndjson"))


def fetch_usb(port, captures, out_path):
    sys.path.insert(0, os.path.dirname(__file__))
    import yg_serial

    s = yg_serial.open_port(port)
    s.reset_input_buffer()
    s.write(("logs" if captures else "logs nocapture").encode() + b"\n")
    good = bad = 0
    data = bytearray()
    last = time.time()
    with open(out_path, "w", encoding="utf-8") as out:
        begun = False
        while time.time() - last < 15:
            chunk = s.read(65536)
            if not chunk:
                continue
            last = time.time()
            data += chunk
            *lines, rest = data.split(b"\n")
            data = bytearray(rest)
            done = False
            for raw in lines:
                line = raw.decode(errors="replace").strip()
                if line == "#BEGIN":
                    begun = True
                elif line == "#END":
                    done = True
                elif begun and line.startswith("{"):
                    try:
                        json.loads(line)
                        out.write(line + "\n")
                        good += 1
                    except ValueError:
                        bad += 1
            if done:
                break
    s.close()
    print(f"{good} records saved to {out_path}" + (f", {bad} damaged lines skipped" if bad else ""))


def fetch_http(host, captures, out_path):
    url = f"http://{host}/api/logs/export" + ("" if captures else "?captures=0")
    with urllib.request.urlopen(url, timeout=120) as response, open(out_path, "wb") as out:
        total = 0
        while True:
            chunk = response.read(65536)
            if not chunk:
                break
            out.write(chunk)
            total += len(chunk)
    print(f"{total} bytes saved to {out_path}")


def read_records(path):
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    yield json.loads(line)
                except ValueError:
                    pass


def trace_samples(record):
    """Yields (time_ms, rssi or None, adverts, accel_mg, yaw_dps, flags)."""
    for i, s in enumerate(record["s"]):
        yield record["t"] + i * record["period"] * 1000, s[0] if s[1] else None, s[1], s[2], s[3], s[4]


def summarise(records):
    trips = {}
    ratings = {}

    def trip_of(number):
        return trips.setdefault(number, {
            "trip": number, "fw": "", "duration": 0, "heard": None, "motion": None, "press": None, "presses": 0,
            "parked": None, "peak": None, "adverts": 0, "temp": None, "errors": [], "env": None, "near_miss": 0,
        })

    for r in records:
        if "trip" not in r:
            continue
        t = trip_of(r["trip"])
        kind = r.get("k")
        t["duration"] = max(t["duration"], r["t"])
        if kind == "boot":
            t["fw"] = r["fw"]
        elif kind == "first_heard" and t["heard"] is None:
            t["heard"] = r
        elif kind == "motion" and t["motion"] is None:
            t["motion"] = r
        elif kind == "press":
            t["presses"] += 1
            if t["press"] is None:
                t["press"] = r
        elif kind == "temp" and t["temp"] is None:
            t["temp"] = r["c"]
        elif kind == "error":
            t["errors"].append(r["code"])
        elif kind == "env":
            t["env"] = r
        elif kind == "rating":
            ratings[r["rated_trip"]] = r["rating"]
        elif kind == "trace":
            for at, rssi, adverts, _, _, flags in trace_samples(r):
                t["duration"] = max(t["duration"], at)
                t["adverts"] += adverts
                if flags & 2:
                    t["near_miss"] += 1
                if rssi is None:
                    continue
                if t["peak"] is None or rssi > t["peak"][1]:
                    t["peak"] = (at, rssi)
    for r in records:
        # A second pass, because trace records can reach the log after a motion record.
        if r.get("k") == "trace":
            t = trips[r["trip"]]
            for at, rssi, *_ in trace_samples(r):
                if rssi is not None and (t["motion"] is None or at < t["motion"]["t"]):
                    t["parked"] = rssi if t["parked"] is None else max(t["parked"], rssi)
    for number, rating in ratings.items():
        trip_of(number)["rating"] = rating
    return trips


def cmd_summary(args):
    records = list(read_records(args.file))
    trips = summarise(records)
    print(f"{'trip':>5} {'fw':>7} {'length':>7} {'parked':>7} {'moved':>7} {'press':>7} {'n':>2} {'reason':>8} "
          f"{'warn':>6} {'peak':>5} {'adv/s':>6} {'temp':>5} {'near':>4}  rating / errors")
    warnings, parked, peaks = [], [], []
    for number in sorted(trips):
        t = trips[number]
        seconds = t["duration"] / 1000
        press_at = f"{t['press']['t'] / 1000:.1f}" if t["press"] else "-"
        moved_at = f"{t['motion']['t'] / 1000:.1f}" if t["motion"] else "-"
        warn = "-"
        if t["press"] and t["peak"] and t["peak"][0] >= t["press"]["t"]:
            value = (t["peak"][0] - t["press"]["t"]) / 1000
            warnings.append(value)
            warn = f"{value:.1f}"
        if t["parked"] is not None:
            parked.append(t["parked"])
        if t["peak"]:
            peaks.append(t["peak"][1])
        note = " ".join(filter(None, [t.get("rating"), ",".join(t["errors"])]))
        print(f"{number:>5} {t['fw']:>7} {seconds:>6.0f}s {t['parked'] if t['parked'] is not None else '-':>7} "
              f"{moved_at:>7} {press_at:>7} {t['presses']:>2} {t['press']['reason'] if t['press'] else '-':>8} "
              f"{warn:>6} {t['peak'][1] if t['peak'] else '-':>5} {t['adverts'] / max(seconds, 1):>6.1f} "
              f"{t['temp'] if t['temp'] is not None else '-':>5} {t['near_miss']:>4}  {note}")
    print()
    pressed_parked = sum(1 for t in trips.values() if t["press"] and t["press"]["reason"] == "auto" and t["motion"] is None)
    print(f"{len(trips)} trips, {sum(t['presses'] for t in trips.values())} presses, "
          f"{sum(1 for t in trips.values() if t['errors'])} with errors, "
          f"{pressed_parked} automatic presses with no motion logged.")
    if warnings:
        print(f"Warning time: median {statistics.median(warnings):.1f} s, range {min(warnings):.1f} to {max(warnings):.1f} s.")
    if parked:
        print(f"Strongest signal while parked: median {statistics.median(parked):.0f}, range {min(parked)} to {max(parked)} dBm.")
    if peaks:
        print(f"Peak signal: median {statistics.median(peaks):.0f}, range {min(peaks)} to {max(peaks)} dBm.")
    captures = sum(1 for r in records if r.get("k") == "cap_start")
    if captures:
        print(f"{captures} debug captures.")


def capture_events(records, trip):
    """Raw motion samples and adverts from one trip's debug captures. A capture cut
    short by a power cut still counts: samples are timed from its start record."""
    events = []
    start = None
    for r in records:
        if r.get("trip") != trip:
            continue
        kind = r.get("k")
        if kind == "cap_start":
            start = r
        elif kind == "cap_imu" and start is not None:
            data = base64.b64decode(r["b64"])
            for i in range(r["n"]):
                s = struct.unpack_from("<6h", data, i * 12)
                at = start["t"] + (r["first"] + i - start["pre"]) * SAMPLE_MS
                values = [v / ACCEL_LSB_PER_G for v in s[:3]] + [v / GYRO_LSB_PER_DPS for v in s[3:]]
                events.append((at, f"I,{at}," + ",".join(f"{v:.5f}" for v in values)))
        elif kind == "cap_adv" and start is not None:
            data = base64.b64decode(r["b64"])
            for i in range(r["n"]):
                at, rssi = struct.unpack_from("<Ib", data, i * 5)
                events.append((at, f"A,{at},{rssi}"))
    return events


def trace_events(records, trip):
    events = []
    for r in records:
        if r.get("trip") != trip:
            continue
        if r.get("k") == "motion":
            events.append((r["t"], f"M,{r['t']}"))
        elif r.get("k") == "trace":
            for at, rssi, *_ in trace_samples(r):
                if rssi is not None:
                    events.append((at, f"A,{at},{rssi}"))
    return events


def cmd_replay(args):
    exe = os.path.join(ROOT, "build-host", "replay" + (".exe" if os.name == "nt" else ""))
    if not os.path.exists(exe):
        sys.exit("Build the replay program first: python test/host/run_tests.py")
    records = list(read_records(args.file))
    events = capture_events(records, args.trip)
    source = "raw captures"
    if not any(line.startswith("I,") for _, line in events):
        events = trace_events(records, args.trip)
        source = "per-second trace, with motion as logged"
    if not events:
        sys.exit(f"Trip {args.trip} has nothing to replay.")
    events.sort(key=lambda e: e[0])
    lines = []
    for change in args.settings:
        key, _, value = change.partition("=")
        lines.append(f"set,{key},{value}")
    next_tick = events[0][0]
    for at, line in events:
        while next_tick <= at:
            lines.append(f"T,{next_tick}")
            next_tick += TICK_MS
        lines.append(line)
    for _ in range(600):
        lines.append(f"T,{next_tick}")
        next_tick += TICK_MS
    result = subprocess.run([exe], input="\n".join(lines) + "\n", capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.stderr.strip())
    print(f"Trip {args.trip}, from {source}.")
    print("Recorded:")
    for r in records:
        if r.get("trip") == args.trip and r.get("k") in ("press", "motion", "rearm"):
            print(f"  {r['k'].upper()},{r['t']},{r.get('reason') or r.get('cause')}")
    print("Replayed with" + (" " + ", ".join(args.settings) if args.settings else " current defaults") + ":")
    print("  " + (result.stdout.strip().replace("\n", "\n  ") or "nothing"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    fetch = sub.add_parser("fetch")
    fetch.add_argument("--port")
    fetch.add_argument("--host")
    fetch.add_argument("--out")
    fetch.add_argument("--no-captures", action="store_true")
    summary = sub.add_parser("summary")
    summary.add_argument("file")
    replay = sub.add_parser("replay")
    replay.add_argument("file")
    replay.add_argument("--trip", type=int, required=True)
    replay.add_argument("--set", dest="settings", action="append", default=[], metavar="KEY=VALUE")
    args = parser.parse_args()

    if args.command == "fetch":
        out = args.out or default_output()
        if args.port:
            fetch_usb(args.port, not args.no_captures, out)
        elif args.host:
            fetch_http(args.host, not args.no_captures, out)
        else:
            sys.exit("Give --port or --host.")
    elif args.command == "summary":
        cmd_summary(args)
    else:
        cmd_replay(args)


if __name__ == "__main__":
    main()
