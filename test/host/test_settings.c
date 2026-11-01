#include "yg_settings.h"
#include "yg_test.h"

static yg_settings_t s;

static void defaults_match_spec(void) {
    yg_settings_defaults(&s);
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);
    CHECK_INT(s.press_ms, 2000);
    CHECK_INT(s.moved_window_s, 60);
    CHECK_INT(s.gone_s, 60);
    CHECK_INT(s.still_s, 120);
    CHECK_INT(s.fallback_dbm, -90);
    CHECK(s.motion_enabled);
    CHECK_INT(s.motion_accel_mg, 40);
    CHECK_INT(s.motion_accel_ms, 800);
    CHECK_INT(s.motion_yaw_deg, 15);
    CHECK_INT(s.motion_yaw_window_ms, 3000);
    CHECK_INT(s.pair_min_dbm, -45);
    CHECK_INT(s.pair_margin_db, 15);
    CHECK_INT(s.pair_hold_s, 2);
    CHECK_INT(s.pair_timeout_s, 60);
    CHECK_INT(s.hotspot_s, 0);
    CHECK_INT(s.wifi_tx_dbm, 8);
    CHECK(s.led_enabled);
    CHECK(s.buzzer_enabled);
    CHECK_INT(s.buzzer_type, YG_BUZZER_ACTIVE);
    CHECK(strcmp(s.hotspot_password, "password") == 0);
    CHECK(!s.debug);
}

/* Reset to defaults logs each setting it changes in this form, and a log replay reads it back. */
static void text_form_reads_back_as_the_same_value(void) {
    yg_settings_t changed;
    yg_settings_defaults(&s);
    s.set_point_dbm = -77;
    s.press_ms = 1500;
    s.fallback_dbm = -80;
    s.motion_enabled = false;
    s.pair_hold_s = 4;
    s.buzzer_type = YG_BUZZER_PASSIVE;
    strcpy(s.hotspot_ssid, "Unit A");
    strcpy(s.hotspot_password, "secret123");
    size_t count = 0;
    const yg_setting_desc_t *table = yg_settings_table(&count);
    yg_settings_defaults(&changed);
    for (size_t i = 0; i < count; i++) {
        char text[YG_PASSWORD_MAX + 1];
        CHECK(yg_settings_format(&s, &table[i], text, sizeof(text)) > 0);
        CHECK(yg_settings_set_str(&changed, table[i].key, text));
    }
    CHECK(memcmp(&changed, &s, sizeof(s)) == 0);

    char text[8];
    s.set_point_dbm = YG_SET_POINT_ANY;
    CHECK(yg_settings_format(&s, yg_settings_find("set_point_dbm"), text, sizeof(text)) > 0);
    CHECK(strcmp(text, "any") == 0);
}

static void numeric_ranges_are_enforced(void) {
    struct { const char *key; int min, max; } ranges[] = {
        { "press_ms", 200, 5000 },
        { "moved_window_s", 10, 300 },
        { "gone_s", 10, 600 },
        { "still_s", 30, 1800 },
        { "fallback_dbm", -100, -30 },
        { "motion_accel_mg", 10, 300 },
        { "motion_accel_ms", 200, 3000 },
        { "motion_yaw_deg", 5, 90 },
        { "motion_yaw_window_ms", 1000, 10000 },
        { "pair_min_dbm", -80, -20 },
        { "pair_margin_db", 3, 40 },
        { "pair_hold_s", 1, 10 },
        { "pair_timeout_s", 10, 300 },
        { "hotspot_s", 0, 3600 },
        { "wifi_tx_dbm", 2, 20 },
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        yg_settings_defaults(&s);
        const yg_setting_desc_t *d = yg_settings_find(ranges[i].key);
        CHECK(d != NULL);
        if (d == NULL) continue;
        int32_t before = yg_settings_get_int(&s, d);
        CHECK(!yg_settings_set_int(&s, ranges[i].key, ranges[i].min - 1));
        CHECK(!yg_settings_set_int(&s, ranges[i].key, ranges[i].max + 1));
        CHECK_INT(yg_settings_get_int(&s, d), before);
        CHECK(yg_settings_set_int(&s, ranges[i].key, ranges[i].min));
        CHECK_INT(yg_settings_get_int(&s, d), ranges[i].min);
        CHECK(yg_settings_set_int(&s, ranges[i].key, ranges[i].max));
        CHECK_INT(yg_settings_get_int(&s, d), ranges[i].max);
    }
}

static void set_point_accepts_any_or_range(void) {
    yg_settings_defaults(&s);
    CHECK(yg_settings_set_int(&s, "set_point_dbm", -85));
    CHECK_INT(s.set_point_dbm, -85);
    CHECK(yg_settings_set_int(&s, "set_point_dbm", YG_SET_POINT_ANY));
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);
    CHECK(yg_settings_set_str(&s, "set_point_dbm", "-70"));
    CHECK_INT(s.set_point_dbm, -70);
    CHECK(yg_settings_set_str(&s, "set_point_dbm", "any"));
    CHECK_INT(s.set_point_dbm, YG_SET_POINT_ANY);
    CHECK(!yg_settings_set_int(&s, "set_point_dbm", -101));
    CHECK(!yg_settings_set_int(&s, "set_point_dbm", -29));
    CHECK(!yg_settings_set_int(&s, "set_point_dbm", 5));
}

static void booleans_and_enums(void) {
    yg_settings_defaults(&s);
    CHECK(yg_settings_set_int(&s, "buzzer_enabled", 1));
    CHECK(s.buzzer_enabled);
    CHECK(yg_settings_set_int(&s, "buzzer_enabled", 0));
    CHECK(!s.buzzer_enabled);
    CHECK(!yg_settings_set_int(&s, "debug", 2));
    CHECK(yg_settings_set_str(&s, "buzzer_type", "active"));
    CHECK_INT(s.buzzer_type, YG_BUZZER_ACTIVE);
    CHECK(yg_settings_set_str(&s, "buzzer_type", "passive"));
    CHECK_INT(s.buzzer_type, YG_BUZZER_PASSIVE);
    CHECK(!yg_settings_set_str(&s, "buzzer_type", "loud"));
}

static void strings_are_length_checked(void) {
    yg_settings_defaults(&s);
    CHECK(!yg_settings_set_str(&s, "hotspot_password", "1234567"));
    CHECK(yg_settings_set_str(&s, "hotspot_password", "12345678"));
    CHECK_STR(s.hotspot_password, "12345678");
    char long_pass[65];
    memset(long_pass, 'a', 64);
    long_pass[64] = 0;
    CHECK(!yg_settings_set_str(&s, "hotspot_password", long_pass));
    long_pass[63] = 0;
    CHECK(yg_settings_set_str(&s, "hotspot_password", long_pass));
    CHECK_INT(strlen(s.hotspot_password), 63);

    CHECK(!yg_settings_set_str(&s, "hotspot_ssid", ""));
    CHECK(yg_settings_set_str(&s, "hotspot_ssid", "YogiGate-1234"));
    CHECK_STR(s.hotspot_ssid, "YogiGate-1234");
    CHECK(!yg_settings_set_str(&s, "hotspot_ssid", "123456789012345678901234567890123"));
}

static void unknown_keys_are_rejected(void) {
    yg_settings_defaults(&s);
    CHECK(yg_settings_find("nope") == NULL);
    CHECK(!yg_settings_set_int(&s, "nope", 1));
    CHECK(!yg_settings_set_str(&s, "nope", "x"));
    CHECK(!yg_settings_set_int(&s, "hotspot_ssid", 1));
    CHECK(!yg_settings_set_str(&s, "press_ms", "abc"));
}

static void nvs_keys_are_short_and_unique(void) {
    size_t count = 0;
    const yg_setting_desc_t *t = yg_settings_table(&count);
    CHECK(count >= 23);
    for (size_t i = 0; i < count; i++) {
        CHECK(strlen(t[i].nvs) > 0);
        CHECK(strlen(t[i].nvs) <= 15);
        for (size_t j = i + 1; j < count; j++) {
            CHECK(strcmp(t[i].nvs, t[j].nvs) != 0);
            CHECK(strcmp(t[i].key, t[j].key) != 0);
        }
    }
}

static void json_describes_every_setting(void) {
    static char buf[8192];
    yg_settings_defaults(&s);
    yg_settings_set_str(&s, "hotspot_password", "supersecret1");
    yg_settings_set_str(&s, "hotspot_ssid", "Unit \"A\"");
    int n = yg_settings_to_json(&s, buf, sizeof(buf), false);
    CHECK(n > 0);
    CHECK_HAS(buf, "{\"settings\":[");
    CHECK_HAS(buf, "\"key\":\"press_ms\",\"type\":\"int\",\"value\":2000,\"min\":200,\"max\":5000");
    CHECK_HAS(buf, "\"key\":\"set_point_dbm\",\"type\":\"dbm_or_any\",\"value\":\"any\",\"min\":-100,\"max\":-30");
    CHECK_HAS(buf, "\"key\":\"buzzer_enabled\",\"type\":\"bool\",\"value\":true");
    CHECK_HAS(buf, "\"key\":\"buzzer_type\",\"type\":\"enum\",\"value\":\"active\",\"options\":[\"passive\",\"active\"]");
    CHECK_HAS(buf, "\"key\":\"hotspot_password\",\"type\":\"password\"");
    CHECK_LACKS(buf, "supersecret1");
    CHECK_HAS(buf, "Unit \\\"A\\\"");

    n = yg_settings_to_json(&s, buf, sizeof(buf), true);
    CHECK(n > 0);
    CHECK_HAS(buf, "supersecret1");

    CHECK_INT(yg_settings_to_json(&s, buf, 16, false), -1);
}

int main(void) {
    RUN(defaults_match_spec);
    RUN(text_form_reads_back_as_the_same_value);
    RUN(numeric_ranges_are_enforced);
    RUN(set_point_accepts_any_or_range);
    RUN(booleans_and_enums);
    RUN(strings_are_length_checked);
    RUN(unknown_keys_are_rejected);
    RUN(nvs_keys_are_short_and_unique);
    RUN(json_describes_every_setting);
    return TEST_RESULT();
}
