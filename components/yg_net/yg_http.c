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
#include "yg_log.h"
#include "yg_nvs.h"

#define JSON_BUF_SIZE 6144
#define BODY_MAX      1024
#define CHUNK_SIZE    2048
#define OTA_BUF_SIZE  4096

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
    char body[128];
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}", message);
    httpd_resp_set_status(req, status);
    return send_json(req, body);
}

/* Returns the parsed body, or NULL after sending an error. */
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
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        send_error(req, "400 Bad Request", "bad json");
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
    yg_engine_status_json(e, now, arg, JSON_BUF_SIZE);
}

static esp_err_t get_status(httpd_req_t *req) {
    yg_app_call(fn_status, s_json);
    return send_json(req, s_json);
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
             yg_io_remote_level() ? "true" : "false");
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
        a->ok = yg_engine_setpoint_dbm(e, now, (int8_t)dbm->valueint);
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

static void fn_press(yg_engine_t *e, uint32_t now, void *arg) {
    *(bool *)arg = yg_engine_press(e, now, YG_PRESS_WEB);
}

static esp_err_t post_press(httpd_req_t *req) {
    bool started = false;
    yg_app_call(fn_press, &started);
    return started ? send_ok(req) : send_error(req, "409 Conflict", "already pressing");
}

static esp_err_t post_test_led(httpd_req_t *req) {
    yg_io_test_led();
    return send_ok(req);
}

static void fn_test_beep(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    (void)arg;
    yg_io_test_beep(e->s->buzzer_type);
}

static esp_err_t post_test_beep(httpd_req_t *req) {
    yg_app_call(fn_test_beep, NULL);
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

static void fn_candidates(yg_engine_t *e, uint32_t now, void *arg) {
    yg_pairing_candidates_json(&e->pairing, e->s, now, arg, JSON_BUF_SIZE);
}

static esp_err_t get_candidates(httpd_req_t *req) {
    yg_app_call(fn_candidates, s_json);
    return send_json(req, s_json);
}

typedef struct
{
    uint8_t addr[YG_ADDR_LEN];
    uint8_t type;
} pair_arg_t;

static void fn_pair_set(yg_engine_t *e, uint32_t now, void *arg) {
    pair_arg_t *a = arg;
    yg_engine_pair_set(e, now, a->addr, a->type);
}

static esp_err_t post_pair_set(httpd_req_t *req) {
    cJSON *root = read_body(req);
    if (root == NULL) {
        return ESP_OK;
    }
    pair_arg_t arg = { .type = YG_ADDR_TYPE_ANY };
    const cJSON *mac = cJSON_GetObjectItem(root, "mac");
    const cJSON *type = cJSON_GetObjectItem(root, "type");
    bool ok = cJSON_IsString(mac) && yg_addr_parse(mac->valuestring, arg.addr);
    if (cJSON_IsNumber(type)) {
        arg.type = (uint8_t)type->valueint;
    }
    cJSON_Delete(root);
    if (!ok) {
        return send_error(req, "400 Bad Request", "bad mac");
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
    yg_log_trip_list(query_u32(req, "before", current + 1), query_u32(req, "limit", 20), stream_line, &s);
    stream_flush(&s);
    httpd_resp_send_chunk(req, "]}", 2);
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
    snprintf(arg, 256, "{\"k\":\"meta\",\"format\":1,\"fw\":\"%s\",\"current_trip\":%u,\"ssid\":\"%s\"}",
             e->fw, (unsigned)e->trip, e->s->hotspot_ssid);
}

static esp_err_t get_export(httpd_req_t *req) {
    stream_t s;
    if (!stream_begin(&s, req, '\n')) {
        return send_error(req, "500 Internal Server Error", "no memory");
    }
    httpd_resp_set_type(req, "application/x-ndjson");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"yogigate-logs.ndjson\"");
    static char meta[256];
    yg_app_call(fn_meta, meta);
    stream_line(meta, strlen(meta), &s);
    yg_log_export(query_u32(req, "captures", 1) != 0, stream_line, &s);
    return stream_end(&s);
}

static esp_err_t post_logs_clear(httpd_req_t *req) {
    return yg_log_clear() == 0 ? send_ok(req) : send_error(req, "500 Internal Server Error", "clear failed");
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
    while (buf != NULL && left > 0) {
        int n = httpd_req_recv(req, buf, left < OTA_BUF_SIZE ? left : OTA_BUF_SIZE);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            err = ESP_FAIL;
            break;
        }
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
    yg_log_clear();
    yg_nvs_factory_reset();
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
    { .uri = "/api/setpoint", .method = HTTP_POST, .handler = post_setpoint },
    { .uri = "/api/press", .method = HTTP_POST, .handler = post_press },
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

bool yg_http_start(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = sizeof(ROUTES) / sizeof(ROUTES[0]);
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.max_open_sockets = 5;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 20;
    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web server failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return false;
    }
    for (size_t i = 0; i < sizeof(ROUTES) / sizeof(ROUTES[0]); i++) {
        httpd_register_uri_handler(s_server, &ROUTES[i]);
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
