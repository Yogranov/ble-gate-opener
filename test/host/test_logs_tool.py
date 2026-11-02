#!/usr/bin/env python3
"""Checks tools/yg_logs.py on made-up logs. Needs the replay program, which
run_tests.py builds before it runs this."""
import base64
import json
import os
import struct
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

    def test_summary_counts_starts_that_a_brownout_ended(self):
        # Trips 6, 7 and 8 died before they could log anything, and trip 9 says why.
        out = self.run_tool([BOOT, dict(BOOT, trip=9, reset=9)], "summary")
        line = [l for l in out.splitlines() if l.strip().startswith("9 ")][0]
        self.assertIn("brownout after 3 failed starts", line)
        self.assertIn("3 failed starts before 1 power-up", out)

    def test_a_gap_with_no_brownout_is_not_a_failed_start(self):
        # Reset logs, or an old sector overwritten, also leaves a gap in the trip numbers.
        out = self.run_tool([BOOT, dict(BOOT, trip=9, reset=1)], "summary")
        self.assertNotIn("failed start", out)

    def test_summary_marks_a_trip_that_guessed_its_gyro_zero(self):
        guessed = {"k": "gyro_zero", "how": "time_up", "saved": False, "x": 2.0, "y": 0.0, "z": 6.0, "trip": 5, "t": 21000}
        line = [l for l in self.run_tool([BOOT, guessed], "summary").splitlines() if l.strip().startswith("5 ")][0]
        self.assertIn("gyro zero guessed", line)
        known = dict(guessed, how="known")
        line = [l for l in self.run_tool([BOOT, known], "summary").splitlines() if l.strip().startswith("5 ")][0]
        self.assertNotIn("gyro zero", line)

    def imu_capture(self, seconds_moving, seconds_still):
        """A raw capture: a car turning with no two seconds alike, so that calibration runs out of
        time, then still. The small change in x keeps the sensor from looking stuck."""
        records = [{"k": "cap_start", "cap": 1, "cause": "manual", "pre": 0, "trip": 5, "t": 1000}]
        total = (seconds_moving + seconds_still) * 50
        for first in range(0, total, 19):
            samples = b""
            for i in range(first, first + 19):
                yaw = 0 if i >= seconds_moving * 50 else 10 + i // 50 * 7 % 5
                samples += struct.pack("<6h", i % 3 * 8, 0, 16384, 0, 0, yaw * 131)
            records.append({"k": "cap_imu", "cap": 1, "first": first, "n": 19, "b64": base64.b64encode(samples).decode(), "trip": 5, "t": 1000})
        records.append({"k": "cap_end", "cap": 1, "samples": total, "adverts": 0, "trip": 5, "t": 1000 + total * 20})
        return records

    def motion_while_still(self, records):
        out = self.replayed(records)
        return [l for l in out.splitlines() if "MOTION" in l and int(l.split(",")[1]) >= 30000]

    def test_replay_starts_with_the_saved_gyro_zero_when_the_trip_used_one(self):
        capture = self.imu_capture(25, 15)
        known = {"k": "gyro_zero", "how": "known", "saved": False, "x": 0.0, "y": 0.0, "z": 0.0, "trip": 5, "t": 21000}
        self.assertTrue(self.motion_while_still([BOOT] + capture))
        self.assertFalse(self.motion_while_still([BOOT, known] + capture))

    def test_replay_does_not_invent_a_saved_zero_for_a_trip_that_guessed(self):
        capture = self.imu_capture(25, 15)
        guessed = {"k": "gyro_zero", "how": "time_up", "saved": False, "x": 0.0, "y": 0.0, "z": 0.0, "trip": 5, "t": 21000}
        self.assertTrue(self.motion_while_still([BOOT, guessed] + capture))

    def test_summary_names_a_restart_that_went_wrong(self):
        out = self.run_tool([BOOT, dict(BOOT, trip=6, reset=6)], "summary")
        line = [l for l in out.splitlines() if l.strip().startswith("6 ")][0]
        self.assertIn("watchdog", line)


if __name__ == "__main__":
    unittest.main(verbosity=0)
