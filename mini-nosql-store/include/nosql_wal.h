#ifndef NOSQL_WAL_H
#define NOSQL_WAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/*
 * Write-Ahead Log (WAL) — ARIES-style durability for NoSQL KV stores
 *
 * Theorem (ARIES, Mohan et al. 1992):
 *   Write-Ahead Logging ensures atomicity and durability by recording
 *   all modifications in a sequential log BEFORE applying them to the
 *   main data store. Recovery replays log from last checkpoint.
 *
 * Record types follow ARIES protocol:
 *   - UPDATE:  Key-value mutation
 *   - COMMIT:  Transaction boundary
 *   - ABORT:   Rollback marker
 *   - CHECKPOINT: Recovery acceleration point
 *
 * Complexity: O(1) append, O(N) recovery scan
 */

#define WAL_MAX_KEY_LEN    64
#define WAL_MAX_VALUE_LEN 256
#define WAL_RECORD_SIZE   512
#define WAL_MAGIC         0x57414C01  /* "WAL" + version 1 */
#define WAL_CHECKSUM_SEED 0xEDB88320

typedef enum wal_op_t {
    WAL_OP_UPDATE     = 0x01,
    WAL_OP_DELETE     = 0x02,
    WAL_OP_COMMIT     = 0x03,
    WAL_OP_ABORT      = 0x04,
    WAL_OP_CHECKPOINT = 0x05
} WALOp;

typedef struct wal_record_t {
    uint32_t magic;          /* Magic number for validation */
    uint32_t checksum;       /* CRC32 of payload */
    uint64_t lsn;            /* Log Sequence Number (monotonic) */
    uint64_t timestamp;      /* Wall-clock time of write */
    uint8_t  op;             /* WALOp enum */
    uint16_t key_len;
    uint16_t value_len;
    char     key[WAL_MAX_KEY_LEN];
    char     value[WAL_MAX_VALUE_LEN];
    uint32_t padding;        /* Align to 512 bytes */
} WALRecord;

typedef struct wal_stats_t {
    uint64_t total_appends;
    uint64_t total_bytes;
    uint64_t records_since_checkpoint;
    uint64_t last_checkpoint_lsn;
} WALStats;

typedef struct wal_writer_t {
    FILE    *file;
    char     filepath[512];
    uint64_t next_lsn;
    int      sync_on_write;
    WALStats stats;
} WALWriter;

typedef struct wal_reader_t {
    FILE    *file;
    char     filepath[512];
    uint64_t read_lsn;
} WALReader;

/*
 * CRC32 checksum using IEEE 802.3 polynomial (L5: Error Detection)
 *
 * Algorithm: Table-driven CRC32 (Koopman, 2002)
 * Polynomial: 0xEDB88320 (reflected)
 * Used in: Ethernet, gzip, PNG, WAL integrity
 */
uint32_t wal_crc32(const uint8_t *data, size_t len);
void     wal_crc32_init(void);

/* WAL file lifecycle */
WALWriter *wal_writer_open(const char *filepath, int sync_on_write);
void       wal_writer_close(WALWriter *w);
int        wal_writer_append(WALWriter *w, uint8_t op,
                             const char *key, const char *value);
int        wal_writer_commit(WALWriter *w);
int        wal_writer_checkpoint(WALWriter *w);
int        wal_writer_sync(WALWriter *w);
const WALStats *wal_writer_stats(WALWriter *w);

WALReader *wal_reader_open(const char *filepath);
void       wal_reader_close(WALReader *r);
int        wal_reader_next(WALReader *r, WALRecord *rec_out);
int        wal_reader_seek_lsn(WALReader *r, uint64_t target_lsn);

/*
 * Recovery interface (L7: Crash Recovery Application)
 *
 * Replays all committed records from the WAL to rebuild in-memory state.
 * Skips aborted transactions and records after last checkpoint.
 */
typedef int (*wal_replay_callback)(const WALRecord *rec, void *ctx);
int wal_recover(const char *filepath, wal_replay_callback cb, void *ctx);

#endif
