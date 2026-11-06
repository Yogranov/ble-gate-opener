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

    // The logo and favicon are in the page itself, because a phone on the hotspot has no internet.
    const logo = q("header img");
    out.the_header_shows_the_logo = !!logo && logo.naturalWidth > 0 && logo.alt === "YogiGate";
    out.the_header_logo_is_small = !!logo && logo.src.startsWith("data:image/") && logo.src.length < 12000 && logo.getBoundingClientRect().height <= 36;
    out.the_header_keeps_the_trip_and_version = q("#sub").textContent.includes("Trip");
    const icon = q('link[rel="icon"]');
    out.the_favicon_is_small = !!icon && icon.href.startsWith("data:image/png") && icon.href.length < 2000;
    out.the_page_loads_nothing_from_elsewhere = [...doc.querySelectorAll("img, script[src], link[href]")].every(x => (x.src || x.href || "").startsWith("data:") || !(x.src || x.href));

    // Simple by default, with everything one switch away.
    const shown = el => !!el && el.getClientRects().length > 0;
    const fields = () => [...doc.querySelectorAll("#setForm .field")].filter(shown).map(x => x.textContent);
    out.a_new_phone_starts_in_simple_mode = !js("advanced");

    // The manual is on the page, for when the printed one is gone.
    const helpTab = q('nav button[data-tab="help"]');
    out.help_is_a_tab_in_simple_mode = shown(helpTab);
    helpTab.click();
    const help = q("#help");
    out.help_opens_on_its_own = shown(help) && !shown(q("#status"));
    out.help_gives_the_way_in = ["Triple-click", "password", "192.168.4.1"].every(text => help.textContent.includes(text));
    out.help_lists_every_sound = help.querySelectorAll("[data-sound]").length === 8;
    out.help_draws_every_rhythm = help.querySelectorAll(".r").length === 8 && [...help.querySelectorAll(".r")].every(r => r.querySelector("i"));
    out.help_lists_every_gesture = ["Triple", "Hold 3", "Hold 15"].every(text => help.textContent.includes(text));
    out.help_fits_the_phone = doc.documentElement.scrollWidth <= win.innerWidth;
    out.six_tabs_fit_the_bar = doc.querySelectorAll("nav button").length === 6 && [...doc.querySelectorAll("nav button")].every(b => b.scrollWidth <= b.clientWidth);
    const helpPosts = [], beforeHelp = win.fetch;
    win.fetch = (path, options) => {
      if (options && options.method === "POST") helpPosts.push({ path, body: options.body });
      return realFetch(path, options);
    };
    q('#help [data-sound="error"] button').click();
    await wait(100);
    out.a_sound_in_help_can_be_played = helpPosts.some(p => p.path === "/api/test/beep" && JSON.parse(p.body).sound === "error");
    win.fetch = beforeHelp;
    js('showTab("status")');
    out.simple_status_hides_the_details = !shown(q("#mAdv")) && !shown(q("#mTemp")) && !shown(q("#mHot"));
    out.simple_status_keeps_the_signal = shown(q("#mRssi")) && shown(q("#mSet")) && shown(q("#mMoved"));
    out.simple_status_shows_live_motion = shown(q("#liveAccel")) && shown(q("#liveYaw"));
    js('showTab("system")');
    out.simple_system_hides_firmware_and_reset = !shown(q("#otaGo")) && !shown(q("#sReset")) && !shown(q("#soundList"));
    out.simple_system_keeps_the_beep_and_restart = shown(q("#tBeep")) && shown(q("#sReboot"));
    out.simple_system_hides_the_led_test = !shown(q("#tLed"));
    js('showTab("logs")');
    out.simple_logs_hide_erase = !shown(q("#logClear"));
    out.simple_logs_keep_the_trips_and_download = shown(q("#tripList")) && shown(q("a.button"));
    await js('showTab("settings"); loadSettings()');
    const simple = fields();
    out.simple_settings_are_short = simple.length === 2;
    out.simple_settings_are_the_useful_ones = ["Set point", "Buzzer"].every(name => simple.some(text => text.startsWith(name)));
    out.simple_settings_leave_out_the_led = !simple.some(text => text.startsWith("LED"));
    out.simple_settings_offer_the_password_page = shown(q("#sPassword"));
    out.simple_settings_hide_reset_to_defaults = !shown(q("#sDefaults"));

    q("#advMode").click();
    out.advanced_shows_every_setting_but_the_password = fields().length === 22;
    out.advanced_shows_the_led_setting = fields().some(text => text.startsWith("LED"));
    out.advanced_is_remembered = win.localStorage.getItem("yg_advanced") === "1";
    js('showTab("system")');
    out.advanced_system_shows_firmware_reset_and_sounds = shown(q("#otaGo")) && shown(q("#sReset")) && shown(q("#soundList"));
    out.advanced_system_shows_the_led_test = shown(q("#tLed"));
    js('showTab("logs")');
    out.advanced_logs_show_erase = shown(q("#logClear"));
    js('showTab("status")');
    out.advanced_status_shows_the_details = shown(q("#mAdv")) && shown(q("#mTemp")) && shown(q("#mHot"));

    // Reset to defaults, on the settings tab in advanced mode.
    js('showTab("settings")');
    out.advanced_shows_reset_to_defaults = shown(q("#sDefaults"));
    const sent = [];
    win.fetch = (path, options) => {
      if (options && options.method === "POST") sent.push(path);
      return realFetch(path, options);
    };
    const realConfirm = win.confirm;
    let asked = "";
    win.confirm = text => { asked = text; return false; };
    q("#sDefaults").click();
    await wait(100);
    out.reset_to_defaults_asks_first = asked.includes("beacon and logs stay") && asked.includes("password");
    out.declining_resets_nothing = sent.length === 0;
    win.confirm = () => true;
    await js('post("/api/settings", { press_ms: 1234 })');
    await js("loadSettings()");
    out.a_setting_can_be_changed_before_the_reset = num("Press time").value === "1234";
    sent.length = 0;
    q("#sDefaults").click();
    await wait(300);
    out.accepting_resets_the_settings = sent.includes("/api/settings/reset");
    out.the_form_shows_the_defaults_afterwards = num("Press time").value === "2000";
    win.confirm = realConfirm;
    win.fetch = realFetch;

    // Trying each sound, on the system tab.
    let posted = [];
    win.fetch = (path, options) => {
      if (options && options.method === "POST") posted.push({ path, body: options.body });
      return realFetch(path, options);
    };
    js('showTab("system")');
    const sounds = [...doc.querySelectorAll("#soundList button")];
    out.all_eight_sounds_can_be_tried = sounds.length === 8;
    sounds[4].click();
    await wait(100);
    out.a_sound_button_names_its_sound = posted.some(p => p.path === "/api/test/beep" && JSON.parse(p.body).sound === "error");

    js('showTab("settings")');
    q("#advMode").click();
    out.simple_mode_comes_back = !js("advanced") && win.localStorage.getItem("yg_advanced") === "0";

    // Reset logs: everything erased, and the numbers start again, with no restart.
    js('showTab("logs")');
    out.the_button_says_reset_logs = q("#logClear").textContent === "Reset logs";
    const resetPosts = [], fetchBefore = win.fetch;
    win.fetch = (path, options) => {
      if (options && options.method === "POST") resetPosts.push(path);
      return realFetch(path, options);
    };
    const keepConfirm = win.confirm;
    let question = "";
    win.confirm = text => { question = text; return false; };
    q("#logClear").click();
    await wait(100);
    out.reset_logs_asks_first = question.includes("from 1") && resetPosts.length === 0;
    win.confirm = () => true;
    q("#logClear").click();
    await wait(600);
    out.reset_logs_erases_them = resetPosts.includes("/api/logs/clear");
    out.reset_logs_gives_the_new_log_a_time = resetPosts.includes("/api/time");
    out.reset_logs_never_restarts_the_unit = !resetPosts.includes("/api/reboot");
    await js("poll()");
    out.the_trip_number_starts_again = q("#sub").textContent.startsWith("Trip 1 ");
    out.the_trip_list_starts_again = q("#tripList").textContent.includes("Trip 1") && !q("#tripList").textContent.includes("Trip 42");
    win.confirm = keepConfirm;
    win.fetch = fetchBefore;

    // The password page.
    posted = [];
    q("#sPassword").click();
    out.password_page_opens_on_its_own = shown(q("#pw1")) && shown(q("#pw2")) && !shown(q("#setForm"));
    out.password_page_stays_under_settings = q('nav button[aria-selected="true"]').dataset.tab === "settings";
    out.password_is_not_autofilled_on_its_page = q("#pw1").autocomplete === "new-password";
    q("#pw1").value = "short"; q("#pw2").value = "short"; q("#pwSave").click();
    await wait(50);
    out.short_password_is_refused = posted.length === 0 && shown(q("#pwError"));
    q("#pw1").value = "longenough1"; q("#pw2").value = "different1"; q("#pwSave").click();
    await wait(50);
    out.different_passwords_are_refused = posted.length === 0 && shown(q("#pwError"));
    q("#pw2").value = "longenough1"; q("#pwSave").click();
    await wait(300);
    out.a_good_password_is_sent = posted.length === 1 && JSON.parse(posted[0].body).hotspot_password === "longenough1";
    out.it_goes_back_to_settings_with_the_fields_empty = shown(q("#setForm")) && q("#pw1").value === "" && q("#pw2").value === "";
    win.fetch = realFetch;

    // The motion graphs.
    js('showTab("status")');
    const motionBase = { ...js("st"), sensor: "ok", calibrated: true, motion_accel_mg: 40, motion_yaw_deg: 15 };
    js("render")({ ...motionBase, uptime_ms: 9000000, motion_mg_peak: 120, motion_yaw_peak: 22.5 });
    out.motion_graphs_take_the_peaks = js("accelPoints[accelPoints.length - 1].v") === 120 && js("yawPoints[yawPoints.length - 1].v") === 22.5;
    js("render")({ ...motionBase, uptime_ms: 9000500, sensor: "failed", motion_mg_peak: 500, motion_yaw_peak: 90 });
    out.a_failed_sensor_leaves_a_gap = js("accelPoints[accelPoints.length - 1].v") === null && js("yawPoints[yawPoints.length - 1].v") === null;
    js("render")({ ...motionBase, uptime_ms: 9001000, calibrated: false, motion_mg_peak: 500, motion_yaw_peak: 90 });
    out.an_uncalibrated_sensor_leaves_a_gap = js("accelPoints[accelPoints.length - 1].v") === null;
    out.motion_scale_covers_the_threshold = js("niceTop([{ v: 3 }], 40)") >= 40 && js("niceTop([{ v: 260 }], 40)") >= 260 && js("niceTop([{ v: null }], 15)") >= 15;
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
