#ifndef STREAM_PROCESSOR_H
#define STREAM_PROCESSOR_H

#include <stdint.h>
#include <stddef.h>

/* === L1: Core Definitions === */

#define MAX_WINDOW_SIZE         1024
#define MAX_AGGREGATOR_KEYS     32
#define MAX_AGGREGATIONS        8

/**
 * WindowType - stream processing window types
 * L2: Core concept - different window types for different use cases
 */
typedef enum {
    WINDOW_TUMBLING,    /* Fixed-size, non-overlapping */
    WINDOW_HOPPING,     /* Fixed-size, sliding by hop interval */
    WINDOW_SLIDING,     /* Continuous sliding, triggered by each event */
    WINDOW_SESSION      /* Dynamic, based on inactivity gap */
} WindowType;

/**
 * AggregationType - supported aggregation functions
 * L5: Each type implements a different associative & commutative operation
 */
typedef enum {
    AGG_COUNT,
    AGG_SUM,
    AGG_AVG,
    AGG_MIN,
    AGG_MAX,
    AGG_TOP_K,
    AGG_DISTINCT_COUNT
} AggregationType;

/**
 * AggregationResult - result of an aggregation operation
 */
typedef struct {
    AggregationType type;
    double          value_double;
    int64_t         value_int64;
    int             key_count;
} AggregationResult;

/**
 * WindowConfig - configuration for a windowed aggregation
 * L3: Engineering structure for window definition
 */
typedef struct {
    WindowType      type;
    int64_t         window_size_ms;     /* Window duration */
    int64_t         hop_ms;             /* Hop interval (hopping windows only) */
    int64_t         session_gap_ms;     /* Inactivity gap (session windows only) */
    int64_t         grace_period_ms;    /* L8: How long to wait for late data */
    int             max_events;         /* Max events in window */
} WindowConfig;

/**
 * WindowState - tracks events within a specific time window
 */
typedef struct {
    int64_t  window_start_ms;
    int64_t  window_end_ms;
    int      event_count;
    double   event_values[MAX_WINDOW_SIZE];
    int64_t  event_timestamps[MAX_WINDOW_SIZE];
    char*    event_keys[MAX_WINDOW_SIZE];  /* L7: keyed aggregation support */
    int      key_count;
    int      is_closed;   /* L8: watermark has passed window end */
} WindowState;

/**
 * StreamProcessor - main stream processing context
 * L3: Engineering structure for stateful stream processing
 */
typedef struct {
    WindowConfig    config;
    WindowState     windows[8];
    int             active_window_count;
    AggregationType aggregators[MAX_AGGREGATIONS];
    int             aggregator_count;
    int64_t         watermark_ms;      /* L8: event-time watermark */
    int64_t         total_events_processed;
    int64_t         total_late_events; /* Events arriving after watermark */
} StreamProcessor;

/* === L1: API Declarations === */

StreamProcessor* stream_processor_create(const WindowConfig *config);
void             stream_processor_destroy(StreamProcessor *sp);

/* Event ingestion */
int              stream_processor_push(StreamProcessor *sp, int64_t timestamp_ms,
                                       double value, const char *key);
int              stream_processor_push_batch(StreamProcessor *sp,
                                              const int64_t *timestamps,
                                              const double *values,
                                              const char **keys,
                                              int count);

/* L5: Aggregation operations */
int              stream_processor_add_aggregation(StreamProcessor *sp,
                                                   AggregationType agg_type);
AggregationResult stream_processor_aggregate_window(const WindowState *ws,
                                                     AggregationType type);
AggregationResult stream_processor_aggregate_current(StreamProcessor *sp,
                                                      AggregationType type);

/* L7: Application-level aggregations */
int              stream_processor_top_k(const WindowState *ws, int k,
                                         double *out_values, int *out_count);
int              stream_processor_distinct_count(const WindowState *ws);
double           stream_processor_percentile(const WindowState *ws,
                                              double percentile);

/* L8: Watermark-based window management */
int64_t          stream_processor_advance_watermark(StreamProcessor *sp,
                                                     int64_t new_watermark_ms);
int              stream_processor_close_expired_windows(StreamProcessor *sp);
int              stream_processor_get_late_event_count(const StreamProcessor *sp);

/* L4: Event time skew analysis */
double           stream_processor_estimate_event_time_skew(const StreamProcessor *sp);
int64_t          stream_processor_watermark_lag(const StreamProcessor *sp);

/* Window inspection */
int              stream_processor_active_window_count(const StreamProcessor *sp);
const WindowState* stream_processor_get_window(const StreamProcessor *sp,
                                                int index);

#endif /* STREAM_PROCESSOR_H */