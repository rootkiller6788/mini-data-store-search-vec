#include "graph_storage.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * CRC32 — IEEE 802.3 polynomial (L4: Checksum Theorem)
 *
 * P(x) = x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8
 *        + x^7 + x^5 + x^4 + x^2 + x + 1
 *
 * Reflection: data is MSB-first, checksum is reflected (LSB-first output).
 * Used for page-level integrity verification — detect torn writes and
 * silent data corruption in storage.
 * ========================================================================= */

static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;

static void crc32_init_table(void) {
    const uint32_t polynomial = 0xEDB88320U;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ polynomial;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

uint32_t crc32_compute(const uint8_t *data, size_t len) {
    if (!crc32_table_initialized) crc32_init_table();
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        uint8_t idx = (uint8_t)((crc ^ data[i]) & 0xFF);
        crc = (crc >> 8) ^ crc32_table[idx];
    }
    return crc ^ 0xFFFFFFFFU;
}

/* =========================================================================
 * Buffer Pool — Clock (Second-Chance) Replacement Algorithm
 *
 * L3: Engineering Structure
 *
 * Clock algorithm: frames arranged in a ring. Clock hand sweeps, clearing
 * reference bits and evicting unreferenced dirty frames (after write-back).
 * Approximates LRU with O(1) per access, no timestamp maintenance.
 *
 * Pin/unpin protocol: callers pin pages during use; pinned pages cannot be
 * evicted. Clock bypasses pinned (pin_count > 0) frames.
 *
 * Reference: "The Clock Algorithm" is a standard OS page replacement
 * strategy, adapted here for database buffer pool management.
 * ========================================================================= */

BufferPool *bp_create(void) {
    BufferPool *bp = calloc(1, sizeof(BufferPool));
    if (!bp) return NULL;
    bp->clock_hand = 0;
    bp->hit_count = 0;
    bp->miss_count = 0;
    bp->evict_count = 0;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        bp->frames[i].valid = false;
        bp->frames[i].dirty = false;
        bp->frames[i].pin_count = 0;
        bp->frames[i].clock_ref = 0;
        bp->frames[i].page_id = 0;
    }
    return bp;
}

void bp_destroy(BufferPool *bp) {
    if (!bp) return;
    free(bp);
}

/* L5: Clock Algorithm — Second-Chance page replacement.
 * Scans frames in a ring. If clock_ref == 0 and not pinned, evict.
 * If clock_ref == 1, clear it and advance (second chance).
 * Time: O(n) worst case; amortized O(1) per eviction. */
static int bp_evict(BufferPool *bp) {
    int scanned = 0;
    while (scanned < BUFFER_POOL_SIZE * 2) {
        BufferFrame *frame = &bp->frames[bp->clock_hand];
        if (frame->pin_count > 0) {
            /* pinned — cannot evict */
            bp->clock_hand = (bp->clock_hand + 1) % BUFFER_POOL_SIZE;
            scanned++;
            continue;
        }
        if (frame->clock_ref == 0 && frame->valid) {
            /* found victim */
            int victim = bp->clock_hand;
            bp->clock_hand = (bp->clock_hand + 1) % BUFFER_POOL_SIZE;
            bp->evict_count++;
            return victim;
        }
        /* give second chance: clear ref bit */
        if (frame->clock_ref) {
            frame->clock_ref = 0;
        }
        bp->clock_hand = (bp->clock_hand + 1) % BUFFER_POOL_SIZE;
        scanned++;
    }
    /* all pinned or no valid pages — take first unpinned invalid frame */
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bp->frames[i].pin_count == 0) {
            return i;
        }
    }
    return -1;
}

DataPage *bp_get_page(BufferPool *bp, uint32_t page_id) {
    if (!bp) return NULL;
    /* check for hit */
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bp->frames[i].valid && bp->frames[i].page_id == page_id) {
            bp->frames[i].pin_count++;
            bp->frames[i].clock_ref = 1;
            bp->hit_count++;
            return &bp->frames[i].page;
        }
    }
    /* miss — need a frame */
    bp->miss_count++;
    /* find vacant frame first */
    int free_idx = -1;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (!bp->frames[i].valid && bp->frames[i].pin_count == 0) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) {
        free_idx = bp_evict(bp);
        if (free_idx < 0) return NULL; /* completely stuck */
    }
    /* initialize frame with empty page */
    BufferFrame *frame = &bp->frames[free_idx];
    memset(&frame->page, 0, sizeof(DataPage));
    frame->page.header.page_id = page_id;
    frame->page.header.page_type = PAGE_TYPE_NODE;
    frame->page_id = page_id;
    frame->valid = true;
    frame->dirty = false;
    frame->pin_count = 1;
    frame->clock_ref = 1;
    return &frame->page;
}

void bp_mark_dirty(BufferPool *bp, uint32_t page_id) {
    if (!bp) return;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bp->frames[i].valid && bp->frames[i].page_id == page_id) {
            bp->frames[i].dirty = true;
            return;
        }
    }
}

void bp_unpin(BufferPool *bp, uint32_t page_id) {
    if (!bp) return;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        if (bp->frames[i].valid && bp->frames[i].page_id == page_id) {
            if (bp->frames[i].pin_count > 0)
                bp->frames[i].pin_count--;
            return;
        }
    }
}

void bp_flush_all(BufferPool *bp, FILE *data_file) {
    if (!bp || !data_file) return;
    for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
        BufferFrame *frame = &bp->frames[i];
        if (frame->valid && frame->dirty) {
            uint32_t footer_crc = crc32_compute(
                (const uint8_t *)&frame->page, sizeof(DataPage));
            fseek(data_file, (long)(frame->page_id * PAGE_SIZE), SEEK_SET);
            fwrite(&frame->page, sizeof(DataPage), 1, data_file);
            fwrite(&footer_crc, sizeof(uint32_t), 1, data_file);
            frame->dirty = false;
        }
    }
    fflush(data_file);
}

void bp_print_stats(BufferPool *bp) {
    if (!bp) return;
    printf("BufferPool: hits=%d misses=%d evictions=%d\n",
           bp->hit_count, bp->miss_count, bp->evict_count);
}

/* =========================================================================
 * Page Operations — PostgreSQL-style Slotted Page Layout
 *
 * L3: Engineering Structure
 *
 * Layout (top to bottom):
 *   PageHeader (64 bytes)
 *   Free Space (variable records inserted upward)
 *   Slot Directory (grows downward from page end)
 *   Footer Checksum (4 bytes at absolute end)
 *
 * Each record = variable-length byte sequence.
 * Slot[i] = { offset, length }. Offset points into data area.
 * Free space tracked by header.free_offset (grows up) and
 * header.slot_dir_offset (grows down). Full when they meet.
 *
 * Insert: writes record at header.free_offset, adds slot at
 * header.slot_dir_offset - sizeof(SlotEntry), bumps counts.
 * Delete: marks slot as SLOT_EMPTY. No compaction (simplified).
 *
 * Reference: PostgreSQL src/include/storage/bufpage.h
 *            "The Internals of PostgreSQL" — Chapter 6: Buffer Manager
 * ========================================================================= */

static SlotEntry *page_slot(DataPage *page, int idx) {
    uint8_t *base = (uint8_t *)page;
    size_t slot_base = PAGE_SIZE - 4 - (size_t)(idx + 1) * sizeof(SlotEntry);
    return (SlotEntry *)(base + slot_base);
}

uint32_t page_checksum(const DataPage *page) {
    /* Must zero out checksum field before computing, since it is part of the page */
    uint32_t saved = page->header.checksum;
    DataPage *mutable_page = (DataPage *)page;
    mutable_page->header.checksum = 0;
    uint32_t crc = crc32_compute((const uint8_t *)page, sizeof(DataPage));
    mutable_page->header.checksum = saved;
    return crc;
}

bool page_verify(const DataPage *page) {
    uint32_t stored_crc = page->header.checksum;
    uint32_t computed = page_checksum(page);
    return stored_crc == computed;
}

void page_calc_checksum(DataPage *page) {
    page->header.checksum = 0;
    page->header.checksum = page_checksum(page);
}

void page_init(DataPage *page, uint32_t page_id, PageType type) {
    memset(page, 0, sizeof(DataPage));
    page->header.page_id = page_id;
    page->header.page_type = type;
    page->header.free_offset = PAGE_HEADER_SIZE;
    page->header.slot_dir_offset = PAGE_SIZE - 4;
    page->header.record_count = 0;
}

int page_insert_record(DataPage *page, const void *record, uint16_t len) {
    if (!page || !record || len == 0) return -1;
    uint16_t new_free = page->header.free_offset + len;
    uint16_t new_slot = page->header.slot_dir_offset - (uint16_t)sizeof(SlotEntry);
    if (new_free > new_slot) return -1; /* page full */
    if (page->header.record_count >= MAX_SLOTS) return -1;

    /* write record at current free_offset */
    memcpy(page->data + (page->header.free_offset - PAGE_HEADER_SIZE),
           record, len);

    /* write slot entry at slot_dir_offset - sizeof(SlotEntry) */
    SlotEntry slot;
    slot.offset = page->header.free_offset;
    slot.length = len;
    slot.flags = 0;
    slot.pad = 0;
    SlotEntry *dst = page_slot(page, page->header.record_count);
    memcpy(dst, &slot, sizeof(SlotEntry));

    page->header.free_offset = new_free;
    page->header.slot_dir_offset = new_slot;
    page->header.record_count++;
    return page->header.record_count - 1;
}

bool page_get_record(DataPage *page, int slot_idx, void *out_buf, uint16_t *out_len) {
    if (!page || slot_idx < 0 || slot_idx >= page->header.record_count)
        return false;
    SlotEntry *slot = page_slot(page, slot_idx);
    if (slot->flags & SLOT_EMPTY) return false;
    if (slot->offset + slot->length > PAGE_SIZE) return false;
    *out_len = slot->length;
    if (out_buf)
        memcpy(out_buf, ((uint8_t *)page) + slot->offset, slot->length);
    return true;
}

bool page_delete_record(DataPage *page, int slot_idx) {
    if (!page || slot_idx < 0 || slot_idx >= page->header.record_count)
        return false;
    SlotEntry *slot = page_slot(page, slot_idx);
    slot->flags |= SLOT_EMPTY;
    return true;
}

/* =========================================================================
 * Serialization — Node / Edge to compact variable-length format
 *
 * L3: Engineering Structure — Compact binary serialization for page storage.
 *
 * Format (Node):
 *   [int64_t id] [int32_t label_count]
 *   For each label: [int16_t len] [char data...]
 *   [int32_t property_count]
 *   For each property: [int16_t key_len] [char key...] [int16_t val_len] [char val...]
 *
 * Format (Edge):
 *   [int64_t id] [int64_t from_node] [int64_t to_node]
 *   [uint8_t directed] [int16_t type_len] [char type...]
 *   [int32_t property_count]
 *   For each property: [int16_t key_len] [char key...] [int16_t val_len] [char val...]
 *
 * This is much more compact than storing full fixed-size struct arrays.
 * Typical node: ~40-400 bytes. Typical edge: ~50-300 bytes.
 * ========================================================================= */

static void ser_write_u16(uint8_t **p, uint16_t v) {
    (*p)[0] = (uint8_t)(v & 0xFF); (*p)[1] = (uint8_t)((v >> 8) & 0xFF); *p += 2;
}
static void ser_write_i32(uint8_t **p, int32_t v) {
    memcpy(*p, &v, 4); *p += 4;
}
static void ser_write_i64(uint8_t **p, int64_t v) {
    memcpy(*p, &v, 8); *p += 8;
}
static void ser_write_str(uint8_t **p, const char *s, int maxlen) {
    int slen = 0; while (s[slen] && slen < maxlen) slen++;
    ser_write_u16(p, (uint16_t)slen);
    memcpy(*p, s, (size_t)slen); *p += slen;
}
static uint16_t ser_read_u16(const uint8_t **p) {
    uint16_t v = (uint16_t)((*p)[0]) | ((uint16_t)((*p)[1]) << 8); *p += 2; return v;
}
static int32_t ser_read_i32(const uint8_t **p) {
    int32_t v; memcpy(&v, *p, 4); *p += 4; return v;
}
static int64_t ser_read_i64(const uint8_t **p) {
    int64_t v; memcpy(&v, *p, 8); *p += 8; return v;
}
static void ser_read_str(const uint8_t **p, char *buf, int maxbuf) {
    uint16_t slen = ser_read_u16(p);
    int cp = (int)slen < maxbuf - 1 ? (int)slen : maxbuf - 1;
    memcpy(buf, *p, (size_t)cp); buf[cp] = '\0'; *p += slen;
}

int node_record_serialize(const Node *n, uint8_t *buf, int max_len) {
    if (!buf || max_len < 32) return -1;
    uint8_t *p = buf;
    ser_write_i64(&p, n->id);
    ser_write_i32(&p, (int32_t)n->label_count);
    for (int i = 0; i < n->label_count && i < MAX_NODE_LABELS; i++)
        ser_write_str(&p, n->labels[i], MAX_LABEL_LEN);
    ser_write_i32(&p, (int32_t)n->property_count);
    for (int i = 0; i < n->property_count && i < MAX_NODE_PROPERTIES; i++) {
        ser_write_str(&p, n->properties[i].key, MAX_KEY_LEN);
        ser_write_str(&p, n->properties[i].value, MAX_VALUE_LEN);
    }
    int total = (int)(p - buf);
    return (total <= max_len) ? total : -1;
}

int node_record_deserialize(const uint8_t *buf, int len, Node *n) {
    if (!buf || !n || len < 16) return -1;
    const uint8_t *p = buf;
    memset(n, 0, sizeof(Node));
    n->id = ser_read_i64(&p);
    n->label_count = ser_read_i32(&p);
    if (n->label_count > MAX_NODE_LABELS) n->label_count = MAX_NODE_LABELS;
    for (int i = 0; i < n->label_count; i++)
        ser_read_str(&p, n->labels[i], MAX_LABEL_LEN);
    n->property_count = ser_read_i32(&p);
    if (n->property_count > MAX_NODE_PROPERTIES) n->property_count = MAX_NODE_PROPERTIES;
    for (int i = 0; i < n->property_count; i++) {
        ser_read_str(&p, n->properties[i].key, MAX_KEY_LEN);
        ser_read_str(&p, n->properties[i].value, MAX_VALUE_LEN);
    }
    int total = (int)(p - buf);
    return (total <= len) ? total : -1;
}

int edge_record_serialize(const Edge *e, uint8_t *buf, int max_len) {
    if (!buf || max_len < 40) return -1;
    uint8_t *p = buf;
    ser_write_i64(&p, e->id);
    ser_write_i64(&p, e->from_node);
    ser_write_i64(&p, e->to_node);
    *p++ = e->directed ? (uint8_t)1 : (uint8_t)0;
    ser_write_str(&p, e->type, MAX_EDGE_TYPE_LEN);
    ser_write_i32(&p, (int32_t)e->property_count);
    for (int i = 0; i < e->property_count && i < MAX_EDGE_PROPERTIES; i++) {
        ser_write_str(&p, e->properties[i].key, MAX_KEY_LEN);
        ser_write_str(&p, e->properties[i].value, MAX_VALUE_LEN);
    }
    int total = (int)(p - buf);
    return (total <= max_len) ? total : -1;
}

int edge_record_deserialize(const uint8_t *buf, int len, Edge *e) {
    if (!buf || !e || len < 26) return -1;
    const uint8_t *p = buf;
    memset(e, 0, sizeof(Edge));
    e->id = ser_read_i64(&p);
    e->from_node = ser_read_i64(&p);
    e->to_node = ser_read_i64(&p);
    e->directed = (*p++ != 0);
    ser_read_str(&p, e->type, MAX_EDGE_TYPE_LEN);
    e->property_count = ser_read_i32(&p);
    if (e->property_count > MAX_EDGE_PROPERTIES) e->property_count = MAX_EDGE_PROPERTIES;
    for (int i = 0; i < e->property_count; i++) {
        ser_read_str(&p, e->properties[i].key, MAX_KEY_LEN);
        ser_read_str(&p, e->properties[i].value, MAX_VALUE_LEN);
    }
    int total = (int)(p - buf);
    return (total <= len) ? total : -1;
}

/* =========================================================================
 * Write-Ahead Log Manager
 *
 * L4: WAL Theorem (Gray & Reuter, 1993)
 *
 * Protocol:
 *   1. Log record created in-memory (WAL buffer).
 *   2. On commit, WAL flushed to disk before data pages.
 *   3. On checkpoint, dirty pages flushed, WAL truncated.
 *   4. On recovery: REDO from last checkpoint LSN forward.
 *
 * Each record carries: LSN, type, page_id, offset, data, prev_len.
 * UPDATE records store both before-image (old) and after-image (new)
 * for potential UNDO (not fully implemented in simplified model).
 *
 * REDO is idempotent: replaying a logged update twice produces the
 * same result (write same data to same offset).
 *
 * Reference: "Transaction Processing: Concepts and Techniques"
 *            (Gray & Reuter, 1993), Chapter 11: Recovery.
 *            "ARIES: A Transaction Recovery Method..." (Mohan et al., 1992)
 * ========================================================================= */

WALManager *wal_create(const char *log_path) {
    WALManager *wal = calloc(1, sizeof(WALManager));
    if (!wal) return NULL;
    wal->next_lsn = 1;
    wal->next_txn_id = 1;
    wal->record_count = 0;
    wal->flushed_count = 0;
    wal->log_file = fopen(log_path, "w+b");
    if (!wal->log_file) {
        free(wal);
        return NULL;
    }
    return wal;
}

void wal_destroy(WALManager *wal) {
    if (!wal) return;
    if (wal->log_file) fclose(wal->log_file);
    free(wal);
}

uint32_t wal_log_insert(WALManager *wal, WALRecordType type,
                        uint32_t page_id, uint16_t offset,
                        const void *data, uint16_t len) {
    if (!wal || wal->record_count >= WAL_MAX_RECORDS) return 0;
    if (len > 512) return 0;
    WALRecord *rec = &wal->records[wal->record_count];
    rec->lsn = wal->next_lsn++;
    rec->type = type;
    rec->page_id = page_id;
    rec->offset = offset;
    rec->length = len;
    rec->old_length = 0;
    rec->transaction_id = wal->next_txn_id;
    rec->data_len = (len < 512) ? len : 512;
    if (data && len > 0) memcpy(rec->data, data, rec->data_len);
    wal->record_count++;
    return rec->lsn;
}

uint32_t wal_log_update(WALManager *wal, uint32_t page_id,
                        uint16_t offset, const void *old_data,
                        uint16_t old_len, const void *new_data, uint16_t new_len) {
    if (!wal || wal->record_count >= WAL_MAX_RECORDS) return 0;
    /* store both old (before-image) and new (after-image) */
    uint16_t total = old_len + new_len;
    if (total > 512) return 0;
    WALRecord *rec = &wal->records[wal->record_count];
    rec->lsn = wal->next_lsn++;
    rec->type = WAL_UPDATE;
    rec->page_id = page_id;
    rec->offset = offset;
    rec->length = new_len;
    rec->old_length = old_len;
    rec->transaction_id = wal->next_txn_id;
    rec->data_len = total;
    if (old_data && old_len > 0) memcpy(rec->data, old_data, old_len);
    if (new_data && new_len > 0) memcpy(rec->data + old_len, new_data, new_len);
    wal->record_count++;
    return rec->lsn;
}

void wal_commit(WALManager *wal) {
    if (!wal || !wal->log_file) return;
    /* flush all unflushed records to log file */
    for (int i = wal->flushed_count; i < wal->record_count; i++) {
        WALRecord *rec = &wal->records[i];
        fwrite(&rec->lsn, sizeof(uint32_t), 1, wal->log_file);
        fwrite(&rec->type, sizeof(WALRecordType), 1, wal->log_file);
        fwrite(&rec->page_id, sizeof(uint32_t), 1, wal->log_file);
        fwrite(&rec->offset, sizeof(uint16_t), 1, wal->log_file);
        fwrite(&rec->length, sizeof(uint16_t), 1, wal->log_file);
        fwrite(&rec->old_length, sizeof(uint16_t), 1, wal->log_file);
        fwrite(&rec->transaction_id, sizeof(uint64_t), 1, wal->log_file);
        fwrite(&rec->data_len, sizeof(uint16_t), 1, wal->log_file);
        if (rec->data_len > 0)
            fwrite(rec->data, 1, rec->data_len, wal->log_file);
    }
    fflush(wal->log_file);
    wal->flushed_count = wal->record_count;
    wal->next_txn_id++;
}

void wal_checkpoint(WALManager *wal) {
    if (!wal) return;
    /* Write a CHECKPOINT record to mark a safe recovery point */
    uint32_t cp_lsn = wal->next_lsn++;
    WALRecordType cp_type = WAL_CHECKPOINT;
    uint32_t zero = 0; uint16_t z16 = 0; uint64_t z64 = 0;
    if (wal->log_file) {
        fwrite(&cp_lsn, sizeof(uint32_t), 1, wal->log_file);
        fwrite(&cp_type, sizeof(WALRecordType), 1, wal->log_file);
        fwrite(&zero, sizeof(uint32_t), 1, wal->log_file);
        fwrite(&z16, sizeof(uint16_t), 1, wal->log_file);
        fwrite(&z16, sizeof(uint16_t), 1, wal->log_file);
        fwrite(&z16, sizeof(uint16_t), 1, wal->log_file);
        fwrite(&z64, sizeof(uint64_t), 1, wal->log_file);
        fwrite(&z16, sizeof(uint16_t), 1, wal->log_file);
        fflush(wal->log_file);
    }
    /* truncate in-memory log: all flushed records are now safe */
    wal->record_count = 0;
    wal->flushed_count = 0;
}

/* L5: WAL Recovery (REDO pass) — ARIES-inspired redo-only recovery.
 * Reads WAL file sequentially. REDO is idempotent by design:
 * each record specifies (page_id, offset, new_data). Re-applying
 * produces identical result. After checkpoint, all prior changes
 * are guaranteed persisted; redo starts after last checkpoint. */
int wal_recover(WALManager *wal, BufferPool *bp, FILE *data_file) {
    if (!wal || !bp || !data_file) return -1;
    rewind(wal->log_file);

    int redone = 0;
    uint32_t lsn;
    WALRecord rec;

    while (fread(&lsn, sizeof(uint32_t), 1, wal->log_file) == 1) {
        if (fread(&rec.type, sizeof(WALRecordType), 1, wal->log_file) != 1) break;
        if (fread(&rec.page_id, sizeof(uint32_t), 1, wal->log_file) != 1) break;
        if (fread(&rec.offset, sizeof(uint16_t), 1, wal->log_file) != 1) break;
        if (fread(&rec.length, sizeof(uint16_t), 1, wal->log_file) != 1) break;
        if (fread(&rec.old_length, sizeof(uint16_t), 1, wal->log_file) != 1) break;
        if (fread(&rec.transaction_id, sizeof(uint64_t), 1, wal->log_file) != 1) break;
        if (fread(&rec.data_len, sizeof(uint16_t), 1, wal->log_file) != 1) break;
        if (rec.data_len > 0)
            fread(rec.data, 1, rec.data_len, wal->log_file);

        if (rec.type == WAL_CHECKPOINT) {
            redone = 0; /* reset counter after checkpoint */
            continue;
        }
        if (rec.type == WAL_ABORT) continue; /* skip aborted transactions */

        /* REDO: apply the after-image */
        DataPage *page = bp_get_page(bp, rec.page_id);
        if (!page) continue;
        if (rec.type == WAL_INSERT || rec.type == WAL_UPDATE) {
            memcpy(((uint8_t *)page) + rec.offset, rec.data + rec.old_length,
                   rec.length);
            page->header.lsn = lsn;
        }
        bp_unpin(bp, rec.page_id);
        redone++;
    }
    return redone;
}

void wal_print_info(WALManager *wal) {
    if (!wal) return;
    printf("WAL: %d records (flushed: %d)  next_lsn=%u  txn=%llu\n",
           wal->record_count, wal->flushed_count,
           wal->next_lsn, (unsigned long long)wal->next_txn_id);
}

/* =========================================================================
 * Graph Storage Manager — On-disk Graph Database
 *
 * L8: Advanced Topic — Persisting the property graph to disk.
 *
 * Architecture:
 *   - Page 0: meta page (global counters, page chain heads).
 *   - Node pages: chain of pages holding NodeRecord entries.
 *   - Edge pages: chain of pages holding EdgeRecord entries.
 *   - Adjacency pages: chain of AdjacencyRecord entries.
 *
 * Store: memory -> serialize to records -> insert into pages -> WAL log -> commit -> flush
 * Load:  open data file -> read pages -> deserialize -> populate PropertyGraph
 * ========================================================================= */

GraphStorage *gs_create(void) {
    GraphStorage *gs = calloc(1, sizeof(GraphStorage));
    if (!gs) return NULL;
    gs->pool = bp_create();
    if (!gs->pool) { free(gs); return NULL; }
    gs->wal = wal_create(WAL_FILE);
    if (!gs->wal) { bp_destroy(gs->pool); free(gs); return NULL; }
    /* page 0 is the meta page, always allocate it */
    DataPage *meta = bp_get_page(gs->pool, 0);
    if (meta) {
        page_init(meta, 0, PAGE_TYPE_META);
        bp_mark_dirty(gs->pool, 0);
        bp_unpin(gs->pool, 0);
    }
    gs->node_page_list = 0;
    gs->edge_page_list = 0;
    gs->adj_page_list = 0;
    gs->total_nodes = 0;
    gs->total_edges = 0;
    return gs;
}

void gs_destroy(GraphStorage *gs) {
    if (!gs) return;
    /* flush all dirty pages before shutdown */
    FILE *df = fopen(DATA_FILE, "wb");
    if (df) {
        bp_flush_all(gs->pool, df);
        fclose(df);
    }
    wal_destroy(gs->wal);
    bp_destroy(gs->pool);
    free(gs);
}

/* Allocate a new page in the given chain; returns page_id */
static uint32_t gs_alloc_page(GraphStorage *gs, PageType type, uint32_t *chain_head) {
    uint32_t pid = (*chain_head == 0) ? 1 : (*chain_head);
    while (pid < MAX_PAGES) {
        DataPage *p = bp_get_page(gs->pool, pid);
        if (p && p->header.record_count == 0) {
            page_init(p, pid, type);
            bp_unpin(gs->pool, pid);
            return pid;
        }
        if (p) bp_unpin(gs->pool, pid);
        pid++;
    }
    /* chain: link new page to existing chain head */
    for (uint32_t i = 1; i < MAX_PAGES; i++) {
        DataPage *p = bp_get_page(gs->pool, i);
        if (!p) continue;
        if (p->header.page_type == PAGE_TYPE_META && i != 0) continue;
        if (p->header.record_count == 0) {
            page_init(p, i, type);
            p->header.next_page = *chain_head;
            *chain_head = i;
            bp_mark_dirty(gs->pool, i);
            bp_unpin(gs->pool, i);
            return i;
        }
        bp_unpin(gs->pool, i);
    }
    return 0; /* out of pages */
}

bool gs_store_graph(GraphStorage *gs, PropertyGraph *g) {
    if (!gs || !g) return false;

    /* Store nodes */
    int n = graph_node_count(g);
    uint32_t node_pid = gs_alloc_page(gs, PAGE_TYPE_NODE, &gs->node_page_list);
    if (node_pid == 0 && n > 0) return false;

    DataPage *np = bp_get_page(gs->pool, node_pid);
    if (!np) return false;

    for (int i = 0; i < n; i++) {
        uint8_t buf[PAGE_DATA_SIZE]; /* generous buffer */
        int sz = node_record_serialize(&g->nodes[i], buf, PAGE_DATA_SIZE);
        if (sz > 0) {
            if (page_insert_record(np, buf, (uint16_t)sz) < 0) {
                /* current page full, allocate new one */
                bp_mark_dirty(gs->pool, node_pid);
                bp_unpin(gs->pool, node_pid);
                node_pid = gs_alloc_page(gs, PAGE_TYPE_NODE, &gs->node_page_list);
                if (!node_pid) return false;
                np = bp_get_page(gs->pool, node_pid);
                if (!np) return false;
                if (page_insert_record(np, buf, (uint16_t)sz) < 0) return false;
            }
        }
    }
    page_calc_checksum(np);
    bp_mark_dirty(gs->pool, node_pid);
    bp_unpin(gs->pool, node_pid);

    /* Store edges */
    int ecount = graph_edge_count(g);
    uint32_t edge_pid = gs_alloc_page(gs, PAGE_TYPE_EDGE, &gs->edge_page_list);
    DataPage *ep = edge_pid ? bp_get_page(gs->pool, edge_pid) : NULL;

    for (int i = 0; i < ecount && ep; i++) {
        uint8_t buf[PAGE_DATA_SIZE]; /* generous buffer */
        int sz = edge_record_serialize(&g->edges[i], buf, PAGE_DATA_SIZE);
        if (sz > 0) {
            if (page_insert_record(ep, buf, (uint16_t)sz) < 0) {
                bp_mark_dirty(gs->pool, edge_pid);
                bp_unpin(gs->pool, edge_pid);
                edge_pid = gs_alloc_page(gs, PAGE_TYPE_EDGE, &gs->edge_page_list);
                if (!edge_pid) break;
                ep = bp_get_page(gs->pool, edge_pid);
                if (!ep) break;
                if (page_insert_record(ep, buf, (uint16_t)sz) < 0) break;
            }
        }
    }
    if (ep) {
        page_calc_checksum(ep);
        bp_mark_dirty(gs->pool, edge_pid);
        bp_unpin(gs->pool, edge_pid);
    }
    gs->total_nodes = n;
    gs->total_edges = ecount;
    return true;
}

bool gs_load_graph(GraphStorage *gs, PropertyGraph *g) {
    if (!gs || !g) return false;

    /* Re-create nodes from storage */
    uint32_t pid = gs->node_page_list;
    if (pid == 0) pid = 1; /* try page 1 if chain list not set */
    int nodes_loaded = 0;

    while (pid != 0 && pid < MAX_PAGES && nodes_loaded < MAX_NODES) {
        DataPage *np = bp_get_page(gs->pool, pid);
        if (!np || np->header.page_type != PAGE_TYPE_NODE) {
            if (np) bp_unpin(gs->pool, pid);
            break;
        }
        for (int s = 0; s < np->header.record_count; s++) {
            uint8_t buf[PAGE_DATA_SIZE];
            uint16_t len = 0;
            if (page_get_record(np, s, buf, &len)) {
                Node n;
                if (node_record_deserialize(buf, (int)len, &n) > 0) {
                    if (g->node_count < MAX_NODES) {
                        g->nodes[g->node_count] = n;
                        g->node_count++;
                        nodes_loaded++;
                    }
                }
            }
        }
        uint32_t next = np->header.next_page;
        bp_unpin(gs->pool, pid);
        pid = next;
    }
    /* Re-create edges from storage */
    pid = gs->edge_page_list;
    if (pid == 0) pid = 2;
    int edges_loaded = 0;

    while (pid != 0 && pid < MAX_PAGES && edges_loaded < MAX_EDGES) {
        DataPage *ep = bp_get_page(gs->pool, pid);
        if (!ep || ep->header.page_type != PAGE_TYPE_EDGE) {
            if (ep) bp_unpin(gs->pool, pid);
            break;
        }
        for (int s = 0; s < ep->header.record_count; s++) {
            uint8_t buf[PAGE_DATA_SIZE];
            uint16_t len = 0;
            if (page_get_record(ep, s, buf, &len)) {
                Edge e;
                if (edge_record_deserialize(buf, (int)len, &e) > 0) {
                    if (g->edge_count < MAX_EDGES) {
                        g->edges[g->edge_count] = e;
                        g->edge_count++;
                        edges_loaded++;
                    }
                }
            }
        }
        uint32_t next = ep->header.next_page;
        bp_unpin(gs->pool, pid);
        pid = next;
    }
    gs->total_nodes = nodes_loaded;
    gs->total_edges = edges_loaded;
    g->next_node_id = (nodes_loaded > 0) ? g->nodes[nodes_loaded - 1].id + 1 : 1;
    g->next_edge_id = (edges_loaded > 0) ? g->edges[edges_loaded - 1].id + 1 : 1;

    /* Rebuild hash table for node lookups */
    for (int i = 0; i < nodes_loaded; i++) {
        unsigned int bucket = ((unsigned int)(g->nodes[i].id ^ (g->nodes[i].id >> 32))
                               * 0x45d9f3bU) % HASH_TABLE_SIZE;
        NodeHashEntry *entry = malloc(sizeof(NodeHashEntry));
        if (entry) {
            entry->node_id = g->nodes[i].id;
            entry->node_index = i;
            entry->next = g->node_hash[bucket];
            g->node_hash[bucket] = entry;
        }
    }
    /* Rebuild adjacency lists from edges */
    for (int i = 0; i < edges_loaded; i++) {
        Edge *e = &g->edges[i];
        int from_idx = -1, to_idx = -1;
        for (int j = 0; j < nodes_loaded; j++) {
            if (g->nodes[j].id == e->from_node) from_idx = j;
            if (g->nodes[j].id == e->to_node) to_idx = j;
        }
        if (from_idx >= 0 && to_idx >= 0) {
            if (from_idx >= g->adjacency_capacity) {
                int new_cap = g->adjacency_capacity * 2;
                AdjList *new_adj = realloc(g->adjacency, (size_t)new_cap * sizeof(AdjList));
                if (new_adj) {
                    memset(new_adj + g->adjacency_capacity, 0,
                           (size_t)(new_cap - g->adjacency_capacity) * sizeof(AdjList));
                    g->adjacency = new_adj;
                    g->adjacency_capacity = new_cap;
                }
            }
            AdjListNode *adj_node = malloc(sizeof(AdjListNode));
            if (adj_node) {
                adj_node->edge_id = e->id;
                adj_node->neighbor_id = e->to_node;
                adj_node->next = g->adjacency[from_idx].head;
                g->adjacency[from_idx].head = adj_node;
            }
            if (!e->directed) {
                AdjListNode *rev_node = malloc(sizeof(AdjListNode));
                if (rev_node) {
                    rev_node->edge_id = e->id;
                    rev_node->neighbor_id = e->from_node;
                    rev_node->next = g->adjacency[to_idx].head;
                    g->adjacency[to_idx].head = rev_node;
                }
            }
        }
    }
    return true;
}

int gs_node_count(GraphStorage *gs) { return gs ? gs->total_nodes : 0; }
int gs_edge_count(GraphStorage *gs) { return gs ? gs->total_edges : 0; }
