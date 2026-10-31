#include <stdlib.h>

#include "fake_flash.h"
#include "yg_ring.h"
#include "yg_test.h"

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
    RUN(wrapping_drops_the_oldest_and_keeps_order);
    RUN(reopening_continues_where_it_stopped);
    RUN(reopening_after_wrap_finds_the_newest);
    RUN(torn_record_is_skipped_and_later_records_survive);
    RUN(torn_sector_header_does_not_break_open_or_read);
    RUN(half_erased_sector_does_not_break_open_or_read);
    RUN(clear_empties_the_ring);
    RUN(flash_is_not_erased_more_than_needed);
    RUN(failed_write_reports_an_error);
    return TEST_RESULT();
}
