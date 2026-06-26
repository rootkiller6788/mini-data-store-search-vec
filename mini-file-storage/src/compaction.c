#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "compaction.h"
#include "sstable.h"

/* ─────────────────────────────────────────────
   Internal: get current time in ms
   ───────────────────────────────────────────── */
static double now_ms(void) {
    return (double)clock() / (CLOCKS_PER_SEC / 1000.0);
}

/* ─────────────────────────────────────────────
   Multi-way merge compaction
   Opens iterators on all input SSTables,
   merge-sorts entries, writes output SSTable.
   ───────────────────────────────────────────── */
int compaction_merge(CompactionJob *job, CompactionStats *stats) {
    if (!job || !job->input_tables || job->num_inputs == 0) return -1;

    double t_start = now_ms();
    if (stats) memset(stats, 0, sizeof(CompactionStats));

    /* Collect all entries from all input SSTables */
    /* We'll use simple merge-iterator and buffer them */
    SSTableMergeIterator *merge_iter =
        sstable_merge_iterator_create(job->input_tables, job->num_inputs);
    if (!merge_iter) return -1;

    /* Phase 1: Gather all entries into a sorted array */
    uint32_t cap = 1024;
    uint32_t count = 0;
    uint8_t  **keys   = (uint8_t **)malloc(cap * sizeof(uint8_t *));
    uint32_t *key_lens = (uint32_t *)malloc(cap * sizeof(uint32_t));
    uint8_t  **vals   = (uint8_t **)malloc(cap * sizeof(uint8_t *));
    uint32_t *val_lens = (uint32_t *)malloc(cap * sizeof(uint32_t));
    uint8_t  key_buf[MAX_KEY_SIZE], val_buf[MAX_VALUE_SIZE];
    uint32_t klen, vlen;

    while (sstable_merge_iterator_next(merge_iter, key_buf, &klen, val_buf, &vlen)) {
        /* Dedup: keep the latest value for each key (last-write-wins).
           Since merge-iterator outputs sorted, check if previous key is same. */
        if (count > 0 && key_lens[count - 1] == klen &&
            memcmp(keys[count - 1], key_buf, klen) == 0) {
            /* Overwrite previous entry's value */
            memcpy(vals[count - 1], val_buf, vlen);
            val_lens[count - 1] = vlen;
            continue;
        }

        if (count >= cap) {
            cap *= 2;
            keys     = (uint8_t **)realloc(keys, cap * sizeof(uint8_t *));
            key_lens = (uint32_t *)realloc(key_lens, cap * sizeof(uint32_t));
            vals     = (uint8_t **)realloc(vals, cap * sizeof(uint8_t *));
            val_lens = (uint32_t *)realloc(val_lens, cap * sizeof(uint32_t));
        }
        keys[count]     = (uint8_t *)malloc(klen);
        memcpy(keys[count], key_buf, klen);
        key_lens[count] = klen;
        vals[count]     = (uint8_t *)malloc(vlen);
        memcpy(vals[count], val_buf, vlen);
        val_lens[count] = vlen;
        count++;
    }

    sstable_merge_iterator_destroy(merge_iter);

    if (stats) {
        stats->input_files = job->num_inputs;
        stats->bytes_read  = 0; /* approximate */
    }

    /* Phase 2: Write output SSTable */
    if (count > 0) {
        int rc = sstable_write(job->output_name,
                               (const uint8_t **)keys, key_lens,
                               (const uint8_t **)vals, val_lens, count);
        if (rc != 0) {
            for (uint32_t i = 0; i < count; i++) { free(keys[i]); free(vals[i]); }
            free(keys); free(key_lens); free(vals); free(val_lens);
            return -1;
        }
        /* Open output */
        job->output_table = sstable_read(job->output_name);
    }

    double t_end = now_ms();
    if (stats) {
        stats->output_files = (count > 0) ? 1 : 0;
        stats->duration_ms  = t_end - t_start;
    }

    /* Cleanup */
    for (uint32_t i = 0; i < count; i++) { free(keys[i]); free(vals[i]); }
    free(keys); free(key_lens); free(vals); free(val_lens);

    return 0;
}

/* ─────────────────────────────────────────────
   Leveled compaction: pick one SSTable from level N,
   find all overlapping files in level N+1, build job.
   Uses round-robin selection from level N.
   ───────────────────────────────────────────── */
int compaction_pick_leveled(SSTable **level_n, uint32_t num_n,
                             SSTable **level_np1, uint32_t num_np1,
                             CompactionJob *job_out) {
    if (!level_n || num_n == 0 || !job_out) return -1;
    memset(job_out, 0, sizeof(CompactionJob));

    static uint32_t round_robin_idx = 0;
    uint32_t pick_idx = round_robin_idx % num_n;
    round_robin_idx++;

    SSTable *chosen = level_n[pick_idx];
    SSTableIndexEntry *first = &chosen->index_block.entries[0];
    SSTableIndexEntry *last  = &chosen->index_block.entries[chosen->index_block.num_entries - 1];

    uint32_t overlap_count = 0;
    SSTable *overlap[COMPACTION_INPUT_MAX];

    for (uint32_t i = 0; i < num_np1 && overlap_count < COMPACTION_INPUT_MAX; i++) {
        SSTable *cand = level_np1[i];
        SSTableIndexEntry *fst = &cand->index_block.entries[0];
        SSTableIndexEntry *lst = &cand->index_block.entries[cand->index_block.num_entries - 1];

        int overlaps = 1;
        /* cand's last_key < chosen's first_key → no overlap */
        if (memcmp(lst->last_key, first->last_key,
                   lst->last_key_len < first->last_key_len
                   ? lst->last_key_len : first->last_key_len) < 0) {
            int cmp = memcmp(lst->last_key, first->last_key,
                             lst->last_key_len < first->last_key_len
                             ? lst->last_key_len : first->last_key_len);
            if (cmp < 0) overlaps = 0;
            else if (cmp == 0 && lst->last_key_len < first->last_key_len) overlaps = 0;
        }
        /* cand's first_key > chosen's last_key → no overlap */
        if (memcmp(fst->last_key, last->last_key,
                   fst->last_key_len < last->last_key_len
                   ? fst->last_key_len : last->last_key_len) > 0) {
            int cmp = memcmp(fst->last_key, last->last_key,
                             fst->last_key_len < last->last_key_len
                             ? fst->last_key_len : last->last_key_len);
            if (cmp > 0) overlaps = 0;
            else if (cmp == 0 && fst->last_key_len > last->last_key_len) overlaps = 0;
        }
        if (overlaps) {
            overlap[overlap_count++] = cand;
        }
    }

    /* Build input array: chose + all overlaps */
    uint32_t total = 1 + overlap_count;
    SSTable **inputs = (SSTable **)malloc(total * sizeof(SSTable *));
    inputs[0] = chosen;
    for (uint32_t i = 0; i < overlap_count; i++)
        inputs[i + 1] = overlap[i];

    job_out->input_tables = inputs;
    job_out->num_inputs   = total;
    job_out->level_n      = 0;   /* caller sets */
    job_out->level_np1    = 1;

    printf("[compaction_pick_leveled] Chose %u files (%u from N, %u overlapping from N+1)\n",
           total, 1u, overlap_count);
    return 0;
}

/* ─────────────────────────────────────────────
   Universal compaction: pick ALL files in level 0
   when count exceeds the threshold.
   ───────────────────────────────────────────── */
int compaction_pick_universal(SSTable **level0, uint32_t num_level0,
                               uint32_t threshold, CompactionJob *job_out) {
    if (!level0 || !job_out) return -1;
    if (num_level0 < threshold) return 0; /* not enough files yet */

    memset(job_out, 0, sizeof(CompactionJob));

    SSTable **inputs = (SSTable **)malloc(num_level0 * sizeof(SSTable *));
    for (uint32_t i = 0; i < num_level0; i++)
        inputs[i] = level0[i];

    job_out->input_tables = inputs;
    job_out->num_inputs   = num_level0;
    job_out->level_n      = 0;
    job_out->level_np1    = 1;

    printf("[compaction_pick_universal] Picking all %u files from Level 0 "
           "(threshold=%u)\n", num_level0, threshold);
    return 0;
}

/* ─────────────────────────────────────────────
   Print compaction statistics
   ───────────────────────────────────────────── */
void compaction_print_stats(const CompactionStats *stats) {
    if (!stats) return;
    printf("── Compaction Stats ──\n");
    printf("  Input files : %u\n", stats->input_files);
    printf("  Output files: %u\n", stats->output_files);
    printf("  Bytes read  : %llu\n", (unsigned long long)stats->bytes_read);
    printf("  Bytes written: %llu\n", (unsigned long long)stats->bytes_written);
    printf("  Duration    : %.2f ms\n", stats->duration_ms);
    printf("────────────────────────\n");
}
