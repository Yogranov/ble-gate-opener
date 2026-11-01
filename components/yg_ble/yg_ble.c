#include "yg_ble.h"

#include <string.h>

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#define SCAN_MS_UNITS(ms) ((uint16_t)((ms) * 1000 / 625))
#define SCAN_PERIOD SCAN_MS_UNITS(100)
#define SCAN_SHARED_WINDOW SCAN_MS_UNITS(25)
#define COUNT_MAX 64

static const char *TAG = "yg_ble";

static yg_ble_advert_cb_t s_on_advert;
static volatile bool s_synced;
static yg_scan_mode_t s_mode = YG_SCAN_OFF;
static yg_beacon_t s_beacon;
static bool s_share_radio;
static uint8_t s_own_addr_type;

static volatile bool s_counting;
static uint8_t s_counted[COUNT_MAX][YG_ADDR_LEN];
static uint8_t s_counted_type[COUNT_MAX];
static int8_t s_counted_rssi[COUNT_MAX];
static uint16_t s_count;

static void to_yg_addr(const ble_addr_t *in, yg_addr_t *out) {
    for (int i = 0; i < YG_ADDR_LEN; i++) {
        out->addr[i] = in->val[YG_ADDR_LEN - 1 - i];
    }
    out->type = in->type == BLE_ADDR_RANDOM || in->type == BLE_ADDR_RANDOM_ID ? YG_ADDR_TYPE_RANDOM : YG_ADDR_TYPE_PUBLIC;
}

static void count_device(const yg_addr_t *a, int8_t rssi) {
    for (uint16_t i = 0; i < s_count && i < COUNT_MAX; i++) {
        if (memcmp(s_counted[i], a->addr, YG_ADDR_LEN) == 0) {
            s_counted_rssi[i] = rssi;
            return;
        }
    }
    if (s_count < COUNT_MAX) {
        memcpy(s_counted[s_count], a->addr, YG_ADDR_LEN);
        s_counted_type[s_count] = a->type;
        s_counted_rssi[s_count] = rssi;
        s_count++;
    }
}

static void handle_advert(const ble_addr_t *addr, int8_t rssi, const uint8_t *data, uint8_t len) {
    yg_addr_t a;
    yg_ibeacon_t ib;
    to_yg_addr(addr, &a);
    yg_ibeacon_parse(data, len, &ib);
    if (s_mode == YG_SCAN_OPEN) {
        if (s_counting) {
            count_device(&a, rssi);
        }
        bool is_paired = s_beacon.paired && yg_addr_equal(&a, &s_beacon.addr);
        if (!is_paired && !(ib.valid && yg_addr_is_stable(&a))) {
            return;
        }
    }
    s_on_advert(&a, rssi, &ib);
}

static int gap_event(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_advert(&event->disc.addr, event->disc.rssi, event->disc.data, event->disc.length_data);
        break;
#if CONFIG_BT_NIMBLE_EXT_ADV
    case BLE_GAP_EVENT_EXT_DISC:
        handle_advert(&event->ext_disc.addr, event->ext_disc.rssi, event->ext_disc.data, event->ext_disc.length_data);
        break;
#endif
    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGD(TAG, "Scan ended, reason %d", event->disc_complete.reason);
        break;
    default:
        break;
    }
    return 0;
}

static void add_filter_entry(ble_addr_t *entry, const yg_addr_t *a, uint8_t type) {
    entry->type = type;
    for (int i = 0; i < YG_ADDR_LEN; i++) {
        entry->val[i] = a->addr[YG_ADDR_LEN - 1 - i];
    }
}

static void apply_scan(void) {
    if (!s_synced) {
        return;
    }
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
    }
    if (s_mode == YG_SCAN_OFF) {
        return;
    }
    struct ble_gap_disc_params params = {
        .itvl = SCAN_PERIOD,
        .window = s_share_radio ? SCAN_SHARED_WINDOW : SCAN_PERIOD,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 1,
        .filter_duplicates = 0,
    };
    if (s_mode == YG_SCAN_FILTERED) {
        ble_addr_t list[2];
        uint8_t count = 0;
        const yg_addr_t *a = &s_beacon.addr;
        if (a->type == YG_ADDR_TYPE_ANY) {
            add_filter_entry(&list[count++], a, BLE_ADDR_PUBLIC);
            add_filter_entry(&list[count++], a, BLE_ADDR_RANDOM);
        } else {
            add_filter_entry(&list[count++], a, a->type == YG_ADDR_TYPE_RANDOM ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC);
        }
        int rc = ble_gap_wl_set(list, count);
        if (rc != 0) {
            ESP_LOGE(TAG, "Filter list failed: %d", rc);
        }
        params.filter_policy = BLE_HCI_SCAN_FILT_USE_WL;
    }
    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Scan start failed: %d", rc);
    }
}

static void on_sync(void) {
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    s_synced = true;
    apply_scan();
}

static void on_reset(int reason) {
    ESP_LOGW(TAG, "Stack reset, reason %d", reason);
    s_synced = false;
}

static void host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void yg_ble_start(yg_ble_advert_cb_t on_advert) {
    s_on_advert = on_advert;
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
}

void yg_ble_scan(yg_scan_mode_t mode, const yg_beacon_t *beacon, bool share_radio) {
    s_mode = mode;
    s_beacon = *beacon;
    s_share_radio = share_radio;
    apply_scan();
}

void yg_ble_ensure_scanning(void) {
    if (s_synced && s_mode != YG_SCAN_OFF && !ble_gap_disc_active()) {
        ESP_LOGW(TAG, "Scan was not running, restarting");
        apply_scan();
    }
}

bool yg_ble_is_scanning(void) {
    return s_synced && (s_mode == YG_SCAN_OFF || ble_gap_disc_active());
}

void yg_ble_count_begin(void) {
    s_count = 0;
    s_counting = true;
}

uint16_t yg_ble_count_end(void) {
    s_counting = false;
    return s_count;
}

bool yg_ble_counted_device(uint16_t index, yg_addr_t *addr, int8_t *rssi) {
    if (index >= s_count || index >= COUNT_MAX) {
        return false;
    }
    memcpy(addr->addr, s_counted[index], YG_ADDR_LEN);
    addr->type = s_counted_type[index];
    *rssi = s_counted_rssi[index];
    return true;
}
