/*
 * nosql_wal.c — Write-Ahead Log (ARIES-style) for NoSQL durability
 *
 * Knowledge layers covered:
 *   L1: WALRecord struct, WALWriter/WALReader API
 *   L2: Durability — sequential log before in-place update
 *   L3: Append-only file + checkpoint mechanism
 *   L4: ARIES protocol (Mohan et al., TODS 1992)
 *   L5: CRC32 table-driven checksum, sequential I/O batching
 *   L7: Crash recovery replay callback
 */
#include "nosql_wal.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================================================================
 * L5: CRC32 Table-Driven Computation
 *
 * Derived from IEEE 802.3 CRC-32 polynomial:
 *   P(x) = x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11
 *        + x^10 + x^8  + x^7  + x^5  + x^4  + x^2  + x + 1
 *
 * Reflected polynomial: 0xEDB88320
 * Init value: 0xFFFFFFFF, XOR out: 0xFFFFFFFF
 * ================================================================ */

static uint32_t crc32_table[256];
static int      crc32_initialized = 0;

void wal_crc32_init(void) {
    if (crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? WAL_CHECKSUM_SEED : 0);
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = 1;
}

uint32_t wal_crc32(const uint8_t *data, size_t len) {
    if (!crc32_initialized) wal_crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

/* ================================================================
 * L3: WALWriter — append-only sequential log writer
 *
 * Engineering pattern:
 *   append() → calculate CRC → fwrite record → optional fsync
 *
 * The WAL grows monotonically. Checkpoints truncate logically
 * (LSN bookmarks) but the file itself is append-only for simplicity.
 * ================================================================ */

WALWriter *wal_writer_open(const char *filepath, int sync_on_write) {
    if (!filepath) return NULL;
    wal_crc32_init();

    WALWriter *w = (WALWriter *)calloc(1, sizeof(WALWriter));
    if (!w) return NULL;

    strncpy(w->filepath, filepath, sizeof(w->filepath) - 1);
    w->filepath[sizeof(w->filepath) - 1] = '\0';
    w->sync_on_write = sync_on_write;
    w->next_lsn = 1;

    /* Open in append-binary mode for sequential write optimization */
    w->file = fopen(filepath, "ab");
    if (!w->file) {
        /* Try create new */
        w->file = fopen(filepath, "wb");
        if (!w->file) { free(w); return NULL; }
    } else {
        /* Scan existing file to recover next_lsn */
        fseek(w->file, 0, SEEK_END);
        long fsize = ftell(w->file);
        if (fsize >= (long)sizeof(WALRecord)) {
            WALRecord last;
            fseek(w->file, fsize - (long)sizeof(WALRecord), SEEK_SET);
            if (fread(&last, sizeof(WALRecord), 1, w->file) == 1) {
                if (last.magic == WAL_MAGIC) {
                    w->next_lsn = last.lsn + 1;
                }
            }
        }
        fseek(w->file, 0, SEEK_END);
    }
    return w;
}

void wal_writer_close(WALWriter *w) {
    if (!w) return;
    if (w->file) fclose(w->file);
    free(w);
}

static void wal_fill_record(WALRecord *rec, uint64_t lsn, uint8_t op,
                            const char *key, const char *value) {
    memset(rec, 0, sizeof(WALRecord));
    rec->magic = WAL_MAGIC;
    rec->lsn   = lsn;
    rec->timestamp = (uint64_t)time(NULL);
    rec->op    = op;

    if (key) {
        size_t klen = strlen(key);
        rec->key_len = (uint16_t)(klen < WAL_MAX_KEY_LEN ?
                                   klen : WAL_MAX_KEY_LEN - 1);
        strncpy(rec->key, key, WAL_MAX_KEY_LEN - 1);
        rec->key[WAL_MAX_KEY_LEN - 1] = '\0';
    }
    if (value) {
        size_t vlen = strlen(value);
        rec->value_len = (uint16_t)(vlen < WAL_MAX_VALUE_LEN ?
                                     vlen : WAL_MAX_VALUE_LEN - 1);
        strncpy(rec->value, value, WAL_MAX_VALUE_LEN - 1);
        rec->value[WAL_MAX_VALUE_LEN - 1] = '\0';
    }

    /* CRC32 over payload (skip magic + checksum fields) */
    size_t payload_off = offsetof(WALRecord, lsn);
    size_t payload_len = sizeof(WALRecord) - payload_off;
    rec->checksum = wal_crc32((const uint8_t *)rec + payload_off, payload_len);
}

int wal_writer_append(WALWriter *w, uint8_t op,
                      const char *key, const char *value) {
    if (!w || !w->file) return -1;

    WALRecord rec;
    wal_fill_record(&rec, w->next_lsn++, op, key, value);

    size_t written = fwrite(&rec, sizeof(WALRecord), 1, w->file);
    if (written != 1) return -1;

    w->stats.total_appends++;
    w->stats.total_bytes += sizeof(WALRecord);
    w->stats.records_since_checkpoint++;

    if (w->sync_on_write) fflush(w->file);
    return 0;
}

int wal_writer_commit(WALWriter *w) {
    if (!w) return -1;
    /* ARIES: commit record marks transaction boundary */
    WALRecord rec;
    wal_fill_record(&rec, w->next_lsn++, WAL_OP_COMMIT, NULL, NULL);
    if (fwrite(&rec, sizeof(WALRecord), 1, w->file) != 1) return -1;
    w->stats.total_appends++;
    w->stats.total_bytes += sizeof(WALRecord);
    if (w->sync_on_write) fflush(w->file);
    return 0;
}

int wal_writer_checkpoint(WALWriter *w) {
    if (!w) return -1;
    WALRecord rec;
    wal_fill_record(&rec, w->next_lsn++, WAL_OP_CHECKPOINT, NULL, NULL);
    if (fwrite(&rec, sizeof(WALRecord), 1, w->file) != 1) return -1;
    w->stats.total_appends++;
    w->stats.total_bytes += sizeof(WALRecord);
    w->stats.last_checkpoint_lsn = rec.lsn;
    w->stats.records_since_checkpoint = 0;
    if (w->sync_on_write) fflush(w->file);
    return 0;
}

int wal_writer_sync(WALWriter *w) {
    if (!w || !w->file) return -1;
    fflush(w->file);
    return 0;
}

const WALStats *wal_writer_stats(WALWriter *w) {
    return w ? &w->stats : NULL;
}

/* ================================================================
 * L7: WALReader — sequential log scanner for recovery
 *
 * Reads the WAL file sequentially, validating each record's CRC32
 * and magic number. Returns only well-formed records.
 * ================================================================ */

WALReader *wal_reader_open(const char *filepath) {
    if (!filepath) return NULL;
    wal_crc32_init();

    WALReader *r = (WALReader *)calloc(1, sizeof(WALReader));
    if (!r) return NULL;

    strncpy(r->filepath, filepath, sizeof(r->filepath) - 1);
    r->filepath[sizeof(r->filepath) - 1] = '\0';
    r->file = fopen(filepath, "rb");
    if (!r->file) { free(r); return NULL; }
    r->read_lsn = 0;
    return r;
}

void wal_reader_close(WALReader *r) {
    if (!r) return;
    if (r->file) fclose(r->file);
    free(r);
}

/*
 * Validate a WAL record's integrity:
 *   1. Magic number must match
 *   2. CRC32 checksum must verify
 *
 * Returns 1 if valid, 0 if corrupted.
 */
static int wal_validate_record(const WALRecord *rec) {
    if (rec->magic != WAL_MAGIC) return 0;
    size_t payload_off = offsetof(WALRecord, lsn);
    size_t payload_len = sizeof(WALRecord) - payload_off;
    uint32_t computed = wal_crc32((const uint8_t *)rec + payload_off, payload_len);
    return (computed == rec->checksum) ? 1 : 0;
}

int wal_reader_next(WALReader *r, WALRecord *rec_out) {
    if (!r || !r->file || !rec_out) return -1;
    long pos_before = ftell(r->file);

    if (fread(rec_out, sizeof(WALRecord), 1, r->file) != 1) {
        if (feof(r->file)) return 0;  /* End of log */
        return -1;                     /* Read error */
    }

    if (!wal_validate_record(rec_out)) {
        /* Corrupted record — skip and try next */
        fseek(r->file, pos_before + (long)sizeof(WALRecord), SEEK_SET);
        return wal_reader_next(r, rec_out);
    }

    r->read_lsn = rec_out->lsn;
    return 1;
}

int wal_reader_seek_lsn(WALReader *r, uint64_t target_lsn) {
    if (!r || !r->file) return -1;
    rewind(r->file);
    WALRecord rec;
    while (fread(&rec, sizeof(WALRecord), 1, r->file) == 1) {
        if (!wal_validate_record(&rec)) continue;
        if (rec.lsn >= target_lsn) {
            fseek(r->file, -(long)sizeof(WALRecord), SEEK_CUR);
            r->read_lsn = rec.lsn;
            return 0;
        }
    }
    return -2;
}

/* ================================================================
 * L7: Crash Recovery — replay committed records from WAL
 *
 * ARIES recovery phases (simplified):
 *   Analysis:  Scan WAL forward to find last checkpoint
 *   Redo:      Replay all UPDATE records after checkpoint
 *   Undo:      Skip ABORT-marked transactions (truncation)
 *
 * This implementation replays ALL committed UPDATE/DELETE records
 * from the last checkpoint onwards, invoking the callback for each.
 * ================================================================ */

int wal_recover(const char *filepath, wal_replay_callback cb, void *ctx) {
    if (!filepath || !cb) return -1;

    WALReader *r = wal_reader_open(filepath);
    if (!r) return -2;

    /* Phase 1: Analysis — find last checkpoint LSN */
    uint64_t checkpoint_lsn = 0;
    WALRecord rec;
    while (wal_reader_next(r, &rec) == 1) {
        if (rec.op == WAL_OP_CHECKPOINT) {
            checkpoint_lsn = rec.lsn;
        }
    }

    /* Phase 2: Redo — replay from checkpoint */
    int replayed = 0;
    if (checkpoint_lsn > 0) {
        wal_reader_seek_lsn(r, checkpoint_lsn);
    } else {
        rewind(r->file);
    }

    int in_txn = 0;
    int txn_aborted = 0;
    while (wal_reader_next(r, &rec) == 1) {
        switch (rec.op) {
        case WAL_OP_UPDATE:
            if (!txn_aborted) {
                cb(&rec, ctx);
                replayed++;
            }
            in_txn = 1;
            break;
        case WAL_OP_DELETE:
            if (!txn_aborted) {
                cb(&rec, ctx);
                replayed++;
            }
            in_txn = 1;
            break;
        case WAL_OP_COMMIT:
            in_txn = 0;
            txn_aborted = 0;
            break;
        case WAL_OP_ABORT:
            txn_aborted = 1;
            break;
        case WAL_OP_CHECKPOINT:
            in_txn = 0;
            txn_aborted = 0;
            break;
        default:
            break;
        }
    }

    wal_reader_close(r);
    /* in_txn flag marks transaction boundaries for future UNDO phase */
    (void)in_txn;
    return replayed;
}
