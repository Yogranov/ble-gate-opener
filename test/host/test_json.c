#include "yg_json.h"
#include "yg_test.h"

static void shallow_json_is_fine(void) {
    CHECK(yg_json_depth_ok("{}", 8));
    CHECK(yg_json_depth_ok("{\"a\":[1,2,{\"b\":[3]}]}", 8));
    CHECK(yg_json_depth_ok("", 8));
    CHECK(yg_json_depth_ok("[[[[[[[[1]]]]]]]]", 8));
}

static void deep_json_is_refused(void) {
    CHECK(!yg_json_depth_ok("[[[[[[[[[1]]]]]]]]]", 8));
    CHECK(!yg_json_depth_ok("{\"a\":{\"b\":{\"c\":{\"d\":{\"e\":{\"f\":{\"g\":{\"h\":{}}}}}}}}}", 8));
    static char deep[1025];
    memset(deep, '[', 1024);
    CHECK(!yg_json_depth_ok(deep, 8));
}

static void brackets_inside_strings_do_not_count(void) {
    CHECK(yg_json_depth_ok("{\"hotspot_password\":\"[[[[[[[[[[[[\"}", 8));
    CHECK(yg_json_depth_ok("{\"p\":\"a\\\"[[[[[[[[[[\"}", 8));
}

static void a_lone_close_cannot_hide_depth(void) {
    CHECK(!yg_json_depth_ok("]]]][[[[[[[[[", 8));
}

static void strings_are_escaped(void) {
    char buf[64];
    yg_jw_t w;
    yg_jw_init(&w, buf, sizeof(buf));
    yg_jw_string(&w, "a\"b\\c\n");
    CHECK(yg_jw_finish(&w) > 0);
    CHECK_STR(buf, "\"a\\\"b\\\\c\\u000a\"");
}

int main(void) {
    RUN(shallow_json_is_fine);
    RUN(deep_json_is_refused);
    RUN(brackets_inside_strings_do_not_count);
    RUN(a_lone_close_cannot_hide_depth);
    RUN(strings_are_escaped);
    return TEST_RESULT();
}
