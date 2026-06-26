#ifndef LOG_CLEANER_H
#define LOG_CLEANER_H

#include <stdint.h>
#include "topic_partition.h"

/* === L1: Core Definitions === */

#define MAX_CLEANER_THREADS     4
#define MAX_COMPACTION_MAP_SIZE 1024
#define CLEANER_DIRTY_RATIO     0.5    /* min dirty ratio to trigger cleaning */

/**
 * RetentionPolicy - log retention strategy
 * L2: Core concept - Kafka supports time-based, size-based, and compaction
 */
typedef enum {
    RETENTION_TIME,        /* Delete segments older than retention_ms */
    RETENTION_SIZE,        /* Delete oldest segments when total size exceeds limit */
    RETENTION_COMPACTION   /* Keep only latest value per key */
} RetentionPolicy;

/**
 * LogCleanerConfig - configuration for the log cleaner
 * L3: Engineering structure
 */
typedef struct {
    RetentionPolicy  policy;
    int64_t          retention_ms;          /* Time-based: max age of segments */
    int64_t          max_partition_bytes;    /* Size-based: max bytes per partition */
    double           min_cleanable_ratio;    /* Min dirty/total ratio to trigger clean */
    int              min_compaction_lag_ms;  /* Min time before message compactable */
    int64_t          delete_retention_ms;    /* How long to retain tombstones */
} LogCleanerConfig;

/**
 * CompactionEntry - key-value mapping for log compaction
 * L3: Engineering structure for key-based deduplication
 */
typedef struct {
    char     key[256];
    int64_t  offset;           /* Latest offset for this key */
    int64_t  timestamp;
    int      is_tombstone;     /* 1 if value is null (delete marker) */
} CompactionEntry;

/**
 * LogCompactionMap - hash-like structure for compaction state
 */
typedef struct {
    CompactionEntry entries[MAX_COMPACTION_MAP_SIZE];
    int             entry_count;
} LogCompactionMap;

/**
 * CleanerStats - statistics from log cleaning operations
 */
typedef struct {
    int64_t  segments_scanned;
    int64_t  segments_deleted;
    int64_t  segments_compacted;
    int64_t  bytes_before_cleaning;
    int64_t  bytes_after_cleaning;
    int64_t  tombstones_removed;
    double   dirty_ratio;
} CleanerStats;

/* === L1: API Declarations === */

/* Configuration */
int             log_cleaner_config_init(LogCleanerConfig *cfg, RetentionPolicy policy,
                                        int64_t retention_ms, int64_t max_bytes);
int             log_cleaner_config_validate(const LogCleanerConfig *cfg);

/* L5: Segment-level cleaning operations */
int             log_cleaner_delete_expired_segments(Partition *p,
                                                     const LogCleanerConfig *cfg,
                                                     int64_t now_ms,
                                                     CleanerStats *stats);
int             log_cleaner_delete_by_size(Partition *p,
                                            const LogCleanerConfig *cfg,
                                            CleanerStats *stats);

/* L5: Log compaction - keep only latest value per key */
int             log_compact_partition(Partition *p, const LogCleanerConfig *cfg,
                                       int64_t now_ms, CleanerStats *stats);
int             log_compact_build_map(const Partition *p,
                                       LogCompactionMap *map);
int             log_compact_apply_map(Partition *p, const LogCompactionMap *map,
                                       CleanerStats *stats);

/* L7: Application-level retention management */
int64_t         log_cleaner_calculate_retention_bytes(const Partition *p);
double          log_cleaner_calculate_dirty_ratio(const Partition *p);
int             log_cleaner_estimate_cleanable_segments(const Partition *p,
                                                         const LogCleanerConfig *cfg);

/* L8: Tombstone management */
int             log_cleaner_remove_expired_tombstones(Partition *p,
                                                       const LogCleanerConfig *cfg,
                                                       int64_t now_ms,
                                                       CleanerStats *stats);
int             log_cleaner_count_tombstones(const Partition *p);

/* L6: End-to-end cleaning cycle */
int             log_cleaner_run_cycle(Partition *p, const LogCleanerConfig *cfg,
                                       int64_t now_ms, CleanerStats *stats);

/* Stats reporting */
void            log_cleaner_stats_print(const CleanerStats *stats);
void            log_cleaner_stats_reset(CleanerStats *stats);

#endif /* LOG_CLEANER_H */