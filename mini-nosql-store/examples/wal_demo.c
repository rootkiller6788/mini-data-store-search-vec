#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nosql_wal.h"

static int replay_printer(const WALRecord *rec, void *ctx) {
    (void)ctx;
    printf("  [REPLAY] LSN=%llu op=%d key=%.*s value=%.*s\n",
           (unsigned long long)rec->lsn, rec->op,
           rec->key_len, rec->key, rec->value_len, rec->value);
    return 0;
}

int main(void) {
    printf("=== NoSQL Write-Ahead Log Demo ===\n\n");
    const char *path = "build/wal_demo.log";

    /* Create WAL writer */
    WALWriter *w = wal_writer_open(path, 0);
    if (!w) { printf("Failed to open WAL\n"); return 1; }

    printf("Writing 10 records to WAL...\n");
    for (int i = 0; i < 5; i++) {
        char key[32], val[64];
        snprintf(key, sizeof(key), "user:%d", i);
        snprintf(val, sizeof(val), "value_for_user_%d", i);
        wal_writer_append(w, WAL_OP_UPDATE, key, val);
    }
    wal_writer_commit(w);
    wal_writer_checkpoint(w);

    for (int i = 5; i < 10; i++) {
        char key[32], val[64];
        snprintf(key, sizeof(key), "user:%d", i);
        snprintf(val, sizeof(val), "second_batch_%d", i);
        wal_writer_append(w, WAL_OP_UPDATE, key, val);
    }

    const WALStats *st = wal_writer_stats(w);
    printf("WAL stats: %llu appends, %llu bytes, %llu since checkpoint\n",
           (unsigned long long)st->total_appends,
           (unsigned long long)st->total_bytes,
           (unsigned long long)st->records_since_checkpoint);
    wal_writer_close(w);

    /* Read back and validate */
    printf("\nReading WAL records:\n");
    WALReader *r = wal_reader_open(path);
    if (!r) { printf("Failed to open WAL for reading\n"); return 1; }

    WALRecord rec;
    int count = 0;
    while (wal_reader_next(r, &rec) == 1) {
        count++;
        if (rec.op == WAL_OP_CHECKPOINT)
            printf("  [CP] LSN=%llu\n", (unsigned long long)rec.lsn);
    }
    printf("Read %d valid records\n", count);
    wal_reader_close(r);

    /* Recovery demo */
    printf("\nRecovery replay:\n");
    int n = wal_recover(path, replay_printer, NULL);
    printf("Recovered %d records\n", n);

    remove(path);
    printf("\nWAL demo complete.\n");
    return 0;
}
