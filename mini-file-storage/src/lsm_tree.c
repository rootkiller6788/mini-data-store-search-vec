#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lsm_tree.h"
#include "compaction.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#else
#include <sys/stat.h>
#include <sys/types.h>
#define mkdir_p(d) mkdir(d, 0755)
#endif

/* ─────────────────────────────────────────────
   Internal: generate SSTable filename
   ───────────────────────────────────────────── */
static void make_sst_name(char *buf, uint32_t bufsz,
                          const char *dir, uint32_t level, uint32_t seq) {
    snprintf(buf, bufsz, "%s/level%u_%06u.sst", dir, level, seq);
}

static void make_wal_name(char *buf, uint32_t bufsz, const char *dir) {
    snprintf(buf, bufsz, "%s/MINI_LOG", dir);
}

/* ─────────────────────────────────────────────
   Internal: flush memtable to SSTable (level 0)
   ───────────────────────────────────────────── */
static int flush_memtable_to_sstable(LSMTree *tree, SkipList *mem) {
    SkipListIterator *it = skiplist_iterator(mem);
    if (!it) return -1;

    uint32_t cap = 1024, count = 0;
    uint8_t  **keys   = (uint8_t **)malloc(cap * sizeof(uint8_t *));
    uint32_t *key_lens = (uint32_t *)malloc(cap * sizeof(uint32_t));
    uint8_t  **vals   = (uint8_t **)malloc(cap * sizeof(uint8_t *));
    uint32_t *val_lens = (uint32_t *)malloc(cap * sizeof(uint32_t));

    SkipNode *node;
    while ((node = skiplist_iterator_next(it)) != NULL) {
        if (count >= cap) {
            cap *= 2;
            keys     = (uint8_t **)realloc(keys, cap * sizeof(uint8_t *));
            key_lens = (uint32_t *)realloc(key_lens, cap * sizeof(uint32_t));
            vals     = (uint8_t **)realloc(vals, cap * sizeof(uint8_t *));
            val_lens = (uint32_t *)realloc(val_lens, cap * sizeof(uint32_t));
        }
        keys[count]     = (uint8_t *)malloc(node->key_len);
        memcpy(keys[count], node->key, node->key_len);
        key_lens[count] = node->key_len;
        vals[count]     = (uint8_t *)malloc(node->value_len);
        memcpy(vals[count], node->value, node->value_len);
        val_lens[count] = node->value_len;
        count++;
    }
    skiplist_iterator_destroy(it);

    if (count == 0) {
        for (uint32_t i = 0; i < count; i++) { free(keys[i]); free(vals[i]); }
        free(keys); free(key_lens); free(vals); free(val_lens);
        return 0;
    }

    char name[512];
    make_sst_name(name, sizeof(name), tree->dir_path, 0, tree->sstable_counter++);
    int rc = sstable_write(name, (const uint8_t **)keys, key_lens,
                           (const uint8_t **)vals, val_lens, count);

    if (rc == 0) {
        SSTable *sst = sstable_read(name);
        if (sst) {
            LSLevel *lv0 = &tree->levels[0];
            if (lv0->num_files >= lv0->capacity) {
                lv0->capacity = lv0->capacity == 0 ? 8 : lv0->capacity * 2;
                lv0->files = (SSTable **)realloc(lv0->files,
                    lv0->capacity * sizeof(SSTable *));
            }
            lv0->files[lv0->num_files++] = sst;
        }
    }

    for (uint32_t i = 0; i < count; i++) { free(keys[i]); free(vals[i]); }
    free(keys); free(key_lens); free(vals); free(val_lens);
    return rc;
}

/* ─────────────────────────────────────────────
   Internal: WAL replay callback
   ───────────────────────────────────────────── */
static int wal_replay_cb(uint8_t type, const uint8_t *key, uint32_t key_len,
                         const uint8_t *value, uint32_t value_len, void *ctx) {
    SkipList *mem = (SkipList *)ctx;
    if (type == WAL_RECORD_PUT) {
        skiplist_insert(mem, key, key_len, value, value_len);
    } else if (type == WAL_RECORD_DEL) {
        /* Tombstone: insert empty value */
        uint8_t empty = 0;
        skiplist_insert(mem, key, key_len, &empty, 1);
    }
    return 0;
}

/* ─────────────────────────────────────────────
   Open an LSM tree instance
   ───────────────────────────────────────────── */
LSMTree *lsm_open(const char *dir_path, CompactionPicker picker) {
    mkdir_p(dir_path);

    LSMTree *tree = (LSMTree *)calloc(1, sizeof(LSMTree));
    if (!tree) return NULL;

    tree->dir_path = strdup(dir_path);
    tree->compaction_picker = picker;
    tree->sequence = 0;
    tree->sstable_counter = 0;
    tree->memtable = skiplist_create();
    if (!tree->memtable) { free(tree->dir_path); free(tree); return NULL; }

    tree->imm_head = NULL;
    tree->imm_tail = NULL;
    tree->imm_count = 0;

    for (int i = 0; i < LSM_MAX_LEVELS; i++) {
        tree->levels[i].files = NULL;
        tree->levels[i].num_files = 0;
        tree->levels[i].capacity = 0;
    }

    /* Open WAL and recover */
    char wal_name[512];
    make_wal_name(wal_name, sizeof(wal_name), dir_path);
    tree->wal = wal_open(wal_name);
    if (tree->wal) {
        int rec = wal_recover(tree->wal, wal_replay_cb, tree->memtable);
        if (rec > 0) {
            printf("[lsm_open] Recovered %d WAL records\n", rec);
        }
    }

    printf("[lsm_open] LSM tree opened at: %s (picker=%s)\n",
           dir_path, picker == COMPACTION_LEVELED ? "leveled" : "tiered");
    return tree;
}

/* ─────────────────────────────────────────────
   Make memtable immutable (freeze for flush)
   ───────────────────────────────────────────── */
static void freeze_memtable(LSMTree *tree) {
    ImmutableMem *imm = (ImmutableMem *)calloc(1, sizeof(ImmutableMem));
    imm->table = tree->memtable;
    imm->next = NULL;

    if (!tree->imm_tail) {
        tree->imm_head = tree->imm_tail = imm;
    } else {
        tree->imm_tail->next = imm;
        tree->imm_tail = imm;
    }
    tree->imm_count++;

    tree->memtable = skiplist_create();
}

/* ─────────────────────────────────────────────
   Flush the oldest immutable memtable
   ───────────────────────────────────────────── */
static int flush_oldest_immutable(LSMTree *tree) {
    if (!tree->imm_head) return -1;
    ImmutableMem *imm = tree->imm_head;
    int rc = flush_memtable_to_sstable(tree, imm->table);
    if (rc == 0) {
        skiplist_destroy(imm->table);
        tree->imm_head = imm->next;
        if (!tree->imm_head) tree->imm_tail = NULL;
        tree->imm_count--;
        free(imm);
    }
    return rc;
}

/* ─────────────────────────────────────────────
   Put a key-value pair
   ───────────────────────────────────────────── */
int lsm_put(LSMTree *tree,
            const uint8_t *key, uint32_t key_len,
            const uint8_t *value, uint32_t value_len) {
    if (!tree || !tree->memtable) return -1;

    /* WAL first */
    if (tree->wal) {
        wal_append(tree->wal, WAL_RECORD_PUT, key, key_len, value, value_len);
    }

    int rc = skiplist_insert(tree->memtable, key, key_len, value, value_len);
    tree->sequence++;

    /* Check if memtable is full */
    if (tree->memtable->size >= (LSM_MEMTABLE_SIZE / 128)) {
        freeze_memtable(tree);
        /* Flush old immutables if too many */
        while (tree->imm_count > LSM_IMMUTABLE_MAX) {
            flush_oldest_immutable(tree);
        }
    }

    return rc;
}

/* ─────────────────────────────────────────────
   Delete a key (write tombstone)
   ───────────────────────────────────────────── */
int lsm_delete(LSMTree *tree,
               const uint8_t *key, uint32_t key_len) {
    if (!tree) return -1;
    uint8_t tombstone[] = "__TOMBSTONE__";
    uint32_t tlen = 13;
    return lsm_put(tree, key, key_len, tombstone, tlen);
}

/* ─────────────────────────────────────────────
   Get a key (search memtable → immutable → levels)
   ───────────────────────────────────────────── */
int lsm_get(LSMTree *tree,
            const uint8_t *key, uint32_t key_len,
            uint8_t *value_out, uint32_t *value_len_out) {
    if (!tree) return -1;

    SkipNode *node;

    /* 1. Check active memtable */
    node = skiplist_search(tree->memtable, key, key_len);
    if (node) {
        /* Check if tombstone */
        if (node->value_len == 13 &&
            memcmp(node->value, "__TOMBSTONE__", 13) == 0)
            return 0; /* deleted */
        if (value_out) memcpy(value_out, node->value, node->value_len);
        if (value_len_out) *value_len_out = node->value_len;
        return 1;
    }

    /* 2. Check immutable memtables (from newest to oldest)
       linked list is head=oldest, tail=newest. Search from tail (newest)
       to head (oldest) so newer entries override older ones. */
    /* Build a stack of immutables for reverse traversal */
    {
        ImmutableMem *stack[LSM_IMMUTABLE_MAX + 1];
        uint32_t sc = 0;
        for (ImmutableMem *p = tree->imm_head; p; p = p->next)
            stack[sc++] = p;
        for (int i = (int)sc - 1; i >= 0; i--) {
            node = skiplist_search(stack[i]->table, key, key_len);
            if (node) {
                if (node->value_len == 13 &&
                    memcmp(node->value, "__TOMBSTONE__", 13) == 0)
                    return 0;
                if (value_out) memcpy(value_out, node->value, node->value_len);
                if (value_len_out) *value_len_out = node->value_len;
                return 1;
            }
        }
    }

    /* 3. Check SSTable levels (level 0 → level 6) */
    for (int lv = 0; lv < LSM_MAX_LEVELS; lv++) {
        LSLevel *level = &tree->levels[lv];
        /* Level 0: files may overlap, check all from newest */
        if (lv == 0) {
            for (int f = (int)level->num_files - 1; f >= 0; f--) {
                SSTable *sst = level->files[f];
                if (sstable_get(sst, key, key_len, value_out, value_len_out) > 0) {
                    /* Check tombstone */
                    if (*value_len_out == 13 && value_out &&
                        memcmp(value_out, "__TOMBSTONE__", 13) == 0)
                        return 0;
                    return 1;
                }
            }
        } else {
            /* Level 1+: non-overlapping, binary search across files */
            uint32_t lo = 0, hi = level->num_files;
            while (lo < hi) {
                uint32_t mid = lo + (hi - lo) / 2;
                SSTableIndexEntry *first = &level->files[mid]->index_block.entries[0];
                int cmp = memcmp(key, first->last_key,
                                 key_len < first->last_key_len ? key_len : first->last_key_len);
                if (cmp > 0) lo = mid + 1;
                else hi = mid;
            }
            if (lo < level->num_files) {
                SSTable *sst = level->files[lo];
                if (sstable_get(sst, key, key_len, value_out, value_len_out) > 0) {
                    if (*value_len_out == 13 && value_out &&
                        memcmp(value_out, "__TOMBSTONE__", 13) == 0)
                        return 0;
                    return 1;
                }
            }
        }
    }

    return 0; /* not found */
}

/* ─────────────────────────────────────────────
   Check if compaction is needed and run it
   ───────────────────────────────────────────── */
int lsm_maybe_compact(LSMTree *tree) {
    if (!tree) return -1;
    int total_compacted = 0;

    /* Flush immutables first */
    while (tree->imm_count > 0) {
        flush_oldest_immutable(tree);
    }

    /* Level 0: if file count > threshold, compact to level 1 */
    if (tree->compaction_picker == COMPACTION_LEVELED) {
        while (tree->levels[0].num_files > LSM_LEVEL0_FILE_LIMIT) {
            lsm_compact_level(tree, 0);
            total_compacted++;
        }
        /* Cascade to higher levels */
        for (int lv = 0; lv < LSM_MAX_LEVELS - 1; lv++) {
            uint32_t limit = 1;
            for (int i = 0; i <= lv; i++) limit *= LSM_LEVEL_SIZE_RATIO;
            while (tree->levels[lv].num_files > limit) {
                lsm_compact_level(tree, lv);
                total_compacted++;
            }
        }
    } else {
        /* Tiered / Universal: pick all of level 0 */
        if (tree->levels[0].num_files >= 4) {
            lsm_compact_level(tree, 0);
            total_compacted++;
        }
    }

    return total_compacted;
}

/* ─────────────────────────────────────────────
   Compact level N into level N+1
   ───────────────────────────────────────────── */
int lsm_compact_level(LSMTree *tree, uint32_t level) {
    if (level >= LSM_MAX_LEVELS - 1) return -1;
    LSLevel *src = &tree->levels[level];
    LSLevel *dst = &tree->levels[level + 1];
    if (src->num_files == 0) return 0;

    CompactionJob job;
    memset(&job, 0, sizeof(job));
    job.input_tables = src->files;
    job.num_inputs   = src->num_files;
    job.level_n      = level;
    job.level_np1    = level + 1;
    make_sst_name(job.output_name, sizeof(job.output_name),
                  tree->dir_path, level + 1, tree->sstable_counter++);

    printf("[compact] Level %u -> %u: %u files\n",
           level, level + 1, src->num_files);

    CompactionStats cstats;
    int rc = compaction_merge(&job, &cstats);
    compaction_print_stats(&cstats);

    if (rc == 0 && job.output_table) {
        /* Add output to destination level */
        if (dst->num_files >= dst->capacity) {
            dst->capacity = dst->capacity == 0 ? 8 : dst->capacity * 2;
            dst->files = (SSTable **)realloc(dst->files,
                dst->capacity * sizeof(SSTable *));
        }
        /* Insert maintaining sorted order (by first key) */
        uint32_t ins = dst->num_files;
        SSTableIndexEntry *new_first = &job.output_table->index_block.entries[0];
        for (uint32_t i = 0; i < dst->num_files; i++) {
            SSTableIndexEntry *ei = &dst->files[i]->index_block.entries[0];
            if (memcmp(new_first->last_key, ei->last_key,
                       new_first->last_key_len < ei->last_key_len
                       ? new_first->last_key_len : ei->last_key_len) < 0) {
                ins = i;
                break;
            }
        }
        for (uint32_t i = dst->num_files; i > ins; i--)
            dst->files[i] = dst->files[i - 1];
        dst->files[ins] = job.output_table;
        dst->num_files++;

        /* Delete old SSTable files from source level */
        for (uint32_t i = 0; i < src->num_files; i++) {
            /* Remove the file from disk */
            if (src->files[i]->filename)
                remove(src->files[i]->filename);
            sstable_destroy(src->files[i]);
        }
        free(src->files);
        src->files = NULL;
        src->num_files = 0;
        src->capacity = 0;
    }

    return rc;
}

/* ─────────────────────────────────────────────
   Close the LSM tree (flush, compact, cleanup)
   ───────────────────────────────────────────── */
void lsm_close(LSMTree *tree) {
    if (!tree) return;

    /* Flush all immutables */
    while (tree->imm_count > 0) {
        flush_oldest_immutable(tree);
    }

    /* Flush memtable */
    freeze_memtable(tree);
    while (tree->imm_count > 0) {
        flush_oldest_immutable(tree);
    }

    /* Compaction one last time */
    lsm_maybe_compact(tree);

    /* Close WAL */
    if (tree->wal) {
        wal_close(tree->wal);
        tree->wal = NULL;
    }

    /* Destroy memtable */
    if (tree->memtable) {
        skiplist_destroy(tree->memtable);
        tree->memtable = NULL;
    }

    /* Destroy all SSTables */
    for (int lv = 0; lv < LSM_MAX_LEVELS; lv++) {
        for (uint32_t i = 0; i < tree->levels[lv].num_files; i++) {
            sstable_destroy(tree->levels[lv].files[i]);
        }
        free(tree->levels[lv].files);
    }

    free(tree->dir_path);
    free(tree);
    printf("[lsm_close] LSM tree closed.\n");
}
