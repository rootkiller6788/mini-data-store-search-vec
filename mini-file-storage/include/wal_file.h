#ifndef MINI_WAL_FILE_H
#define MINI_WAL_FILE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define WAL_MAGIC        0xC0A1B0C4
#define WAL_RECORD_PUT   0x01
#define WAL_RECORD_DEL   0x02
#define WAL_BLOCK_SIZE   32768   /* 32 KB write granularity */

/* ─────────────────────────────────────────────
   WAL Record (on-disk layout)
   ───────────────────────────────────────────── */
typedef struct {
    uint32_t checksum;           /* crc32c of payload            */
    uint32_t length;             /* payload length (key+value+1) */
    uint8_t  type;               /* PUT or DELETE                */
    uint32_t key_len;
    uint32_t value_len;
    uint8_t  key[256];
    uint8_t  value[1024];
} WALRecord;

/* ─────────────────────────────────────────────
   WAL Writer handle
   ───────────────────────────────────────────── */
typedef struct {
    FILE    *file;
    char    *filename;
    int      fd;
    uint32_t current_offset;
    uint64_t record_count;
} WALWriter;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

WALWriter *wal_open(const char *filename);
int        wal_append(WALWriter *wal, uint8_t type,
                     const uint8_t *key, uint32_t key_len,
                     const uint8_t *value, uint32_t value_len);
int        wal_sync(WALWriter *wal);
int        wal_recover(WALWriter *wal,
                       int (*replay_fn)(uint8_t type,
                                        const uint8_t *key, uint32_t key_len,
                                        const uint8_t *value, uint32_t value_len,
                                        void *ctx),
                       void *ctx);
void       wal_close(WALWriter *wal);

#endif /* MINI_WAL_FILE_H */
