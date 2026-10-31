#include "yg_json.h"

#include <stdarg.h>
#include <stdio.h>

void yg_jw_init(yg_jw_t *w, char *buf, size_t len) {
    w->buf = buf;
    w->len = len;
    w->pos = 0;
    w->overflow = false;
    if (len > 0) {
        buf[0] = 0;
    }
}

void yg_jw_raw(yg_jw_t *w, const char *fmt, ...) {
    if (w->overflow) {
        return;
    }
    size_t room = w->len - w->pos;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(w->buf + w->pos, room, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= room) {
        w->overflow = true;
        return;
    }
    w->pos += (size_t)n;
}

void yg_jw_string(yg_jw_t *w, const char *text) {
    yg_jw_raw(w, "\"");
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            yg_jw_raw(w, "\\%c", c);
        } else if (c < 0x20) {
            yg_jw_raw(w, "\\u%04x", c);
        } else {
            yg_jw_raw(w, "%c", c);
        }
    }
    yg_jw_raw(w, "\"");
}

void yg_jw_tenths(yg_jw_t *w, int tenths) {
    int magnitude = tenths < 0 ? -tenths : tenths;
    yg_jw_raw(w, "%s%d.%d", tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
}

int yg_jw_finish(yg_jw_t *w) {
    return w->overflow ? -1 : (int)w->pos;
}
