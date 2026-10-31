#include <stdlib.h>

#include "fake_flash.h"
#include "yg_ring.h"
#include "yg_test.h"
#include "yg_trigger.h"

#define SECTORS 8

static yg_ring_flash_t flash;
static yg_ring_t ring;
static uint8_t sector_buf[YG_RING_SECTOR];

static void fresh(void) {
    fake_flash_init(&flash, SECTORS);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
}

static void append(uint32_t index) {
    yg_record_t r;
    yg_rec_time_anchor(&r, index / 10, index, index);
    yg_ring_append(&ring, &r);
}

/* Reads everything oldest first, returning the count and recording the first
 * and last index. Fails the test if indexes ever go backwards. */
static int read_all(uint32_t *first, uint32_t *last) {
    int count = 0;
    uint32_t used = 0;
    uint32_t sector, trip;
    while (yg_ring_sector_at(&ring, used, &sector, &trip)) used++;
    uint32_t prev = 0;
    for (uint32_t age = used; age-- > 0;) {
        CHECK(yg_ring_sector_at(&ring, age, &sector, &trip));
        CHECK_INT(yg_ring_read_sector(&ring, sector, sector_buf), 0);
        size_t pos = YG_RING_FIRST_RECORD;
        yg_record_t r;
        while (yg_record_next(sector_buf, YG_RING_SECTOR, &pos, &r)) {
            uint32_t index = r.uptime_ms;
            if (count == 0 && first) *first = index;
            CHECK(count == 0 || index > prev);
            prev = index;
            if (last) *last = index;
            count++;
        }
    }
    return count;
}

static uint32_t records_per_sector(void) {
    /* A time anchor has a 4 byte payload: 12 + 4 + 2 = 18, padded to 20. */
    return (YG_RING_SECTOR - YG_RING_HEADER) / 20;
}

static void empty_ring_reads_nothing(void) {
    fresh();
    uint32_t sector, trip;
    CHECK(!yg_ring_sector_at(&ring, 0, &sector, &trip));
    CHECK_INT(read_all(NULL, NULL), 0);
}

static void reads_back_in_order_across_sectors(void) {
    fresh();
    uint32_t n = records_per_sector() * 3 + 5;
    for (uint32_t i = 1; i <= n; i++) append(i);
    uint32_t first = 0, last = 0;
    CHECK_INT(read_all(&first, &last), n);
    CHECK_INT(first, 1);
    CHECK_INT(last, n);
}

static void sector_header_gives_the_first_trip(void) {
    fresh();
    uint32_t n = records_per_sector() * 2 + 5;
    for (uint32_t i = 1; i <= n; i++) append(i);
    uint32_t sector, trip;
    CHECK(yg_ring_sector_at(&ring, 2, &sector, &trip));
    CHECK_INT(trip, 0);
    CHECK(yg_ring_sector_at(&ring, 0, &sector, &trip));
    CHECK_INT(trip, (records_per_sector() * 2 + 1) / 10);
}

static void append_trip(uint32_t trip, uint32_t index) {
    yg_record_t r;
    yg_rec_time_anchor(&r, trip, index, index);
    yg_ring_append(&ring, &r);
}

static void finds_the_start_of_a_trip_that_spans_sectors(void) {
    fresh();
    uint32_t per = records_per_sector();
    uint32_t index = 1;
    for (uint32_t i = 0; i < per / 2; i++) append_trip(1, index++);
    for (uint32_t i = 0; i < per * 2; i++) append_trip(2, index++);   /* Starts mid sector. */
    for (uint32_t i = 0; i < 10; i++) append_trip(3, index++);

    uint32_t sector, trip;
    uint32_t age = yg_ring_age_for_trip(&ring, 2);
    CHECK(yg_ring_sector_at(&ring, age, &sector, &trip));
    CHECK_INT(trip, 1);
    CHECK_INT(yg_ring_read_sector(&ring, sector, sector_buf), 0);
    size_t pos = YG_RING_FIRST_RECORD;
    yg_record_t r;
    bool saw_start = false;
    while (yg_record_next(sector_buf, YG_RING_SECTOR, &pos, &r)) {
        if (r.trip == 2) saw_start = true;
    }
    CHECK(saw_start);

    /* The newest trip began inside the newest sector, or the one before it. */
    CHECK(yg_ring_sector_at(&ring, yg_ring_age_for_trip(&ring, 3), &sector, &trip));
    CHECK(trip <= 2);
    /* Older than anything held: start from the oldest. */
    CHECK_INT(yg_ring_age_for_trip(&ring, 0), 2);
}

static void reopening_never_loses_a_record(void) {
    /* Records of every length, written then found again after a power cycle. */
    fresh();
    for (uint32_t i = 1; i <= 3000; i++) {
        yg_record_t r;
        switch (i % 4) {
        case 0: yg_rec_press(&r, 1, i, YG_PRESS_AUTO, -85, i * 13, YG_SET_POINT_ANY); break;
        case 1: yg_rec_time_anchor(&r, 1, i, i * 31); break;
        case 2: yg_rec_first_heard(&r, 1, i, -90); break;
        default: yg_rec_hotspot(&r, 1, i, true, 1); break;
        }
        CHECK_INT(yg_ring_append(&ring, &r), 0);
        CHECK_INT(yg_ring_open(&ring, &flash), 0);
        uint32_t sector, trip;
        CHECK(yg_ring_sector_at(&ring, 0, &sector, &trip));
        CHECK_INT(yg_ring_read_sector(&ring, sector, sector_buf), 0);
        size_t pos = YG_RING_FIRST_RECORD;
        yg_record_t back;
        uint32_t last = 0;
        while (yg_record_next(sector_buf, YG_RING_SECTOR, &pos, &back)) {
            CHECK(last == 0 || back.uptime_ms == last + 1);
            last = back.uptime_ms;
        }
        CHECK_INT(last, i);
    }
}

/* One of each length that ends at a different place in a 4-byte word. */
static void record_of_kind(yg_record_t *r, int kind, uint32_t index, uint32_t value) {
    switch (kind) {
    case 0: yg_rec_time_anchor(r, value, index, value); break;
    case 1: yg_rec_first_heard(r, value, index, -90); break;
    case 2: yg_rec_hotspot(r, value, index, true, 1); break;
    default: yg_rec_press(r, value, index, YG_PRESS_AUTO, -85, value, YG_SET_POINT_ANY); break;
    }
}

static void a_cut_anywhere_in_a_record_loses_only_that_record(void) {
    for (int kind = 0; kind < 4; kind++) {
        for (uint32_t value = 1; value <= 300; value++) {
            yg_record_t before, torn, after;
            uint8_t encoded[YG_RECORD_MAX_ENCODED];
            record_of_kind(&before, kind, 1, value);
            record_of_kind(&torn, kind, 2, value);
            record_of_kind(&after, kind, 3, value);
            uint32_t size = (uint32_t)yg_record_encode(&torn, encoded);
            for (uint32_t cut = 0; cut < size; cut++) {
                fresh();
                yg_ring_append(&ring, &before);
                fake_flash_cut_after(cut);
                yg_ring_append(&ring, &torn);
                fake_flash_cut_after(FAKE_FLASH_NO_CUT);
                CHECK_INT(yg_ring_open(&ring, &flash), 0);
                yg_ring_append(&ring, &after);

                uint32_t sector, trip;
                CHECK(yg_ring_sector_at(&ring, 0, &sector, &trip));
                CHECK_INT(yg_ring_read_sector(&ring, sector, sector_buf), 0);
                size_t pos = YG_RING_FIRST_RECORD;
                yg_record_t back;
                bool saw_before = false, saw_after = false;
                while (yg_record_next(sector_buf, YG_RING_SECTOR, &pos, &back)) {
                    saw_before = saw_before || back.uptime_ms == 1;
                    saw_after = saw_after || back.uptime_ms == 3;
                }
                CHECK(saw_before);
                CHECK(saw_after);
            }
        }
    }
}

static void a_reader_walking_by_sequence_survives_a_moving_head(void) {
    fresh();
    uint32_t per = records_per_sector();
    uint32_t index = 1;
    for (uint32_t i = 0; i < per * 3; i++) append(index++);

    uint32_t seq;
    CHECK(yg_ring_oldest_seq(&ring, &seq));
    uint32_t expected = 1;
    uint32_t newest = 0;
    int rounds = 0;
    while (yg_ring_newest_seq(&ring, &newest) && seq <= newest && rounds++ < 20) {
        uint32_t sector, trip;
        CHECK(yg_ring_sector_for_seq(&ring, seq, &sector, &trip));
        CHECK_INT(yg_ring_read_sector(&ring, sector, sector_buf), 0);
        size_t pos = YG_RING_FIRST_RECORD;
        yg_record_t r;
        while (yg_record_next(sector_buf, YG_RING_SECTOR, &pos, &r)) {
            CHECK_INT(r.uptime_ms, expected);
            expected = r.uptime_ms + 1;
        }
        seq++;
        /* New records arrive while the reader works, moving the head on. */
        if (rounds == 1) {
            for (uint32_t i = 0; i < per * 2; i++) append(index++);
        }
    }
    CHECK_INT(expected, index);
}

static void sequence_lookup_refuses_what_is_gone(void) {
    fresh();
    uint32_t sector, trip, seq;
    CHECK(!yg_ring_oldest_seq(&ring, &seq));
    CHECK(!yg_ring_newest_seq(&ring, &seq));
    uint32_t per = records_per_sector();
    uint32_t index = 1;
    for (uint32_t i = 0; i < per * (SECTORS + 3); i++) append(index++);
    uint32_t oldest, newest;
    CHECK(yg_ring_oldest_seq(&ring, &oldest));
    CHECK(yg_ring_newest_seq(&ring, &newest));
    CHECK(oldest > 1);
    CHECK(yg_ring_sector_for_seq(&ring, oldest, &sector, &trip));
    CHECK(!yg_ring_sector_for_seq(&ring, oldest - 1, &sector, &trip));
    CHECK(!yg_ring_sector_for_seq(&ring, newest + 1, &sector, &trip));
}

static void wrapping_drops_the_oldest_and_keeps_order(void) {
    fresh();
    uint32_t n = records_per_sector() * SECTORS * 5 + 17;
    for (uint32_t i = 1; i <= n; i++) append(i);
    uint32_t first = 0, last = 0;
    int count = read_all(&first, &last);
    CHECK_INT(last, n);
    CHECK(first > n - records_per_sector() * SECTORS);
    CHECK(first < n);
    CHECK_INT(count, last - first + 1);
    CHECK((uint32_t)count >= records_per_sector() * (SECTORS - 1));
}

static void reopening_continues_where_it_stopped(void) {
    fresh();
    for (uint32_t i = 1; i <= 100; i++) append(i);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    for (uint32_t i = 101; i <= 700; i++) append(i);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    uint32_t first = 0, last = 0;
    CHECK_INT(read_all(&first, &last), 700);
    CHECK_INT(first, 1);
    CHECK_INT(last, 700);
}

static void reopening_after_wrap_finds_the_newest(void) {
    fresh();
    uint32_t n = records_per_sector() * SECTORS * 2 + 33;
    for (uint32_t i = 1; i <= n; i++) append(i);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    append(n + 1);
    uint32_t first = 0, last = 0;
    read_all(&first, &last);
    CHECK_INT(last, n + 1);
}

static void torn_record_is_skipped_and_later_records_survive(void) {
    fresh();
    for (uint32_t i = 1; i <= 10; i++) append(i);
    fake_flash_cut_after(9);
    append(11);                 /* Power is cut during this record. */
    fake_flash_cut_after(FAKE_FLASH_NO_CUT);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    append(12);
    append(13);
    uint32_t first = 0, last = 0;
    CHECK_INT(read_all(&first, &last), 12);
    CHECK_INT(last, 13);
}

static void torn_sector_header_does_not_break_open_or_read(void) {
    fresh();
    uint32_t n = records_per_sector();
    for (uint32_t i = 1; i <= n; i++) append(i);
    fake_flash_cut_after(7);
    append(n + 1);              /* Needs a new sector: the cut lands in its header. */
    fake_flash_cut_after(FAKE_FLASH_NO_CUT);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    uint32_t first = 0, last = 0;
    CHECK_INT(read_all(&first, &last), n);
    append(n + 2);
    read_all(&first, &last);
    CHECK_INT(last, n + 2);
}

static void half_erased_sector_does_not_break_open_or_read(void) {
    fresh();
    uint32_t n = records_per_sector() * SECTORS;
    for (uint32_t i = 1; i <= n; i++) append(i);
    fake_flash_cut_erase_after(1000);
    append(n + 1);              /* Wrapping: power is cut during the erase. */
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    uint32_t first = 0, last = 0;
    read_all(&first, &last);
    CHECK_INT(last, n);
    append(n + 2);
    read_all(&first, &last);
    CHECK_INT(last, n + 2);
}

static void clear_empties_the_ring(void) {
    fresh();
    for (uint32_t i = 1; i <= 500; i++) append(i);
    CHECK_INT(yg_ring_clear(&ring), 0);
    CHECK_INT(read_all(NULL, NULL), 0);
    append(1000);
    uint32_t first = 0, last = 0;
    CHECK_INT(read_all(&first, &last), 1);
    CHECK_INT(first, 1000);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    CHECK_INT(read_all(&first, &last), 1);
}

static void clearing_one_sector_at_a_time_empties_the_ring(void) {
    fresh();
    for (uint32_t i = 1; i <= 500; i++) append(i);
    int erased = 0;
    for (uint32_t s = 0; s < SECTORS; s++) {
        int result = yg_ring_clear_sector(&ring, s);
        CHECK(result >= 0);
        erased += result;
    }
    CHECK(erased >= 1);
    CHECK_INT(yg_ring_clear_sector(&ring, 0), 0);
    yg_ring_reset(&ring);
    CHECK_INT(read_all(NULL, NULL), 0);
    append(1000);
    CHECK_INT(yg_ring_open(&ring, &flash), 0);
    uint32_t first = 0, last = 0;
    CHECK_INT(read_all(&first, &last), 1);
    CHECK_INT(first, 1000);
}

static void flash_is_not_erased_more_than_needed(void) {
    fresh();
    uint32_t n = records_per_sector() * SECTORS;
    for (uint32_t i = 1; i <= n; i++) append(i);
    CHECK_INT(fake_flash_erase_count(), 0);
    append(n + 1);
    CHECK_INT(fake_flash_erase_count(), 1);
}

static void failed_write_reports_an_error(void) {
    fresh();
    fake_flash_cut_after(0);
    yg_record_t r;
    yg_rec_time_anchor(&r, 0, 1, 1);
    CHECK(yg_ring_append(&ring, &r) != 0);
    fake_flash_cut_after(FAKE_FLASH_NO_CUT);
}

int main(void) {
    RUN(empty_ring_reads_nothing);
    RUN(reads_back_in_order_across_sectors);
    RUN(sector_header_gives_the_first_trip);
    RUN(finds_the_start_of_a_trip_that_spans_sectors);
    RUN(reopening_never_loses_a_record);
    RUN(a_cut_anywhere_in_a_record_loses_only_that_record);
    RUN(a_reader_walking_by_sequence_survives_a_moving_head);
    RUN(sequence_lookup_refuses_what_is_gone);
    RUN(wrapping_drops_the_oldest_and_keeps_order);
    RUN(reopening_continues_where_it_stopped);
    RUN(reopening_after_wrap_finds_the_newest);
    RUN(torn_record_is_skipped_and_later_records_survive);
    RUN(torn_sector_header_does_not_break_open_or_read);
    RUN(half_erased_sector_does_not_break_open_or_read);
    RUN(clear_empties_the_ring);
    RUN(clearing_one_sector_at_a_time_empties_the_ring);
    RUN(flash_is_not_erased_more_than_needed);
    RUN(failed_write_reports_an_error);
    return TEST_RESULT();
}
