#include "wal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void wal_init(WALManager *wm) {
    wm->capacity = WAL_MAX_RECORDS;
    wm->records = malloc(sizeof(WALRecord) * wm->capacity);
    wm->num_records = 0;
    wm->next_lsn = 1;
    wm->flush_lsn = 0;
    wm->checkpoint_lsn = 0;
}

int64_t wal_write(WALManager *wm, WALRecordType type, int32_t page_id,
                  int32_t offset, int32_t txn_id,
                  const uint8_t *before, int32_t before_size,
                  const uint8_t *after,  int32_t after_size) {
    if (wm->num_records >= wm->capacity) {
        wm->capacity *= 2;
        wm->records = realloc(wm->records, sizeof(WALRecord) * wm->capacity);
    }
    WALRecord *rec = &wm->records[wm->num_records];
    rec->lsn = wm->next_lsn++;
    rec->type = type;
    rec->page_id = page_id;
    rec->offset = offset;
    rec->txn_id = txn_id;
    rec->before_size = before_size;
    rec->after_size = after_size;
    if (before && before_size > 0) {
        memcpy(rec->before_data, before,
               before_size < (int32_t)sizeof(rec->before_data) ? before_size : (int32_t)sizeof(rec->before_data));
    } else {
        rec->before_size = 0;
    }
    if (after && after_size > 0) {
        memcpy(rec->after_data, after,
               after_size < (int32_t)sizeof(rec->after_data) ? after_size : (int32_t)sizeof(rec->after_data));
    } else {
        rec->after_size = 0;
    }
    wm->num_records++;
    return rec->lsn;
}

void wal_flush(WALManager *wm, int64_t lsn) {
    int64_t max_flushed = wm->flush_lsn;
    for (int32_t i = 0; i < wm->num_records; i++) {
        if (wm->records[i].lsn <= lsn && wm->records[i].lsn > max_flushed) {
            max_flushed = wm->records[i].lsn;
        }
    }
    wm->flush_lsn = max_flushed;
}

typedef struct {
    int32_t txn_id;
    bool    committed;
} TxnStatus;

typedef struct {
    TxnStatus *txns;
    int32_t    num_txns;
    int32_t    capacity;
} TxnTable;

static TxnTable *txn_table_create(void) {
    TxnTable *tt = malloc(sizeof(TxnTable));
    tt->capacity = 256;
    tt->num_txns = 0;
    tt->txns = calloc(tt->capacity, sizeof(TxnStatus));
    return tt;
}

static void txn_table_set(TxnTable *tt, int32_t txn_id, bool committed) {
    for (int32_t i = 0; i < tt->num_txns; i++) {
        if (tt->txns[i].txn_id == txn_id) {
            tt->txns[i].committed = committed;
            return;
        }
    }
    if (tt->num_txns < tt->capacity) {
        tt->txns[tt->num_txns].txn_id = txn_id;
        tt->txns[tt->num_txns].committed = committed;
        tt->num_txns++;
    }
}

static bool txn_table_is_committed(TxnTable *tt, int32_t txn_id) {
    for (int32_t i = 0; i < tt->num_txns; i++) {
        if (tt->txns[i].txn_id == txn_id) return tt->txns[i].committed;
    }
    return false;
}

void wal_recover(WALManager *wm) {
    if (wm->num_records == 0) return;
    TxnTable *tt = txn_table_create();
    printf("[RECOVERY] Analysis phase: %d records\n", wm->num_records);
    for (int32_t i = 0; i < wm->num_records; i++) {
        WALRecord *rec = &wm->records[i];
        if (rec->type == WAL_COMMIT) {
            txn_table_set(tt, rec->txn_id, true);
            printf("  LSN %lld: COMMIT txn=%d\n", (long long)rec->lsn, rec->txn_id);
        } else if (rec->type == WAL_ABORT) {
            txn_table_set(tt, rec->txn_id, false);
            printf("  LSN %lld: ABORT txn=%d\n", (long long)rec->lsn, rec->txn_id);
        }
    }
    printf("[RECOVERY] Redo phase: redo committed transactions\n");
    for (int32_t i = 0; i < wm->num_records; i++) {
        WALRecord *rec = &wm->records[i];
        if (rec->type == WAL_INSERT || rec->type == WAL_UPDATE || rec->type == WAL_DELETE) {
            if (txn_table_is_committed(tt, rec->txn_id)) {
                printf("  REDO LSN %lld: type=%d page=%d txn=%d\n",
                       (long long)rec->lsn, rec->type, rec->page_id, rec->txn_id);
            }
        }
    }
    printf("[RECOVERY] Undo phase: undo uncommitted transactions\n");
    for (int32_t i = wm->num_records - 1; i >= 0; i--) {
        WALRecord *rec = &wm->records[i];
        if (rec->type == WAL_INSERT || rec->type == WAL_UPDATE || rec->type == WAL_DELETE) {
            if (!txn_table_is_committed(tt, rec->txn_id)) {
                printf("  UNDO LSN %lld: type=%d page=%d txn=%d\n",
                       (long long)rec->lsn, rec->type, rec->page_id, rec->txn_id);
            }
        }
    }
    printf("[RECOVERY] Complete\n");
    free(tt->txns);
    free(tt);
}

void wal_checkpoint(WALManager *wm) {
    wm->checkpoint_lsn = wm->flush_lsn;
    wal_write(wm, WAL_BEGIN_CHECKPOINT, -1, 0, -1, NULL, 0, NULL, 0);
    printf("[CHECKPOINT] LSN=%lld\n", (long long)wm->checkpoint_lsn);
}

void wal_print(WALManager *wm) {
    printf("WAL Manager: %d records, next_lsn=%lld, flush_lsn=%lld, checkpoint_lsn=%lld\n",
           wm->num_records, (long long)wm->next_lsn,
           (long long)wm->flush_lsn, (long long)wm->checkpoint_lsn);
    for (int32_t i = 0; i < wm->num_records && i < 40; i++) {
        WALRecord *rec = &wm->records[i];
        printf("  [%lld] type=%d page=%d txn=%d before=%d after=%d\n",
               (long long)rec->lsn, rec->type, rec->page_id,
               rec->txn_id, rec->before_size, rec->after_size);
    }
    if (wm->num_records > 40) printf("  ... (%d more)\n", wm->num_records - 40);
}
