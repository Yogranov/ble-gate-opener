#!/usr/bin/env python3
"""Checks tools/yg_logs.py on made-up logs. Needs the replay program, which
run_tests.py builds before it runs this."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TOOL = os.path.join(ROOT, "tools", "yg_logs.py")

BOOT = {
    "k": "boot", "fw": "2.0.0", "reset": 1, "sensor_id": 104, "beacon": "c5:b0:e6:53:59:02", "beacon_type": 1,
    "debug": False, "set_point": -80, "press_ms": 2000, "moved_window_s": 60, "gone_s": 60, "still_s": 120,
    "fallback_dbm": -90, "motion_enabled": True, "motion_accel_mg": 40, "motion_accel_ms": 800, "motion_yaw_deg": 15,
    "motion_yaw_window_ms": 3000, "trip": 5, "t": 100,
}


def trace(t, samples, period=1):
    return {"k": "trace", "period": period, "s": samples, "trip": 5, "t": t}


class Tool(unittest.TestCase):
    def run_tool(self, records, *args):
        with tempfile.NamedTemporaryFile("w", suffix=".ndjson", delete=False) as f:
            f.write("\n".join(json.dumps(r) for r in records) + "\n")
        try:
            result = subprocess.run([sys.executable, TOOL, args[0], f.name, *args[1:]], capture_output=True, text=True)
        finally:
            os.unlink(f.name)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def replayed(self, records, *args):
        out = self.run_tool(records, "replay", "--trip", "5", *args)
        return out.split("Replayed", 1)[1]

    def test_replay_starts_from_the_trips_own_settings(self):
        # Heard at -85 after motion: the recorded set point of -80 must stop that press.
        records = [BOOT, {"k": "motion", "cause": "yaw", "mg": 0, "yaw": 20.0, "trip": 5, "t": 1000},
                   trace(1300, [[-85, 3, 0, 0, 0]]), trace(5000, [[-70, 3, 0, 0, 0]])]
        self.assertIn("PRESS,5000,auto", self.replayed(records))
        self.assertNotIn("PRESS,1300", self.replayed(records))

    def test_set_overrides_the_trips_settings(self):
        records = [BOOT, {"k": "motion", "cause": "yaw", "mg": 0, "yaw": 20.0, "trip": 5, "t": 1000},
                   trace(1300, [[-85, 3, 0, 0, 0]])]
        self.assertIn("PRESS,1300,auto", self.replayed(records, "--set", "set_point_dbm=any"))

    def test_a_setting_changed_during_the_trip_applies_from_then_on(self):
        records = [BOOT, {"k": "motion", "cause": "yaw", "mg": 0, "yaw": 20.0, "trip": 5, "t": 1000},
                   trace(1300, [[-85, 3, 0, 0, 0]]),
                   {"k": "setting", "key": "set_point_dbm", "value": "any", "trip": 5, "t": 1400},
                   trace(1500, [[-85, 3, 0, 0, 0]])]
        out = self.replayed(records)
        self.assertNotIn("PRESS,1300", out)
        self.assertIn("PRESS,1500,auto", out)

    def test_a_sensor_failure_replays_as_fallback(self):
        boot = dict(BOOT, set_point="any")
        records = [boot, trace(1000, [[-85, 3, 0, 0, 0]]),
                   {"k": "error", "code": "sensor_reads", "detail": 104, "trip": 5, "t": 2000},
                   trace(2100, [[-85, 3, 0, 0, 0]])]
        out = self.replayed(records)
        self.assertNotIn("PRESS,1000", out)
        self.assertIn("PRESS,2100,fallback", out)

    def test_a_recovered_sensor_stops_the_fallback(self):
        boot = dict(BOOT, set_point="any")
        records = [boot, {"k": "error", "code": "sensor_reads", "detail": 104, "trip": 5, "t": 900},
                   {"k": "error", "code": "sensor_recovered", "detail": 0, "trip": 5, "t": 950},
                   trace(1000, [[-85, 3, 0, 0, 0]])]
        self.assertNotIn("PRESS", self.replayed(records))

    def test_trace_outside_a_capture_still_counts(self):
        boot = dict(BOOT, set_point="any")
        records = [boot,
                   {"k": "cap_start", "cap": 1, "cause": "manual", "pre": 0, "trip": 5, "t": 1000},
                   {"k": "cap_imu", "cap": 1, "first": 0, "n": 1, "b64": "AAAAAABAAAAAAAAA", "trip": 5, "t": 1000},
                   {"k": "cap_end", "cap": 1, "samples": 1, "adverts": 0, "trip": 5, "t": 1100},
                   {"k": "motion", "cause": "yaw", "mg": 0, "yaw": 20.0, "trip": 5, "t": 9000},
                   trace(9100, [[-85, 3, 0, 0, 0]])]
        self.assertIn("PRESS,9100,auto", self.replayed(records))

    def test_adverts_per_second_counts_only_the_seconds_heard(self):
        # 10 heard seconds at 3 per second, then a long stretch with nothing heard.
        records = [BOOT, trace(0, [[-85, 3, 0, 0, 0]] * 10), trace(10000, [[0, 0, 0, 0, 0]] * 40, period=30),
                   {"k": "temp", "c": 30.0, "trip": 5, "t": 1200000}]
        line = [l for l in self.run_tool(records, "summary").splitlines() if l.strip().startswith("5 ")][0]
        self.assertIn(" 3.0 ", line)


if __name__ == "__main__":
    unittest.main(verbosity=0)
