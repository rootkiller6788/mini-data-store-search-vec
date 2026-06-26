#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "log_cleaner.h"

/* ================================================================
 * L2: Log Retention & Compaction
 *
 * Kafka's log cleaning combines two strategies:
 *
 * 1. Time/Size Retention (DELETE):
 *    - Delete segments older than retention.ms
 *    - Delete oldest segments when partition exceeds retention.bytes
 *    - Simple, non-selective cleanup
 *
 * 2. Log Compaction (COMPACT):
 *    - Keep only the latest value for each key
 *    - Enables key-based state reconstruction
 *    - Used for changelog topics, KTable state stores
 *
 * L4: Compaction guarantees:
 *   - For any key K, at least the last known value is retained
 *   - Tombstones (null values) may be removed after delete.retention.ms
 *   - Compaction is non-blocking: reads work on the old log during cleaning
 *
 * Reference: Kafka Log Compaction Design (KIP-14, KIP-101, KIP-280)
 * Course: CMU 15-445 (Database Systems) - Log-structured merge trees
 *         Berkeley CS 186 (Databases) - Write-optimized data structures
 * ================================================================ */

int log_cleaner_config_init(LogCleanerConfig *cfg, RetentionPolicy policy,
                             int64_t retention_ms, int64_t max_bytes)
{
    if (!cfg) return -1;
    memset(cfg, 0, sizeof(LogCleanerConfig));
    cfg->policy = policy;
    cfg->retention_ms = retention_ms;
    cfg->max_partition_bytes = max_bytes;
    cfg->min_cleanable_ratio = CLEANER_DIRTY_RATIO;
    cfg->min_compaction_lag_ms = 0;
    cfg->delete_retention_ms = 86400000;  /* 24 hours default */
    return 0;
}

int log_cleaner_config_validate(const LogCleanerConfig *cfg)
{
    if (!cfg) return -1;
    if (cfg->retention_ms < 0) return -1;
    if (cfg->max_partition_bytes < -1) return -1;
    /* -1 means "no size limit" */
    if (cfg->min_cleanable_ratio < 0.0 || cfg->min_cleanable_ratio > 1.0)
        return -1;
    return 0;
}

/* ================================================================
 * L5: Time-Based Retention
 *
 * Delete log segments whose max timestamp + retention_ms < now_ms.
 *
 * Algorithm: Scan segments from oldest to newest.
 * For each inactive segment, check if all records are expired.
 * Active segment is never deleted.
 *
 * L4: Segment immutability - once a segment is closed (rolled),
 * it becomes immutable. Immutable segments are safe to delete.
 * This property simplifies concurrent read/write safety.
 *
 * Time Complexity: O(num_segments) per partition
 * ================================================================ */

int log_cleaner_delete_expired_segments(Partition *p,
                                         const LogCleanerConfig *cfg,
                                         int64_t now_ms, CleanerStats *stats)
{
    int i, deleted;

    if (!p || !cfg) return -1;

    deleted = 0;

    for (i = 0; i < p->segment_count; i++) {
        LogSegment *seg = &p->segments[i];

        /* Never delete the active segment */
        if (seg->is_active) continue;

        /* Calculate segment age: use the max timestamp of its records */
        int64_t max_ts = 0;
        int j;
        for (j = 0; j < seg->size; j++) {
            if (seg->records[j].timestamp > max_ts) {
                max_ts = seg->records[j].timestamp;
            }
        }

        /* If the newest record in this segment is expired, delete it */
        if (max_ts > 0 && (now_ms - max_ts) > cfg->retention_ms) {
            if (stats) {
                stats->segments_scanned++;
                stats->segments_deleted++;
                stats->bytes_before_cleaning +=
                    (int64_t)(seg->size * (int)sizeof(Record));
            }

            printf("log-cleaner(part=%d): deleting segment %d (base_offset=%"
                   PRId64 ", max_ts=%" PRId64 ", age=%" PRId64 "ms)\n",
                   p->id, i, seg->base_offset, max_ts, now_ms - max_ts);

            /* Mark segment as empty (simple implementation) */
            seg->size = 0;
            deleted++;
        } else if (stats) {
            stats->segments_scanned++;
        }
    }

    return deleted;
}

/* ================================================================
 * L5: Size-Based Retention
 *
 * When partition exceeds max_partition_bytes, delete oldest segments
 * until total size <= limit.
 *
 * Algorithm: Calculate total bytes, then delete from oldest to newest
 * until under the limit. Active segment is protected.
 *
 * Time Complexity: O(num_segments)
 * ================================================================ */

int log_cleaner_delete_by_size(Partition *p, const LogCleanerConfig *cfg,
                                CleanerStats *stats)
{
    int64_t total_bytes, target;
    int i, deleted;

    if (!p || !cfg || cfg->max_partition_bytes <= 0) return -1;

    total_bytes = 0;
    for (i = 0; i < p->segment_count; i++) {
        total_bytes += (int64_t)p->segments[i].size * (int64_t)sizeof(Record);
    }

    if (stats) stats->bytes_before_cleaning = total_bytes;

    if (total_bytes <= cfg->max_partition_bytes) return 0;

    target = cfg->max_partition_bytes;
    deleted = 0;

    for (i = 0; i < p->segment_count && total_bytes > target; i++) {
        LogSegment *seg = &p->segments[i];
        if (seg->is_active) continue;

        int64_t seg_bytes = (int64_t)seg->size * (int64_t)sizeof(Record);

        printf("log-cleaner(part=%d): size-based delete segment %d "
               "(%" PRId64 " bytes), total=%" PRId64 ", target=%" PRId64 "\n",
               p->id, i, seg_bytes, total_bytes, target);

        seg->size = 0;
        total_bytes -= seg_bytes;
        deleted++;

        if (stats) stats->segments_deleted++;
    }

    if (stats) stats->bytes_after_cleaning = total_bytes;

    return deleted;
}

/* ================================================================
 * L5: Log Compaction
 *
 * Key-based deduplication: for each unique key, keep only the
 * record with the highest offset (latest value).
 *
 * Two-phase algorithm:
 *   Phase 1 (build_map): Scan all segments, build compaction map.
 *     For each key, keep the latest (highest offset) entry.
 *   Phase 2 (apply_map): Create new compacted segment with only
 *     the entries in the map.
 *
 * L4: Correctness - after compaction, for any key K:
 *   - If K has a non-tombstone latest value, it's retained
 *   - If K has a tombstone, it's retained until delete.retention.ms passes
 *   - Old values for K are removed (space reclamation)
 *
 * This is analogous to LSM-tree compaction (LevelDB/RocksDB).
 *
 * Time Complexity: O(N) to build map, O(M) to apply where N=total records,
 * M=unique keys
 * Space Complexity: O(M) for the compaction map
 * ================================================================ */

int log_compact_build_map(const Partition *p, LogCompactionMap *map)
{
    int seg_idx, rec_idx;
    int i;

    if (!p || !map) return -1;

    map->entry_count = 0;

    /* Scan all segments (oldest to newest) */
    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        const LogSegment *seg = &p->segments[seg_idx];

        for (rec_idx = 0; rec_idx < seg->size; rec_idx++) {
            const Record *rec = &seg->records[rec_idx];
            const char *key = rec->key[0] ? rec->key : NULL;
            int found = 0;

            if (!key || key[0] == '\0') {
                /* Records without keys are always retained */
                continue;
            }

            /* Check if key already exists in map */
            for (i = 0; i < map->entry_count; i++) {
                if (strcmp(map->entries[i].key, key) == 0) {
                    /* Update to latest (higher offset) */
                    if (rec->offset > map->entries[i].offset) {
                        map->entries[i].offset = rec->offset;
                        map->entries[i].timestamp = rec->timestamp;
                        map->entries[i].is_tombstone =
                            (rec->value[0] == '\0') ? 1 : 0;
                    }
                    found = 1;
                    break;
                }
            }

            if (!found && map->entry_count < MAX_COMPACTION_MAP_SIZE) {
                i = map->entry_count;
                strncpy(map->entries[i].key, key, 255);
                map->entries[i].key[255] = '\0';
                map->entries[i].offset = rec->offset;
                map->entries[i].timestamp = rec->timestamp;
                map->entries[i].is_tombstone =
                    (rec->value[0] == '\0') ? 1 : 0;
                map->entry_count++;
            }
        }
    }

    printf("log-compact(part=%d): built map with %d unique keys\n",
           p->id, map->entry_count);

    return 0;
}

int log_compact_apply_map(Partition *p, const LogCompactionMap *map,
                           CleanerStats *stats)
{
    int seg_idx, rec_idx, i, retained, removed;

    if (!p || !map) return -1;

    retained = 0;
    removed = 0;

    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        LogSegment *seg = &p->segments[seg_idx];

        if (seg->is_active) continue;  /* Don't compact active segment */

        for (rec_idx = 0; rec_idx < seg->size; rec_idx++) {
            Record *rec = &seg->records[rec_idx];
            const char *key = rec->key[0] ? rec->key : NULL;

            if (!key || key[0] == '\0') {
                retained++;
                continue;  /* Records without keys always retained */
            }

            /* Check if this is the latest record for this key */
            int is_latest = 0;
            for (i = 0; i < map->entry_count; i++) {
                if (strcmp(map->entries[i].key, key) == 0 &&
                    map->entries[i].offset == rec->offset) {
                    is_latest = 1;
                    break;
                }
            }

            if (is_latest) {
                retained++;
            } else {
                /* Remove this record by zeroing it out */
                rec->value[0] = '\0';
                rec->key[0] = '\0';
                removed++;
            }
        }
    }

    if (stats) {
        stats->segments_compacted++;
        stats->tombstones_removed += removed;
    }

    printf("log-compact(part=%d): retained=%d, removed=%d\n",
           p->id, retained, removed);

    return removed;
}

int log_compact_partition(Partition *p, const LogCleanerConfig *cfg,
                           int64_t now_ms, CleanerStats *stats)
{
    LogCompactionMap map;
    int removed;

    (void)now_ms;  /* Reserved for future time-based compaction gating */
    if (!p || !cfg) return -1;

    memset(&map, 0, sizeof(map));

    if (log_compact_build_map(p, &map) != 0) return -1;
    removed = log_compact_apply_map(p, &map, stats);

    return removed;
}

/* ================================================================
 * L8: Tombstone Management
 *
 * Tombstones are delete markers (records with empty value).
 * They must be retained for delete.retention.ms to ensure all
 * consumers see the deletion. After that period, tombstones can
 * be removed, permanently deleting the key.
 *
 * This is critical for GDPR compliance (right to be forgotten).
 * ================================================================ */

int log_cleaner_count_tombstones(const Partition *p)
{
    int seg_idx, rec_idx, count;
    if (!p) return -1;
    count = 0;
    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        const LogSegment *seg = &p->segments[seg_idx];
        for (rec_idx = 0; rec_idx < seg->size; rec_idx++) {
            if (seg->records[rec_idx].value[0] == '\0' &&
                seg->records[rec_idx].key[0] != '\0') {
                count++;
            }
        }
    }
    return count;
}

int log_cleaner_remove_expired_tombstones(Partition *p,
                                           const LogCleanerConfig *cfg,
                                           int64_t now_ms, CleanerStats *stats)
{
    int seg_idx, rec_idx, removed;
    if (!p || !cfg) return -1;

    removed = 0;
    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        LogSegment *seg = &p->segments[seg_idx];
        if (seg->is_active) continue;

        for (rec_idx = 0; rec_idx < seg->size; rec_idx++) {
            Record *rec = &seg->records[rec_idx];
            /* Check if this is a tombstone */
            if (rec->value[0] == '\0' && rec->key[0] != '\0') {
                int64_t age = now_ms - rec->timestamp;
                if (age > cfg->delete_retention_ms) {
                    /* Remove tombstone */
                    rec->key[0] = '\0';
                    removed++;
                }
            }
        }
    }

    if (stats) stats->tombstones_removed += removed;

    if (removed > 0) {
        printf("log-cleaner(part=%d): removed %d expired tombstones\n",
               p->id, removed);
    }

    return removed;
}

/* ================================================================
 * L6: End-to-End Cleaning Cycle
 *
 * A full cleaning cycle runs time-based retention, size-based retention,
 * compaction, and tombstone removal in sequence.
 *
 * This is the canonical "log cleaning" problem - every log-based
 * storage system needs it (Kafka, Pulsar, DistributedLog, etc.)
 * ================================================================ */

int log_cleaner_run_cycle(Partition *p, const LogCleanerConfig *cfg,
                           int64_t now_ms, CleanerStats *stats)
{
    int total_actions;

    if (!p || !cfg) return -1;

    total_actions = 0;

    /* 1. Time-based retention */
    if (cfg->policy == RETENTION_TIME || cfg->policy == RETENTION_SIZE) {
        int n = log_cleaner_delete_expired_segments(p, cfg, now_ms, stats);
        if (n > 0) total_actions += n;
    }

    /* 2. Size-based retention */
    if (cfg->policy == RETENTION_SIZE) {
        int n = log_cleaner_delete_by_size(p, cfg, stats);
        if (n > 0) total_actions += n;
    }

    /* 3. Log compaction */
    if (cfg->policy == RETENTION_COMPACTION) {
        int n = log_compact_partition(p, cfg, now_ms, stats);
        if (n > 0) total_actions += n;
    }

    /* 4. Tombstone cleanup */
    {
        int n = log_cleaner_remove_expired_tombstones(p, cfg, now_ms, stats);
        if (n > 0) total_actions += n;
    }

    return total_actions;
}

/* ================================================================
 * L7: Application Utilities
 * ================================================================ */

int64_t log_cleaner_calculate_retention_bytes(const Partition *p)
{
    int i;
    int64_t total;
    if (!p) return -1;
    total = 0;
    for (i = 0; i < p->segment_count; i++) {
        total += (int64_t)p->segments[i].size * (int64_t)sizeof(Record);
    }
    return total;
}

double log_cleaner_calculate_dirty_ratio(const Partition *p)
{
    int seg_idx, rec_idx, total_records, dirty_records;
    LogCompactionMap map;
    int i;

    if (!p) return -1.0;

    total_records = 0;
    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        total_records += p->segments[seg_idx].size;
    }

    if (total_records == 0) return 0.0;

    /* Build compaction map to find dirty records */
    memset(&map, 0, sizeof(map));

    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        const LogSegment *seg = &p->segments[seg_idx];
        for (rec_idx = 0; rec_idx < seg->size; rec_idx++) {
            const Record *rec = &seg->records[rec_idx];
            const char *key = rec->key[0] ? rec->key : NULL;
            if (!key || key[0] == '\0') continue;

            for (i = 0; i < map.entry_count; i++) {
                if (strcmp(map.entries[i].key, key) == 0) {
                    map.entries[i].offset = rec->offset;
                    break;
                }
            }
            if (i >= map.entry_count && map.entry_count < MAX_COMPACTION_MAP_SIZE) {
                strncpy(map.entries[map.entry_count].key, key, 255);
                map.entries[map.entry_count].offset = rec->offset;
                map.entry_count++;
            }
        }
    }

    /* Count dirty records: records whose offset < latest for their key */
    dirty_records = 0;
    for (seg_idx = 0; seg_idx < p->segment_count; seg_idx++) {
        const LogSegment *seg = &p->segments[seg_idx];
        for (rec_idx = 0; rec_idx < seg->size; rec_idx++) {
            const Record *rec = &seg->records[rec_idx];
            const char *key = rec->key[0] ? rec->key : NULL;
            if (!key || key[0] == '\0') continue;

            for (i = 0; i < map.entry_count; i++) {
                if (strcmp(map.entries[i].key, key) == 0 &&
                    rec->offset < map.entries[i].offset) {
                    dirty_records++;
                    break;
                }
            }
        }
    }

    return (double)dirty_records / (double)total_records;
}

int log_cleaner_estimate_cleanable_segments(const Partition *p,
                                             const LogCleanerConfig *cfg)
{
    int i, cleanable;
    if (!p || !cfg) return -1;

    cleanable = 0;
    for (i = 0; i < p->segment_count; i++) {
        if (!p->segments[i].is_active) cleanable++;
    }
    return cleanable;
}

/* ================================================================
 * Stats reporting
 * ================================================================ */

void log_cleaner_stats_print(const CleanerStats *stats)
{
    if (!stats) return;

    printf("=== Log Cleaner Stats ===\n");
    printf("  segments scanned:    %" PRId64 "\n", stats->segments_scanned);
    printf("  segments deleted:    %" PRId64 "\n", stats->segments_deleted);
    printf("  segments compacted:  %" PRId64 "\n", stats->segments_compacted);
    printf("  bytes before:        %" PRId64 "\n", stats->bytes_before_cleaning);
    printf("  bytes after:         %" PRId64 "\n", stats->bytes_after_cleaning);
    printf("  tombstones removed:  %" PRId64 "\n", stats->tombstones_removed);
    printf("  dirty ratio:         %.4f\n", stats->dirty_ratio);

    int64_t saved = stats->bytes_before_cleaning - stats->bytes_after_cleaning;
    if (stats->bytes_before_cleaning > 0) {
        double pct = 100.0 * (double)saved / (double)stats->bytes_before_cleaning;
        printf("  space saved:         %" PRId64 " bytes (%.1f%%)\n", saved, pct);
    }
}

void log_cleaner_stats_reset(CleanerStats *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(CleanerStats));
}