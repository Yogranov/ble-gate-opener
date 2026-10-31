#include "yg_hotspot_policy.h"

#include <string.h>

#define CLIENT_GRACE_MS   30000u
#define REQUEST_DEFAULT_S 300u    /* Time limit for a requested hotspot when hotspot_s is 0. */

static uint32_t time_limit_s(const yg_hotspot_policy_t *h, const yg_settings_t *s) {
    if (h->manual && s->hotspot_s == 0) {
        return REQUEST_DEFAULT_S;
    }
    return s->hotspot_s;
}

void yg_hotspot_init(yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now, bool force_on) {
    memset(h, 0, sizeof(*h));
    h->manual = force_on;
    h->on = force_on || s->hotspot_s > 0;
    h->started = now;
}

void yg_hotspot_on_motion(yg_hotspot_policy_t *h) {
    h->motion_seen = true;
}

void yg_hotspot_set_clients(yg_hotspot_policy_t *h, uint32_t now, uint8_t clients) {
    if (clients > 0) {
        h->had_client = true;
    }
    if (clients > 0 || h->clients > 0) {
        h->last_client = now;
    }
    h->clients = clients;
}

void yg_hotspot_request(yg_hotspot_policy_t *h, uint32_t now) {
    h->on = true;
    h->manual = true;
    h->started = now;
}

bool yg_hotspot_tick(yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now) {
    if (!h->on) {
        return false;
    }
    if (h->clients > 0) {
        h->last_client = now;
        return true;
    }
    bool time_up = now - h->started >= time_limit_s(h, s) * 1000u;
    bool moved = h->motion_seen && !h->manual && !s->debug;
    if (!time_up && !moved) {
        return true;
    }
    if (h->had_client && now - h->last_client < CLIENT_GRACE_MS) {
        return true;
    }
    h->on = false;
    return false;
}

uint32_t yg_hotspot_left_s(const yg_hotspot_policy_t *h, const yg_settings_t *s, uint32_t now) {
    if (!h->on) {
        return 0;
    }
    uint32_t limit = time_limit_s(h, s);
    uint32_t elapsed = (now - h->started) / 1000u;
    return elapsed >= limit ? 0 : limit - elapsed;
}
