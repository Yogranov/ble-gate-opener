/* Runs the replay program on a small script, the way tools/yg_logs.py does. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "yg_test.h"

static void compile_and_run(const char *script, char *out, size_t out_len) {
    const char *input = "build-host/replay_input.txt";
    FILE *f = fopen(input, "w");
    CHECK(f != NULL);
    if (f == NULL) return;
    fputs(script, f);
    fclose(f);
    char cmd[256];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "build-host\\replay.exe < build-host\\replay_input.txt");
#else
    snprintf(cmd, sizeof(cmd), "build-host/replay < %s", input);
#endif
#ifdef _WIN32
    FILE *p = _popen(cmd, "r");
#else
    FILE *p = popen(cmd, "r");
#endif
    CHECK(p != NULL);
    if (p == NULL) return;
    size_t n = fread(out, 1, out_len - 1, p);
    out[n] = 0;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
}

static void a_recorded_trip_replays_to_one_press(void) {
    static char out[1024];
    compile_and_run(
        "A,1000,-91\nT,1100\nA,2000,-91\nT,2100\n"
        "M,3000\nA,3300,-88\nT,3400\nA,3600,-85\nT,3700\nT,6000\n",
        out, sizeof(out));
    CHECK_HAS(out, "MOTION,3000,accel");
    CHECK_HAS(out, "PRESS,3300,auto");
    CHECK_LACKS(out, "PRESS,3600");
}

static void settings_change_what_happens(void) {
    static char out[1024];
    compile_and_run(
        "set,set_point_dbm,-80\n"
        "M,1000\nA,1300,-88\nT,1400\nA,1600,-79\nT,1700\n",
        out, sizeof(out));
    CHECK_LACKS(out, "PRESS,1300");
    CHECK_HAS(out, "PRESS,1600,auto");
}

static void a_sensor_failure_in_the_log_replays_as_fallback(void) {
    static char out[1024];
    compile_and_run("A,1000,-85\nT,1100\nF,2000\nA,2100,-85\nT,2200\n", out, sizeof(out));
    CHECK_LACKS(out, "PRESS,1000");
    CHECK_HAS(out, "PRESS,2100,fallback");
}

/* 25 s with the car turning, no two seconds alike, then 15 s still, at a true gyro zero of
 * nothing. The small change in x keeps the sensor from looking stuck. */
static void moving_then_still_script(char *out, size_t len, const char *first_line) {
    size_t used = (size_t)snprintf(out, len, "%s", first_line);
    for (int i = 0; i < 2000 && used < len; i++) {
        int yaw = i < 1250 ? 10 + i / 50 * 7 % 5 : 0;
        used += (size_t)snprintf(out + used, len - used, "I,%d,%.4f,0,1,0,0,%d\nT,%d\n", 1000 + i * 20, 0.0005f * (float)(i % 3), yaw, 1000 + i * 20);
    }
}

static bool has_motion_while_still(const char *out) {
    const char *p = out;
    while ((p = strstr(p, "MOTION,")) != NULL) {
        if (atoi(p + 7) >= 30000) return true;
        p += 7;
    }
    return false;
}

static void a_replay_uses_the_saved_gyro_zero(void) {
    static char script[120000];
    static char out[4096];
    moving_then_still_script(script, sizeof(script), "");
    compile_and_run(script, out, sizeof(out));
    CHECK(has_motion_while_still(out));
    moving_then_still_script(script, sizeof(script), "zero,0,0,0\n");
    compile_and_run(script, out, sizeof(out));
    CHECK(!has_motion_while_still(out));
}

int main(void) {
    RUN(a_replay_uses_the_saved_gyro_zero);
    RUN(a_sensor_failure_in_the_log_replays_as_fallback);
    RUN(a_recorded_trip_replays_to_one_press);
    RUN(settings_change_what_happens);
    return TEST_RESULT();
}
