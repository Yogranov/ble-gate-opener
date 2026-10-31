#include "yg_log.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "yg_ring.h"
#include "yg_trips.h"

#define MAX_LIST 20

static const char *TAG = "yg_log";

static yg_ring_t s_trips;
static yg_ring_t s_captures;
static bool s_trips_ok;
static bool s_captures_ok;
static SemaphoreHandle_t s_lock;

static int flash_read(void *ctx, uint32_t off, void *buf, uint32_t len) {
    return esp_partition_read(ctx, off, buf, len) == ESP_OK ? 0 : -1;
}

static int flash_write(void *ctx, uint32_t off, const void *buf, uint32_t len) {
    return esp_partition_write(ctx, off, buf, len) == ESP_OK ? 0 : -1;
}

static int flash_erase(void *ctx, uint32_t off) {
    return esp_partition_erase_range(ctx, off, YG_RING_SECTOR) == ESP_OK ? 0 : -1;
}

static bool open_ring(yg_ring_t *ring, const char *label) {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    if (part == NULL) {
        ESP_LOGE(TAG, "No %s partition", label);
        return false;
    }
    const yg_ring_flash_t flash = {
        .read = flash_read,
        .write = flash_write,
        .erase = flash_erase,
        .ctx = (void *)part,
        .size = part->size,
    };
    return yg_ring_open(ring, &flash) == 0;
}

void yg_log_init(void) {
    s_lock = xSemaphoreCreateMutex();
    s_trips_ok = open_ring(&s_trips, "triplog");
    s_captures_ok = open_ring(&s_captures, "capture");
}

static void append(yg_ring_t *ring, bool ok, const yg_record_t *r) {
    if (!ok) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (yg_ring_append(ring, r) != 0) {
        ESP_LOGW(TAG, "Log write failed");
    }
    xSemaphoreGive(s_lock);
}

void yg_log_trip(const yg_record_t *r) {
    append(&s_trips, s_trips_ok, r);
}

void yg_log_capture(const yg_record_t *r) {
    append(&s_captures, s_captures_ok, r);
}

static uint32_t sectors_used(const yg_ring_t *ring) {
    uint32_t used = 0;
    uint32_t sector, trip;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (yg_ring_sector_at(ring, used, &sector, &trip)) {
        used++;
    }
    xSemaphoreGive(s_lock);
    return used;
}

/* Reads the sector `age` steps back from the newest. */
static bool read_at_age(const yg_ring_t *ring, uint32_t age, uint8_t *buf) {
    uint32_t sector, trip;
    bool ok = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (yg_ring_sector_at(ring, age, &sector, &trip)) {
        ok = yg_ring_read_sector(ring, sector, buf) == 0;
    }
    xSemaphoreGive(s_lock);
    return ok;
}

typedef struct
{
    uint8_t *sector;
    char *json;
    yg_record_t *record;
} work_t;

static bool work_alloc(work_t *w) {
    w->sector = malloc(YG_RING_SECTOR);
    w->json = malloc(YG_RECORD_JSON_MAX);
    w->record = malloc(sizeof(yg_record_t));
    return w->sector != NULL && w->json != NULL && w->record != NULL;
}

static void work_free(work_t *w) {
    free(w->sector);
    free(w->json);
    free(w->record);
}

/* Sends a ring's records as JSON, oldest first. Returns false if told to stop. */
static bool send_ring(const yg_ring_t *ring, work_t *w, bool only_trip, uint32_t trip, uint32_t from_age, yg_log_line_cb_t cb, void *ctx) {
    for (uint32_t age = from_age + 1; age-- > 0;) {
        if (!read_at_age(ring, age, w->sector)) {
            continue;
        }
        size_t pos = YG_RING_FIRST_RECORD;
        while (yg_record_next(w->sector, YG_RING_SECTOR, &pos, w->record)) {
            if (only_trip && w->record->trip != trip) {
                continue;
            }
            int n = yg_record_to_json(w->record, w->json, YG_RECORD_JSON_MAX);
            if (n > 0 && !cb(w->json, (size_t)n, ctx)) {
                return false;
            }
        }
    }
    return true;
}

int yg_log_export(bool captures, yg_log_line_cb_t cb, void *ctx) {
    work_t w;
    if (!work_alloc(&w)) {
        work_free(&w);
        return -1;
    }
    bool more = true;
    uint32_t used = sectors_used(&s_trips);
    if (used > 0) {
        more = send_ring(&s_trips, &w, false, 0, used - 1, cb, ctx);
    }
    if (more && captures) {
        used = sectors_used(&s_captures);
        if (used > 0) {
            send_ring(&s_captures, &w, false, 0, used - 1, cb, ctx);
        }
    }
    work_free(&w);
    return 0;
}

/* Finds the newest sector that started at or before `trip`, or the oldest one. */
static uint32_t age_for_trip(uint32_t trip) {
    uint32_t age = 0;
    uint32_t sector, first_trip;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    while (yg_ring_sector_at(&s_trips, age, &sector, &first_trip) && first_trip > trip) {
        uint32_t next_sector, next_first;
        if (!yg_ring_sector_at(&s_trips, age + 1, &next_sector, &next_first)) {
            break;
        }
        age++;
    }
    xSemaphoreGive(s_lock);
    return age;
}

int yg_log_trip_records(uint32_t trip, yg_log_line_cb_t cb, void *ctx) {
    work_t w;
    if (!work_alloc(&w)) {
        work_free(&w);
        return -1;
    }
    if (sectors_used(&s_trips) > 0) {
        send_ring(&s_trips, &w, true, trip, age_for_trip(trip), cb, ctx);
    }
    work_free(&w);
    return 0;
}

int yg_log_trip_list(uint32_t before, uint32_t limit, yg_log_line_cb_t cb, void *ctx) {
    if (limit == 0 || limit > MAX_LIST) {
        limit = MAX_LIST;
    }
    if (before == 0 || sectors_used(&s_trips) == 0) {
        return 0;
    }
    uint32_t high = before - 1;
    uint32_t low = high >= limit ? high - limit + 1 : 1;
    uint32_t slots = high >= low ? high - low + 1 : 0;
    if (slots == 0) {
        return 0;
    }
    work_t w;
    yg_trip_summary_t *trips = calloc(slots, sizeof(yg_trip_summary_t));
    if (!work_alloc(&w) || trips == NULL) {
        work_free(&w);
        free(trips);
        return -1;
    }
    for (uint32_t i = 0; i < slots; i++) {
        yg_trip_summary_init(&trips[i], low + i);
    }
    /* Read on to the newest sector, because ratings are stored in later trips. */
    for (uint32_t age = age_for_trip(low) + 1; age-- > 0;) {
        if (!read_at_age(&s_trips, age, w.sector)) {
            continue;
        }
        size_t pos = YG_RING_FIRST_RECORD;
        while (yg_record_next(w.sector, YG_RING_SECTOR, &pos, w.record)) {
            const yg_record_t *r = w.record;
            if (r->type == YG_REC_RATING) {
                uint32_t rated = yg_get_u32(r->payload);
                if (rated >= low && rated <= high) {
                    yg_trip_summary_add(&trips[rated - low], r);
                }
            }
            if (r->trip >= low && r->trip <= high) {
                yg_trip_summary_add(&trips[r->trip - low], r);
            }
        }
    }
    for (uint32_t i = slots; i-- > 0;) {
        if (!trips[i].seen) {
            continue;
        }
        int n = yg_trip_summary_to_json(&trips[i], w.json, YG_RECORD_JSON_MAX);
        if (n > 0 && !cb(w.json, (size_t)n, ctx)) {
            break;
        }
    }
    work_free(&w);
    free(trips);
    return 0;
}

int yg_log_clear(void) {
    int result = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_trips_ok && yg_ring_clear(&s_trips) != 0) {
        result = -1;
    }
    if (s_captures_ok && yg_ring_clear(&s_captures) != 0) {
        result = -1;
    }
    xSemaphoreGive(s_lock);
    return result;
}

void yg_log_usage(uint32_t *trip_used, uint32_t *trip_total, uint32_t *capture_used, uint32_t *capture_total) {
    *trip_used = s_trips_ok ? sectors_used(&s_trips) : 0;
    *trip_total = s_trips.sectors;
    *capture_used = s_captures_ok ? sectors_used(&s_captures) : 0;
    *capture_total = s_captures.sectors;
}
