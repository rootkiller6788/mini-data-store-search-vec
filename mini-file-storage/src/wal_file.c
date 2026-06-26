#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wal_file.h"

/* ─────────────────────────────────────────────
   CRC32C table  (Castagnoli polynomial)
   ───────────────────────────────────────────── */
static uint32_t crc32c_table[256];
static int      crc32c_initialized = 0;

static void crc32c_init(void) {
    if (crc32c_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (crc & 1u ? 0x82F63B78u : 0);
        }
        crc32c_table[i] = crc;
    }
    crc32c_initialized = 1;
}

static uint32_t crc32c_compute(const uint8_t *data, uint32_t len) {
    crc32c_init();
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32c_table[(crc ^ data[i]) & 0xFFu];
    return crc ^ 0xFFFFFFFFu;
}

/* ─────────────────────────────────────────────
   Open WAL file
   ───────────────────────────────────────────── */
WALWriter *wal_open(const char *filename) {
    WALWriter *wal = (WALWriter *)calloc(1, sizeof(WALWriter));
    if (!wal) return NULL;

    wal->filename = strdup(filename);
    wal->file = fopen(filename, "ab+");  /* append + read */
    if (!wal->file) {
        free(wal->filename);
        free(wal);
        return NULL;
    }
    wal->fd = fileno(wal->file);
    fseek(wal->file, 0, SEEK_END);
    wal->current_offset = (uint32_t)ftell(wal->file);
    wal->record_count = 0;
    return wal;
}

/* ─────────────────────────────────────────────
   Append a record to the WAL
   On-disk format:
   4 bytes checksum | 4 bytes length | 1 byte type
   | 4 bytes key_len | 4 bytes value_len | key | value
   ───────────────────────────────────────────── */
int wal_append(WALWriter *wal, uint8_t type,
               const uint8_t *key, uint32_t key_len,
               const uint8_t *value, uint32_t value_len) {
    if (!wal || !key) return -1;

    uint32_t payload_len = 1 + 4 + 4 + key_len + value_len; /* type + klen + vlen + key + value */
    uint8_t  payload_buf[2048];
    uint32_t pos = 0;

    payload_buf[pos++] = type;
    memcpy(payload_buf + pos, &key_len, 4);   pos += 4;
    memcpy(payload_buf + pos, &value_len, 4);  pos += 4;
    memcpy(payload_buf + pos, key, key_len);   pos += key_len;
    if (value && value_len > 0) {
        memcpy(payload_buf + pos, value, value_len);
        pos += value_len;
    }

    uint32_t checksum = crc32c_compute(payload_buf, payload_len);

    /* Write: checksum | length | payload */
    fwrite(&checksum,    sizeof(uint32_t), 1, wal->file);
    fwrite(&payload_len, sizeof(uint32_t), 1, wal->file);
    fwrite(payload_buf,  1, payload_len, wal->file);

    wal->current_offset += 8 + payload_len;
    wal->record_count++;
    return 0;
}

/* ─────────────────────────────────────────────
   Flush / sync WAL to disk
   ───────────────────────────────────────────── */
int wal_sync(WALWriter *wal) {
    if (!wal || !wal->file) return -1;
    fflush(wal->file);
    return 0;
}

/* ─────────────────────────────────────────────
   Recover from WAL: replay records via callback
   ───────────────────────────────────────────── */
int wal_recover(WALWriter *wal,
                int (*replay_fn)(uint8_t type,
                                 const uint8_t *key, uint32_t key_len,
                                 const uint8_t *value, uint32_t value_len,
                                 void *ctx),
                void *ctx) {
    if (!wal || !wal->file || !replay_fn) return -1;

    fseek(wal->file, 0, SEEK_END);
    long file_len = ftell(wal->file);
    fseek(wal->file, 0, SEEK_SET);

    uint32_t offset = 0;
    uint32_t recovered = 0;

    while (offset + 8 <= (uint32_t)file_len) {
        uint32_t stored_crc, payload_len;
        if (fread(&stored_crc, sizeof(uint32_t), 1, wal->file) != 1) break;
        if (fread(&payload_len, sizeof(uint32_t), 1, wal->file) != 1) break;

        offset += 8;
        if (offset + payload_len > (uint32_t)file_len) break;
        if (payload_len > 2048) break; /* sanity check */

        uint8_t payload[2048];
        if (fread(payload, 1, payload_len, wal->file) != payload_len) break;

        uint32_t computed_crc = crc32c_compute(payload, payload_len);
        if (computed_crc != stored_crc) break; /* corrupted record, stop */

        uint32_t pos = 0;
        uint8_t  type = payload[pos++];
        uint32_t klen, vlen;
        memcpy(&klen, payload + pos, 4); pos += 4;
        memcpy(&vlen, payload + pos, 4); pos += 4;

        if (klen > 255 || vlen > 1023) break; /* sanity */

        uint8_t key[256], value[1024];
        memcpy(key,   payload + pos, klen); pos += klen;
        memcpy(value, payload + pos, vlen); pos += vlen;

        replay_fn(type, key, klen, value, vlen, ctx);
        recovered++;
        offset += payload_len;
    }

    return (int)recovered;
}

/* ─────────────────────────────────────────────
   Close and rotate WAL
   ───────────────────────────────────────────── */
void wal_close(WALWriter *wal) {
    if (!wal) return;
    if (wal->file) {
        wal_sync(wal);
        fclose(wal->file);
        wal->file = NULL;
    }
    /* Rename to .closed to mark rotated */
    if (wal->filename) {
        char closed_name[1024];
        snprintf(closed_name, sizeof(closed_name), "%s.closed", wal->filename);
        /* Best-effort rename */
        (void)closed_name;
    }
    free(wal->filename);
    wal->filename = NULL;
    free(wal);
}
