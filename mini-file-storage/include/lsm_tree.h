#ifndef MINI_LSM_TREE_H
#define MINI_LSM_TREE_H

#include <stdint.h>
#include <stddef.h>
#include "skiplist.h"
#include "sstable.h"
#include "wal_file.h"

#define LSM_MAX_LEVELS        7
#define LSM_MEMTABLE_SIZE     (64 * 1024)       /* 64 KB  */
#define LSM_IMMUTABLE_MAX     4
#define LSM_LEVEL0_FILE_LIMIT 4
#define LSM_LEVEL_SIZE_RATIO  10

/* ─────────────────────────────────────────────
   Compaction strategy enumeration
   ───────────────────────────────────────────── */
typedef enum {
    COMPACTION_LEVELED = 0,
    COMPACTION_TIERED  = 1
} CompactionPicker;

/* ─────────────────────────────────────────────
   A single level holds an array of SSTable ptrs
   ───────────────────────────────────────────── */
typedef struct {
    SSTable  **files;
    uint32_t   num_files;
    uint32_t   capacity;
} LSLevel;

/* ─────────────────────────────────────────────
   Immutable memtable (frozen, ready to flush)
   ───────────────────────────────────────────── */
typedef struct ImmutableMem {
    SkipList       *table;
    struct ImmutableMem *next;
} ImmutableMem;

/* ─────────────────────────────────────────────
   LSMTree handle
   ───────────────────────────────────────────── */
typedef struct {
    char        *dir_path;
    SkipList    *memtable;
    ImmutableMem *imm_head;
    ImmutableMem *imm_tail;
    uint32_t      imm_count;
    LSLevel       levels[LSM_MAX_LEVELS];
    WALWriter     *wal;
    CompactionPicker compaction_picker;
    uint64_t      sequence;           /* global seqno  */
    uint32_t      sstable_counter;    /* for file naming */
} LSMTree;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

LSMTree *lsm_open(const char *dir_path, CompactionPicker picker);
int      lsm_put(LSMTree *tree,
                 const uint8_t *key, uint32_t key_len,
                 const uint8_t *value, uint32_t value_len);
int      lsm_get(LSMTree *tree,
                 const uint8_t *key, uint32_t key_len,
                 uint8_t *value_out, uint32_t *value_len_out);
int      lsm_delete(LSMTree *tree,
                    const uint8_t *key, uint32_t key_len);
int      lsm_maybe_compact(LSMTree *tree);
int      lsm_compact_level(LSMTree *tree, uint32_t level);
void     lsm_close(LSMTree *tree);

#endif /* MINI_LSM_TREE_H */
