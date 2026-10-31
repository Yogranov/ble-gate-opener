#!/usr/bin/env python3
"""Checks firmware update and rollback on a real unit, with no Wi-Fi client: the
unit uploads its own running image to its own /api/ota, through the real HTTP code.
It erases the logs first, because it reads them back. Run with the ESP-IDF Python.
Takes about three minutes.

    python test/bench/ota_check.py [--port COM3]
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
    print(("ok   " if condition else "FAIL ") + name + ("" if condition else f"   -> {str(detail)[:400]}"))
    if not condition:
        failures.append(name)


def command(text, timeout=8):
    """Opens the port for each command, because a restart can drop it."""
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


def system():
    """The running slot and uptime, once the web server answers."""
    deadline = time.time() + 30
    last = ""
    while time.time() < deadline:
        command("wifi", 5)
        last = command("api GET /api/system", 8)
        status = command("status", 5).split("\n")[0]
        if '"partition"' in last and status.startswith("{"):
            body = last[last.index("{"):].split("\n")[0]
            return json.loads(body)["partition"], json.loads(status)["uptime_ms"]
        time.sleep(2)
    raise RuntimeError("No answer: " + last)


def reset():
    s = yg_serial.open_port(port)
    yg_serial.reset(s)
    s.close()


def main():
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    args = parser.parse_args()
    port = args.port

    # An interrupted soak check leaves a short hotspot time behind.
    command("set hotspot_s 300")
    command("log-clear", 240)
    start_slot, _ = system()
    other_slot = "ota_1" if start_slot == "ota_0" else "ota_0"
    print(f"Running from {start_slot}")

    r = command("ota-self corrupt", 60)
    check("a damaged image is refused", "not a valid firmware" in r or "400" in r, r)
    time.sleep(3)
    slot, _ = system()
    check("still on the same slot after a damaged image", slot == start_slot, slot)

    print("     an upload that goes silent half way, which takes 30 s to give up on")
    started = time.time()
    r = command("ota-self abandon", 60)
    check("an abandoned upload is given up on", "upload failed" in r, r)
    check("and within a reasonable time", time.time() - started < 45, time.time() - started)
    slot, _ = system()
    check("the web server answers again, on the same slot", slot == start_slot, slot)

    r = command("ota-self", 60)
    check("upload accepted", '"ok":true' in r, r)
    time.sleep(8)
    slot, _ = system()
    check("restarted into the new slot", slot == other_slot, slot)
    time.sleep(3)
    reset()
    time.sleep(6)
    slot, up = system()
    check("the restart really happened", up < 20000, up)
    check("confirmed firmware stays after another restart", slot == other_slot, slot)
    logs = command("logs nocapture", 25)
    check("update logged as uploaded and confirmed", '"event":"uploaded"' in logs and '"event":"confirmed"' in logs, logs[-300:])

    r = command("ota-self fail", 60)
    check("upload for the failing self-check accepted", '"ok":true' in r, r)
    print("     waiting for the 30 s self-check deadline")
    time.sleep(8)
    slot, up = system()
    check("new firmware running, waiting on its self-check", slot == start_slot and up < 30000, (slot, up))
    time.sleep(32)
    slot, _ = system()
    check("failed self-check rolled back", slot == other_slot, slot)

    r = command("ota-self fail", 60)
    check("upload for the power cut test accepted", '"ok":true' in r, r)
    time.sleep(6)
    slot, _ = system()
    check("new firmware running, not yet confirmed", slot == start_slot, slot)
    reset()   # A power cut before confirming.
    time.sleep(6)
    slot, up = system()
    check("the restart really happened", up < 20000, up)
    check("restart before confirming rolled back", slot == other_slot, slot)

    print()
    print(f"{len(failures)} failures" if failures else "All update and rollback checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
