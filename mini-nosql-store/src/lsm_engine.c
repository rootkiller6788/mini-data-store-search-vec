#include "lsm_engine.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static int skiplist_random_level(void) {
    int lvl = 0;
    while (lvl < LSM_SKIPLIST_MAXLVL - 1 && (rand() % 4) == 0)
        lvl++;
    return lvl;
}

MemTable *memtable_create(int max_count) {
    MemTable *mt = (MemTable *)calloc(1, sizeof(MemTable));
    if (!mt) return NULL;
    mt->max_count = max_count > 0 ? max_count : LSM_MEMTABLE_MAX;
    mt->head = (SkipListNode *)calloc(1, sizeof(SkipListNode));
    if (!mt->head) { free(mt); return NULL; }
    for (int i = 0; i < LSM_SKIPLIST_MAXLVL; i++)
        mt->head->forward[i] = NULL;
    return mt;
}

void memtable_destroy(MemTable *mt) {
    if (!mt) return;
    SkipListNode *cur = mt->head->forward[0];
    while (cur) {
        SkipListNode *tmp = cur;
        cur = cur->forward[0];
        free(tmp);
    }
    free(mt->head);
    free(mt);
}

int memtable_put(MemTable *mt, const char *key, const char *value) {
    if (!mt || !key || !value) return -1;
    SkipListNode *update[LSM_SKIPLIST_MAXLVL] = {0};
    SkipListNode *cur = mt->head;
    for (int i = LSM_SKIPLIST_MAXLVL - 1; i >= 0; i--) {
        while (cur->forward[i] && strcmp(cur->forward[i]->key, key) < 0)
            cur = cur->forward[i];
        update[i] = cur;
    }
    cur = cur->forward[0];
    if (cur && strcmp(cur->key, key) == 0) {
        strncpy(cur->value, value, LSM_MAX_VALUE_LEN - 1);
        cur->value[LSM_MAX_VALUE_LEN - 1] = '\0';
        return 0;
    }

    SkipListNode *node = (SkipListNode *)calloc(1, sizeof(SkipListNode));
    if (!node) return -1;
    strncpy(node->key, key, LSM_MAX_KEY_LEN - 1);
    node->key[LSM_MAX_KEY_LEN - 1] = '\0';
    strncpy(node->value, value, LSM_MAX_VALUE_LEN - 1);
    node->value[LSM_MAX_VALUE_LEN - 1] = '\0';
    int lvl = skiplist_random_level();
    for (int i = 0; i <= lvl; i++) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }
    mt->count++;
    return 0;
}

int memtable_get(MemTable *mt, const char *key, char *value_out, size_t max_len) {
    if (!mt || !key || !value_out) return -1;
    SkipListNode *cur = mt->head;
    for (int i = LSM_SKIPLIST_MAXLVL - 1; i >= 0; i--) {
        while (cur->forward[i] && strcmp(cur->forward[i]->key, key) < 0)
            cur = cur->forward[i];
    }
    cur = cur->forward[0];
    if (cur && strcmp(cur->key, key) == 0) {
        strncpy(value_out, cur->value, max_len - 1);
        value_out[max_len - 1] = '\0';
        return 0;
    }
    return -2;
}

void bloom_init(BloomFilter *bf) {
    memset(bf->bits, 0, sizeof(bf->bits));
}

static uint32_t bloom_hash1(const char *key) {
    uint32_t h = 5381;
    while (*key) h = ((h << 5) + h) + (unsigned char)*key++;
    return h;
}

static uint32_t bloom_hash2(const char *key) {
    uint32_t h = 0;
    while (*key) h = (unsigned char)*key++ + (h << 6) + (h << 16) - h;
    return h;
}

void bloom_add(BloomFilter *bf, const char *key) {
    uint32_t h1 = bloom_hash1(key);
    uint32_t h2 = bloom_hash2(key);
    for (int i = 0; i < LSM_BLOOM_HASHES; i++) {
        uint32_t pos = (h1 + i * h2) % LSM_BLOOM_BITS;
        bf->bits[pos / 8] |= (1 << (pos % 8));
    }
}

int bloom_check(BloomFilter *bf, const char *key) {
    uint32_t h1 = bloom_hash1(key);
    uint32_t h2 = bloom_hash2(key);
    for (int i = 0; i < LSM_BLOOM_HASHES; i++) {
        uint32_t pos = (h1 + i * h2) % LSM_BLOOM_BITS;
        if (!(bf->bits[pos / 8] & (1 << (pos % 8))))
            return 0;
    }
    return 1;
}

SSTable *sstable_create(int id, int level) {
    SSTable *sst = (SSTable *)calloc(1, sizeof(SSTable));
    if (!sst) return NULL;
    sst->id = id;
    sst->level = level;
    sst->data_size = LSM_BLOCK_SIZE * 4;
    sst->data_blocks = (char *)calloc(1, sst->data_size);
    if (!sst->data_blocks) { free(sst); return NULL; }
    sst->keys = (char (*)[LSM_MAX_KEY_LEN])calloc(512, LSM_MAX_KEY_LEN);
    if (!sst->keys) { free(sst->data_blocks); free(sst); return NULL; }
    sst->block_index_offsets = (int *)calloc(512, sizeof(int));
    if (!sst->block_index_offsets) {
        free(sst->keys); free(sst->data_blocks); free(sst); return NULL;
    }
    bloom_init(&sst->bloom);
    return sst;
}

void sstable_destroy(SSTable *sst) {
    if (!sst) return;
    free(sst->data_blocks);
    free(sst->keys);
    free(sst->block_index_offsets);
    free(sst);
}

int sstable_add_key(SSTable *sst, const char *key, const char *value) {
    if (!sst || !key || !value || sst->key_count >= 512) return -1;
    strncpy(sst->keys[sst->key_count], key, LSM_MAX_KEY_LEN - 1);
    sst->keys[sst->key_count][LSM_MAX_KEY_LEN - 1] = '\0';
    bloom_add(&sst->bloom, key);
    if (sst->key_count == 0)
        sst->block_index_offsets[0] = 0;
    else
        sst->block_index_offsets[sst->key_count] =
            sst->block_index_offsets[sst->key_count - 1] + LSM_MAX_KEY_LEN + LSM_MAX_VALUE_LEN;
    sst->index_size = sst->key_count + 1;
    sst->key_count++;
    return 0;
}

int sstable_get(SSTable *sst, const char *key, char *value_out, size_t max_len) {
    if (!sst || !key || !value_out) return -1;
    if (!bloom_check(&sst->bloom, key)) return -2;
    for (int i = 0; i < sst->key_count; i++) {
        if (strcmp(sst->keys[i], key) == 0) {
            strncpy(value_out, "", max_len - 1);
            value_out[max_len - 1] = '\0';
            return 0;
        }
    }
    return -2;
}

SSTable *sstable_from_memtable(MemTable *mt, int id, int level) {
    if (!mt) return NULL;
    SSTable *sst = sstable_create(id, level);
    if (!sst) return NULL;
    SkipListNode *cur = mt->head->forward[0];
    while (cur) {
        sstable_add_key(sst, cur->key, cur->value);
        cur = cur->forward[0];
    }
    return sst;
}

LSMEngine *lsm_create(const char *data_dir) {
    LSMEngine *eng = (LSMEngine *)calloc(1, sizeof(LSMEngine));
    if (!eng) return NULL;
    eng->memtable = memtable_create(LSM_MEMTABLE_MAX);
    if (!eng->memtable) { free(eng); return NULL; }
    eng->next_sstable_id = 1;
    if (data_dir) strncpy(eng->data_dir, data_dir, 255);
    return eng;
}

void lsm_destroy(LSMEngine *engine) {
    if (!engine) return;
    memtable_destroy(engine->memtable);
    for (int i = 0; i < engine->imm_count; i++)
        memtable_destroy(engine->immutable_memtables[i]);
    free(engine->immutable_memtables);
    for (int i = 0; i < engine->level0_count; i++)
        sstable_destroy(engine->level0[i]);
    free(engine->level0);
    for (int i = 0; i < engine->level1_count; i++)
        sstable_destroy(engine->level1[i]);
    free(engine->level1);
    free(engine);
}

int lsm_put(LSMEngine *engine, const char *key, const char *value) {
    if (!engine || !key || !value) return -1;
    return memtable_put(engine->memtable, key, value);
}

int lsm_get(LSMEngine *engine, const char *key, char *value_out, size_t max_len) {
    if (!engine || !key || !value_out) return -1;

    if (memtable_get(engine->memtable, key, value_out, max_len) == 0)
        return 0;

    for (int i = engine->imm_count - 1; i >= 0; i--) {
        if (memtable_get(engine->immutable_memtables[i], key, value_out, max_len) == 0)
            return 0;
    }

    for (int i = engine->level0_count - 1; i >= 0; i--) {
        if (sstable_get(engine->level0[i], key, value_out, max_len) == 0)
            return 0;
    }

    for (int i = 0; i < engine->level1_count; i++) {
        if (sstable_get(engine->level1[i], key, value_out, max_len) == 0)
            return 0;
    }

    return -2;
}

int lsm_delete(LSMEngine *engine, const char *key) {
    if (!engine || !key) return -1;
    return memtable_put(engine->memtable, key, "__TOMBSTONE__");
}

int lsm_flush_memtable(LSMEngine *engine) {
    if (!engine) return -1;
    if (engine->memtable->count == 0) return 0;

    engine->immutable_memtables = (MemTable **)realloc(
        engine->immutable_memtables,
        (engine->imm_count + 1) * sizeof(MemTable *));
    if (!engine->immutable_memtables) return -1;
    engine->immutable_memtables[engine->imm_count] = engine->memtable;
    engine->imm_count++;

    SSTable *sst = sstable_from_memtable(engine->memtable,
                                         engine->next_sstable_id++, 0);
    if (sst) {
        engine->level0 = (SSTable **)realloc(
            engine->level0, (engine->level0_count + 1) * sizeof(SSTable *));
        if (engine->level0) {
            engine->level0[engine->level0_count] = sst;
            engine->level0_count++;
        }
    }

    engine->memtable = memtable_create(LSM_MEMTABLE_MAX);
    return 0;
}

int lsm_compact(LSMEngine *engine, int level) {
    if (!engine) return -1;
    if (level == 0 && engine->level0_count >= LSM_LEVEL0_MAX) {
        SSTable *merged = sstable_create(engine->next_sstable_id++, 1);
        if (!merged) return -1;
        for (int i = 0; i < engine->level0_count && merged->key_count < 512; i++) {
            for (int j = 0; j < engine->level0[i]->key_count && merged->key_count < 512; j++) {
                sstable_add_key(merged,
                    engine->level0[i]->keys[j],
                    engine->level0[i]->keys[j]);
            }
        }
        engine->level1 = (SSTable **)realloc(
            engine->level1, (engine->level1_count + 1) * sizeof(SSTable *));
        if (engine->level1) {
            engine->level1[engine->level1_count++] = merged;
        }
        for (int i = 0; i < engine->level0_count; i++)
            sstable_destroy(engine->level0[i]);
        free(engine->level0);
        engine->level0 = NULL;
        engine->level0_count = 0;
        return 0;
    }
    return 0;
}

int lsm_compact_all(LSMEngine *engine) {
    if (!engine) return -1;
    return lsm_compact(engine, 0);
}

int lsm_count(LSMEngine *engine) {
    if (!engine) return -1;
    int total = engine->memtable->count;
    for (int i = 0; i < engine->level0_count; i++)
        total += engine->level0[i]->key_count;
    for (int i = 0; i < engine->level1_count; i++)
        total += engine->level1[i]->key_count;
    return total;
}
