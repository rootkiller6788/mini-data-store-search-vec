#include "disk_manager.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int passed = 0, failed = 0;

#define TEST(n) { bool ok = true; do
#define END(n) while(0); if (ok) { printf("  PASS %s\n", n); passed++; } \
    else { printf("  FAIL %s\n", n); failed++; } }
#define CHK(c, m) if (!(c)) { printf("    ! %s\n", m); ok = false; }

int main(void) {
    printf("=== Disk Manager Tests ===\n");

    TEST("init") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        CHK(dm.num_pages == 0, "initial pages should be 0");
        CHK(strcmp(dm.db_name, "test.db") == 0, "db name should match");
    } END("init");

    TEST("allocate pages") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        int32_t p0 = dm_allocate_page(&dm);
        int32_t p1 = dm_allocate_page(&dm);
        CHK(p0 == 0, "first page id should be 0");
        CHK(p1 == 1, "second page id should be 1");
        CHK(dm.num_pages == 2, "num_pages should be 2");
        CHK(dm.allocated[0] && dm.allocated[1], "pages should be allocated");
        dm_destroy(&dm);
    } END("allocate pages");

    TEST("read/write page") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        int32_t pid = dm_allocate_page(&dm);
        uint8_t write_buf[DM_PAGE_SIZE];
        memset(write_buf, 0xAB, DM_PAGE_SIZE);
        CHK(dm_write_page(&dm, pid, write_buf, DM_PAGE_SIZE), "write should succeed");
        CHK(dm.write_count == 1, "write count incremented");

        uint8_t read_buf[DM_PAGE_SIZE];
        CHK(dm_read_page(&dm, pid, read_buf, DM_PAGE_SIZE), "read should succeed");
        CHK(dm.read_count == 1, "read count incremented");
        CHK(memcmp(read_buf, write_buf, DM_PAGE_SIZE) == 0, "data should match");
        dm_destroy(&dm);
    } END("read/write page");

    TEST("deallocate page") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        int32_t pid = dm_allocate_page(&dm);
        CHK(pid >= 0, "allocate should succeed");
        dm_deallocate_page(&dm, pid);
        CHK(dm.num_pages == 0, "num_pages should be 0 after dealloc");
        CHK(!dm.allocated[pid], "page should not be allocated");
        dm_destroy(&dm);
    } END("deallocate page");

    TEST("crash simulation") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        int32_t pid = dm_allocate_page(&dm);
        dm_simulate_crash(&dm, true);
        CHK(dm_is_crashed(&dm), "should be crashed");
        CHK(dm_allocate_page(&dm) == -1, "allocate should fail during crash");
        uint8_t buf[DM_PAGE_SIZE];
        CHK(!dm_read_page(&dm, pid, buf, DM_PAGE_SIZE), "read should fail during crash");
        CHK(!dm_write_page(&dm, pid, buf, DM_PAGE_SIZE), "write should fail during crash");

        dm_simulate_crash(&dm, false);
        CHK(!dm_is_crashed(&dm), "should be recovered");
        CHK(dm_allocate_page(&dm) >= 0, "allocate should work after crash");
        dm_destroy(&dm);
    } END("crash simulation");

    TEST("big buffer check") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        int32_t pid = dm_allocate_page(&dm);
        uint8_t small_buf[100];
        CHK(!dm_read_page(&dm, pid, small_buf, 100), "small buffer should fail");
        CHK(!dm_write_page(&dm, pid, small_buf, 100), "small buffer should fail");
        dm_destroy(&dm);
    } END("big buffer check");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
