#!/usr/bin/env python3
"""Checks every API endpoint on a real unit over USB, using its loopback console
command. Run with the ESP-IDF Python, which has pyserial. It restores the settings
it changes, but it forgets the paired beacon and leaves records in the log.

    python test/bench/api_check.py [--port COM3]
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import yg_serial  # noqa: E402

failures = []


def api(s, method, path, body=None, timeout=20):
    command = f"api {method} {path}"
    if body is not None:
        text = json.dumps(body, separators=(",", ":")).replace('"', '\\"')
        command += f' "{text}"'
    return yg_serial.run_command(s, command, timeout)


def check(name, condition, detail=""):
    print(("ok   " if condition else "FAIL ") + name + ("" if condition else f"   -> {detail[:300]}"))
    if not condition:
        failures.append(name)


def body_of(response):
    """The JSON after the headers, ignoring chunk sizes."""
    return response.split("\n\n", 1)[-1] if "\n\n" in response else response


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM3")
    args = parser.parse_args()
    s = yg_serial.open_port(args.port)
    # Restarts the hotspot and its timer, because the web server runs with it.
    yg_serial.run_command(s, "wifi", 5)
    time.sleep(2)
    api(s, "POST", "/api/pair/forget", {"x": 1})

    r = api(s, "GET", "/api/status")
    check("status", "200 OK" in r and '"fw"' in r and '"trip"' in r, r)

    r = api(s, "GET", "/api/system")
    check("system", '"log_total":512' in r and '"partition":"' in r and '"remote_pin_high":false' in r, r)

    r = api(s, "GET", "/api/settings")
    check("settings list", '"key":"press_ms"' in r and '"min":200' in r, r)
    check("settings hide the password value", '"key":"hotspot_password","type":"password","min"' in r, r)

    r = api(s, "POST", "/api/settings", {"press_ms": 1500, "led_enabled": True})
    check("settings update", '"ok":true' in r, r)
    check("settings update took effect", '"key":"press_ms","type":"int","value":1500' in api(s, "GET", "/api/settings"))
    r = api(s, "POST", "/api/settings", {"press_ms": 2000, "gone_s": 99999})
    check("out of range rejected", "400" in r and "gone_s" in r, r)
    check("rejected update changes nothing", '"value":1500' in api(s, "GET", "/api/settings"))
    api(s, "POST", "/api/settings", {"press_ms": 2000})
    r = api(s, "POST", "/api/settings", {"no_such_key": 1})
    check("unknown setting rejected", "400" in r, r)

    r = api(s, "POST", "/api/setpoint", {"dbm": -85})
    check("set point by value", '"ok":true' in r, r)
    check("set point shows in status", '"set_point":-85' in api(s, "GET", "/api/status"))
    check("set point out of range rejected", "400" in api(s, "POST", "/api/setpoint", {"dbm": -10}))
    check("set point any", '"ok":true' in api(s, "POST", "/api/setpoint", {"any": True}))
    check("set point here with nothing heard rejected", "400" in api(s, "POST", "/api/setpoint", {"here": True}))

    r = api(s, "POST", "/api/press", {"x": 1})
    check("press", '"ok":true' in r, r)
    level = yg_serial.run_command(s, "status", 5)
    check("pin high during press", '"remote_pin_high":true' in level, level)
    check("second press while pressing is refused", "409" in api(s, "POST", "/api/press", {"x": 1}))
    time.sleep(2.2)
    level = yg_serial.run_command(s, "status", 5)
    check("pin low after press", '"remote_pin_high":false' in level, level)

    check("test LED", '"ok":true' in api(s, "POST", "/api/test/led", {"x": 1}))
    check("test beep with no buzzer fitted", '"ok":true' in api(s, "POST", "/api/test/beep", {"x": 1}))

    check("pair scan", '"ok":true' in api(s, "POST", "/api/pair/scan", {"x": 1}))
    r = api(s, "GET", "/api/pair/candidates")
    check("candidates", '"mode":"list"' in r, r)
    check("pair cancel", '"ok":true' in api(s, "POST", "/api/pair/cancel", {"x": 1}))
    check("bad MAC rejected", "400" in api(s, "POST", "/api/pair/manual", {"mac": "nonsense"}))
    check("typed MAC", '"ok":true' in api(s, "POST", "/api/pair/manual", {"mac": "c5:b0:e6:53:59:02"}))
    check("typed MAC shows in status", '"beacon":"c5:b0:e6:53:59:02"' in api(s, "GET", "/api/status"))
    check("forget", '"ok":true' in api(s, "POST", "/api/pair/forget", {"x": 1}))
    check("forgotten", '"beacon":null' in api(s, "GET", "/api/status"))

    r = api(s, "GET", "/api/trips")
    check("trip list", '"current":' in r and '"trips":[' in r and '"fw":"' in r, r)
    current = json.loads(yg_serial.run_command(s, "status", 5).split("\n")[0])["trip"]
    r = api(s, "POST", f"/api/trips/{current}/rating", {"rating": "good"})
    check("rate a trip", '"ok":true' in r, r)
    check("rating shows in the list", '"rating":"good"' in api(s, "GET", f"/api/trips?before={current + 1}&limit=1"))
    check("unknown rating rejected", "400" in api(s, "POST", f"/api/trips/{current}/rating", {"rating": "meh"}))
    r = api(s, "GET", f"/api/trips/{current}")
    check("records of one trip", '"k":"boot"' in r, r)

    r = api(s, "GET", "/api/logs/export?captures=0", timeout=60)
    check("export", '"k":"meta"' in r and '"k":"boot"' in r and "attachment" in r, r[:300])

    check("time anchor", '"ok":true' in api(s, "POST", "/api/time", {"epoch": 1790000000}))

    settings = api(s, "GET", "/api/settings")
    debug_on = '"key":"debug","type":"bool","value":true' in settings
    if not debug_on:
        check("debug endpoints refuse with debug off", "403" in api(s, "GET", "/api/debug/live"))
        api(s, "POST", "/api/settings", {"debug": True})
    check("live view", '"gravity"' in api(s, "GET", "/api/debug/live"))
    check("simulate motion", '"ok":true' in api(s, "POST", "/api/debug/simulate-motion", {"x": 1}))
    check("moved after simulated motion", '"moved_recently":true' in api(s, "GET", "/api/status"))
    if not debug_on:
        api(s, "POST", "/api/settings", {"debug": False})

    r = yg_serial.run_command(s, "api GET /", 10)
    check("page is served gzipped", "Content-Encoding: gzip" in r, r[:200])
    r = api(s, "GET", "/api/nothing")
    check("unknown path is 404", "404" in r, r[:200])

    s.close()
    print()
    print(f"{len(failures)} failures" if failures else "All API checks passed.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
