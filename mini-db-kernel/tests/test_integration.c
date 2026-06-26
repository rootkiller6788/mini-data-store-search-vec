#include "buffer_pool.h"
#include "btree.h"
#include "wal.h"
#include "lock_manager.h"
#include "mvcc.h"
#include "slotted_page.h"
#include "disk_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int passed = 0, failed = 0;
#define TEST(n) { bool ok = true; do
#define END(n) while(0); if (ok) { printf("  PASS %s\n", n); passed++; } else { printf("  FAIL %s\n", n); failed++; } }
#define CHK(c, m) if (!(c)) { printf("    ! %s\n", m); ok = false; }

int main(void) {
    printf("=== Integration Tests ===\n");

    TEST("BufferPool + DiskManager") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        BufferPool bp;
        bp_init(&bp);

        for (int32_t i = 0; i < 128; i++) {
            int32_t pid = dm_allocate_page(&dm);
            CHK(pid == i, "page id should be sequential");
        }
        for (int32_t i = 0; i < 128; i++) {
            int32_t frame = bp_fetch_page(&bp, i);
            CHK(frame >= 0, "fetch should succeed");
        }
        CHK(bp.hits + bp.misses >= 128, "should have accessed pages");

        dm_destroy(&dm);
    } END("BufferPool + DiskManager");

    TEST("SlottedPage + DiskManager") {
        DiskManager dm;
        dm_init(&dm, "test.db");
        int32_t pid = dm_allocate_page(&dm);
        CHK(pid >= 0, "page allocated");

        SlottedPage page;
        sp_init_page(&page, pid);
        uint8_t tup[] = "integration test data";
        int32_t slot;
        CHK(sp_insert_tuple(&page, tup, 22, &slot), "insert should work");
        CHK(dm_write_page(&dm, pid, (const uint8_t *)&page, DM_PAGE_SIZE),
            "write to disk should work");

        SlottedPage loaded;
        memset(&loaded, 0, sizeof(loaded));
        CHK(dm_read_page(&dm, pid, (uint8_t *)&loaded, DM_PAGE_SIZE),
            "read from disk should work");
        CHK(loaded.header.page_id == pid, "page_id should match");

        uint8_t out[64];
        int32_t out_len;
        CHK(sp_get_tuple(&loaded, 0, out, &out_len), "get from loaded page should work");

        dm_destroy(&dm);
    } END("SlottedPage + DiskManager");

    TEST("BTree basic + WAL") {
        BTree tree = btree_create();
        for (int32_t i = 1; i <= 5; i++) {
            btree_insert(&tree, i, i * 10);
        }
        int32_t v = 0;
        CHK(btree_search(tree, 1, &v) && v == 10, "key 1 should be 10");
        CHK(btree_search(tree, 3, &v) && v == 30, "key 3 should be 30");

        WALManager wm;
        wal_init(&wm);
        wal_write(&wm, WAL_INSERT, 1, 0, 1, NULL, 0, NULL, 0);
        wal_write(&wm, WAL_COMMIT, -1, 0, 1, NULL, 0, NULL, 0);
        wal_recover(&wm);
        free(wm.records);
    } END("BTree basic + WAL");

    TEST("LockManager + MVCC") {
        LockManager lm;
        lm_init(&lm);
        mvcc_init();

        CHK(lm_lock_acquire(&lm, 1, 100, LOCK_EXCLUSIVE), "lock should be acquired");
        CHK(!lm_lock_acquire(&lm, 2, 100, LOCK_EXCLUSIVE), "conflicting lock should fail");
        lm_lock_release(&lm, 1, 100);
        CHK(lm_lock_acquire(&lm, 2, 100, LOCK_EXCLUSIVE), "lock should work after release");

        int32_t txn = mvcc_begin();
        CHK(txn > 0, "txn begin should work");
        mvcc_commit(txn);
    } END("LockManager + MVCC");

    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}