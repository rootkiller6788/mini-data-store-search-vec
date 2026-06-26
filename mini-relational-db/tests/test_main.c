#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bptree.h"
#include "buffer_pool.h"
#include "transaction.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int scan_hits = 0;

static void scan_cb(const char *k, const char *v, void *ctx) {
    (void)k; (void)v;
    (*(int *)ctx)++;
}

#define RUN_TEST(name) do { \
    printf("  TEST %-50s ... ", name); \
    tests_run++; \
} while(0)

#define PASS() do { \
    printf("PASSED\n"); \
    tests_passed++; \
} while(0)

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAILED: %s\n", #cond); tests_failed++; return 1; } \
} while(0)

static int test_bptree_all(void) {
    printf("\n--- B+Tree Tests ---\n");

    /* Create */
    RUN_TEST("bptree_create");
    BPTree *t = bptree_create();
    CHECK(t != NULL);
    CHECK(t->count == 0);
    PASS();

    /* Insert & search */
    RUN_TEST("bptree_insert one key");
    CHECK(bptree_insert(t, "hello", "world") == 0);
    CHECK(t->count == 1);
    PASS();

    RUN_TEST("bptree_search found");
    char val[BP_VAL_SIZE];
    CHECK(bptree_search(t, "hello", val) == 1);
    CHECK(strcmp(val, "world") == 0);
    PASS();

    RUN_TEST("bptree_search missing");
    CHECK(bptree_search(t, "missing", val) == 0);
    PASS();

    /* Insert 20 keys to force splits */
    RUN_TEST("bptree_insert 20 keys (force splits)");
    int ok = 1;
    for (int i = 0; i < 20; i++) {
        char k[16], v[16];
        sprintf(k, "key%02d", i);
        sprintf(v, "val%02d", i);
        if (bptree_insert(t, k, v) != 0) ok = 0;
    }
    CHECK(ok);
    CHECK(t->count == 21);
    PASS();

    /* Search all 20 keys */
    RUN_TEST("bptree_search all 20 keys");
    ok = 1;
    for (int i = 0; i < 20; i++) {
        char k[16], v[16], expect[16];
        sprintf(k, "key%02d", i);
        sprintf(expect, "val%02d", i);
        if (!bptree_search(t, k, v) || strcmp(v, expect) != 0) {
            ok = 0; break;
        }
    }
    CHECK(ok);
    PASS();

    /* Duplicate rejection */
    RUN_TEST("bptree_duplicate_rejected");
    CHECK(bptree_insert(t, "hello", "xxx") == -1);
    CHECK(t->count == 21);
    PASS();

    /* Range scan */
    RUN_TEST("bptree_range_scan");
    scan_hits = 0;
    int scanned = bptree_range_scan(t, "key05", "key09", scan_cb, &scan_hits);
    CHECK(scanned == 5);
    CHECK(scan_hits == 5);
    PASS();

    /* Delete */
    RUN_TEST("bptree_delete existing");
    CHECK(bptree_delete(t, "hello") == 0);
    CHECK(t->count == 20);
    CHECK(bptree_search(t, "hello", val) == 0);
    PASS();

    RUN_TEST("bptree_delete missing");
    CHECK(bptree_delete(t, "nope99") == -1);
    PASS();

    bptree_destroy(t);

    /* Large insertion test */
    RUN_TEST("bptree_100_keys");
    t = bptree_create();
    ok = 1;
    for (int i = 0; i < 100; i++) {
        char k[16], v[16];
        sprintf(k, "k%03d", i);
        sprintf(v, "v%03d", i);
        if (bptree_insert(t, k, v) != 0) ok = 0;
    }
    CHECK(ok && t->count == 100);
    for (int i = 0; i < 100; i++) {
        char k[16], v[16];
        sprintf(k, "k%03d", i);
        if (!bptree_search(t, k, v)) ok = 0;
    }
    CHECK(ok);
    bptree_destroy(t);
    PASS();

    printf("B+Tree tests done.\n");
    return 0;
}

static int test_buffer_pool_all(void) {
    printf("\n--- Buffer Pool Tests ---\n");

    BufferPool bp;
    bp_init(&bp, 4);

    RUN_TEST("bp_fetch new page");
    PageID p1 = {1, 0};
    int f1 = bp_fetch(&bp, p1);
    CHECK(f1 >= 0);
    PASS();

    RUN_TEST("bp_fetch same page (cache hit)");
    int f2 = bp_fetch(&bp, p1);
    CHECK(f2 == f1);
    bp_unpin(&bp, f2);
    PASS();

    RUN_TEST("bp_write_and_readback");
    char *d = bp_get_page_data(&bp, f1);
    CHECK(d != NULL);
    strcpy(d, "BufferPoolTest_v1.0!");
    bp_mark_dirty(&bp, f1);
    bp_unpin(&bp, f1);
    bp_flush_page(&bp, f1);

    bp_destroy(&bp);
    bp_init(&bp, 4);
    f1 = bp_fetch(&bp, p1);
    d = bp_get_page_data(&bp, f1);
    CHECK(d != NULL);
    CHECK(strcmp(d, "BufferPoolTest_v1.0!") == 0);
    bp_unpin(&bp, f1);
    PASS();

    RUN_TEST("bp_CLOCK_eviction");
    bp_destroy(&bp);
    bp_init(&bp, 2);
    PageID pa = {10, 0}, pb = {10, 1}, pc = {10, 2};
    int fa = bp_fetch(&bp, pa);
    strcpy(bp_get_page_data(&bp, fa), "pageA");
    bp_unpin(&bp, fa);
    int fb = bp_fetch(&bp, pb);
    strcpy(bp_get_page_data(&bp, fb), "pageB");
    bp_unpin(&bp, fb);
    int fc = bp_fetch(&bp, pc);
    CHECK(fc >= 0);

    int hits, misses, evictions;
    bp_stats(&bp, &hits, &misses, &evictions);
    CHECK(evictions >= 1);
    bp_destroy(&bp);
    PASS();

    printf("Buffer Pool tests done.\n");
    return 0;
}

static int test_transaction_all(void) {
    printf("\n--- Transaction Tests ---\n");

    TXNManager mgr;
    txn_mgr_init(&mgr);

    RUN_TEST("txn_begin_and_commit");
    int t1 = txn_begin(&mgr);
    CHECK(t1 >= 0);
    CHECK(txn_get_status(&mgr, t1) == TXN_ACTIVE);
    CHECK(txn_commit(&mgr, t1) == 0);
    CHECK(txn_get_status(&mgr, t1) == TXN_COMMITTED);
    PASS();

    RUN_TEST("txn_begin_and_abort");
    int t2 = txn_begin(&mgr);
    CHECK(t2 >= 0);
    txn_abort(&mgr, t2);
    CHECK(txn_get_status(&mgr, t2) == TXN_ABORTED);
    PASS();

    RUN_TEST("txn_2PL_shared_lock");
    int t3 = txn_begin(&mgr);
    CHECK(txn_lock_acquire(&mgr, t3, 42, LOCK_SHARED) == 0);
    int t4 = txn_begin(&mgr);
    CHECK(txn_lock_acquire(&mgr, t4, 42, LOCK_SHARED) == 0);
    txn_commit(&mgr, t3);
    txn_commit(&mgr, t4);
    PASS();

    RUN_TEST("txn_2PL_exclusive_conflict");
    int t5 = txn_begin(&mgr);
    CHECK(txn_lock_acquire(&mgr, t5, 99, LOCK_SHARED) == 0);
    int t6 = txn_begin(&mgr);
    int r = txn_lock_acquire(&mgr, t6, 99, LOCK_EXCLUSIVE);
    CHECK(r == -1);
    txn_commit(&mgr, t5);
    txn_commit(&mgr, t6);
    PASS();

    RUN_TEST("txn_WAL_logging");
    TXNManager mgr2;
    txn_mgr_init(&mgr2);
    int t7 = txn_begin(&mgr2);
    txn_wal_append(&mgr2, t7, WAL_INSERT, 200, "", "row1");
    txn_wal_append(&mgr2, t7, WAL_UPDATE, 200, "row1", "row1_mod");
    txn_commit(&mgr2, t7);
    CHECK(mgr2.wal_log.count >= 4);
    PASS();

    RUN_TEST("txn_recovery_undo_uncommitted");
    TXNManager mgr3;
    txn_mgr_init(&mgr3);
    int t8 = txn_begin(&mgr3);
    txn_wal_append(&mgr3, t8, WAL_INSERT, 300, "", "data_t8");
    txn_commit(&mgr3, t8);
    int t9 = txn_begin(&mgr3);
    txn_wal_append(&mgr3, t9, WAL_INSERT, 400, "", "data_t9_uncommit");
    txn_recover(&mgr3);
    CHECK(txn_get_status(&mgr3, t8) == TXN_COMMITTED);
    CHECK(txn_get_status(&mgr3, t9) == TXN_ABORTED);
    PASS();

    printf("Transaction tests done.\n");
    return 0;
}

int main(void) {
    printf("========================================\n");
    printf("  mini-relational-db Test Suite\n");
    printf("========================================\n");

    int failed = 0;
    failed += test_bptree_all();
    failed += test_buffer_pool_all();
    failed += test_transaction_all();

    printf("\n========================================\n");
    printf("  RESULTS: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
