#include "yg_pairing.h"

#include <string.h>

#include "yg_json.h"

#define NO_LEADER    (-1)
#define FRESH_MS     3000u    /* A candidate heard this recently competes for the lead. */
#define LISTED_MS    10000u   /* A candidate heard this recently appears in the list. */
#define LEVEL_ALPHA  0.5f

void yg_pairing_init(yg_pairing_t *p) {
    memset(p, 0, sizeof(*p));
    p->leader = NO_LEADER;
}

void yg_pairing_start(yg_pairing_t *p, yg_pair_mode_t mode, uint32_t now) {
    yg_pairing_init(p);
    p->mode = mode;
    p->started = now;
}

void yg_pairing_cancel(yg_pairing_t *p) {
    p->mode = YG_PAIR_IDLE;
}

void yg_pairing_on_advert(yg_pairing_t *p, uint32_t now, const yg_addr_t *a, int8_t rssi, const yg_ibeacon_t *ib) {
    if (p->mode == YG_PAIR_IDLE || ib == NULL || !ib->valid || !yg_addr_is_stable(a)) {
        return;
    }
    int slot = -1;
    int oldest = 0;
    for (int i = 0; i < YG_PAIR_MAX_CANDIDATES; i++) {
        if (!p->c[i].used) {
            if (slot < 0) slot = i;
        } else if (yg_addr_equal(&p->c[i].addr, a)) {
            p->c[i].level += LEVEL_ALPHA * ((float)rssi - p->c[i].level);
            p->c[i].last_seen = now;
            p->c[i].count++;
            p->c[i].ib = *ib;
            return;
        } else if ((int32_t)(p->c[i].last_seen - p->c[oldest].last_seen) < 0) {
            oldest = i;
        }
    }
    if (slot < 0) {
        slot = oldest;
        if (p->leader == slot) {
            p->leader = NO_LEADER;
        }
    }
    yg_pair_candidate_t *c = &p->c[slot];
    c->used = true;
    c->addr = *a;
    c->level = (float)rssi;
    c->last_seen = now;
    c->count = 1;
    c->ib = *ib;
}

yg_pair_result_t yg_pairing_tick(yg_pairing_t *p, const yg_settings_t *s, uint32_t now, yg_addr_t *winner) {
    if (p->mode == YG_PAIR_IDLE) {
        return YG_PAIR_RESULT_NONE;
    }
    if (p->mode == YG_PAIR_AUTO) {
        int best = NO_LEADER;
        float second = -1000.0f;
        for (int i = 0; i < YG_PAIR_MAX_CANDIDATES; i++) {
            if (!p->c[i].used || now - p->c[i].last_seen > FRESH_MS) {
                continue;
            }
            if (best == NO_LEADER || p->c[i].level > p->c[best].level) {
                if (best != NO_LEADER) second = p->c[best].level;
                best = i;
            } else if (p->c[i].level > second) {
                second = p->c[i].level;
            }
        }
        bool qualified = best != NO_LEADER && p->c[best].level >= (float)s->pair_min_dbm
                         && p->c[best].level - second >= (float)s->pair_margin_db;
        if (!qualified) {
            p->leader = NO_LEADER;
        } else if (p->leader != best) {
            p->leader = (int8_t)best;
            p->leader_since = now;
        } else if (now - p->leader_since >= (uint32_t)s->pair_hold_s * 1000u && p->c[best].count >= 2) {
            *winner = p->c[best].addr;
            p->mode = YG_PAIR_IDLE;
            return YG_PAIR_RESULT_SAVED;
        }
    }
    if (now - p->started >= (uint32_t)s->pair_timeout_s * 1000u) {
        p->mode = YG_PAIR_IDLE;
        return YG_PAIR_RESULT_TIMEOUT;
    }
    return YG_PAIR_RESULT_NONE;
}

int yg_pairing_candidates_json(const yg_pairing_t *p, const yg_settings_t *s, uint32_t now, char *buf, size_t len) {
    static const char *const MODES[] = { "idle", "auto", "list" };
    uint32_t elapsed = (now - p->started) / 1000u;
    uint32_t left = p->mode == YG_PAIR_IDLE || elapsed >= s->pair_timeout_s ? 0 : s->pair_timeout_s - elapsed;

    yg_jw_t w;
    yg_jw_init(&w, buf, len);
    yg_jw_raw(&w, "{\"mode\":\"%s\",\"left_s\":%u,\"candidates\":[", MODES[p->mode], (unsigned)left);

    bool done[YG_PAIR_MAX_CANDIDATES] = { 0 };
    bool first = true;
    for (;;) {
        int best = -1;
        for (int i = 0; i < YG_PAIR_MAX_CANDIDATES; i++) {
            if (done[i] || !p->c[i].used || now - p->c[i].last_seen > LISTED_MS) {
                continue;
            }
            if (best < 0 || p->c[i].level > p->c[best].level) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        done[best] = true;
        const yg_pair_candidate_t *c = &p->c[best];
        char mac[18];
        char uuid[37];
        yg_addr_format(c->addr.addr, mac);
        yg_ibeacon_uuid_str(&c->ib, uuid);
        yg_jw_raw(&w, "%s{\"mac\":\"%s\",\"type\":%u,\"rssi\":%d,\"uuid\":\"%s\",\"major\":%u,\"minor\":%u,\"age_ms\":%u}",
                  first ? "" : ",", mac, (unsigned)c->addr.type, (int)(c->level - 0.5f), uuid,
                  (unsigned)c->ib.major, (unsigned)c->ib.minor, (unsigned)(now - c->last_seen));
        first = false;
    }
    yg_jw_raw(&w, "]}");
    return yg_jw_finish(&w);
}
