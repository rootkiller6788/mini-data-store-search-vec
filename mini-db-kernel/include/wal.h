#ifndef WAL_H
#define WAL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define WAL_MAX_RECORDS  10000
#define WAL_PAGE_SIZE    4096

typedef enum {
    WAL_INSERT           = 0,
    WAL_UPDATE           = 1,
    WAL_DELETE           = 2,
    WAL_COMMIT           = 3,
    WAL_ABORT            = 4,
    WAL_BEGIN_CHECKPOINT = 5
} WALRecordType;

typedef struct {
    int64_t        lsn;
    WALRecordType  type;
    int32_t        page_id;
    int32_t        offset;
    int32_t        txn_id;
    uint8_t        before_data[WAL_PAGE_SIZE];
    uint8_t        after_data[WAL_PAGE_SIZE];
    int32_t        before_size;
    int32_t        after_size;
} WALRecord;

typedef struct {
    WALRecord     *records;
    int32_t        num_records;
    int32_t        capacity;
    int64_t        next_lsn;
    int64_t        flush_lsn;
    int64_t        checkpoint_lsn;
} WALManager;

void wal_init(WALManager *wm);
int64_t wal_write(WALManager *wm, WALRecordType type, int32_t page_id,
                  int32_t offset, int32_t txn_id,
                  const uint8_t *before, int32_t before_size,
                  const uint8_t *after,  int32_t after_size);
void wal_flush(WALManager *wm, int64_t lsn);
void wal_recover(WALManager *wm);
void wal_checkpoint(WALManager *wm);
void wal_print(WALManager *wm);

#endif
