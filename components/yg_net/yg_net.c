#include "yg_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "esp_event.h"
#include "esp_image_format.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "yg_app.h"
#include "yg_http_internal.h"
#include "yg_nvs.h"

#define SCAN_MAX_RECORDS 24

static const char *TAG = "yg_net";

typedef enum
{
    REQUEST_HOTSPOT_ON,
    REQUEST_HOTSPOT_OFF,
    REQUEST_ENV_SCAN,
    REQUEST_JOIN,
} request_kind_t;

typedef struct
{
    request_kind_t kind;
    char ssid[33];
    char password[64];
} request_t;

static QueueHandle_t s_requests;
static volatile bool s_running;
static bool s_joined;
static volatile bool s_env_done;
static int64_t s_env_started;
static yg_env_t s_env;

typedef struct
{
    char ssid[33];
    char password[64];
    uint8_t tx_dbm;
    bool debug;
} hotspot_config_t;

static void read_config(yg_engine_t *e, uint32_t now, void *arg) {
    (void)now;
    hotspot_config_t *c = arg;
    strlcpy(c->ssid, e->s->hotspot_ssid, sizeof(c->ssid));
    strlcpy(c->password, e->s->hotspot_password, sizeof(c->password));
    c->tx_dbm = e->s->wifi_tx_dbm;
    c->debug = e->s->debug;
}

static void hotspot_on(void) {
    if (s_running) {
        return;
    }
    hotspot_config_t c;
    yg_app_call(read_config, &c);

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, c.ssid, sizeof(ap.ap.ssid));
    strlcpy((char *)ap.ap.password, c.password, sizeof(ap.ap.password));
    ap.ap.ssid_len = (uint8_t)strlen(c.ssid);
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    /* The station side is only there for the debug scan and bench tests. */
    esp_wifi_set_mode(c.debug || s_joined ? WIFI_MODE_APSTA : WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap);
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
        return;
    }
    esp_wifi_set_max_tx_power((int8_t)(c.tx_dbm * 4));
    if (yg_http_start()) {
        s_running = true;
        ESP_LOGI(TAG, "Hotspot %s is up", c.ssid);
    }
}

static void hotspot_off(void) {
    if (!s_running || s_joined) {
        return;
    }
    yg_http_stop();
    esp_wifi_stop();
    s_running = false;
    yg_app_post_clients(0);
    ESP_LOGI(TAG, "Hotspot is down");
}

static void env_scan(void) {
    s_env_done = false;
    s_env_started = esp_timer_get_time();
    memset(&s_env, 0, sizeof(s_env));
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (!s_running || esp_wifi_get_mode(&mode) != ESP_OK || mode != WIFI_MODE_APSTA) {
        return;
    }
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
    }
}

static void scan_done(void) {
    static wifi_ap_record_t records[SCAN_MAX_RECORDS];
    uint16_t number = SCAN_MAX_RECORDS;
    uint16_t total = 0;
    esp_wifi_scan_get_ap_num(&total);
    if (esp_wifi_scan_get_ap_records(&number, records) != ESP_OK) {
        return;
    }
    s_env.wifi_networks = total;
    s_env.wifi_strongest = 0;
    for (uint16_t i = 0; i < number; i++) {
        if (s_env.wifi_strongest == 0 || records[i].rssi > s_env.wifi_strongest) {
            s_env.wifi_strongest = records[i].rssi;
        }
        if (records[i].primary >= 1 && records[i].primary <= 13) {
            s_env.per_channel[records[i].primary - 1]++;
        }
    }
    s_env_done = true;
    ESP_LOGI(TAG, "Wi-Fi scan took %d ms: %u networks, strongest %d dBm",
             (int)((esp_timer_get_time() - s_env_started) / 1000), (unsigned)total, (int)s_env.wifi_strongest);
}

static void join_network(const request_t *r) {
    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, r->ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, r->password, sizeof(sta.sta.password));
    s_joined = true;
    if (s_running) {
        esp_wifi_stop();
        yg_http_stop();
        s_running = false;
    }
    hotspot_on();
    esp_wifi_set_config(WIFI_IF_STA, &sta);
    esp_wifi_connect();
}

static void net_task(void *arg) {
    (void)arg;
    request_t r;
    for (;;) {
        if (xQueueReceive(s_requests, &r, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (r.kind) {
        case REQUEST_HOTSPOT_ON:
            hotspot_on();
            break;
        case REQUEST_HOTSPOT_OFF:
            hotspot_off();
            break;
        case REQUEST_ENV_SCAN:
            env_scan();
            break;
        case REQUEST_JOIN:
            join_network(&r);
            break;
        }
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)data;
    if (id == WIFI_EVENT_AP_STACONNECTED || id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_sta_list_t list = { 0 };
        esp_wifi_ap_get_sta_list(&list);
        yg_app_post_clients((uint8_t)list.num);
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        scan_done();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED && s_joined) {
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    (void)base;
    (void)id;
    const ip_event_got_ip_t *event = data;
    printf("Joined network, address " IPSTR "\n", IP2STR(&event->ip_info.ip));
}

void yg_net_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    /* Settings live in our own storage: Wi-Fi need not write flash at every start. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL));
    s_requests = xQueueCreate(4, sizeof(request_t));
    xTaskCreate(net_task, "yg_net", 4096, NULL, 4, NULL);
}

static void post_request(request_kind_t kind) {
    request_t r = { .kind = kind };
    xQueueSend(s_requests, &r, 0);
}

void yg_net_hotspot(bool on) {
    post_request(on ? REQUEST_HOTSPOT_ON : REQUEST_HOTSPOT_OFF);
}

bool yg_net_hotspot_running(void) {
    return s_running;
}

void yg_net_env_start(void) {
    post_request(REQUEST_ENV_SCAN);
}

void yg_net_env_finish(yg_env_t *out) {
    if (s_env_done) {
        out->wifi_scanned = true;
        out->wifi_networks = s_env.wifi_networks;
        out->wifi_strongest = s_env.wifi_strongest;
        memcpy(out->per_channel, s_env.per_channel, sizeof(out->per_channel));
    }
}

/* A connection to this unit's own web server, so that a PC with no Wi-Fi can test
 * the real HTTP code through USB. Returns -1 after printing why it failed. */
static int open_loopback(void) {
    if (!yg_http_running()) {
        printf("The web server is not running. Use the wifi command first.\n");
        return -1;
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(80),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if (sock < 0 || connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("Connect failed: %d\n", errno);
        if (sock >= 0) close(sock);
        return -1;
    }
    struct timeval timeout = { .tv_sec = 20 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return sock;
}

/* Prints the response. The server keeps the connection open, so a quiet moment
 * after the first data means the end. */
static void print_response(int sock) {
    static char buf[1024];
    for (;;) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            break;
        }
        fwrite(buf, 1, (size_t)n, stdout);
        fflush(stdout);
        struct timeval timeout = { .tv_usec = 700 * 1000 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    }
    printf("\n");
    close(sock);
}

/* api GET /api/status, or api POST /api/press {...} */
static int cmd_api(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: api GET|POST <path> [body]\n");
        return 1;
    }
    int sock = open_loopback();
    if (sock < 0) {
        return 1;
    }
    const char *body = argc > 3 ? argv[3] : "";
    static char request[1024];
    int n = snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\nHost: yg\r\nContent-Length: %u\r\n\r\n%s",
                     argv[1], argv[2], (unsigned)strlen(body), body);
    send(sock, request, (size_t)n, 0);
    print_response(sock);
    return 0;
}

static bool send_all(int sock, const uint8_t *data, size_t len) {
    while (len > 0) {
        int n = send(sock, data, len, 0);
        if (n <= 0) {
            return false;
        }
        data += n;
        len -= (size_t)n;
    }
    return true;
}

/* ota-self [corrupt|fail]: uploads the running firmware to this unit's own
 * /api/ota, to test updates and rollback with no Wi-Fi client. "corrupt" damages
 * the image, which must be refused. "fail" makes the new boot fail its self-check,
 * which must roll back. */
static int cmd_ota_self(int argc, char **argv) {
    bool corrupt = argc == 2 && strcmp(argv[1], "corrupt") == 0;
    bool fail = argc == 2 && strcmp(argv[1], "fail") == 0;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_pos_t pos = { .offset = running->address, .size = running->size };
    esp_image_metadata_t image;
    if (esp_image_get_metadata(&pos, &image) != ESP_OK) {
        printf("Cannot read the running image\n");
        return 1;
    }
    int sock = open_loopback();
    if (sock < 0) {
        return 1;
    }
    if (fail) {
        yg_nvs_set_test_flag();
    }
    char head[96];
    int n = snprintf(head, sizeof(head), "POST /api/ota HTTP/1.1\r\nHost: yg\r\nContent-Length: %u\r\n\r\n", (unsigned)image.image_len);
    send_all(sock, (const uint8_t *)head, (size_t)n);
    uint8_t *buf = malloc(4096);
    uint32_t sent = 0;
    while (buf != NULL && sent < image.image_len) {
        size_t chunk = image.image_len - sent < 4096 ? image.image_len - sent : 4096;
        if (esp_partition_read(running, sent, buf, chunk) != ESP_OK) {
            break;
        }
        if (corrupt && sent == 8192) {
            buf[100] ^= 0xFF;
        }
        if (!send_all(sock, buf, chunk)) {
            break;
        }
        sent += (uint32_t)chunk;
    }
    free(buf);
    printf("Sent %u of %u bytes\n", (unsigned)sent, (unsigned)image.image_len);
    print_response(sock);
    return 0;
}

/* Joins a LAN until the next restart. Nothing is stored. For bench tests from a
 * PC that cannot reach the hotspot. */
static int cmd_wifi_join(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: wifi-join <ssid> <password>\n");
        return 1;
    }
    request_t r = { .kind = REQUEST_JOIN };
    strlcpy(r.ssid, argv[1], sizeof(r.ssid));
    strlcpy(r.password, argv[2], sizeof(r.password));
    xQueueSend(s_requests, &r, 0);
    printf("Joining %s\n", r.ssid);
    return 0;
}

void yg_net_console_register(void) {
    const esp_console_cmd_t api = { .command = "api", .help = "HTTP request to this unit: api GET|POST <path> [body]", .func = cmd_api };
    const esp_console_cmd_t join = { .command = "wifi-join", .help = "Join a LAN until restart, for bench tests", .func = cmd_wifi_join };
    const esp_console_cmd_t ota_self = { .command = "ota-self", .help = "Upload this firmware to itself: ota-self [corrupt|fail]", .func = cmd_ota_self };
    ESP_ERROR_CHECK(esp_console_cmd_register(&api));
    ESP_ERROR_CHECK(esp_console_cmd_register(&ota_self));
    ESP_ERROR_CHECK(esp_console_cmd_register(&join));
}
