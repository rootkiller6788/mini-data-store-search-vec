/* ───────────────────────────────────────────────────────────
   mini-file-storage Test Suite

   Comprehensive tests covering all core components:
     - Skip List: insert, search, iterator, duplicate handling
     - WAL: append, sync, recover, CRC32C integrity
     - SSTable: write, read, lookup, bloom filter, merge iterator
     - LSM Tree: put, get, delete, compaction
     - Block Cache: put, get, eviction, hit rate, invalidation
     - Range Query: range scan, multi-table merge, filtering
     - Snapshot/MVCC: version store, snapshot reads, GC
     - Encoding: varint, big-endian, composite keys, prefix

   All tests use standard assert(). Run with:
     make test
   ─────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "skiplist.h"
#include "sstable.h"
#include "wal_file.h"
#include "lsm_tree.h"
#include "compaction.h"
#include "block_cache.h"
#include "range_query.h"
#include "snapshot.h"
#include "encoding.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  TEST: %-50s", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { \
    printf(" PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg) do { \
    printf(" FAIL: %s\n", msg); \
    tests_failed++; \
} while(0)

#define CHECK(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)

/* ============================================================
   Skip List Tests
   ============================================================ */
static void test_skiplist_create(void) {
    TEST("SkipList: create");
    SkipList *list = skiplist_create();
    CHECK(list != NULL, "create returned NULL");
    CHECK(list->size == 0, "initial size not zero");
    CHECK(list->level == 0, "initial level not zero");
    CHECK(list->header != NULL, "header is NULL");
    skiplist_destroy(list);
    PASS();
}

static void test_skiplist_insert_search(void) {
    TEST("SkipList: insert and search");
    SkipList *list = skiplist_create();
    CHECK(list != NULL, "create failed");

    uint8_t key[] = "test_key_001";
    uint8_t val[] = "test_value_001";
    int rc = skiplist_insert(list, key, 12, val, 14);
    CHECK(rc == 0, "insert returned error");

    SkipNode *node = skiplist_search(list, key, 12);
    CHECK(node != NULL, "search returned NULL");
    CHECK(node->key_len == 12, "key_len mismatch");
    CHECK(memcmp(node->key, key, 12) == 0, "key mismatch");
    CHECK(memcmp(node->value, val, 14) == 0, "value mismatch");

    skiplist_destroy(list);
    PASS();
}

static void test_skiplist_many_inserts(void) {
    TEST("SkipList: 500 ordered inserts + search all");
    SkipList *list = skiplist_create();
    CHECK(list != NULL, "create failed");

    for (int i = 0; i < 500; i++) {
        char kbuf[32], vbuf[32];
        snprintf(kbuf, sizeof(kbuf), "key_%06d", i);
        snprintf(vbuf, sizeof(vbuf), "val_%06d", i);
        int rc = skiplist_insert(list, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                                 (uint8_t *)vbuf, (uint32_t)strlen(vbuf));
        CHECK(rc == 0, "insert failed");
    }
    CHECK(list->size == 500, "size mismatch");

    /* Search all */
    for (int i = 0; i < 500; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "key_%06d", i);
        SkipNode *node = skiplist_search(list, (uint8_t *)kbuf, (uint32_t)strlen(kbuf));
        CHECK(node != NULL, "search missed inserted key");
    }

    /* Search non-existent */
    SkipNode *nf = skiplist_search(list, (uint8_t *)"zzz_nonexistent", 15);
    CHECK(nf == NULL, "found non-existent key");

    skiplist_destroy(list);
    PASS();
}

static void test_skiplist_iterator(void) {
    TEST("SkipList: iterator ordered traversal");
    SkipList *list = skiplist_create();
    CHECK(list != NULL, "create failed");

    /* Insert 100 keys in reverse order to test ordering */
    for (int i = 99; i >= 0; i--) {
        char kbuf[16];
        snprintf(kbuf, sizeof(kbuf), "k%03d", i);
        skiplist_insert(list, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                        (uint8_t *)"v", 1);
    }
    CHECK(list->size == 100, "size mismatch");

    /* Iterate and verify order */
    SkipListIterator *it = skiplist_iterator(list);
    CHECK(it != NULL, "iterator create failed");

    SkipNode *node;
    int count = 0;
    while ((node = skiplist_iterator_next(it)) != NULL) {
        char cur[16];
        snprintf(cur, sizeof(cur), "k%03d", count);
        CHECK(strncmp((char *)node->key, cur, (size_t)node->key_len) == 0,
              "iterator order violation");
        count++;
    }
    CHECK(count == 100, "iterator count mismatch");
    skiplist_iterator_destroy(it);
    skiplist_destroy(list);
    PASS();
}

static void test_skiplist_duplicate(void) {
    TEST("SkipList: duplicate key update");
    SkipList *list = skiplist_create();
    CHECK(list != NULL, "create failed");

    uint8_t key[] = "dup_key";
    uint8_t v1[] = "value1";
    uint8_t v2[] = "value2_new";

    skiplist_insert(list, key, 7, v1, 6);
    skiplist_insert(list, key, 7, v2, 10);

    SkipNode *node = skiplist_search(list, key, 7);
    CHECK(node != NULL, "search failed for duplicate key");
    CHECK(node->value_len == 10, "value_len not updated");
    CHECK(memcmp(node->value, v2, 10) == 0, "value not updated");

    CHECK(list->size == 1, "size should be 1 after duplicate");

    skiplist_destroy(list);
    PASS();
}

/* ============================================================
   WAL Tests
   ============================================================ */
static int wal_test_replay_count;
static uint8_t wal_test_last_key[256];
static uint32_t wal_test_last_klen;

static int wal_test_replay_cb(uint8_t type, const uint8_t *key, uint32_t key_len,
                               const uint8_t *value, uint32_t value_len, void *ctx) {
    (void)type;
    (void)value;
    (void)value_len;
    (void)ctx;
    wal_test_replay_count++;
    memcpy(wal_test_last_key, key, key_len);
    wal_test_last_klen = key_len;
    return 0;
}

static void test_wal_append_recover(void) {
    TEST("WAL: append 50 records, sync, recover");
    const char *fname = "test_wal.log";

    WALWriter *wal = wal_open(fname);
    CHECK(wal != NULL, "wal_open failed");

    for (int i = 0; i < 50; i++) {
        char kbuf[32], vbuf[32];
        snprintf(kbuf, sizeof(kbuf), "wal_key_%03d", i);
        snprintf(vbuf, sizeof(vbuf), "wal_val_%03d", i);
        int rc = wal_append(wal, WAL_RECORD_PUT,
                            (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                            (uint8_t *)vbuf, (uint32_t)strlen(vbuf));
        CHECK(rc == 0, "wal_append failed");
    }
    wal_sync(wal);

    /* Recover */
    wal_test_replay_count = 0;
    int recovered = wal_recover(wal, wal_test_replay_cb, NULL);
    CHECK(recovered == 50, "recovery count mismatch");

    wal_close(wal);
    remove(fname);
    remove("test_wal.log.closed");
    PASS();
}

static void test_wal_crc32c_integrity(void) {
    TEST("WAL: CRC32C checksum integrity");
    const char *fname = "test_wal_crc.sst";

    WALWriter *wal = wal_open(fname);
    CHECK(wal != NULL, "wal_open failed");

    uint8_t key[] = "integrity_key";
    uint8_t val[] = "integrity_value_12345";
    int rc = wal_append(wal, WAL_RECORD_PUT, key, 13, val, 21);
    CHECK(rc == 0, "append failed");
    wal_sync(wal);

    wal_test_replay_count = 0;
    int recovered = wal_recover(wal, wal_test_replay_cb, NULL);
    CHECK(recovered == 1, "should recover 1 record");
    CHECK(wal_test_last_klen == 13, "last key length mismatch");
    CHECK(memcmp(wal_test_last_key, key, 13) == 0, "last key mismatch");

    wal_close(wal);
    remove(fname);
    remove("test_wal_crc.sst.closed");
    PASS();
}

/* ============================================================
   SSTable Tests
   ============================================================ */
static void test_sstable_write_read(void) {
    TEST("SSTable: write 100 records, read back, verify all");
    const char *fname = "test_output.sst";
    uint8_t  *keys[100];
    uint32_t  key_lens[100];
    uint8_t  *vals[100];
    uint32_t  val_lens[100];

    for (int i = 0; i < 100; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "sst_key_%06d", i);
        key_lens[i] = (uint32_t)strlen(buf);
        keys[i] = (uint8_t *)strdup(buf);
        snprintf(buf, sizeof(buf), "sst_value_number_%06d_data", i);
        val_lens[i] = (uint32_t)strlen(buf);
        vals[i] = (uint8_t *)strdup(buf);
    }

    int rc = sstable_write(fname, (const uint8_t **)keys, key_lens,
                           (const uint8_t **)vals, val_lens, 100);
    CHECK(rc == 0, "sstable_write failed");

    SSTable *table = sstable_read(fname);
    CHECK(table != NULL, "sstable_read returned NULL");
    CHECK(table->num_data_blocks > 0, "no data blocks");
    CHECK(table->index_block.num_entries > 0, "no index entries");

    /* Verify all 100 records */
    int found = 0;
    for (int i = 0; i < 100; i++) {
        uint8_t vbuf[MAX_VALUE_SIZE];
        uint32_t vlen;
        if (sstable_get(table, keys[i], key_lens[i], vbuf, &vlen) > 0)
            found++;
    }
    CHECK(found == 100, "not all keys found in read-back");

    /* Verify bloom filter for known-present keys */
    int bloom_hits = 0;
    for (int i = 0; i < 100; i++) {
        if (bloom_maybe_contain(&table->bloom, keys[i], key_lens[i]))
            bloom_hits++;
    }
    CHECK(bloom_hits == 100, "bloom filter missed known keys");

    sstable_destroy(table);
    remove(fname);
    for (int i = 0; i < 100; i++) { free(keys[i]); free(vals[i]); }
    PASS();
}

static void test_sstable_merge_iterator(void) {
    TEST("SSTable: merge iterator with 3 files, 50 records each");
    SSTable *tables[3];
    const char *fnames[3] = {"test_m1.sst", "test_m2.sst", "test_m3.sst"};

    for (int t = 0; t < 3; t++) {
        uint8_t  *keys[50];
        uint32_t  kl[50];
        uint8_t  *vals[50];
        uint32_t  vl[50];
        for (int i = 0; i < 50; i++) {
            char buf[32];
            /* Offset keys so they interleave partially */
            snprintf(buf, sizeof(buf), "merge_key_%06d", i * 3 + t);
            kl[i] = (uint32_t)strlen(buf);
            keys[i] = (uint8_t *)strdup(buf);
            snprintf(buf, sizeof(buf), "merge_val_%06d_%d", i, t);
            vl[i] = (uint32_t)strlen(buf);
            vals[i] = (uint8_t *)strdup(buf);
        }
        sstable_write(fnames[t], (const uint8_t **)keys, kl,
                      (const uint8_t **)vals, vl, 50);
        tables[t] = sstable_read(fnames[t]);
        CHECK(tables[t] != NULL, "sstable_read failed");
        for (int i = 0; i < 50; i++) { free(keys[i]); free(vals[i]); }
    }

    SSTableMergeIterator *iter = sstable_merge_iterator_create(tables, 3);
    CHECK(iter != NULL, "merge iterator create failed");

    uint8_t kbuf[256], vbuf[1024];
    uint32_t klen, vlen;
    int count = 0;
    int order_ok = 1;
    uint8_t prev_key[256] = {0};
    uint32_t prev_len = 0;
    (void)prev_len;

    while (sstable_merge_iterator_next(iter, kbuf, &klen, vbuf, &vlen)) {
        /* Check ordering */
        if (count > 0) {
            int cmp = key_compare(prev_key, prev_len, kbuf, klen);
            if (cmp > 0) order_ok = 0;
        }
        memcpy(prev_key, kbuf, klen);
        prev_len = klen;
        count++;
    }
    CHECK(order_ok, "merge output out of order");
    CHECK(count >= 50, "too few merged entries");

    sstable_merge_iterator_destroy(iter);
    for (int t = 0; t < 3; t++) {
        sstable_destroy(tables[t]);
        remove(fnames[t]);
    }
    PASS();
}

/* ============================================================
   LSM Tree Tests
   ============================================================ */
static void test_lsm_put_get(void) {
    TEST("LSM: put 200 records, get all, verify");
    LSMTree *tree = lsm_open("./test_lsm_data", COMPACTION_LEVELED);
    CHECK(tree != NULL, "lsm_open failed");

    for (int i = 0; i < 200; i++) {
        char kbuf[32], vbuf[64];
        snprintf(kbuf, sizeof(kbuf), "lsm_test_key_%06d", i);
        snprintf(vbuf, sizeof(vbuf), "lsm_test_value_%06d", i);
        int rc = lsm_put(tree, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                         (uint8_t *)vbuf, (uint32_t)strlen(vbuf));
        CHECK(rc == 0, "lsm_put failed");
    }

    int found = 0;
    uint8_t vbuf[1024];
    uint32_t vlen;
    for (int i = 0; i < 200; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "lsm_test_key_%06d", i);
        if (lsm_get(tree, (uint8_t *)kbuf, (uint32_t)strlen(kbuf), vbuf, &vlen) > 0)
            found++;
    }
    CHECK(found == 200, "not all keys found");

    lsm_close(tree);
    /* Cleanup */
    remove("./test_lsm_data/level0_000000.sst");
    remove("./test_lsm_data/level0_*");
    remove("./test_lsm_data/MINI_LOG");
    remove("./test_lsm_data/MINI_LOG.closed");
    PASS();
}

static void test_lsm_delete(void) {
    TEST("LSM: insert, delete, verify key gone");
    LSMTree *tree = lsm_open("./test_lsm_del", COMPACTION_LEVELED);
    CHECK(tree != NULL, "lsm_open failed");

    uint8_t key[] = "to_delete_key";
    uint8_t val[] = "value_to_delete";
    lsm_put(tree, key, 13, val, 15);

    /* Verify exists */
    uint8_t vbuf[1024];
    uint32_t vlen;
    int rc = lsm_get(tree, key, 13, vbuf, &vlen);
    CHECK(rc == 1, "key should exist before delete");

    /* Delete */
    lsm_delete(tree, key, 13);

    /* Verify gone */
    rc = lsm_get(tree, key, 13, vbuf, &vlen);
    CHECK(rc == 0, "key should not exist after delete");

    lsm_close(tree);
    remove("./test_lsm_del/level0_000000.sst");
    remove("./test_lsm_del/MINI_LOG");
    remove("./test_lsm_del/MINI_LOG.closed");
    PASS();
}

/* ============================================================
   Block Cache Tests
   ============================================================ */
static void test_bcache_put_get(void) {
    TEST("BlockCache: put 10 entries, get all, check hit rate");
    BlockCache *cache = bcache_create(100);
    CHECK(cache != NULL, "bcache_create failed");

    uint8_t data[256];
    for (int i = 0; i < 10; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "block_%02d", i);
        memset(data, (uint8_t)i, sizeof(data));
        int rc = bcache_put(cache, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                            data, 256);
        CHECK(rc == 0, "put failed");
    }

    uint8_t out[256];
    uint32_t osize;
    int hits = 0;
    for (int i = 0; i < 10; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "block_%02d", i);
        if (bcache_get(cache, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                       out, &osize) == 1) {
            hits++;
        }
    }
    CHECK(hits == 10, "not all entries hit");

    BCacheStats stats;
    bcache_get_stats(cache, &stats);
    CHECK(stats.hits == 10, "hit count mismatch");
    CHECK(stats.hit_rate == 1.0, "hit rate should be 100%");

    bcache_destroy(cache);
    PASS();
}

static void test_bcache_eviction(void) {
    TEST("BlockCache: LRU eviction when capacity exceeded");
    BlockCache *cache = bcache_create(5);
    CHECK(cache != NULL, "create failed");

    uint8_t data[64];
    /* Insert 10 entries, capacity=5 */
    for (int i = 0; i < 10; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "evict_key_%02d", i);
        memset(data, (uint8_t)(100 + i), 64);
        bcache_put(cache, (uint8_t *)kbuf, (uint32_t)strlen(kbuf), data, 64);
    }

    /* Oldest 5 should have been evicted */
    uint8_t out[64];
    uint32_t osize;
    int rc = bcache_get(cache, (uint8_t *)"evict_key_00", 12, out, &osize);
    CHECK(rc == 0, "key_00 should have been evicted");

    rc = bcache_get(cache, (uint8_t *)"evict_key_09", 12, out, &osize);
    CHECK(rc == 1, "key_09 should still be in cache");

    BCacheStats stats;
    bcache_get_stats(cache, &stats);
    CHECK(stats.evictions >= 5, "should have evicted at least 5 entries");

    bcache_destroy(cache);
    PASS();
}

static void test_bcache_invalidate(void) {
    TEST("BlockCache: invalidate specific entry");
    BlockCache *cache = bcache_create(10);
    CHECK(cache != NULL, "create failed");

    uint8_t data[32];
    memset(data, 0xAB, 32);
    bcache_put(cache, (uint8_t *)"inv_key", 7, data, 32);

    uint8_t out[32];
    uint32_t osize;
    int rc = bcache_get(cache, (uint8_t *)"inv_key", 7, out, &osize);
    CHECK(rc == 1, "should find key before invalidation");

    rc = bcache_invalidate(cache, (uint8_t *)"inv_key", 7);
    CHECK(rc == 1, "invalidation should succeed");

    rc = bcache_get(cache, (uint8_t *)"inv_key", 7, out, &osize);
    CHECK(rc == 0, "should not find key after invalidation");

    bcache_destroy(cache);
    PASS();
}

/* ============================================================
   Range Query Tests
   ============================================================ */
static void test_range_scan_single_sstable(void) {
    TEST("RangeQuery: single SSTable range scan");
    const char *fname = "test_range.sst";
    uint8_t  *keys[50];
    uint32_t  kl[50];
    uint8_t  *vals[50];
    uint32_t  vl[50];

    for (int i = 0; i < 50; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "range_key_%06d", i);
        kl[i] = (uint32_t)strlen(buf);
        keys[i] = (uint8_t *)strdup(buf);
        snprintf(buf, sizeof(buf), "range_val_%06d", i);
        vl[i] = (uint32_t)strlen(buf);
        vals[i] = (uint8_t *)strdup(buf);
    }

    sstable_write(fname, (const uint8_t **)keys, kl,
                  (const uint8_t **)vals, vl, 50);
    SSTable *table = sstable_read(fname);
    CHECK(table != NULL, "read failed");

    /* Range: key_010 to key_029 */
    KeyRange range;
    memcpy(range.start_key, "range_key_000010", 16);
    range.start_key_len = 16;
    range.start_inclusive = 1;
    memcpy(range.end_key, "range_key_000030", 16);
    range.end_key_len = 16;
    range.end_inclusive = 0;

    RangeEntry results[30];
    uint32_t num_results = 0;
    int rc = sstable_range_scan(table, &range, results, 30, &num_results);
    CHECK(rc == 0, "range scan failed");
    CHECK(num_results == 20, "expected 20 results in range [010, 030)");

    sstable_destroy(table);
    remove(fname);
    for (int i = 0; i < 50; i++) { free(keys[i]); free(vals[i]); }
    PASS();
}

static void test_key_compare(void) {
    TEST("RangeQuery: key_compare semantics");
    CHECK(key_compare((uint8_t *)"abc", 3, (uint8_t *)"abc", 3) == 0, "equal");
    CHECK(key_compare((uint8_t *)"abc", 3, (uint8_t *)"abd", 3) < 0, "abc < abd");
    CHECK(key_compare((uint8_t *)"abd", 3, (uint8_t *)"abc", 3) > 0, "abd > abc");
    CHECK(key_compare((uint8_t *)"ab", 2, (uint8_t *)"abc", 3) < 0, "ab < abc (prefix)");
    CHECK(key_compare((uint8_t *)"abc", 3, (uint8_t *)"ab", 2) > 0, "abc > ab (prefix)");
    PASS();
}

static void test_key_in_range(void) {
    TEST("RangeQuery: key_in_range boundary tests");
    KeyRange range;
    memcpy(range.start_key, "key_0000050", 11);
    range.start_key_len = 11;
    range.start_inclusive = 1;
    memcpy(range.end_key, "key_0000100", 11);
    range.end_key_len = 11;
    range.end_inclusive = 0;

    CHECK(key_in_range((uint8_t *)"key_0000050", 11, &range) == 1, "start inclusive");
    CHECK(key_in_range((uint8_t *)"key_0000075", 11, &range) == 1, "middle");
    CHECK(key_in_range((uint8_t *)"key_0000099", 11, &range) == 1, "before end exclusive");
    CHECK(key_in_range((uint8_t *)"key_0000100", 11, &range) == 0, "end exclusive");
    CHECK(key_in_range((uint8_t *)"key_0000049", 11, &range) == 0, "before start");
    PASS();
}

/* ============================================================
   Snapshot / MVCC Tests
   ============================================================ */
static void test_snapshot_manager(void) {
    TEST("Snapshot: manager create, snapshot, release");
    SnapshotManager *mgr = snap_mgr_create();
    CHECK(mgr != NULL, "create failed");

    Snapshot *s1 = snap_mgr_create_snapshot(mgr);
    CHECK(s1 != NULL, "create snapshot 1 failed");
    CHECK(s1->snapshot_seq == 1, "snapshot 1 seqno should be 1");

    /* Advance seqno */
    snap_mgr_next_seqno(mgr); /* 1 → 2 */
    snap_mgr_next_seqno(mgr); /* 2 → 3 */

    Snapshot *s2 = snap_mgr_create_snapshot(mgr);
    CHECK(s2 != NULL, "create snapshot 2 failed");
    CHECK(s2->snapshot_seq >= 3, "snapshot 2 seqno should be >= 3");

    CHECK(snap_mgr_get_oldest_active(mgr) == s1->snapshot_seq, "oldest snapshot wrong");

    snap_mgr_release_snapshot(mgr, s1);
    /* After release, s2 should be oldest */
    CHECK(snap_mgr_get_oldest_active(mgr) == s2->snapshot_seq,
          "oldest snapshot wrong after release");

    snap_mgr_release_snapshot(mgr, s2);
    CHECK(snap_mgr_get_oldest_active(mgr) == UINT64_MAX, "should be UINT64_MAX with no active");

    snap_mgr_destroy(mgr);
    PASS();
}

static void test_version_store(void) {
    TEST("Snapshot: version store put/get at snapshot");
    VersionStore *vs = vs_create(64);
    CHECK(vs != NULL, "version store create failed");

    /* Insert at seqno 1 */
    vs_put(vs, (uint8_t *)"mvcc_key", 8, (uint8_t *)"value_v1", 9, 1, 0);

    /* Insert updated value at seqno 5 */
    vs_put(vs, (uint8_t *)"mvcc_key", 8, (uint8_t *)"value_v2_updated", 17, 5, 0);

    /* Read at snapshot seqno 1: should see v1 */
    uint8_t vbuf[64];
    uint32_t vlen;
    int rc = vs_get_at_snapshot(vs, (uint8_t *)"mvcc_key", 8, 1, vbuf, &vlen);
    CHECK(rc == 1, "should find key at snapshot 1");
    CHECK(vlen == 9, "v1 length mismatch");
    CHECK(memcmp(vbuf, "value_v1", 9) == 0, "v1 value mismatch");

    /* Read at snapshot seqno 5: should see v2 */
    rc = vs_get_at_snapshot(vs, (uint8_t *)"mvcc_key", 8, 5, vbuf, &vlen);
    CHECK(rc == 1, "should find key at snapshot 5");
    CHECK(vlen == 17, "v2 length mismatch");
    CHECK(memcmp(vbuf, "value_v2_updated", 17) == 0, "v2 value mismatch");

    /* Read at snapshot seqno 10: should see v2 (latest) */
    rc = vs_get_at_snapshot(vs, (uint8_t *)"mvcc_key", 8, 10, vbuf, &vlen);
    CHECK(rc == 1, "should find key at snapshot 10");

    vs_destroy(vs);
    PASS();
}

static void test_version_store_tombstone(void) {
    TEST("Snapshot: version store with tombstone");
    VersionStore *vs = vs_create(64);
    CHECK(vs != NULL, "create failed");

    /* Insert at seqno 1 */
    vs_put(vs, (uint8_t *)"ts_key", 6, (uint8_t *)"alive", 5, 1, 0);

    /* Tombstone at seqno 3 */
    vs_put(vs, (uint8_t *)"ts_key", 6, NULL, 0, 3, 1);

    uint8_t vbuf[64];
    uint32_t vlen;
    /* At snapshot 1: should see alive */
    int rc = vs_get_at_snapshot(vs, (uint8_t *)"ts_key", 6, 1, vbuf, &vlen);
    CHECK(rc == 1, "should see alive at snapshot 1");

    /* At snapshot 3: should see tombstone (deleted) */
    rc = vs_get_at_snapshot(vs, (uint8_t *)"ts_key", 6, 3, vbuf, &vlen);
    CHECK(rc == 0, "should be deleted at snapshot 3");

    vs_destroy(vs);
    PASS();
}

static void test_version_store_gc(void) {
    TEST("Snapshot: version store garbage collection");
    VersionStore *vs = vs_create(64);
    CHECK(vs != NULL, "create failed");

    /* Insert many versions */
    for (uint64_t s = 1; s <= 50; s++) {
        char vbuf[32];
        snprintf(vbuf, sizeof(vbuf), "version_%llu", (unsigned long long)s);
        vs_put(vs, (uint8_t *)"gc_key", 6, (uint8_t *)vbuf, (uint32_t)strlen(vbuf), s, 0);
    }

    /* GC versions older than seqno 25 */
    int removed = vs_gc_old_versions(vs, 25);
    CHECK(removed > 0, "should have removed some versions");

    /* Version at seqno 50 should remain */
    uint8_t vbuf[64];
    uint32_t vlen;
    int rc = vs_get_at_snapshot(vs, (uint8_t *)"gc_key", 6, 50, vbuf, &vlen);
    CHECK(rc == 1, "latest version should survive GC");

    vs_destroy(vs);
    PASS();
}

/* ============================================================
   Encoding Tests
   ============================================================ */
static void test_varint32(void) {
    TEST("Encoding: varint32 encode/decode round-trip");
    uint32_t test_values[] = {0, 1, 127, 128, 16383, 16384, 2097151, 268435455, UINT32_MAX};
    int num_tests = (int)(sizeof(test_values) / sizeof(test_values[0]));

    for (int i = 0; i < num_tests; i++) {
        uint8_t buf[5];
        int len = varint32_encode(test_values[i], buf);
        CHECK(len >= 1 && len <= 5, "varint encode length out of range");

        const uint8_t *p = buf;
        uint32_t decoded;
        int consumed = varint32_decode(&p, buf + 5, &decoded);
        CHECK(consumed == len, "varint decode consumed wrong bytes");
        CHECK(decoded == test_values[i], "varint round-trip mismatch");
    }
    PASS();
}

static void test_varint64(void) {
    TEST("Encoding: varint64 encode/decode round-trip");
    uint64_t test_values[] = {0, 1, 127, 128, 16383, 16384, 2097151,
                               268435455, 34359738367ULL, UINT64_MAX};
    int num_tests = (int)(sizeof(test_values) / sizeof(test_values[0]));

    for (int i = 0; i < num_tests; i++) {
        uint8_t buf[10];
        int len = varint64_encode(test_values[i], buf);
        CHECK(len >= 1 && len <= 10, "varint64 encode length out of range");

        const uint8_t *p = buf;
        uint64_t decoded;
        int consumed = varint64_decode(&p, buf + 10, &decoded);
        CHECK(consumed == len, "varint64 decode consumed wrong bytes");
        CHECK(decoded == test_values[i], "varint64 round-trip mismatch");
    }
    PASS();
}

static void test_big_endian_encoding(void) {
    TEST("Encoding: int32/int64 big-endian round-trip");
    for (uint32_t v = 0; v < 1000; v++) {
        uint8_t buf[4];
        int32_be_encode(v, buf);
        uint32_t decoded = int32_be_decode(buf);
        CHECK(decoded == v, "int32 BE round-trip mismatch");
    }

    for (uint64_t v = 0; v < 1000; v++) {
        uint8_t buf[8];
        int64_be_encode(v, buf);
        uint64_t decoded = int64_be_decode(buf);
        CHECK(decoded == v, "int64 BE round-trip mismatch");
    }

    /* Edge cases */
    {
        uint8_t buf[4];
        int32_be_encode(0xDEADBEEFu, buf);
        CHECK(int32_be_decode(buf) == 0xDEADBEEFu, "int32 edge case");
    }
    {
        uint8_t buf[8];
        int64_be_encode(0xDEADBEEFCAFEuLL, buf);
        CHECK(int64_be_decode(buf) == 0xDEADBEEFCAFEuLL, "int64 edge case");
    }
    PASS();
}

static void test_composite_key(void) {
    TEST("Encoding: composite key build with string + uint32");
    CompositeKeyBuilder builder;
    composite_key_init(&builder);

    composite_key_add_string(&builder, (uint8_t *)"user", 4);
    composite_key_add_uint32(&builder, 12345);

    uint8_t key[COMPOSITE_KEY_MAX_LEN];
    uint32_t key_len = composite_key_build(&builder, key);
    CHECK(key_len > 4, "composite key too short");

    /* First part should be "user" */
    CHECK(memcmp(key, "user", 4) == 0, "first part wrong");
    /* Separator byte */
    CHECK(key[4] == 0x00, "separator wrong");

    PASS();
}

static void test_common_prefix(void) {
    TEST("Encoding: common_prefix_len");
    uint32_t cp = common_prefix_len((uint8_t *)"hello_world", 11,
                                    (uint8_t *)"hello_there", 11);
    CHECK(cp == 6, "common prefix should be 6 ('hello_')");

    cp = common_prefix_len((uint8_t *)"abc", 3, (uint8_t *)"xyz", 3);
    CHECK(cp == 0, "no common prefix");

    cp = common_prefix_len((uint8_t *)"same", 4, (uint8_t *)"same", 4);
    CHECK(cp == 4, "identical should have full prefix");

    cp = common_prefix_len((uint8_t *)"test", 4, (uint8_t *)"tes", 3);
    CHECK(cp == 3, "prefix with shorter second");
    PASS();
}

/* ============================================================
   Compaction Tests
   ============================================================ */
static void test_compaction_pick_leveled(void) {
    TEST("Compaction: pick_leveled creates job");
    /* Create 2 simple SSTables */
    const char *fn1 = "test_cp1.sst", *fn2 = "test_cp2.sst";
    uint8_t  *k1 = (uint8_t *)"comp_key_a";
    uint8_t  *k2 = (uint8_t *)"comp_key_b";
    uint32_t  kl1 = 10, kl2 = 10;
    uint8_t  *v1 = (uint8_t *)"val_a", *v2 = (uint8_t *)"val_b";
    uint32_t  vl1 = 5, vl2 = 5;

    sstable_write(fn1, (const uint8_t **)&k1, &kl1, (const uint8_t **)&v1, &vl1, 1);
    sstable_write(fn2, (const uint8_t **)&k2, &kl2, (const uint8_t **)&v2, &vl2, 1);

    SSTable *t1 = sstable_read(fn1);
    SSTable *t2 = sstable_read(fn2);
    CHECK(t1 != NULL && t2 != NULL, "sstable_read failed");

    SSTable *level_n[] = {t1};
    SSTable *level_np1[] = {t2};
    CompactionJob job;

    int rc = compaction_pick_leveled(level_n, 1, level_np1, 1, &job);
    CHECK(rc == 0, "pick_leveled failed");
    CHECK(job.num_inputs >= 1, "should have at least 1 input");

    /* Cleanup */
    free(job.input_tables);
    sstable_destroy(t1);
    sstable_destroy(t2);
    remove(fn1);
    remove(fn2);
    PASS();
}

/* ============================================================
   Main
   ============================================================ */
int main(void) {
    printf("\n=== mini-file-storage Test Suite ===\n\n");

    printf("[Skip List]\n");
    test_skiplist_create();
    test_skiplist_insert_search();
    test_skiplist_many_inserts();
    test_skiplist_iterator();
    test_skiplist_duplicate();

    printf("\n[WAL]\n");
    test_wal_append_recover();
    test_wal_crc32c_integrity();

    printf("\n[SSTable]\n");
    test_sstable_write_read();
    test_sstable_merge_iterator();

    printf("\n[LSM Tree]\n");
    test_lsm_put_get();
    test_lsm_delete();

    printf("\n[Block Cache]\n");
    test_bcache_put_get();
    test_bcache_eviction();
    test_bcache_invalidate();

    printf("\n[Range Query]\n");
    test_range_scan_single_sstable();
    test_key_compare();
    test_key_in_range();

    printf("\n[Snapshot / MVCC]\n");
    test_snapshot_manager();
    test_version_store();
    test_version_store_tombstone();
    test_version_store_gc();

    printf("\n[Encoding]\n");
    test_varint32();
    test_varint64();
    test_big_endian_encoding();
    test_composite_key();
    test_common_prefix();

    printf("\n[Compaction]\n");
    test_compaction_pick_leveled();

    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("====================================\n");

    return tests_failed > 0 ? 1 : 0;
}
