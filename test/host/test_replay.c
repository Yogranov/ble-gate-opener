/* Runs the replay program on a small script, the way tools/yg_logs.py does. */
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

int main(void) {
    RUN(a_sensor_failure_in_the_log_replays_as_fallback);
    RUN(a_recorded_trip_replays_to_one_press);
    RUN(settings_change_what_happens);
    return TEST_RESULT();
}
