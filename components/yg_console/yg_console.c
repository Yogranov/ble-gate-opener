#include "yg_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_private/system_internal.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "yg_app.h"
#include "yg_ble.h"
#include "yg_io.h"
#include "yg_log.h"
#include "yg_nvs.h"

#define LINES_PER_REST 64u

static void read_status(yg_engine_t *e, uint32_t now, void *arg) {
    yg_engine_status_json(e, now, arg, 1024);
}

static int cmd_status(int argc, char **argv) {
    (void)argc;
    (void)argv;
    static char buf[1024];
    yg_app_call(read_status, buf);
    printf("%s\n", buf);
    uint32_t trip_used, trip_total, cap_used, cap_total;
    yg_log_usage(&trip_used, &trip_total, &cap_used, &cap_total);
    printf("{\"remote_pin_high\":%s,\"log_sectors\":%u,\"log_total\":%u,\"capture_sectors\":%u,\"capture_total\":%u}\n",
           yg_io_remote_level() ? "true" : "false", (unsigned)trip_used, (unsigned)trip_total, (unsigned)cap_used, (unsigned)cap_total);
    return 0;
}

static void read_wifi(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    (void)arg;
    printf("SSID: %s\nPassword: %s\n", e->s->hotspot_ssid, e->s->hotspot_password);
    yg_engine_hotspot_request(e, now);
}

static int cmd_wifi(int argc, char **argv) {
    (void)argc;
    (void)argv;
    yg_app_call(read_wifi, NULL);
    return 0;
}

typedef struct
{
    uint8_t addr[YG_ADDR_LEN];
} pair_arg_t;

static void do_pair(yg_engine_t *e, uint32_t now, void *arg) {
    yg_engine_pair_set(e, now, ((pair_arg_t *)arg)->addr, YG_ADDR_TYPE_ANY);
}

static int cmd_pair(int argc, char **argv) {
    pair_arg_t arg;
    if (argc != 2 || !yg_addr_parse(argv[1], arg.addr)) {
        printf("Usage: pair c5:b0:e6:53:59:02\n");
        return 1;
    }
    yg_app_call(do_pair, &arg);
    printf("Saved %s\n", argv[1]);
    return 0;
}

/* Leaves NULL if the press started, or says why it did not. */
static void do_press(yg_engine_t *e, uint32_t now, void *arg) {
    if (!yg_engine_press(e, now, YG_PRESS_WEB)) {
        *(const char **)arg = yg_engine_remote_ready(e) ? "Already pressing" : "No remote learned";
    }
}

static int cmd_press(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *refused = NULL;
    yg_app_call(do_press, &refused);
    printf("%s\n", refused == NULL ? "Pressing" : refused);
    return 0;
}

typedef struct
{
    const char *key;
    const char *value;
    bool ok;
} set_arg_t;

static void do_set(yg_engine_t *e, uint32_t now, void *arg) {
    set_arg_t *a = arg;
    const yg_setting_desc_t *d = yg_settings_find(a->key);
    if (d == NULL) {
        return;
    }
    if (d->type == YG_SET_BOOL) {
        a->ok = yg_settings_set_int(e->s, a->key, atoi(a->value));
    } else {
        a->ok = yg_settings_set_str(e->s, a->key, a->value);
    }
    if (a->ok) {
        yg_app_setting_changed(e, now, a->key, a->value);
    }
}

static int cmd_set(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: set <key> <value>\n");
        return 1;
    }
    set_arg_t arg = { argv[1], argv[2], false };
    yg_app_call(do_set, &arg);
    printf(arg.ok ? "Saved\n" : "Rejected\n");
    return arg.ok ? 0 : 1;
}

static void do_simulate(yg_engine_t *e, uint32_t now, void *arg) {
    *(bool *)arg = yg_engine_simulate_motion(e, now);
}

static int cmd_motion(int argc, char **argv) {
    (void)argc;
    (void)argv;
    bool ok = false;
    yg_app_call(do_simulate, &ok);
    printf(ok ? "Simulated motion\n" : "Needs debug on\n");
    return ok ? 0 : 1;
}

static void do_read_debug(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    *(bool *)arg = e->s->debug;
}

/* Restarts with the reset reason a brownout leaves, to test what follows one on the bench. */
static int cmd_brownout(int argc, char **argv) {
    (void)argc;
    (void)argv;
    bool debug = false;
    yg_app_call(do_read_debug, &debug);
    if (!debug) {
        printf("Needs debug on\n");
        return 1;
    }
    fflush(stdout);
    esp_reset_reason_set_hint(ESP_RST_BROWNOUT);
    esp_restart_noos();
}

typedef struct
{
    yg_pair_mode_t mode;
} scan_arg_t;

static void do_scan(yg_engine_t *e, uint32_t now, void *arg) {
    yg_engine_pair_start(e, now, ((scan_arg_t *)arg)->mode);
}

static void do_scan_stop(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_engine_pair_cancel(e, now);
}

static void read_candidates(yg_engine_t *e, uint32_t now, void *arg) {
    yg_pairing_candidates_json(&e->pairing, e->s, now, arg, 2048);
}

/* scan start, scan list, scan stop */
static int cmd_scan(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "start") == 0) {
        scan_arg_t arg = { YG_PAIR_LIST };
        yg_app_call(do_scan, &arg);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "stop") == 0) {
        yg_app_call(do_scan_stop, NULL);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        static char buf[2048];
        yg_app_call(read_candidates, buf);
        printf("%s\n", buf);
        return 0;
    }
    printf("Usage: scan start|list|stop\n");
    return 1;
}

/* devices [seconds]: every device heard, not only iBeacons. Useful to find a MAC. */
static int cmd_devices(int argc, char **argv) {
    int seconds = argc == 2 ? atoi(argv[1]) : 10;
    scan_arg_t arg = { YG_PAIR_LIST };
    yg_app_call(do_scan, &arg);
    yg_ble_count_begin();
    vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
    uint16_t count = yg_ble_count_end();
    yg_app_call(do_scan_stop, NULL);
    printf("%u devices\n", count);
    yg_addr_t addr;
    int8_t rssi;
    for (uint16_t i = 0; yg_ble_counted_device(i, &addr, &rssi); i++) {
        char mac[18];
        yg_addr_format(addr.addr, mac);
        printf("%s %s %s %d dBm\n", mac, addr.type == YG_ADDR_TYPE_RANDOM ? "random" : "public",
               yg_addr_is_stable(&addr) ? "stable" : "rotating", (int)rssi);
    }
    return 0;
}

/* Writing to USB never waits, so without a rest now and then a long export starves the
 * idle task until the watchdog restarts the unit, 10 s in. */
static bool print_line(const char *line, size_t len, void *ctx) {
    (void)ctx;
    static uint32_t lines;
    fwrite(line, 1, len, stdout);
    fputc('\n', stdout);
    if (++lines % LINES_PER_REST == 0) {
        vTaskDelay(1);
    }
    return true;
}

static int cmd_logs(int argc, char **argv) {
    bool captures = !(argc == 2 && strcmp(argv[1], "nocapture") == 0);
    printf("#BEGIN\n");
    yg_log_export(captures, print_line, NULL);
    printf("#END\n");
    fflush(stdout);
    return 0;
}

/* log-fill <sectors>: fills both logs with dummy records, to test a full log. */
static int cmd_log_fill(int argc, char **argv) {
    enum { RECORDS_PER_SECTOR = 16 };
    int sectors = argc == 2 ? atoi(argv[1]) : 0;
    if (sectors <= 0) {
        printf("Usage: log-fill <sectors>\n");
        return 1;
    }
    static yg_record_t r;
    static const int16_t raw[6 * YG_CAP_IMU_PER_RECORD];
    yg_rec_cap_imu(&r, yg_app_current_trip(), 0, 0, 0, YG_CAP_IMU_PER_RECORD, raw);
    for (int i = 0; i < sectors * RECORDS_PER_SECTOR; i++) {
        yg_log_trip(&r);
        yg_log_capture(&r);
        if (i % RECORDS_PER_SECTOR == 0) {
            vTaskDelay(1);
        }
    }
    printf("Filled\n");
    return 0;
}

static volatile bool s_watching;
static volatile uint32_t s_slowest_ms;

static void log_something(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_engine_time_anchor(e, now, 0);
}

/* Times the decision task while it logs, as it would when deciding on a press. */
static void watch_task(void *arg) {
    (void)arg;
    while (s_watching) {
        int64_t start = esp_timer_get_time();
        yg_app_call(log_something, NULL);
        uint32_t ms = (uint32_t)((esp_timer_get_time() - start) / 1000);
        if (ms > s_slowest_ms) {
            s_slowest_ms = ms;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    vTaskDelete(NULL);
}

/* log-clear: erases the logs and reports how long the decision task was held up. */
static int cmd_log_clear(int argc, char **argv) {
    (void)argc;
    (void)argv;
    s_watching = true;
    s_slowest_ms = 0;
    xTaskCreate(watch_task, "yg_watch", 3072, NULL, 5, NULL);
    int64_t start = esp_timer_get_time();
    int result = yg_log_clear();
    uint32_t ms = (uint32_t)((esp_timer_get_time() - start) / 1000);
    s_watching = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    printf("{\"cleared\":%s,\"ms\":%u,\"slowest_ms\":%u}\n", result == 0 ? "true" : "false", (unsigned)ms, (unsigned)s_slowest_ms);
    return result == 0 ? 0 : 1;
}

static int cmd_factory_reset(int argc, char **argv) {
    (void)argc;
    (void)argv;
    yg_app_erase_everything();
    printf("Erased settings and logs. Restarting.\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

static int cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    esp_restart();
    return 0;
}

void yg_console_start(void (*register_more)(void)) {
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "yg>";
    config.max_cmdline_length = 256;
    esp_console_dev_usb_serial_jtag_config_t device = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&device, &config, &repl));

    const esp_console_cmd_t commands[] = {
        { .command = "status", .help = "State, signal, sensor, firmware", .func = cmd_status },
        { .command = "wifi", .help = "Show the hotspot name and password, and start it", .func = cmd_wifi },
        { .command = "pair", .help = "Save a beacon by MAC", .func = cmd_pair },
        { .command = "press", .help = "Press the remote", .func = cmd_press },
        { .command = "set", .help = "Change a setting: set <key> <value>", .func = cmd_set },
        { .command = "motion", .help = "Simulate motion, debug only", .func = cmd_motion },
        { .command = "brownout", .help = "Restart with a brownout's reset reason, debug only", .func = cmd_brownout },
        { .command = "scan", .help = "List nearby iBeacons: scan start|list|stop", .func = cmd_scan },
        { .command = "devices", .help = "List every BLE device heard: devices [seconds]", .func = cmd_devices },
        { .command = "logs", .help = "Stream the logs as NDJSON: logs [nocapture]", .func = cmd_logs },
        { .command = "log-fill", .help = "Fill both logs with dummy records: log-fill <sectors>", .func = cmd_log_fill },
        { .command = "log-clear", .help = "Erase the logs and report the time taken", .func = cmd_log_clear },
        { .command = "factory-reset", .help = "Erase settings and logs", .func = cmd_factory_reset },
        { .command = "reboot", .help = "Restart", .func = cmd_reboot },
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&commands[i]));
    }
    if (register_more != NULL) {
        register_more();
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
