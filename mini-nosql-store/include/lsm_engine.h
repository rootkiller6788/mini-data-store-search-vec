#ifndef LSM_ENGINE_H
#define LSM_ENGINE_H

#include <stdint.h>
#include <stddef.h>

#define LSM_MAX_KEY_LEN      64
#define LSM_MAX_VALUE_LEN   256
#define LSM_MEMTABLE_MAX    512
#define LSM_LEVEL0_MAX      4
#define LSM_LEVEL1_MAX      8
#define LSM_BLOCK_SIZE      4096
#define LSM_BLOOM_BITS      1024
#define LSM_BLOOM_HASHES    3
#define LSM_SKIPLIST_MAXLVL 12

typedef struct skiplist_node_t {
    char  key[LSM_MAX_KEY_LEN];
    char  value[LSM_MAX_VALUE_LEN];
    struct skiplist_node_t *forward[LSM_SKIPLIST_MAXLVL];
} SkipListNode;

typedef struct memtable_t {
    SkipListNode *head;
    int           count;
    int           max_count;
} MemTable;

typedef struct bloom_filter_t {
    uint8_t bits[LSM_BLOOM_BITS / 8];
} BloomFilter;

typedef struct sstable_t {
    int           id;
    int           level;
    size_t        data_size;
    char         *data_blocks;
    BloomFilter   bloom;
    int           key_count;
    char        (*keys)[LSM_MAX_KEY_LEN];
    int           (*block_index_offsets);
    int           index_size;
} SSTable;

typedef struct lsm_engine_t {
    MemTable  *memtable;
    MemTable **immutable_memtables;
    int        imm_count;
    SSTable  **level0;
    int        level0_count;
    SSTable  **level1;
    int        level1_count;
    int        next_sstable_id;
    char       data_dir[256];
} LSMEngine;

MemTable *memtable_create(int max_count);
void      memtable_destroy(MemTable *mt);
int       memtable_put(MemTable *mt, const char *key, const char *value);
int       memtable_get(MemTable *mt, const char *key, char *value_out, size_t max_len);

void      bloom_init(BloomFilter *bf);
void      bloom_add(BloomFilter *bf, const char *key);
int       bloom_check(BloomFilter *bf, const char *key);

SSTable  *sstable_create(int id, int level);
void      sstable_destroy(SSTable *sst);
int       sstable_add_key(SSTable *sst, const char *key, const char *value);
int       sstable_get(SSTable *sst, const char *key, char *value_out, size_t max_len);
SSTable  *sstable_from_memtable(MemTable *mt, int id, int level);

LSMEngine *lsm_create(const char *data_dir);
void       lsm_destroy(LSMEngine *engine);

int  lsm_put(LSMEngine *engine, const char *key, const char *value);
int  lsm_get(LSMEngine *engine, const char *key, char *value_out, size_t max_len);
int  lsm_delete(LSMEngine *engine, const char *key);

int  lsm_flush_memtable(LSMEngine *engine);
int  lsm_compact(LSMEngine *engine, int level);
int  lsm_compact_all(LSMEngine *engine);

int  lsm_count(LSMEngine *engine);

#endif
