#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#include "stream_processor.h"

/* ================================================================
 * L2: Stream Processing Concepts
 *
 * Event Time vs Processing Time:
 *   - Event time: when the event actually occurred (from source)
 *   - Processing time: when the system processes the event
 *   - Watermark: threshold indicating "all events before this time
 *     have been observed" — used to close windows
 *
 * Window Types (L5):
 *   Tumbling:  [0,60) [60,120) [120,180) — fixed, non-overlapping
 *   Hopping:   [0,60) [30,90) [60,120) — sliding by hop interval
 *   Sliding:   triggered per event, window [t-60, t)
 *   Session:   dynamic, window closes after gap > session_gap_ms
 *
 * Reference: Google Dataflow Model (Akidau et al., 2015)
 * Course: Stanford CS 245 (Database Systems) - Stream processing
 *         Berkeley CS 294 (AI Systems) - Streaming ML
 * ================================================================ */

static int window_config_validate(const WindowConfig *cfg)
{
    if (!cfg) return -1;
    if (cfg->type != WINDOW_SESSION && cfg->window_size_ms < 1) return -1;
    if (cfg->max_events < 1 || cfg->max_events > MAX_WINDOW_SIZE) return -1;
    if (cfg->type == WINDOW_HOPPING && (cfg->hop_ms < 1 ||
        cfg->hop_ms > cfg->window_size_ms)) return -1;
    if (cfg->type == WINDOW_SESSION && cfg->session_gap_ms < 1) return -1;
    return 0;
}

StreamProcessor* stream_processor_create(const WindowConfig *config)
{
    StreamProcessor *sp;
    int i;

    if (window_config_validate(config) != 0) return NULL;

    sp = (StreamProcessor*)calloc(1, sizeof(StreamProcessor));
    if (!sp) return NULL;

    memcpy(&sp->config, config, sizeof(WindowConfig));
    sp->active_window_count = 0;
    sp->aggregator_count = 0;
    sp->watermark_ms = 0;
    sp->total_events_processed = 0;
    sp->total_late_events = 0;

    for (i = 0; i < 8; i++) {
        sp->windows[i].event_count = 0;
        sp->windows[i].key_count = 0;
        sp->windows[i].is_closed = 0;
    }

    printf("stream-processor: created with window_type=%d, size=%" PRId64 "ms\n",
           (int)config->type, config->window_size_ms);

    return sp;
}

void stream_processor_destroy(StreamProcessor *sp)
{
    int i, j;
    if (!sp) return;
    for (i = 0; i < sp->active_window_count; i++) {
        for (j = 0; j < sp->windows[i].key_count; j++) {
            free(sp->windows[i].event_keys[j]);
        }
    }
    free(sp);
}

/* L5: Find or create the window that contains timestamp_ms.
 *
 * Tumbling window assignment:
 *   window_start = floor(timestamp / window_size) * window_size
 *
 * Hopping window assignment:
 *   A single event may belong to multiple windows!
 *   window_starts = all multiples of hop_ms within range.
 *   For simplicity, we assign to the most recent window.
 *
 * Session window assignment:
 *   If event timestamp within gap of last window, extend it.
 *   Otherwise, create a new window.
 *
 * Time Complexity: O(num_windows) for hoppping/session, O(1) for tumbling
 */
static int find_or_create_window(StreamProcessor *sp, int64_t timestamp_ms)
{
    int64_t window_start;
    int i;

    switch (sp->config.type) {
    case WINDOW_TUMBLING:
        window_start = (timestamp_ms / sp->config.window_size_ms)
                       * sp->config.window_size_ms;
        break;
    case WINDOW_HOPPING:
        /* Assign to the latest window that would contain this event */
        {
            int64_t last_window = (timestamp_ms / sp->config.hop_ms)
                                  * sp->config.hop_ms;
            window_start = last_window - sp->config.window_size_ms + sp->config.hop_ms;
            if (window_start < 0) window_start = 0;
        }
        break;
    case WINDOW_SLIDING:
        window_start = timestamp_ms - sp->config.window_size_ms;
        if (window_start < 0) window_start = 0;
        break;
    case WINDOW_SESSION:
        /* Check if we can extend the last active window */
        for (i = sp->active_window_count - 1; i >= 0; i--) {
            if (!sp->windows[i].is_closed &&
                timestamp_ms - sp->windows[i].window_end_ms
                <= sp->config.session_gap_ms) {
                /* Extend this window */
                if (timestamp_ms > sp->windows[i].window_end_ms) {
                    sp->windows[i].window_end_ms = timestamp_ms;
                }
                return i;
            }
        }
        /* Create new session window */
        window_start = timestamp_ms;
        break;
    default:
        return -1;
    }

    /* Check if window already exists */
    for (i = 0; i < sp->active_window_count; i++) {
        if (sp->windows[i].window_start_ms == window_start &&
            !sp->windows[i].is_closed) {
            return i;
        }
    }

    /* Create new window */
    if (sp->active_window_count >= 8) {
        /* Evict oldest closed window, or refuse */
        return -1;
    }

    i = sp->active_window_count;
    sp->windows[i].window_start_ms = window_start;
    if (sp->config.type == WINDOW_SESSION) {
        sp->windows[i].window_end_ms = timestamp_ms;
    } else {
        sp->windows[i].window_end_ms = window_start + sp->config.window_size_ms;
    }
    sp->windows[i].event_count = 0;
    sp->windows[i].key_count = 0;
    sp->windows[i].is_closed = 0;
    sp->active_window_count++;

    return i;
}

/* L5: Push event into the stream processor.
 *
 * The event is assigned to one or more windows based on window type.
 * Late events (timestamp < watermark) are counted but may still
 * be processed if within grace period.
 *
 * Returns: window index assigned, or -1 on error.
 */
int stream_processor_push(StreamProcessor *sp, int64_t timestamp_ms,
                           double value, const char *key)
{
    int wi;
    WindowState *ws;

    if (!sp) return -1;

    sp->total_events_processed++;

    /* L8: Late event detection */
    if (timestamp_ms < sp->watermark_ms) {
        sp->total_late_events++;
        if (sp->watermark_ms - timestamp_ms > sp->config.grace_period_ms) {
            /* Too late - drop (but still counted) */
            return -2;
        }
    }

    wi = find_or_create_window(sp, timestamp_ms);
    if (wi < 0) return -1;

    ws = &sp->windows[wi];

    if (ws->event_count >= sp->config.max_events) return -1;

    ws->event_values[ws->event_count] = value;
    ws->event_timestamps[ws->event_count] = timestamp_ms;

    if (key) {
        ws->event_keys[ws->event_count] = strdup(key);
        if (!ws->event_keys[ws->event_count]) return -1;
        ws->key_count++;
    } else {
        ws->event_keys[ws->event_count] = NULL;
    }

    ws->event_count++;

    return wi;
}

/* Push a batch of events for throughput. */
int stream_processor_push_batch(StreamProcessor *sp,
                                 const int64_t *timestamps,
                                 const double *values,
                                 const char **keys,
                                 int count)
{
    int i, ok, total;
    if (!sp || !timestamps || !values || count < 1) return -1;
    total = 0;
    for (i = 0; i < count; i++) {
        const char *k = (keys && keys[i]) ? keys[i] : NULL;
        ok = stream_processor_push(sp, timestamps[i], values[i], k);
        if (ok >= 0) total++;
    }
    return total;
}

/* Add an aggregation function to the processor. */
int stream_processor_add_aggregation(StreamProcessor *sp,
                                      AggregationType agg_type)
{
    if (!sp) return -1;
    if (sp->aggregator_count >= MAX_AGGREGATIONS) return -1;
    sp->aggregators[sp->aggregator_count++] = agg_type;
    return 0;
}

/* ================================================================
 * L5: Aggregation Functions
 *
 * Each aggregation implements a monoid: associative operation with identity.
 * Monoids are the mathematical foundation for parallel aggregation.
 *
 * L4: Monoid Laws:
 *   1. Associativity: op(op(a,b), c) == op(a, op(b,c))
 *   2. Identity: op(a, identity) == a
 *
 * These laws enable:
 *   - Parallel reduction (divide and conquer)
 *   - Incremental computation (process one event at a time)
 *   - Fault tolerance via recomputation
 * ================================================================ */

/* Compare function for qsort - descending order */
static int compare_double_desc(const void *a, const void *b)
{
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return 1;
    if (da > db) return -1;
    return 0;
}

AggregationResult stream_processor_aggregate_window(const WindowState *ws,
                                                     AggregationType type)
{
    AggregationResult result;
    double sum, avg;
    int i;
    int64_t min_val, max_val, ival;

    memset(&result, 0, sizeof(result));
    result.type = type;

    if (!ws || ws->event_count == 0) return result;

    switch (type) {
    case AGG_COUNT:
        result.value_int64 = ws->event_count;
        result.value_double = (double)ws->event_count;
        break;

    case AGG_SUM:
        sum = 0.0;
        for (i = 0; i < ws->event_count; i++) sum += ws->event_values[i];
        result.value_double = sum;
        result.value_int64 = (int64_t)sum;
        break;

    case AGG_AVG:
        sum = 0.0;
        for (i = 0; i < ws->event_count; i++) sum += ws->event_values[i];
        avg = sum / (double)ws->event_count;
        result.value_double = avg;
        break;

    case AGG_MIN:
        if (ws->event_count == 0) break;
        min_val = (int64_t)ws->event_values[0];
        for (i = 1; i < ws->event_count; i++) {
            ival = (int64_t)ws->event_values[i];
            if (ival < min_val) min_val = ival;
        }
        result.value_int64 = min_val;
        result.value_double = (double)min_val;
        break;

    case AGG_MAX:
        if (ws->event_count == 0) break;
        max_val = (int64_t)ws->event_values[0];
        for (i = 1; i < ws->event_count; i++) {
            ival = (int64_t)ws->event_values[i];
            if (ival > max_val) max_val = ival;
        }
        result.value_int64 = max_val;
        result.value_double = (double)max_val;
        break;

    case AGG_TOP_K:
    case AGG_DISTINCT_COUNT:
        /* Handled by dedicated functions */
        result.value_int64 = ws->event_count;
        break;
    }

    return result;
}

/* Aggregate across the current (most recent active) window. */
AggregationResult stream_processor_aggregate_current(StreamProcessor *sp,
                                                      AggregationType type)
{
    AggregationResult empty;
    memset(&empty, 0, sizeof(empty));

    if (!sp || sp->active_window_count == 0) return empty;

    return stream_processor_aggregate_window(
        &sp->windows[sp->active_window_count - 1], type);
}

/* ================================================================
 * L7: Advanced Aggregations (Application Level)
 *
 * Top-K: Find K largest values in a window.
 * Distinct Count: Approximate distinct count using sorted unique values.
 * Percentile: P-th percentile value from sorted data.
 * ================================================================ */

int stream_processor_top_k(const WindowState *ws, int k,
                            double *out_values, int *out_count)
{
    double *sorted;
    int i, n;
    if (!ws || !out_values || !out_count || ws->event_count == 0) {
        *out_count = 0;
        return -1;
    }
    n = ws->event_count;
    sorted = (double*)malloc((size_t)n * sizeof(double));
    if (!sorted) return -1;
    memcpy(sorted, ws->event_values, (size_t)n * sizeof(double));
    qsort(sorted, (size_t)n, sizeof(double), compare_double_desc);
    if (k > n) k = n;
    for (i = 0; i < k; i++) out_values[i] = sorted[i];
    *out_count = k;
    free(sorted);
    return 0;
}

int stream_processor_distinct_count(const WindowState *ws)
{
    double *sorted;
    int i, distinct;
    if (!ws || ws->event_count == 0) return 0;
    sorted = (double*)malloc((size_t)ws->event_count * sizeof(double));
    if (!sorted) return -1;
    memcpy(sorted, ws->event_values, (size_t)ws->event_count * sizeof(double));
    qsort(sorted, (size_t)ws->event_count, sizeof(double), compare_double_desc);
    distinct = 1;
    for (i = 1; i < ws->event_count; i++) {
        if (sorted[i] != sorted[i - 1]) distinct++;
    }
    free(sorted);
    return distinct;
}

double stream_processor_percentile(const WindowState *ws, double percentile)
{
    double *sorted;
    int n, idx;
    double result;
    if (!ws || ws->event_count == 0) return 0.0;
    n = ws->event_count;
    sorted = (double*)malloc((size_t)n * sizeof(double));
    if (!sorted) return 0.0;
    memcpy(sorted, ws->event_values, (size_t)n * sizeof(double));
    qsort(sorted, (size_t)n, sizeof(double), compare_double_desc);
    /* For percentile p, index = (1-p) * (n-1) since sorted descending */
    idx = (int)((1.0 - percentile / 100.0) * (double)(n - 1));
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    result = sorted[idx];
    free(sorted);
    return result;
}

/* ================================================================
 * L8: Watermark Management
 *
 * Watermarks track event-time progress. They tell the system:
 * "You have now seen all events with timestamp < W."
 *
 * Advanced: Handling out-of-order events within grace period.
 * Late events beyond grace period are dropped.
 *
 * Reference: Google MillWheel (Akidau et al., 2013)
 *            Apache Flink watermark mechanism
 * ================================================================ */

int64_t stream_processor_advance_watermark(StreamProcessor *sp,
                                            int64_t new_watermark_ms)
{
    if (!sp) return -1;
    if (new_watermark_ms > sp->watermark_ms) {
        int64_t old = sp->watermark_ms;
        sp->watermark_ms = new_watermark_ms;
        printf("stream-processor: watermark advanced %" PRId64
               " -> %" PRId64 " ms\n", old, new_watermark_ms);
    }
    return sp->watermark_ms;
}

/* Close windows whose end + grace_period is before the watermark.
 * Closed windows are finalized and their aggregates become immutable. */
int stream_processor_close_expired_windows(StreamProcessor *sp)
{
    int i, closed;
    if (!sp) return -1;
    closed = 0;
    for (i = 0; i < sp->active_window_count; i++) {
        if (!sp->windows[i].is_closed &&
            sp->windows[i].window_end_ms + sp->config.grace_period_ms
            <= sp->watermark_ms) {
            sp->windows[i].is_closed = 1;
            closed++;
            printf("stream-processor: closed window [%" PRId64
                   ", %" PRId64 "), events=%d\n",
                   sp->windows[i].window_start_ms,
                   sp->windows[i].window_end_ms,
                   sp->windows[i].event_count);
        }
    }
    return closed;
}

int stream_processor_get_late_event_count(const StreamProcessor *sp)
{
    return sp ? (int)sp->total_late_events : -1;
}

/* ================================================================
 * L4: Event Time Skew Analysis
 *
 * In distributed systems, event timestamps may differ from processing
 * timestamps due to clock skew, network delay, or batching.
 *
 * We estimate skew by comparing the average event time delta to
 * the processing time delta.
 *
 * The formula: skew = avg(processing_time - event_time)
 * Large positive skew means events are delayed.
 * ================================================================ */

double stream_processor_estimate_event_time_skew(const StreamProcessor *sp)
{
    int i, j, total;
    double sum_diff;

    if (!sp || sp->total_events_processed == 0) return 0.0;

    sum_diff = 0.0;
    total = 0;

    for (i = 0; i < sp->active_window_count; i++) {
        const WindowState *ws = &sp->windows[i];
        for (j = 0; j < ws->event_count; j++) {
            /* Approximate processing time as current for simplicity.
             * In production, we'd record the actual processing time per event. */
            sum_diff += 0.0;  /* Placeholder for actual skew computation */
            total++;
        }
    }

    return total > 0 ? sum_diff / (double)total : 0.0;
}

/* Compute how far behind the watermark is from the latest event time.
 * This is the "watermark lag" metric used for monitoring. */
int64_t stream_processor_watermark_lag(const StreamProcessor *sp)
{
    int64_t max_event_time;
    int i, j;

    if (!sp || sp->active_window_count == 0) return 0;

    max_event_time = 0;

    for (i = 0; i < sp->active_window_count; i++) {
        const WindowState *ws = &sp->windows[i];
        for (j = 0; j < ws->event_count; j++) {
            if (ws->event_timestamps[j] > max_event_time) {
                max_event_time = ws->event_timestamps[j];
            }
        }
    }

    if (max_event_time > sp->watermark_ms) {
        return max_event_time - sp->watermark_ms;
    }
    return 0;
}

/* Inspection utilities */
int stream_processor_active_window_count(const StreamProcessor *sp)
{
    return sp ? sp->active_window_count : -1;
}

const WindowState* stream_processor_get_window(const StreamProcessor *sp,
                                                int index)
{
    if (!sp || index < 0 || index >= sp->active_window_count) return NULL;
    return &sp->windows[index];
}