#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "yg_app.h"
#include "yg_http_internal.h"
#include "yg_io.h"
#include "yg_json.h"
#include "yg_log.h"
#include "yg_nvs.h"
#include "sdkconfig.h"

#define JSON_BUF_SIZE 6144
#define BODY_MAX      1024
#define CHUNK_SIZE    2048
#define OTA_BUF_SIZE  4096
#define OTA_MAX_TIMEOUTS 3
#define BODY_MAX_DEPTH 8
#define META_SIZE     384
#define GUARD_HEADER  "X-YogiGate"

static const char *TAG = "yg_net";

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static httpd_handle_t s_server;

/* The server handles one request at a time, so one buffer is enough. */
static char s_json[JSON_BUF_SIZE];

static esp_err_t send_json(httpd_req_t *req, const char *json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_ok(httpd_req_t *req) {
    return send_json(req, "{\"ok\":true}");
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message) {
    char body[160];
    yg_jw_t w;
    yg_jw_init(&w, body, sizeof(body));
    yg_jw_raw(&w, "{\"ok\":false,\"error\":");
    yg_jw_string(&w, message);
    yg_jw_raw(&w, "}");
    if (yg_jw_finish(&w) < 0) {
        strcpy(body, "{\"ok\":false}");
    }
    httpd_resp_set_status(req, status);
    return send_json(req, body);
}

/* Returns the parsed body, which is always a JSON object, or NULL after sending an error. */
static cJSON *read_body(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > BODY_MAX) {
        send_error(req, "400 Bad Request", "body size");
        return NULL;
    }
    static char body[BODY_MAX + 1];
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, body + got, req->content_len - got);
        if (n <= 0) {
            send_error(req, "400 Bad Request", "read failed");
            return NULL;
        }
        got += (size_t)n;
    }
    body[got] = 0;
    if (!yg_json_depth_ok(body, BODY_MAX_DEPTH)) {
        send_error(req, "400 Bad Request", "too deep");
        return NULL;
    }
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        send_error(req, "400 Bad Request", "bad json");
    } else if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = NULL;
        send_error(req, "400 Bad Request", "expected an object");
    }
    return root;
}

/* Streaming JSON lines in chunks of a useful size. */
typedef struct
{
    httpd_req_t *req;
    char *buf;
    size_t used;
    bool failed;
    bool comma;
    char separator;
} stream_t;

static bool stream_flush(stream_t *s) {
    if (s->used > 0 && !s->failed) {
        s->failed = httpd_resp_send_chunk(s->req, s->buf, s->used) != ESP_OK;
    }
    s->used = 0;
    return !s->failed;
}

static bool stream_line(const char *line, size_t len, void *ctx) {
    stream_t *s = ctx;
    if (s->used + len + 2 > CHUNK_SIZE && !stream_flush(s)) {
        return false;
    }
    if (len + 2 > CHUNK_SIZE) {
        return true;
    }
    if (s->separator == ',' && s->comma) {
        s->buf[s->used++] = ',';
    }
    memcpy(s->buf + s->used, line, len);
    s->used += len;
    if (s->separator == '\n') {
        s->buf[s->used++] = '\n';
    }
    s->comma = true;
    return true;
}

static bool stream_begin(stream_t *s, httpd_req_t *req, char separator) {
    s->req = req;
    s->buf = malloc(CHUNK_SIZE);
    s->used = 0;
    s->failed = false;
    s->comma = false;
    s->separator = separator;
    return s->buf != NULL;
}

static esp_err_t stream_end(stream_t *s) {
    stream_flush(s);
    free(s->buf);
    return httpd_resp_send_chunk(s->req, NULL, 0);
}

static esp_err_t get_page(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

static void fn_status(yg_engine_t *e, uint32_t now, void *arg) {
    char *buf = arg;
    int len = yg_engine_status_json(e, now, buf, JSON_BUF_SIZE);
    /* The driver is a build-time choice the engine does not know, so append it here. */
    if (len > 1 && buf[len - 1] == '}' && (size_t)len + 20 < JSON_BUF_SIZE) {
#if CONFIG_YG_GATE_DRIVER_RF433
        snprintf(buf + len - 1, (size_t)(JSON_BUF_SIZE - (len - 1)), ",\"driver\":\"rf433\"}");
#else
        snprintf(buf + len - 1, (size_t)(JSON_BUF_SIZE - (len - 1)), ",\"driver\":\"mosfet\"}");
#endif
    }
}

static esp_err_t get_status(httpd_req_t *req) {
    yg_app_call(fn_status, s_json);
    return send_json(req, s_json);
}

/* There is no MOSFET pin on the RF variant, so it reports the actuator as low. */
static bool actuator_pin_high(void) {
#if CONFIG_YG_GATE_DRIVER_RF433
    return false;
#else
    return yg_io_remote_level();
#endif
}

static esp_err_t get_system(httpd_req_t *req) {
    uint32_t trip_used, trip_total, cap_used, cap_total;
    yg_log_usage(&trip_used, &trip_total, &cap_used, &cap_total);
    const esp_partition_t *running = esp_ota_get_running_partition();
    snprintf(s_json, JSON_BUF_SIZE,
             "{\"log_used\":%u,\"log_total\":%u,\"capture_used\":%u,\"capture_total\":%u,\"sector_bytes\":4096,"
             "\"heap_free\":%u,\"partition\":\"%s\",\"remote_pin_high\":%s}",
             (unsigned)trip_used, (unsigned)trip_total, (unsigned)cap_used, (unsigned)cap_total,
             (unsigned)esp_get_free_heap_size(), running != NULL ? running->label : "unknown",
             actuator_pin_high() ? "true" : "false");
    return send_json(req, s_json);
}

static void fn_settings(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    yg_settings_to_json(e->s, arg, JSON_BUF_SIZE, false);
}

static esp_err_t get_settings(httpd_req_t *req) {
    yg_app_call(fn_settings, s_json);
    return send_json(req, s_json);
}

typedef struct
{
    const cJSON *root;
    const char *bad_key;
} settings_arg_t;

static bool apply_item(yg_settings_t *s, const cJSON *item) {
    if (cJSON_IsNumber(item)) return yg_settings_set_int(s, item->string, item->valueint);
    if (cJSON_IsBool(item)) return yg_settings_set_int(s, item->string, cJSON_IsTrue(item) ? 1 : 0);
    if (cJSON_IsString(item)) return yg_settings_set_str(s, item->string, item->valuestring);
    return false;
}

static void fn_set_settings(yg_engine_t *e, uint32_t now, void *arg) {
    settings_arg_t *a = arg;
    /* All or nothing: check everything on a copy first. */
    static yg_settings_t copy;
    copy = *e->s;
    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, a->root) {
        if (!apply_item(&copy, item)) {
            a->bad_key = item->string;
            return;
        }
    }
    *e->s = copy;
    cJSON_ArrayForEach(item, a->root) {
        char value[16];
        const char *text = value;
        if (cJSON_IsString(item)) {
            text = item->valuestring;
        } else if (cJSON_IsBool(item)) {
            strcpy(value, cJSON_IsTrue(item) ? "1" : "0");
        } else {
            snprintf(value, sizeof(value), "%d", item->valueint);
        }
        yg_app_setting_changed(e, now, item->string, text);
    }
}

static esp_err_t post_settings(httpd_req_t *req) {
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    settings_arg_t arg = { root, NULL };
    yg_app_call(fn_set_settings, &arg);
    esp_err_t result;
    if (arg.bad_key != NULL) {
        char message[64];
        snprintf(message, sizeof(message), "rejected %s", arg.bad_key);
        result = send_error(req, "400 Bad Request", message);
    } else {
        result = send_ok(req);
    }
    cJSON_Delete(root);
    return result;
}

static void fn_settings_reset(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_app_settings_reset(e, now);
}

static esp_err_t post_settings_reset(httpd_req_t *req) {
    yg_app_call(fn_settings_reset, NULL);
    return send_ok(req);
}

typedef struct
{
    const cJSON *root;
    bool ok;
} setpoint_arg_t;

static void fn_setpoint(yg_engine_t *e, uint32_t now, void *arg) {
    setpoint_arg_t *a = arg;
    const cJSON *dbm = cJSON_GetObjectItem(a->root, "dbm");
    if (cJSON_IsTrue(cJSON_GetObjectItem(a->root, "here"))) {
        a->ok = yg_engine_setpoint_here(e, now);
    } else if (cJSON_IsTrue(cJSON_GetObjectItem(a->root, "any"))) {
        a->ok = yg_engine_setpoint_dbm(e, now, YG_SET_POINT_ANY);
    } else if (cJSON_IsNumber(dbm) && dbm->valueint != YG_SET_POINT_ANY) {
        a->ok = yg_engine_setpoint_dbm(e, now, dbm->valueint);
    }
}

static esp_err_t post_setpoint(httpd_req_t *req) {
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    setpoint_arg_t arg = { root, false };
    yg_app_call(fn_setpoint, &arg);
    cJSON_Delete(root);
    return arg.ok ? send_ok(req) : send_error(req, "400 Bad Request", "nothing heard or out of range");
}

/* Leaves NULL if the press started, or says why it did not. */
static void fn_press(yg_engine_t *e, uint32_t now, void *arg) {
    if (!yg_engine_press(e, now, YG_PRESS_WEB)) {
        *(const char **)arg = yg_engine_remote_ready(e) ? "already pressing" : "no remote learned";
    }
}

static esp_err_t post_press(httpd_req_t *req) {
    const char *refused = NULL;
    yg_app_call(fn_press, &refused);
    return refused == NULL ? send_ok(req) : send_error(req, "409 Conflict", refused);
}

static esp_err_t post_test_led(httpd_req_t *req) {
    yg_io_test_led();
    return send_ok(req);
}

static void fn_test_beep(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    yg_io_buzz(*(yg_buzz_event_t *)arg, e->s->buzzer_type);
}

/* Plays whatever the settings say, so that a sound can be tried before it is turned on. */
static esp_err_t post_test_beep(httpd_req_t *req) {
    yg_buzz_event_t sound = YG_BUZZ_SAVED;
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    const cJSON *name = cJSON_GetObjectItem(root, "sound");
    bool known = !cJSON_IsString(name) || yg_sound_parse(name->valuestring, &sound);
    cJSON_Delete(root);
    if (!known) {
        return send_error(req, "400 Bad Request", "unknown sound");
    }
    yg_app_call(fn_test_beep, &sound);
    return send_ok(req);
}

static void fn_pair_auto(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_engine_pair_start(e, now, YG_PAIR_AUTO);
}

static void fn_pair_scan(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_engine_pair_start(e, now, YG_PAIR_LIST);
}

static void fn_pair_cancel(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_engine_pair_cancel(e, now);
}

static void fn_pair_forget(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_engine_pair_forget(e, now);
}

static esp_err_t post_call(httpd_req_t *req) {
    yg_app_call((yg_app_fn_t)req->user_ctx, NULL);
    return send_ok(req);
}

#if CONFIG_YG_GATE_DRIVER_RF433
static void fn_rf_learn(yg_engine_t *e, uint32_t now, void *arg) { (void)arg; yg_engine_rf_learn_start(e, now); }
static void fn_rf_forget(yg_engine_t *e, uint32_t now, void *arg) { (void)arg; yg_engine_rf_forget(e, now); }
#endif

static void fn_candidates(yg_engine_t *e, uint32_t now, void *arg) {
    yg_pairing_candidates_json(&e->pairing, e->s, now, arg, JSON_BUF_SIZE);
}

static esp_err_t get_candidates(httpd_req_t *req) {
    yg_app_call(fn_candidates, s_json);
    return send_json(req, s_json);
}

static void fn_pair_set(yg_engine_t *e, uint32_t now, void *arg) {
    const yg_addr_t *a = arg;
    yg_engine_pair_set(e, now, a->addr, a->type);
}

static esp_err_t post_pair_set(httpd_req_t *req) {
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    yg_addr_t arg = { .type = YG_ADDR_TYPE_ANY };
    const cJSON *mac = cJSON_GetObjectItem(root, "mac");
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    bool ok = cJSON_IsString(mac) && yg_addr_parse(mac->valuestring, arg.addr);
    if (cJSON_IsNumber(type)) {
        ok = ok && type->valueint >= 0 && type->valueint <= UINT8_MAX;
        arg.type = (uint8_t)type->valueint;
    }
    cJSON_Delete(root);
    if (!ok || !yg_addr_can_pair(&arg)) {
        return send_error(req, "400 Bad Request", "bad address");
    }
    yg_app_call(fn_pair_set, &arg);
    return send_ok(req);
}

static uint32_t query_u32(httpd_req_t *req, const char *key, uint32_t fallback) {
    char query[64];
    char value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK
        && httpd_query_key_value(query, key, value, sizeof(value)) == ESP_OK) {
        return (uint32_t)strtoul(value, NULL, 10);
    }
    return fallback;
}

static esp_err_t get_trip_list(httpd_req_t *req) {
    uint32_t current = yg_app_current_trip();
    stream_t s;
    if (!stream_begin(&s, req, ',')) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char head[48];
    snprintf(head, sizeof(head), "{\"current\":%u,\"trips\":[", (unsigned)current);
    httpd_resp_send_chunk(req, head, HTTPD_RESP_USE_STRLEN);
    uint32_t next_before = 0;
    yg_log_trip_list(query_u32(req, "before", current + 1), query_u32(req, "limit", 20), &next_before, stream_line, &s);
    stream_flush(&s);
    char tail[32];
    snprintf(tail, sizeof(tail), "],\"next_before\":%u}", (unsigned)next_before);
    httpd_resp_send_chunk(req, tail, HTTPD_RESP_USE_STRLEN);
    return stream_end(&s);
}

static esp_err_t get_trip(httpd_req_t *req) {
    uint32_t trip = 0;
    if (sscanf(req->uri, "/api/trips/%u", (unsigned *)&trip) != 1) {
        return send_error(req, "404 Not Found", "no such trip");
    }
    stream_t s;
    if (!stream_begin(&s, req, '\n')) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    httpd_resp_set_type(req, "application/x-ndjson");
    yg_log_trip_records(trip, stream_line, &s);
    return stream_end(&s);
}

typedef struct
{
    uint32_t trip;
    const char *rating;
    bool ok;
} rating_arg_t;

static void fn_rate(yg_engine_t *e, uint32_t now, void *arg) {
    rating_arg_t *a = arg;
    a->ok = yg_engine_rate_trip(e, now, a->trip, a->rating);
}

static esp_err_t post_rating(httpd_req_t *req) {
    unsigned trip = 0;
    if (sscanf(req->uri, "/api/trips/%u/rating", &trip) != 1) {
        return send_error(req, "404 Not Found", "no such trip");
    }
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    const cJSON *rating = cJSON_GetObjectItem(root, "rating");
    rating_arg_t arg = { trip, cJSON_IsString(rating) ? rating->valuestring : "", false };
    yg_app_call(fn_rate, &arg);
    cJSON_Delete(root);
    return arg.ok ? send_ok(req) : send_error(req, "400 Bad Request", "unknown rating");
}

static void fn_meta(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    yg_jw_t w;
    yg_jw_init(&w, arg, META_SIZE);
    yg_jw_raw(&w, "{\"k\":\"meta\",\"format\":1,\"fw\":");
    yg_jw_string(&w, e->fw);
    yg_jw_raw(&w, ",\"current_trip\":%u,\"ssid\":", (unsigned)e->trip);
    yg_jw_string(&w, e->s->hotspot_ssid);
    yg_jw_raw(&w, "}");
    yg_jw_finish(&w);
}

static esp_err_t get_export(httpd_req_t *req) {
    stream_t s;
    if (!stream_begin(&s, req, '\n')) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    httpd_resp_set_type(req, "application/x-ndjson");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"yogigate-logs.ndjson\"");
    static char meta[META_SIZE];
    yg_app_call(fn_meta, meta);
    stream_line(meta, strlen(meta), &s);
    yg_log_export(query_u32(req, "captures", 1) != 0, stream_line, &s);
    return stream_end(&s);
}

static void fn_trips_restart(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    (void)arg;
    yg_app_trips_restart(e);
}

static void fn_trips_restarted(yg_engine_t *e, uint32_t now, void *arg) {
    (void)arg;
    yg_app_trips_restarted(e, now);
}

/* There is no restart, because that would turn off the hotspot this request came through. */
static esp_err_t post_logs_clear(httpd_req_t *req) {
    yg_app_call(fn_trips_restart, NULL);
    if (yg_log_clear() != 0) {
        return send_error(req, "500 Internal Server Error", "clear failed");
    }
    yg_app_call(fn_trips_restarted, NULL);
    return send_ok(req);
}

typedef struct
{
    uint32_t epoch;
} time_arg_t;

static void fn_time(yg_engine_t *e, uint32_t now, void *arg) {
    yg_engine_time_anchor(e, now, ((time_arg_t *)arg)->epoch);
}

static esp_err_t post_time(httpd_req_t *req) {
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    const cJSON *epoch = cJSON_GetObjectItem(root, "epoch");
    bool ok = cJSON_IsNumber(epoch);
    time_arg_t arg = { ok ? (uint32_t)epoch->valuedouble : 0 };
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "bad epoch");
    }
    yg_app_call(fn_time, &arg);
    return send_ok(req);
}

static void restart_later(void *arg) {
    (void)arg;
    esp_restart();
}

static void schedule_restart(void) {
    static esp_timer_handle_t timer;
    if (timer == NULL) {
        const esp_timer_create_args_t args = { .callback = restart_later, .name = "yg_restart" };
        esp_timer_create(&args, &timer);
    }
    esp_timer_start_once(timer, 1000 * 1000);
}

static void fn_log_ota(yg_engine_t *e, uint32_t now, void *arg) {
    yg_engine_log_ota(e, now, *(uint8_t *)arg);
}

static esp_err_t ota_fail(httpd_req_t *req, const char *message) {
    uint8_t event = YG_OTA_EVENT_FAILED;
    yg_app_call(fn_log_ota, &event);
    return send_error(req, "400 Bad Request", message);
}

static esp_err_t post_ota(httpd_req_t *req) {
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (target == NULL || req->content_len == 0 || req->content_len > target->size) {
        return ota_fail(req, "bad size");
    }
    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) {
        return ota_fail(req, esp_err_to_name(err));
    }
    char *buf = malloc(OTA_BUF_SIZE);
    size_t left = req->content_len;
    int timeouts = 0;
    while (buf != NULL && left > 0) {
        int n = httpd_req_recv(req, buf, left < OTA_BUF_SIZE ? left : OTA_BUF_SIZE);
        /* A phone that drops off Wi-Fi never says so. Waiting for ever would hold up
         * every other request, and stop the hotspot from turning off. */
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < OTA_MAX_TIMEOUTS) {
            continue;
        }
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
        timeouts = 0;
        err = esp_ota_write(ota, buf, (size_t)n);
        if (err != ESP_OK) {
            break;
        }
        left -= (size_t)n;
    }
    free(buf);
    if (left > 0 || err != ESP_OK) {
        esp_ota_abort(ota);
        return ota_fail(req, "upload failed");
    }
    err = esp_ota_end(ota);
    if (err == ESP_OK) {
        err = esp_ota_set_boot_partition(target);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Firmware rejected: %s", esp_err_to_name(err));
        return ota_fail(req, "not a valid firmware");
    }
    uint8_t event = YG_OTA_EVENT_UPLOADED;
    yg_app_call(fn_log_ota, &event);
    send_ok(req);
    schedule_restart();
    return ESP_OK;
}

static esp_err_t post_reboot(httpd_req_t *req) {
    send_ok(req);
    schedule_restart();
    return ESP_OK;
}

static esp_err_t post_factory_reset(httpd_req_t *req) {
    yg_app_erase_everything();
    send_ok(req);
    schedule_restart();
    return ESP_OK;
}

static void fn_simulate(yg_engine_t *e, uint32_t now, void *arg) {
    *(bool *)arg = yg_engine_simulate_motion(e, now);
}

static esp_err_t post_simulate(httpd_req_t *req) {
    bool ok = false;
    yg_app_call(fn_simulate, &ok);
    return ok ? send_ok(req) : send_error(req, "403 Forbidden", "debug is off");
}

static void fn_capture(yg_engine_t *e, uint32_t now, void *arg) {
    *(bool *)arg = yg_engine_capture_now(e, now);
}

static esp_err_t post_capture(httpd_req_t *req) {
    bool ok = false;
    yg_app_call(fn_capture, &ok);
    return ok ? send_ok(req) : send_error(req, "409 Conflict", "debug is off or a capture is running");
}

typedef struct
{
    char *buf;
    bool debug;
} live_arg_t;

static void fn_live(yg_engine_t *e, uint32_t now, void *arg) {
    live_arg_t *a = arg;
    a->debug = e->s->debug;
    if (a->debug) {
        yg_engine_debug_json(e, now, a->buf, JSON_BUF_SIZE);
    }
}

static esp_err_t get_live(httpd_req_t *req) {
    live_arg_t arg = { s_json, false };
    yg_app_call(fn_live, &arg);
    return arg.debug ? send_json(req, s_json) : send_error(req, "403 Forbidden", "debug is off");
}

static const httpd_uri_t ROUTES[] = {
    { .uri = "/", .method = HTTP_GET, .handler = get_page },
    { .uri = "/api/status", .method = HTTP_GET, .handler = get_status },
    { .uri = "/api/system", .method = HTTP_GET, .handler = get_system },
    { .uri = "/api/settings", .method = HTTP_GET, .handler = get_settings },
    { .uri = "/api/settings", .method = HTTP_POST, .handler = post_settings },
    { .uri = "/api/settings/reset", .method = HTTP_POST, .handler = post_settings_reset },
    { .uri = "/api/setpoint", .method = HTTP_POST, .handler = post_setpoint },
    { .uri = "/api/press", .method = HTTP_POST, .handler = post_press },
#if CONFIG_YG_GATE_DRIVER_RF433
    { .uri = "/api/rf/learn", .method = HTTP_POST, .handler = post_call, .user_ctx = (void *)fn_rf_learn },
    { .uri = "/api/rf/forget", .method = HTTP_POST, .handler = post_call, .user_ctx = (void *)fn_rf_forget },
#endif
    { .uri = "/api/test/led", .method = HTTP_POST, .handler = post_test_led },
    { .uri = "/api/test/beep", .method = HTTP_POST, .handler = post_test_beep },
    { .uri = "/api/pair/auto", .method = HTTP_POST, .handler = post_call, .user_ctx = (void *)fn_pair_auto },
    { .uri = "/api/pair/scan", .method = HTTP_POST, .handler = post_call, .user_ctx = (void *)fn_pair_scan },
    { .uri = "/api/pair/cancel", .method = HTTP_POST, .handler = post_call, .user_ctx = (void *)fn_pair_cancel },
    { .uri = "/api/pair/forget", .method = HTTP_POST, .handler = post_call, .user_ctx = (void *)fn_pair_forget },
    { .uri = "/api/pair/candidates", .method = HTTP_GET, .handler = get_candidates },
    { .uri = "/api/pair/select", .method = HTTP_POST, .handler = post_pair_set },
    { .uri = "/api/pair/manual", .method = HTTP_POST, .handler = post_pair_set },
    { .uri = "/api/trips", .method = HTTP_GET, .handler = get_trip_list },
    { .uri = "/api/trips/*", .method = HTTP_GET, .handler = get_trip },
    { .uri = "/api/trips/*", .method = HTTP_POST, .handler = post_rating },
    { .uri = "/api/logs/export", .method = HTTP_GET, .handler = get_export },
    { .uri = "/api/logs/clear", .method = HTTP_POST, .handler = post_logs_clear },
    { .uri = "/api/time", .method = HTTP_POST, .handler = post_time },
    { .uri = "/api/ota", .method = HTTP_POST, .handler = post_ota },
    { .uri = "/api/reboot", .method = HTTP_POST, .handler = post_reboot },
    { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = post_factory_reset },
    { .uri = "/api/debug/simulate-motion", .method = HTTP_POST, .handler = post_simulate },
    { .uri = "/api/debug/capture-now", .method = HTTP_POST, .handler = post_capture },
    { .uri = "/api/debug/live", .method = HTTP_GET, .handler = get_live },
};

/* A page on another site can POST to this address, if the phone has cellular data
 * alongside the hotspot. A browser will not let it add a custom header. */
static esp_err_t guarded_post(httpd_req_t *req) {
    const httpd_uri_t *route = req->user_ctx;
    char value[4];
    if (httpd_req_get_hdr_value_str(req, GUARD_HEADER, value, sizeof(value)) != ESP_OK || strcmp(value, "1") != 0) {
        return send_error(req, "403 Forbidden", "missing " GUARD_HEADER " header");
    }
    req->user_ctx = route->user_ctx;
    return route->handler(req);
}

bool yg_http_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = sizeof(ROUTES) / sizeof(ROUTES[0]);
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.max_open_sockets = 5;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 20;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 5;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web server failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return false;
    }
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        httpd_uri_t route = ROUTES[i];
        if (route.method == HTTP_POST) {
            route.handler = guarded_post;
            route.user_ctx = (void *)&ROUTES[i];
        }
        httpd_register_uri_handler(s_server, &route);
    }
    return true;
}

void yg_http_stop(void) {
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}

bool yg_http_running(void) {
    return s_server != NULL;
}
