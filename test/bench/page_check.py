#!/usr/bin/env python3
"""Checks the web page in a real browser, against tools/mock_api.py. Needs Microsoft
Edge, and no unit. A phone can still differ, so this does not replace trying one.

    python test/bench/page_check.py
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
from http.server import ThreadingHTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "tools"))
import mock_api  # noqa: E402

EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"

# Runs in the browser, next to the page in a frame, and reports what it saw.
SCRIPT = r"""
const f = document.getElementById("page");
const out = {};
const wait = ms => new Promise(r => setTimeout(r, ms));
f.onload = async () => {
  try {
    const win = f.contentWindow, doc = f.contentDocument;
    const js = text => win.eval(text);
    const q = s => doc.querySelector(s);
    const rows = () => [...doc.querySelectorAll(".cand code")].map(c => c.textContent).join();
    await wait(800);

    out.post_carries_the_guard = await js('post("/api/test/led")').then(() => true, () => false);

    // The mock reverses the list of beacons on every request, like signals changing.
    win.location.hash = "beacon"; js('showTab("beacon")');
    q("#pScan").click();
    await wait(1600);
    const order = rows(), button = q(".cand button");
    out.two_beacons_listed = order.split(",").length === 2;
    let stayed = true;
    for (let i = 0; i < 12; i++) {
      await wait(250);
      stayed = stayed && rows() === order;
    }
    out.beacon_rows_keep_their_order = stayed;
    out.beacon_buttons_survive_polling = q(".cand button") === button;
    q("#pScan").click();
    await wait(1200);
    out.beacon_list_empties_after_scan = q("#pList").textContent === "";

    const beacon = (mac, rssi) => ({ mac, type: 1, rssi, uuid: "u", major: 1, minor: 1 });
    js("renderCandidates")({ candidates: [beacon("aa", -60), beacon("bb", -70)] });
    const lower = doc.querySelectorAll(".cand")[1], topBefore = lower.getBoundingClientRect().top;
    js("renderCandidates")({ candidates: [beacon("bb", -71)] });
    out.a_row_above_going_quiet_does_not_move_the_rows = lower.getBoundingClientRect().top === topBefore;
    out.a_quiet_row_says_so = doc.querySelectorAll(".cand")[0].classList.contains("stale");
    js("renderCandidates")({ candidates: [beacon("aa", -62), beacon("bb", -71)] });
    out.a_row_coming_back_is_normal_again = !doc.querySelectorAll(".cand")[0].classList.contains("stale");
    js("clearCandidates")();

    js('renderPairing({pair_mode:"auto",paired:true,beacon:"old",pair_saves:1})');
    js('renderPairing({pair_mode:"idle",paired:true,beacon:"old",pair_saves:1})');
    out.pairing_timeout_says_no_beacon = q("#toast").textContent === "No beacon saved";
    js('renderPairing({pair_mode:"auto",paired:true,beacon:"old",pair_saves:1})');
    js('renderPairing({pair_mode:"idle",paired:true,beacon:"new",pair_saves:2})');
    out.pairing_success_says_saved = q("#toast").textContent === "Beacon saved";
    js('renderPairing({pair_mode:"auto",paired:true,beacon:"old",pair_saves:2})');
    js('renderPairing({pair_mode:"idle",paired:true,beacon:"old",pair_saves:3})');
    out.pairing_the_same_beacon_again_says_saved = q("#toast").textContent === "Beacon saved";

    win.location.hash = "settings"; await js('showTab("settings"); loadSettings()');
    const num = key => [...doc.querySelectorAll(".field")].find(x => x.textContent.includes(key)).querySelector("input[type=number]");
    out.negative_field_has_no_number_pad = num("Fixed threshold").inputMode !== "numeric";
    out.positive_field_has_a_number_pad = num("Press time").inputMode === "numeric";
    out.set_point_field_has_no_number_pad = num("Set point").inputMode !== "numeric";
    out.password_is_not_autofilled = q("input[type=password]").autocomplete === "new-password";
    const anyBox = [...doc.querySelectorAll(".setpoint input[type=checkbox]")][0];
    anyBox.click();
    const point = num("Set point");
    point.value = "";
    point.dispatchEvent(new Event("input"));
    out.blank_set_point_changes_nothing = !("set_point_dbm" in js("changedSettings()"));
    point.value = "-85";
    out.typed_set_point_is_saved = js("changedSettings()").set_point_dbm === -85;
    anyBox.click();
    const press = num("Press time");
    press.value = "";
    out.blank_number_changes_nothing = !("press_ms" in js("changedSettings()"));

    js('showTab("status")');
    const base = js("st");
    js("render")({ ...base, uptime_ms: 500000 });
    js("render")({ ...base, uptime_ms: 500500 });
    js("render")({ ...base, uptime_ms: 2000 });
    out.chart_starts_over_after_a_restart = js("livePoints.length") === 1;
    js("render")({ ...base, uptime_ms: 2500 });
    out.chart_keeps_going_after_a_restart = js("livePoints.length") === 2;

    const realFetch = win.fetch;
    win.fetch = (path, options) => new Promise((_, reject) => options.signal.addEventListener("abort", () => reject(new Error("timed out"))));
    out.polling_gives_up_when_nothing_answers = await js('api("/api/status", null, 200)').then(() => false, e => e.message === "timed out");
    win.fetch = () => Promise.reject(new Error("no answer"));
    js("clearInterval")(0);
    await js("poll()");
    out.one_slow_answer_is_not_a_lost_connection = q("#lost").hidden;
    await js("poll()");
    await js("poll()");
    out.repeated_failures_are_a_lost_connection = !q("#lost").hidden;
    win.fetch = realFetch;
    await js("poll()");
    out.an_answer_clears_the_warning = q("#lost").hidden;

    // Trip numbers 20 to 11 hold nothing, which must not hide the older ones.
    const pages = { "": { trips: [], next_before: 11 }, "11": { trips: [{ trip: 9, duration_ms: 1000, presses: 0, warning_ms: null, peak: null, temp_c: null, errors: 0, rating: null }], next_before: 0 } };
    win.fetch = (path, options) => {
      const before = /api\/trips\?limit=10(?:&before=(\d+))?/.exec(path);
      if (!before) return realFetch(path, options);
      return Promise.resolve(new win.Response(JSON.stringify(pages[before[1] || ""]), { headers: { "Content-Type": "application/json" } }));
    };
    await js("loadTrips(true)");
    win.fetch = realFetch;
    out.empty_trip_numbers_do_not_hide_older_trips = q("#tripList").textContent.includes("Trip 9");
    out.no_more_button_at_the_end = q("#tripMore").hidden;
  } catch (e) {
    out.script_error = String(e);
  }
  document.getElementById("result").textContent = JSON.stringify(out);
};
"""
PAGE = '<!doctype html><iframe id="page" src="/" style="width:390px;height:800px"></iframe><pre id="result">pending</pre><script>' + SCRIPT + "</script>"


class Handler(mock_api.Handler):
    flip = False

    def do_GET(self):
        if self.path == "/check":
            self.send_body(PAGE, "text/html")
        elif self.path == "/api/pair/candidates":
            Handler.flip = not Handler.flip
            rows = [
                {"mac": "c5:b0:e6:53:59:02", "type": 1, "rssi": -62, "uuid": "fda50693-a4e2-4fb1-afcf-c6eb07647825", "major": 1, "minor": 2, "age_ms": 120},
                {"mac": "d1:22:33:44:55:66", "type": 1, "rssi": -84, "uuid": "e2c56db5-dffb-48d2-b060-d0f5a71096e0", "major": 10, "minor": 7, "age_ms": 900},
            ]
            self.send_json({"mode": mock_api.state["pair_mode"], "left_s": 48, "candidates": rows[::-1] if Handler.flip else rows})
        else:
            super().do_GET()


def main():
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp()
    try:
        url = f"http://127.0.0.1:{server.server_address[1]}/check"
        dom = subprocess.run([EDGE, "--headless=new", "--disable-gpu", f"--user-data-dir={profile}", "--virtual-time-budget=20000", "--dump-dom", url],
                             capture_output=True, text=True, timeout=120).stdout
    finally:
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    found = re.search(r'<pre id="result">(.*?)</pre>', dom, re.S)
    if not found or not found.group(1).startswith("{"):
        print("The page check never finished:", found.group(1) if found else dom[:300])
        return 1
    results = json.loads(found.group(1).replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">"))
    failed = [name for name, passed in results.items() if passed is not True]
    for name, passed in results.items():
        print(("ok   " if passed is True else "FAIL ") + name.replace("_", " ") + ("" if passed in (True, False) else f"   -> {passed}"))
    print()
    print(f"{len(failed)} failures" if failed else "All page checks passed.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
