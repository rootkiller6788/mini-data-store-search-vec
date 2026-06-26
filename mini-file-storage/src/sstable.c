#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sstable.h"

/* ─────────────────────────────────────────────
   Internal helpers: murmur3-ish hash for bloom
   ───────────────────────────────────────────── */
static uint32_t hash_mix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

static uint32_t bloom_hash(const uint8_t *data, uint32_t len, uint32_t seed) {
    uint32_t h = seed;
    for (uint32_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x5BD1E995u;
        h ^= h >> 15;
    }
    return hash_mix(h);
}

/* ─────────────────────────────────────────────
   Bloom filter init / may-contain / destroy
   ───────────────────────────────────────────── */
static void bloom_init(BloomFilter *bf, uint32_t num_keys) {
    bf->seed1 = 0x9747B28Cu;
    bf->seed2 = 0xE17A1465u;
    bf->seed3 = 0x9AE16A3Bu;
    bf->bits_per_key = 10;
    bf->num_bits = num_keys * bf->bits_per_key;
    if (bf->num_bits < 64) bf->num_bits = 64;
    bf->num_bytes = (bf->num_bits + 7) / 8;
    bf->bit_array = (uint8_t *)calloc(bf->num_bytes, 1);
}

static void bloom_add(BloomFilter *bf, const uint8_t *key, uint32_t key_len) {
    uint32_t h1 = bloom_hash(key, key_len, bf->seed1);
    uint32_t h2 = bloom_hash(key, key_len, bf->seed2);
    uint32_t h3 = bloom_hash(key, key_len, bf->seed3);
    uint32_t bits = bf->num_bits;
    uint32_t pos1 = h1 % bits;
    uint32_t pos2 = h2 % bits;
    uint32_t pos3 = h3 % bits;
    bf->bit_array[pos1 / 8] |= (uint8_t)(1u << (pos1 % 8));
    bf->bit_array[pos2 / 8] |= (uint8_t)(1u << (pos2 % 8));
    bf->bit_array[pos3 / 8] |= (uint8_t)(1u << (pos3 % 8));
}

int bloom_maybe_contain(BloomFilter *bf, const uint8_t *key, uint32_t key_len) {
    if (!bf->bit_array) return 0;
    uint32_t h1 = bloom_hash(key, key_len, bf->seed1);
    uint32_t h2 = bloom_hash(key, key_len, bf->seed2);
    uint32_t h3 = bloom_hash(key, key_len, bf->seed3);
    uint32_t bits = bf->num_bits;
    uint32_t pos1 = h1 % bits;
    uint32_t pos2 = h2 % bits;
    uint32_t pos3 = h3 % bits;
    int ok1 = (bf->bit_array[pos1 / 8] >> (pos1 % 8)) & 1u;
    int ok2 = (bf->bit_array[pos2 / 8] >> (pos2 % 8)) & 1u;
    int ok3 = (bf->bit_array[pos3 / 8] >> (pos3 % 8)) & 1u;
    return ok1 && ok2 && ok3;
}

static void bloom_destroy(BloomFilter *bf) {
    free(bf->bit_array);
    bf->bit_array = NULL;
}

/* ─────────────────────────────────────────────
   Serialize a data block in memory
   ───────────────────────────────────────────── */
static uint32_t serialize_data_block(
    const uint8_t **keys, const uint32_t *key_lens,
    const uint8_t **values, const uint32_t *value_lens,
    uint32_t from, uint32_t to,
    uint8_t *buf, uint32_t *restart_offsets, uint32_t *num_restarts)
{
    uint32_t offset = 0;
    uint32_t restart_count = 0;
    for (uint32_t i = from; i < to; i++) {
        if ((i - from) % RESTART_INTERVAL == 0) {
            restart_offsets[restart_count++] = offset;
        }
        uint32_t shared = 0;
        if ((i - from) % RESTART_INTERVAL != 0) {
            /* delta encoding: share prefix with prev key */
            const uint8_t *prev = keys[i - 1];
            uint32_t prev_len = key_lens[i - 1];
            uint32_t min_len = prev_len < key_lens[i] ? prev_len : key_lens[i];
            while (shared < min_len && prev[shared] == keys[i][shared]) shared++;
        }
        uint32_t non_shared = key_lens[i] - shared;
        /* shared_len */
        buf[offset++] = (uint8_t)(shared & 0xFF);
        buf[offset++] = (uint8_t)((shared >> 8) & 0xFF);
        buf[offset++] = (uint8_t)((shared >> 16) & 0xFF);
        buf[offset++] = (uint8_t)((shared >> 24) & 0xFF);
        /* non_shared_len */
        buf[offset++] = (uint8_t)(non_shared & 0xFF);
        buf[offset++] = (uint8_t)((non_shared >> 8) & 0xFF);
        buf[offset++] = (uint8_t)((non_shared >> 16) & 0xFF);
        buf[offset++] = (uint8_t)((non_shared >> 24) & 0xFF);
        /* value_len */
        uint32_t vlen = value_lens[i];
        buf[offset++] = (uint8_t)(vlen & 0xFF);
        buf[offset++] = (uint8_t)((vlen >> 8) & 0xFF);
        buf[offset++] = (uint8_t)((vlen >> 16) & 0xFF);
        buf[offset++] = (uint8_t)((vlen >> 24) & 0xFF);
        /* key non-shared bytes */
        memcpy(buf + offset, keys[i] + shared, non_shared);
        offset += non_shared;
        /* value */
        memcpy(buf + offset, values[i], vlen);
        offset += vlen;
    }
    *num_restarts = restart_count;
    return offset;
}

/* ─────────────────────────────────────────────
   Write SSTable
   ───────────────────────────────────────────── */
int sstable_write(const char *filename,
                  const uint8_t **keys, const uint32_t *key_lens,
                  const uint8_t **values, const uint32_t *value_lens,
                  uint32_t num_entries)
{
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;

    uint32_t *block_starts = (uint32_t *)malloc(num_entries * sizeof(uint32_t));
    uint32_t  num_blocks = 0;
    uint32_t  entry_idx = 0;

    uint8_t  *buf = (uint8_t *)malloc(BLOCK_SIZE);
    uint32_t  restart_offsets[BLOCK_SIZE];

    uint32_t  index_entry_count = 0;
    SSTableIndexEntry *index_entries =
        (SSTableIndexEntry *)malloc(num_entries * sizeof(SSTableIndexEntry));

    BloomFilter bf;
    bloom_init(&bf, num_entries);

    /* Write data blocks */
    while (entry_idx < num_entries) {
        /* Estimate how many entries fit in one block */
        uint32_t start = entry_idx;
        uint32_t est_size = 0;
        while (entry_idx < num_entries &&
               est_size + key_lens[entry_idx] + value_lens[entry_idx] + 16 < BLOCK_SIZE * 3 / 4) {
            est_size += key_lens[entry_idx] + value_lens[entry_idx] + 16;
            entry_idx++;
        }
        if (entry_idx == start) entry_idx = start + 1; /* at least one */

        block_starts[num_blocks] = start;

        uint32_t num_rst = 0;
        uint32_t data_len = serialize_data_block(
            keys, key_lens, values, value_lens, start, entry_idx,
            buf, restart_offsets, &num_rst);

        /* block header: num_entries(4) + data_len(4) + restart_count(4) */
        uint32_t nent = entry_idx - start;
        fwrite(&nent,       sizeof(uint32_t), 1, fp);
        fwrite(&data_len,   sizeof(uint32_t), 1, fp);
        fwrite(&num_rst,    sizeof(uint32_t), 1, fp);
        fwrite(buf,         1, data_len, fp);
        /* restart offsets array */
        for (uint32_t r = 0; r < num_rst; r++) {
            fwrite(&restart_offsets[r], sizeof(uint32_t), 1, fp);
        }
        /* trailer: num_restarts (yes, redundant, for RocksDB compat) */
        fwrite(&num_rst, sizeof(uint32_t), 1, fp);

        /* Index entry for this block */
        SSTableIndexEntry *ie = &index_entries[index_entry_count];
        ie->last_key_len = key_lens[entry_idx - 1];
        memcpy(ie->last_key, keys[entry_idx - 1], ie->last_key_len);
        ie->block_offset = (uint32_t)ftell(fp) -
            (data_len + 12 + num_rst * 4 + 4);
        ie->block_size   = data_len + 12 + num_rst * 4 + 4;
        /* rewind to compute correct offset – actually store the offset */
        /* we recorded block_start at fwrite time, recalc: */
        index_entry_count++;
        num_blocks++;

        /* Add keys to bloom */
        for (uint32_t k = start; k < entry_idx; k++) {
            bloom_add(&bf, keys[k], key_lens[k]);
        }
    }

    free(buf);

    /* Write index block */
    uint32_t index_start = (uint32_t)ftell(fp);
    uint32_t iblk_sz = 0;
    /* compute index block size */
    for (uint32_t i = 0; i < index_entry_count; i++) {
        iblk_sz += 12 + index_entries[i].last_key_len; /* offset+size+keylen+key */
    }
    iblk_sz += 4; /* num_entries */

    fwrite(&index_entry_count, sizeof(uint32_t), 1, fp);
    for (uint32_t i = 0; i < index_entry_count; i++) {
        fwrite(&index_entries[i].last_key_len, sizeof(uint32_t), 1, fp);
        fwrite(index_entries[i].last_key, 1, index_entries[i].last_key_len, fp);
        fwrite(&index_entries[i].block_offset, sizeof(uint32_t), 1, fp);
        fwrite(&index_entries[i].block_size,   sizeof(uint32_t), 1, fp);
    }

    /* Write bloom filter */
    uint32_t bloom_start = (uint32_t)ftell(fp);
    fwrite(&bf.num_bytes, sizeof(uint32_t), 1, fp);
    fwrite(bf.bit_array, 1, bf.num_bytes, fp);

    /* Write footer */
    SSTableFooter footer;
    footer.index_block_offset = index_start;
    footer.index_block_size   = iblk_sz + 4;
    footer.magic_number       = SSTABLE_MAGIC;
    memset(footer.padding, 0, sizeof(footer.padding));
    fwrite(&footer, sizeof(SSTableFooter), 1, fp);

    fclose(fp);
    bloom_destroy(&bf);
    free(index_entries);
    free(block_starts);
    return 0;
}

/* ─────────────────────────────────────────────
   Read SSTable into memory
   ───────────────────────────────────────────── */
SSTable *sstable_read(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    uint64_t fsize = (uint64_t)ftell(fp);

    if (fsize < sizeof(SSTableFooter)) { fclose(fp); return NULL; }

    /* Read footer */
    SSTableFooter footer;
    fseek(fp, (long)(fsize - sizeof(SSTableFooter)), SEEK_SET);
    if (fread(&footer, sizeof(SSTableFooter), 1, fp) != 1) { fclose(fp); return NULL; }
    if (footer.magic_number != SSTABLE_MAGIC) { fclose(fp); return NULL; }

    SSTable *table = (SSTable *)calloc(1, sizeof(SSTable));
    table->file = fp;
    table->filename = strdup(filename);
    table->file_size = fsize;
    table->footer = footer;

    /* Read index block */
    fseek(fp, footer.index_block_offset, SEEK_SET);
    uint32_t ie_count;
    if (fread(&ie_count, sizeof(uint32_t), 1, fp) != 1) { sstable_destroy(table); return NULL; }
    table->index_block.num_entries = ie_count;
    table->index_block.entries = (SSTableIndexEntry *)malloc(
        ie_count * sizeof(SSTableIndexEntry));

    for (uint32_t i = 0; i < ie_count; i++) {
        SSTableIndexEntry *e = &table->index_block.entries[i];
        if (fread(&e->last_key_len, sizeof(uint32_t), 1, fp) != 1) { sstable_destroy(table); return NULL; }
        if (e->last_key_len > MAX_KEY_SIZE) { sstable_destroy(table); return NULL; }
        if (fread(e->last_key, 1, e->last_key_len, fp) != e->last_key_len) { sstable_destroy(table); return NULL; }
        if (fread(&e->block_offset, sizeof(uint32_t), 1, fp) != 1) { sstable_destroy(table); return NULL; }
        if (fread(&e->block_size, sizeof(uint32_t), 1, fp) != 1) { sstable_destroy(table); return NULL; }
    }

    /* Read bloom filter (right after index) */
    uint32_t bloom_start = footer.index_block_offset + footer.index_block_size;
    fseek(fp, bloom_start, SEEK_SET);
    if (fread(&table->bloom.num_bytes, sizeof(uint32_t), 1, fp) == 1) {
        table->bloom.bit_array = (uint8_t *)malloc(table->bloom.num_bytes);
        if (fread(table->bloom.bit_array, 1, table->bloom.num_bytes, fp) != table->bloom.num_bytes) {
            free(table->bloom.bit_array);
            table->bloom.bit_array = NULL;
        }
        table->bloom.seed1 = 0x9747B28Cu;
        table->bloom.seed2 = 0xE17A1465u;
        table->bloom.seed3 = 0x9AE16A3Bu;
        table->bloom.bits_per_key = 10;
        table->bloom.num_bits = table->bloom.num_bytes * 8;
    }

    /* Pre-load all data blocks into memory */
    table->num_data_blocks = ie_count;
    table->data_blocks = (SSTableDataBlock **)calloc(ie_count, sizeof(SSTableDataBlock *));
    for (uint32_t i = 0; i < ie_count; i++) {
        SSTableDataBlock *db = (SSTableDataBlock *)calloc(1, sizeof(SSTableDataBlock));
        table->data_blocks[i] = db;
        uint32_t boff = table->index_block.entries[i].block_offset;
        fseek(fp, boff, SEEK_SET);
        if (fread(&db->num_entries, sizeof(uint32_t), 1, fp) != 1) continue;
        if (fread(&db->data_size, sizeof(uint32_t), 1, fp) != 1) continue;
        if (fread(&db->num_restarts, sizeof(uint32_t), 1, fp) != 1) continue;
        db->data = (uint8_t *)malloc(db->data_size);
        if (fread(db->data, 1, db->data_size, fp) != db->data_size) continue;
        db->restart_offsets = (uint32_t *)malloc(db->num_restarts * sizeof(uint32_t));
        for (uint32_t r = 0; r < db->num_restarts; r++) {
            if (fread(&db->restart_offsets[r], sizeof(uint32_t), 1, fp) != 1) continue;
        }
        /* skip trailing num_restarts */
        uint32_t dummy;
        fread(&dummy, sizeof(uint32_t), 1, fp);
    }

    return table;
}

/* ─────────────────────────────────────────────
   Binary search within a data block for a key
   ───────────────────────────────────────────── */
static int block_search(SSTableDataBlock *block,
                        const uint8_t *target_key, uint32_t target_len,
                        uint8_t *value_out, uint32_t *value_len_out)
{
    /* Use restart points to narrow down */
    uint32_t lo_r = 0, hi_r = block->num_restarts;
    while (lo_r < hi_r) {
        uint32_t mid = lo_r + (hi_r - lo_r) / 2;
        uint32_t off = block->restart_offsets[mid];
        /* Decode the full key at this restart point */
        uint32_t pos = off;
        uint32_t shared   = *(uint32_t *)(block->data + pos); pos += 4;
        uint32_t nonshared= *(uint32_t *)(block->data + pos); pos += 4;
        uint32_t vlen     = *(uint32_t *)(block->data + pos); pos += 4;
        (void)shared; /* at restart, shared==0 */
        int cmp = memcmp(block->data + pos, target_key,
                         nonshared < target_len ? nonshared : target_len);
        if (cmp == 0 && nonshared != target_len)
            cmp = nonshared < target_len ? -1 : 1;
        if (cmp < 0) lo_r = mid + 1;
        else hi_r = mid;
    }

    uint32_t start_off = (lo_r > 0) ? block->restart_offsets[lo_r - 1] : 0;
    uint32_t end_off   = (lo_r < block->num_restarts)
        ? block->restart_offsets[lo_r] : block->data_size;

    uint32_t pos = start_off;
    uint8_t prev_key[256];
    uint32_t prev_len = 0;

    while (pos < end_off) {
        uint32_t shared    = *(uint32_t *)(block->data + pos); pos += 4;
        uint32_t nonshared = *(uint32_t *)(block->data + pos); pos += 4;
        uint32_t vlen      = *(uint32_t *)(block->data + pos); pos += 4;

        uint8_t cur_key[256];
        memcpy(cur_key, prev_key, shared);
        memcpy(cur_key + shared, block->data + pos, nonshared);
        uint32_t cur_len = shared + nonshared;
        pos += nonshared;

        if (cur_len == target_len && memcmp(cur_key, target_key, target_len) == 0) {
            if (value_out && vlen <= MAX_VALUE_SIZE) {
                memcpy(value_out, block->data + pos, vlen);
            }
            if (value_len_out) *value_len_out = vlen;
            return 1;
        }
        memcpy(prev_key, cur_key, cur_len);
        prev_len = cur_len;
        pos += vlen;
    }
    return 0;
}

/* ─────────────────────────────────────────────
   sstable_get – lookup key in SSTable
   ───────────────────────────────────────────── */
int sstable_get(SSTable *table,
                const uint8_t *key, uint32_t key_len,
                uint8_t *value_out, uint32_t *value_len_out)
{
    if (!table) return -1;

    /* Check bloom filter */
    if (table->bloom.bit_array &&
        !bloom_maybe_contain(&table->bloom, key, key_len)) {
        return 0; /* definitely not present */
    }

    /* Binary search in index for the right block */
    uint32_t ie_count = table->index_block.num_entries;
    if (ie_count == 0) return 0;
    uint32_t lo = 0, hi = ie_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        SSTableIndexEntry *e = &table->index_block.entries[mid];
        int cmp = memcmp(key, e->last_key, key_len < e->last_key_len ? key_len : e->last_key_len);
        if (cmp == 0 && key_len != e->last_key_len)
            cmp = key_len < e->last_key_len ? -1 : 1;
        if (cmp > 0) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= ie_count) lo = ie_count - 1;

    /* Search within the chosen block */
    if (!table->data_blocks[lo]) return 0;
    return block_search(table->data_blocks[lo], key, key_len, value_out, value_len_out);
}

/* ─────────────────────────────────────────────
   Merge iterator heap helpers
   ───────────────────────────────────────────── */
static void heap_swap(SSTableMergeNode **a, SSTableMergeNode **b) {
    SSTableMergeNode *t = *a; *a = *b; *b = t;
}

static int heap_cmp(SSTableMergeNode *a, SSTableMergeNode *b) {
    if (a->exhausted && b->exhausted) return 0;
    if (a->exhausted) return 1;
    if (b->exhausted) return -1;
    uint32_t min_len = a->key_len < b->key_len ? a->key_len : b->key_len;
    int c = memcmp(a->key, b->key, min_len);
    if (c != 0) return c;
    if (a->key_len < b->key_len) return -1;
    if (a->key_len > b->key_len) return 1;
    return 0;
}

static void heap_sift_down(SSTableMergeIterator *iter, uint32_t idx) {
    uint32_t n = iter->heap_size;
    while (1) {
        uint32_t smallest = idx;
        uint32_t left  = 2 * idx + 1;
        uint32_t right = 2 * idx + 2;
        if (left < n && heap_cmp(iter->nodes[left], iter->nodes[smallest]) < 0)
            smallest = left;
        if (right < n && heap_cmp(iter->nodes[right], iter->nodes[smallest]) < 0)
            smallest = right;
        if (smallest == idx) break;
        heap_swap(&iter->nodes[idx], &iter->nodes[smallest]);
        idx = smallest;
    }
}

static void heap_build(SSTableMergeIterator *iter) {
    uint32_t n = iter->heap_size;
    if (n == 0) return;
    for (int32_t i = (int32_t)(n / 2) - 1; i >= 0; i--)
        heap_sift_down(iter, (uint32_t)i);
}

/* Load the current entry from a node's data block */
static int merge_node_load_entry(SSTableMergeNode *node) {
    if (node->exhausted) return -1;
    SSTableDataBlock *db = node->table->data_blocks[node->block_idx];
    if (!db) { node->exhausted = 1; return -1; }

    uint32_t pos = db->restart_offsets[0]; /* Start from first restart for simplicity */
    uint32_t cur_entry = 0;
    uint8_t prev_key[256];
    uint32_t prev_len = 0;

    while (pos < db->data_size) {
        uint32_t shared    = *(uint32_t *)(db->data + pos); pos += 4;
        uint32_t nonshared = *(uint32_t *)(db->data + pos); pos += 4;
        uint32_t vlen      = *(uint32_t *)(db->data + pos); pos += 4;
        memcpy(node->key, prev_key, shared);
        memcpy(node->key + shared, db->data + pos, nonshared);
        node->key_len = shared + nonshared;
        pos += nonshared;
        memcpy(node->value, db->data + pos, vlen);
        node->value_len = vlen;
        pos += vlen;
        memcpy(prev_key, node->key, node->key_len);
        prev_len = node->key_len;

        if (cur_entry == node->entry_idx) {
            return 0;
        }
        cur_entry++;
    }
    node->exhausted = 1;
    return -1;
}

SSTableMergeIterator *sstable_merge_iterator_create(SSTable **tables, uint32_t num) {
    SSTableMergeIterator *iter = (SSTableMergeIterator *)calloc(1, sizeof(SSTableMergeIterator));
    iter->num_nodes = num;
    iter->heap_size = num;
    iter->nodes = (SSTableMergeNode **)calloc(num, sizeof(SSTableMergeNode *));
    for (uint32_t i = 0; i < num; i++) {
        iter->nodes[i] = (SSTableMergeNode *)calloc(1, sizeof(SSTableMergeNode));
        iter->nodes[i]->table = tables[i];
        iter->nodes[i]->block_idx = 0;
        iter->nodes[i]->entry_idx = 0;
        merge_node_load_entry(iter->nodes[i]);
    }
    heap_build(iter);
    return iter;
}

int sstable_merge_iterator_next(SSTableMergeIterator *iter,
                                uint8_t *key_out, uint32_t *key_len_out,
                                uint8_t *value_out, uint32_t *value_len_out) {
    if (iter->heap_size == 0) return 0;
    SSTableMergeNode *top = iter->nodes[0];
    if (top->exhausted) return 0;

    memcpy(key_out, top->key, top->key_len);
    *key_len_out = top->key_len;
    memcpy(value_out, top->value, top->value_len);
    *value_len_out = top->value_len;

    /* Advance the top node */
    top->entry_idx++;
    if (merge_node_load_entry(top) != 0) {
        /* Move to next block */
        top->block_idx++;
        if (top->block_idx >= top->table->num_data_blocks) {
            top->exhausted = 1;
        } else {
            top->entry_idx = 0;
            merge_node_load_entry(top);
        }
    }

    /* Remove exhausted nodes from heap */
    while (iter->heap_size > 0 && iter->nodes[0]->exhausted) {
        heap_swap(&iter->nodes[0], &iter->nodes[iter->heap_size - 1]);
        iter->heap_size--;
        heap_sift_down(iter, 0);
    }
    if (iter->heap_size > 0) heap_sift_down(iter, 0);
    return 1;
}

void sstable_merge_iterator_destroy(SSTableMergeIterator *iter) {
    if (!iter) return;
    for (uint32_t i = 0; i < iter->num_nodes; i++) free(iter->nodes[i]);
    free(iter->nodes);
    free(iter);
}

/* ─────────────────────────────────────────────
   Destroy SSTable handle
   ───────────────────────────────────────────── */
void sstable_destroy(SSTable *table) {
    if (!table) return;
    if (table->file) fclose(table->file);
    free(table->filename);
    for (uint32_t i = 0; i < table->num_data_blocks; i++) {
        if (table->data_blocks[i]) {
            free(table->data_blocks[i]->data);
            free(table->data_blocks[i]->restart_offsets);
            free(table->data_blocks[i]);
        }
    }
    free(table->data_blocks);
    free(table->index_block.entries);
    if (table->bloom.bit_array) free(table->bloom.bit_array);
    free(table);
}
