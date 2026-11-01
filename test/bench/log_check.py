#!/usr/bin/env python3
"""Checks that full logs can be erased on a real unit without holding up decisions,
and that a factory reset works on full logs. It erases the logs, the settings and
the paired beacon, and the hotspot password goes back to the default. Run with the ESP-IDF Python.

    python test/bench/log_check.py [--port COM3]
"""
import argparse
import json
import re
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import yg_serial  # noqa: E402

failures = []
FILL_SECTORS = 640        # More than either log holds.
SLOWEST_ALLOWED_MS = 1000  # The decision task must never wait long on the log.


def check(name, condition, detail=""):
    print(("ok   " if condition else "FAIL ") + name + ("" if condition else f"   -> {str(detail)[:400]}"), flush=True)
    if not condition:
        failures.append(name)


def status(s):
    lines = yg_serial.run_command(s, "status", 8).split("\n")
    return json.loads(lines[0]), json.loads(lines[1])


def fill(s):
    started = time.time()
    r = yg_serial.run_command(s, f"log-fill {FILL_SECTORS}", 240)
    check("logs filled", "Filled" in r, r)
    print(f"     took {time.time() - started:.0f} s", flush=True)
    _, usage = status(s)
    check("the trip log is full", usage["log_sectors"] == usage["log_total"], usage)
    check("the capture log is full", usage["capture_sectors"] == usage["capture_total"], usage)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    args = parser.parse_args()
    s = yg_serial.open_port(args.port)
    # An interrupted soak check leaves a short hotspot time behind.
    yg_serial.run_command(s, "set hotspot_s 0", 5)

    fill(s)
    first, _ = status(s)
    r = yg_serial.run_command(s, "log-clear", 240)
    result = json.loads(r[r.index("{"):].split("\n")[0])
    print(f"     erased in {result['ms'] / 1000:.1f} s, slowest decision {result['slowest_ms']} ms", flush=True)
    check("full logs erased", result["cleared"], r)
    check("the decision task was never held up for long", result["slowest_ms"] < SLOWEST_ALLOWED_MS, result)
    after, usage = status(s)
    check("no restart while erasing", after["uptime_ms"] > first["uptime_ms"] and after["trip"] == first["trip"], (first, after))
    check("logs are empty", usage["log_sectors"] <= 1 and usage["capture_sectors"] == 0, usage)
    r = yg_serial.run_command(s, "logs nocapture", 20)
    check("the unit logs again afterwards", "#END" in r, r[-200:])

    yg_serial.run_command(s, "set press_ms 1500", 5)
    fill(s)
    # Restarts the hotspot and its timer, because the web server runs with it.
    yg_serial.run_command(s, "wifi", 5)
    time.sleep(2)
    r = yg_serial.run_command(s, 'api POST /api/factory-reset "{\\"x\\":1}"', 240)
    check("factory reset on full logs answers", '"ok":true' in r, r[-300:])
    s.close()
    time.sleep(8)
    s = yg_serial.open_port(args.port)
    after, usage = status(s)
    check("restarted", after["uptime_ms"] < 20000, after)
    check("the trip count started again", after["trip"] == 1, after)
    check("the logs are empty", usage["log_sectors"] <= 1 and usage["capture_sectors"] == 0, usage)
    r = yg_serial.run_command(s, "wifi", 5)
    check("the password is back to the default", "Password: password" in r, r)
    time.sleep(2)
    # Reset logs: the same erase, and the numbers start again with no restart.
    yg_serial.reset(s)
    time.sleep(6)
    yg_serial.run_command(s, "wifi", 5)
    time.sleep(2)
    before, _ = status(s)
    check("a restart counts as a new trip", before["trip"] == 2, before)
    r = yg_serial.run_command(s, 'api POST /api/logs/clear "{\\"x\\":1}"', 240)
    check("reset logs is accepted", '"ok":true' in r, r)
    after, usage = status(s)
    check("reset logs makes this trip number 1", after["trip"] == 1, after)
    check("reset logs does not restart the unit", after["uptime_ms"] > before["uptime_ms"], after)
    check("reset logs empties the logs", usage["log_sectors"] <= 1 and usage["capture_sectors"] == 0, usage)
    r = yg_serial.run_command(s, "api GET /api/trips?limit=10", 10)
    check("the trip list starts again from 1", '"current":1' in r and re.findall(r'\{"trip":(\d+)', r) == ["1"], r[:300])
    yg_serial.reset(s)
    time.sleep(6)
    after, _ = status(s)
    check("the next start is trip 2", after["trip"] == 2, after)
    yg_serial.run_command(s, "wifi", 5)
    time.sleep(2)
    r = yg_serial.run_command(s, "api GET /api/settings", 10)
    check("settings are back to their defaults", '"key":"press_ms","type":"int","value":2000' in r, r[:300])
    s.close()

    print()
    print(f"{len(failures)} failures" if failures else "All log checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
