#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "nosql_wal.h"

static int replay_count = 0;

static int replay_cb(const WALRecord *rec, void *ctx) {
    (void)ctx;
    replay_count++;
    assert(rec->magic == WAL_MAGIC);
    assert(rec->op == WAL_OP_UPDATE || rec->op == WAL_OP_DELETE);
    return 0;
}

int main(void) {
    const char *wal_path = "build/test_wal.log";

    /* Test CRC32 */
    wal_crc32_init();
    const char *test_str = "hello world";
    uint32_t c1 = wal_crc32((const uint8_t *)test_str, strlen(test_str));
    uint32_t c2 = wal_crc32((const uint8_t *)test_str, strlen(test_str));
    assert(c1 == c2);
    uint32_t c3 = wal_crc32((const uint8_t *)"different", 9);
    assert(c1 != c3);

    /* Test WAL writer */
    WALWriter *w = wal_writer_open(wal_path, 0);
    assert(w != NULL);
    assert(wal_writer_append(w, WAL_OP_UPDATE, "key1", "value1") == 0);
    assert(wal_writer_append(w, WAL_OP_UPDATE, "key2", "value2") == 0);
    assert(wal_writer_commit(w) == 0);
    assert(wal_writer_checkpoint(w) == 0);
    assert(wal_writer_append(w, WAL_OP_UPDATE, "key3", "value3") == 0);
    assert(wal_writer_append(w, WAL_OP_UPDATE, "key4", "value4") == 0);
    assert(wal_writer_append(w, WAL_OP_UPDATE, "key5", "value5") == 0);
    assert(wal_writer_append(w, WAL_OP_DELETE, "key6", "") == 0);

    const WALStats *stats = wal_writer_stats(w);
    assert(stats != NULL);
    assert(stats->total_appends >= 8);
    wal_writer_close(w);

    /* Test WAL reader */
    WALReader *r = wal_reader_open(wal_path);
    assert(r != NULL);
    WALRecord rec;
    int read_count = 0;
    while (wal_reader_next(r, &rec) == 1) {
        assert(rec.magic == WAL_MAGIC);
        read_count++;
    }
    assert(read_count >= 8);
    wal_reader_close(r);

    /* Test recovery */
    replay_count = 0;
    int recovered = wal_recover(wal_path, replay_cb, NULL);
    assert(recovered >= 3);
    assert(replay_count >= 3);

    /* Cleanup */
    remove(wal_path);
    printf("test_wal: PASSED\n");
    return 0;
}
