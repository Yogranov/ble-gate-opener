#!/usr/bin/env python3
"""Serves web/index.html with a fake API, to work on the page without a unit.

    python tools/mock_api.py [port]        then open http://localhost:8765
"""
import json
import math
import os
import random
import re
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
START = time.time()

SETTINGS = [
    {"key": "set_point_dbm", "type": "dbm_or_any", "value": "any", "min": -100, "max": -30},
    {"key": "press_ms", "type": "int", "value": 2000, "min": 200, "max": 5000},
    {"key": "moved_window_s", "type": "int", "value": 60, "min": 10, "max": 300},
    {"key": "gone_s", "type": "int", "value": 60, "min": 10, "max": 600},
    {"key": "still_s", "type": "int", "value": 120, "min": 30, "max": 1800},
    {"key": "fallback_dbm", "type": "int", "value": -90, "min": -100, "max": -30},
    {"key": "motion_enabled", "type": "bool", "value": True},
    {"key": "motion_accel_mg", "type": "int", "value": 40, "min": 10, "max": 300},
    {"key": "motion_accel_ms", "type": "int", "value": 800, "min": 200, "max": 3000},
    {"key": "motion_yaw_deg", "type": "int", "value": 15, "min": 5, "max": 90},
    {"key": "motion_yaw_window_ms", "type": "int", "value": 3000, "min": 1000, "max": 10000},
    {"key": "pair_min_dbm", "type": "int", "value": -45, "min": -80, "max": -20},
    {"key": "pair_margin_db", "type": "int", "value": 15, "min": 3, "max": 40},
    {"key": "pair_hold_s", "type": "int", "value": 2, "min": 1, "max": 10},
    {"key": "pair_timeout_s", "type": "int", "value": 60, "min": 10, "max": 300},
    {"key": "hotspot_s", "type": "int", "value": 300, "min": 0, "max": 3600},
    {"key": "hotspot_ssid", "type": "string", "value": "YogiGate-0F01", "min": 1, "max": 32},
    {"key": "hotspot_password", "type": "password", "min": 8, "max": 63},
    {"key": "wifi_tx_dbm", "type": "int", "value": 8, "min": 2, "max": 20},
    {"key": "led_enabled", "type": "bool", "value": True},
    {"key": "buzzer_enabled", "type": "bool", "value": False},
    {"key": "buzzer_type", "type": "enum", "value": "passive", "options": ["passive", "active"]},
    {"key": "debug", "type": "bool", "value": True},
]

TRIPS = [
    {"trip": 42, "fw": "2.0.0", "reset": 1, "duration_ms": 184000, "first_heard": {"t": 800, "rssi": -91},
     "first_motion": {"t": 96000, "cause": "yaw"}, "presses": 1,
     "first_press": {"t": 96300, "reason": "auto", "rssi": -88}, "parked_best": -89,
     "peak": {"t": 101000, "rssi": -58}, "warning_ms": 4700, "adverts": 412, "temp_c": 41.5, "errors": 0, "rating": "good"},
    {"trip": 41, "fw": "2.0.0", "reset": 1, "duration_ms": 1320000, "first_heard": {"t": 1296000, "rssi": -92},
     "first_motion": {"t": 7000, "cause": "accel"}, "presses": 1,
     "first_press": {"t": 1296100, "reason": "auto", "rssi": -92}, "parked_best": None,
     "peak": {"t": 1302000, "rssi": -55}, "warning_ms": 5900, "adverts": 96, "temp_c": 33.0, "errors": 0, "rating": None},
    {"trip": 40, "fw": "2.0.0", "reset": 1, "duration_ms": 75000, "first_heard": {"t": 600, "rssi": -94},
     "first_motion": None, "presses": 0, "first_press": None, "parked_best": -92, "peak": {"t": 41000, "rssi": -90},
     "warning_ms": None, "adverts": 120, "temp_c": 52.0, "errors": 1, "rating": None},
    {"trip": 39, "fw": "2.0.0", "reset": 1, "duration_ms": 142000, "first_heard": {"t": 900, "rssi": -96},
     "first_motion": {"t": 52000, "cause": "yaw"}, "presses": 1,
     "first_press": {"t": 57000, "reason": "auto", "rssi": -80}, "parked_best": -95,
     "peak": {"t": 58000, "rssi": -61}, "warning_ms": 1000, "adverts": 205, "temp_c": 38.5, "errors": 0, "rating": "too_late"},
]


def trip_records(number):
    random.seed(number)
    out = [{"k": "boot", "fw": "2.0.0", "reset": 1, "trip": number, "t": 100}]
    samples = []
    for second in range(0, 125):
        if second < 95:
            rssi = -90 + random.randint(-2, 2)
        else:
            rssi = min(-58, -88 + (second - 95) * 5 + random.randint(-4, 2))
        samples.append([rssi, random.randint(1, 3), random.randint(0, 40), random.randint(0, 8), 0])
        if len(samples) == 40:
            out.append({"k": "trace", "period": 1, "s": samples, "trip": number, "t": (second - 39) * 1000})
            samples = []
    if samples:
        out.append({"k": "trace", "period": 1, "s": samples, "trip": number, "t": (125 - len(samples)) * 1000})
    out.append({"k": "motion", "cause": "yaw", "mg": 44, "yaw": 16.0, "trip": number, "t": 96000})
    out.append({"k": "press", "reason": "auto", "rssi": -88, "since_motion_ms": 300, "set_point": "any", "trip": number, "t": 96300})
    return out


state = {"pair_mode": "idle", "pressing_until": 0}


polls = {"count": 200}


def status():
    now = time.time()
    # Advances per request, so a browser with a sped-up clock still sees time pass.
    polls["count"] += 1
    uptime = polls["count"] * 500
    rssi = -82 + int(8 * math.sin(polls["count"] / 9)) + random.randint(-3, 3)
    pressing = now < state["pressing_until"]
    return {
        "fw": "2.0.0", "trip": 43, "uptime_ms": uptime, "state": "pressing" if pressing else "armed", "paired": True,
        "beacon": "c5:b0:e6:53:59:02", "set_point": SETTINGS[0]["value"], "effective_set_point": SETTINGS[0]["value"],
        "rssi": rssi, "rssi_age_ms": 240, "adverts_per_s": random.randint(2, 4), "moved_recently": False,
        "calibrated": True, "tilting": False, "motion_mg": 3, "motion_yaw": 0.2, "sensor": "ok", "sensor_err": "none",
        "sensor_id": 104, "temp_c": 36.5, "hotspot_on": True, "hotspot_left_s": max(0, 300 - int(now - START)),
        "clients": 1, "pair_mode": state["pair_mode"], "debug": True, "fallback": False, "capturing": False,
    }


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send_body(self, body, content_type="application/json", code=200):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def send_json(self, value, code=200):
        self.send_body(json.dumps(value), code=code)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/":
            with open(os.path.join(ROOT, "web", "index.html"), "rb") as f:
                self.send_body(f.read(), "text/html")
        elif path == "/phone":
            # The page in a phone-sized frame, because headless browsers have a
            # minimum window width. click= selects something to click after loading.
            query = self.path.split("?", 1)[1] if "?" in self.path else ""
            params = dict(p.split("=", 1) for p in query.split("&") if "=" in p)
            click = params.get("click", "")
            script = ""
            if click:
                script = ("<script>const f=document.querySelector('iframe');f.onload=()=>setTimeout(()=>"
                          "f.contentDocument.querySelector('" + click + "').click(),800);</script>")
            self.send_body("<!doctype html><body style='margin:0;background:#555'>"
                           "<iframe style='width:390px;height:" + params.get("h", "1400") + "px;border:0;background:#fff' src='/#"
                           + params.get("tab", "status") + "'></iframe>" + script, "text/html")
        elif path == "/api/status":
            self.send_json(status())
        elif path == "/api/system":
            self.send_json({"log_used": 37, "log_total": 512, "capture_used": 96, "capture_total": 608, "sector_bytes": 4096,
                            "heap_free": 142000, "partition": "ota_0", "remote_pin_high": False})
        elif path == "/api/settings":
            self.send_json({"settings": SETTINGS})
        elif path == "/api/pair/candidates":
            self.send_json({"mode": state["pair_mode"], "left_s": 48, "candidates": [
                {"mac": "c5:b0:e6:53:59:02", "type": 1, "rssi": -62, "uuid": "fda50693-a4e2-4fb1-afcf-c6eb07647825", "major": 1, "minor": 2, "age_ms": 120},
                {"mac": "d1:22:33:44:55:66", "type": 1, "rssi": -84, "uuid": "e2c56db5-dffb-48d2-b060-d0f5a71096e0", "major": 10, "minor": 7, "age_ms": 900},
            ]})
        elif path == "/api/trips":
            before = re.search(r"before=(\d+)", self.path)
            trips = [t for t in TRIPS if not before or t["trip"] < int(before.group(1))]
            self.send_json({"current": 43, "trips": trips[:10]})
        elif path.startswith("/api/trips/"):
            number = int(path.rsplit("/", 1)[1])
            self.send_body("\n".join(json.dumps(r) for r in trip_records(number)) + "\n", "application/x-ndjson")
        elif path == "/api/debug/live":
            self.send_json({"uptime_ms": 1, "ax": 0.0, "ay": 0.0, "az": 1.0, "gx": 0.1, "gy": 0.0, "gz": 0.0,
                            "gravity": [12.0, -4.0, 999.0], "bias": [1.2, -0.7, 0.4], "near_miss": False, "scan": 1})
        elif path == "/api/logs/export":
            self.send_body('{"k":"meta","format":1}\n', "application/x-ndjson")
        else:
            self.send_json({"ok": False, "error": "not found"}, 404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length)
        path = self.path
        if path == "/api/press":
            state["pressing_until"] = time.time() + 2
        elif path == "/api/pair/scan":
            state["pair_mode"] = "list"
        elif path == "/api/pair/auto":
            state["pair_mode"] = "auto"
        elif path == "/api/pair/cancel":
            state["pair_mode"] = "idle"
        elif path == "/api/settings":
            changes = json.loads(body)
            for s in SETTINGS:
                if s["key"] in changes and s["type"] != "password":
                    s["value"] = changes[s["key"]]
        elif path == "/api/setpoint":
            data = json.loads(body)
            SETTINGS[0]["value"] = "any" if data.get("any") else data.get("dbm", -82)
        self.send_json({"ok": True})


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    print(f"http://localhost:{port}")
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
