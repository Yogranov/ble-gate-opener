#!/usr/bin/env python3
"""Restarts a real unit repeatedly, then cycles the hotspot and scanning for a while,
watching for crashes and memory leaks. Run with the ESP-IDF Python.

    python test/bench/soak_check.py [--port COM3] [--restarts 20] [--minutes 10]
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import yg_serial  # noqa: E402

failures = []
port = "COM3"


def check(name, condition, detail=""):
    print(("ok   " if condition else "FAIL ") + name + ("" if condition else f"   -> {str(detail)[:300]}"), flush=True)
    if not condition:
        failures.append(name)


def command(text, timeout=8):
    deadline = time.time() + 25
    while True:
        try:
            s = yg_serial.open_port(port)
            try:
                return yg_serial.run_command(s, text, timeout)
            finally:
                s.close()
        except Exception:
            if time.time() > deadline:
                raise
            time.sleep(1)


def status():
    lines = command("status", 5).split("\n")
    return json.loads(lines[0]), json.loads(lines[1])


def restarts(count):
    s = yg_serial.open_port(port)
    yg_serial.reset(s)
    s.close()
    time.sleep(4)
    first, _ = status()
    bad_pin = 0
    pressed = 0
    last_trip = first["trip"]
    for _ in range(count):
        s = yg_serial.open_port(port)
        yg_serial.reset(s)
        s.close()
        time.sleep(2.5)
        st, extra = status()
        if extra["remote_pin_high"]:
            bad_pin += 1
        if st["state"] in ("pressing", "hold"):
            pressed += 1
        check_trip = st["trip"] == last_trip + 1
        if not check_trip:
            print(f"     trip went from {last_trip} to {st['trip']}", flush=True)
        last_trip = st["trip"]
    check(f"{count} restarts: remote pin low after every one", bad_pin == 0, bad_pin)
    check(f"{count} restarts: never pressed", pressed == 0, pressed)
    check(f"{count} restarts: each counted as a new trip", last_trip == first["trip"] + count, (first["trip"], last_trip))
    logs = command("logs nocapture", 30)
    recent = [json.loads(l) for l in logs.split("\n") if l.startswith("{")]
    presses = [r for r in recent if r.get("k") == "press" and r["trip"] > first["trip"]]
    check("no press in the logs of those trips", not presses, presses)


def environment_snapshot():
    command("set debug 1")
    s = yg_serial.open_port(port)
    yg_serial.reset(s)
    s.close()
    time.sleep(17)
    logs = command("logs nocapture", 30)
    records = [json.loads(l) for l in logs.split("\n") if l.startswith('{"k":"env"')]
    check("debug snapshot logged", bool(records))
    if records:
        print("     " + json.dumps(records[-1]), flush=True)
    command("set debug 0")


def soak(minutes):
    command("set hotspot_s 20")
    s = yg_serial.open_port(port)
    yg_serial.reset(s)
    s.close()
    time.sleep(5)
    start_trip = status()[0]["trip"]
    readings = []
    end = time.time() + minutes * 60
    cycles = 0
    while time.time() < end:
        command("wifi")
        time.sleep(3)
        r = command("api GET /api/system", 8)
        if "{" in r:
            readings.append(json.loads(r[r.index("{"):].split("\n")[0])["heap_free"])
        command("api GET /api/status", 8)
        command("scan start")
        time.sleep(2)
        command("scan stop")
        time.sleep(22)   # Long enough for the 20 s hotspot timer to stop it.
        cycles += 1
    st, _ = status()
    check(f"{cycles} hotspot and scan cycles with no restart", st["trip"] == start_trip, (start_trip, st["trip"]))
    command("set hotspot_s 300")
    if len(readings) >= 4:
        early = sum(readings[1:3]) / 2
        late = sum(readings[-2:]) / 2
        print(f"     free memory: first {readings[0]}, early {early:.0f}, late {late:.0f}, lowest {min(readings)}", flush=True)
        check("no memory leak: free memory held within 8 kB", early - late < 8192, (early, late))


def main():
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--restarts", type=int, default=20)
    parser.add_argument("--minutes", type=float, default=10)
    args = parser.parse_args()
    port = args.port
    command("set debug 0")
    command("reboot", 2)
    time.sleep(4)
    restarts(args.restarts)
    environment_snapshot()
    soak(args.minutes)
    print()
    print(f"{len(failures)} failures" if failures else "All soak checks passed.", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
