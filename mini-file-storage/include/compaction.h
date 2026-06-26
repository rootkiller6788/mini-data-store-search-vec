#ifndef MINI_COMPACTION_H
#define MINI_COMPACTION_H

#include <stdint.h>
#include <stddef.h>
#include "sstable.h"

#define COMPACTION_INPUT_MAX 8
#define COMPACTION_OUTPUT_TARGET_SIZE (2 * 1024 * 1024)  /* 2 MB */

/* ─────────────────────────────────────────────
   Compaction Job
   ───────────────────────────────────────────── */
typedef struct {
    SSTable  **input_tables;
    uint32_t   num_inputs;
    SSTable   *output_table;
    char       output_name[512];
    uint32_t   level_n;
    uint32_t   level_np1;
} CompactionJob;

/* ─────────────────────────────────────────────
   Compaction statistics
   ───────────────────────────────────────────── */
typedef struct {
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint32_t input_files;
    uint32_t output_files;
    double   duration_ms;
} CompactionStats;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

int  compaction_merge(CompactionJob *job, CompactionStats *stats);

int  compaction_pick_leveled(SSTable **level_n, uint32_t num_n,
                              SSTable **level_np1, uint32_t num_np1,
                              CompactionJob *job_out);
int  compaction_pick_universal(SSTable **level0, uint32_t num_level0,
                                uint32_t threshold, CompactionJob *job_out);

void compaction_print_stats(const CompactionStats *stats);

#endif /* MINI_COMPACTION_H */
