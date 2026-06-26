#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "stream_processor.h"

static int test_processor_create(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 5000;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);
    assert(sp->config.type == WINDOW_TUMBLING);
    assert(sp->active_window_count == 0);
    assert(sp->total_events_processed == 0);

    stream_processor_destroy(sp);
    return 0;
}

static int test_push_tumbling(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 50;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    /* Push events into same window */
    int i;
    for (i = 0; i < 10; i++) {
        int wi = stream_processor_push(sp, 10000 + i * 100, (double)i, NULL);
        assert(wi >= 0);
    }

    assert(sp->total_events_processed == 10);
    assert(sp->active_window_count == 1);

    /* Push event into next window */
    stream_processor_push(sp, 120000, 99.0, NULL);
    assert(sp->active_window_count >= 1);

    stream_processor_destroy(sp);
    return 0;
}

static int test_push_session(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_SESSION;
    cfg.window_size_ms = 0;
    cfg.session_gap_ms = 10000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    /* Session 1: events within gap */
    stream_processor_push(sp, 1000, 10.0, NULL);
    stream_processor_push(sp, 5000, 20.0, NULL);
    stream_processor_push(sp, 9000, 30.0, NULL);

    assert(sp->active_window_count == 1);

    /* Session 2: new event far after gap */
    stream_processor_push(sp, 30000, 40.0, NULL);
    assert(sp->active_window_count == 2);

    stream_processor_destroy(sp);
    return 0;
}

static int test_push_batch(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    int64_t timestamps[] = { 1000, 2000, 3000, 4000, 5000 };
    double values[] = { 1.0, 2.0, 3.0, 4.0, 5.0 };
    const char *keys[] = { "a", "b", "c", "d", "e" };

    int pushed = stream_processor_push_batch(sp, timestamps, values, keys, 5);
    assert(pushed == 5);
    assert(sp->total_events_processed == 5);

    stream_processor_destroy(sp);
    return 0;
}

static int test_aggregation(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    int i;
    for (i = 0; i < 5; i++) {
        stream_processor_push(sp, 1000, (double)(i * 10), NULL);
    }

    const WindowState *ws = stream_processor_get_window(sp, 0);
    assert(ws != NULL);
    assert(ws->event_count == 5);

    AggregationResult r;

    r = stream_processor_aggregate_window(ws, AGG_COUNT);
    assert(r.value_int64 == 5);

    r = stream_processor_aggregate_window(ws, AGG_SUM);
    assert(r.value_double == 100.0);  /* 0+10+20+30+40 */

    r = stream_processor_aggregate_window(ws, AGG_AVG);
    assert(r.value_double == 20.0);

    r = stream_processor_aggregate_window(ws, AGG_MIN);
    assert(r.value_double == 0.0);

    r = stream_processor_aggregate_window(ws, AGG_MAX);
    assert(r.value_double == 40.0);

    stream_processor_destroy(sp);
    return 0;
}

static int test_top_k(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    double vals[] = { 5.0, 2.0, 8.0, 1.0, 9.0, 3.0 };
    int i;
    for (i = 0; i < 6; i++) {
        stream_processor_push(sp, 1000, vals[i], NULL);
    }

    const WindowState *ws = stream_processor_get_window(sp, 0);
    double top[8];
    int top_count;

    assert(stream_processor_top_k(ws, 3, top, &top_count) == 0);
    assert(top_count == 3);
    assert(top[0] == 9.0);  /* largest */
    assert(top[1] == 8.0);
    assert(top[2] == 5.0);

    stream_processor_destroy(sp);
    return 0;
}

static int test_distinct_count(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    double vals[] = { 1.0, 2.0, 1.0, 3.0, 2.0, 1.0 };
    int i;
    for (i = 0; i < 6; i++) {
        stream_processor_push(sp, 1000, vals[i], NULL);
    }

    const WindowState *ws = stream_processor_get_window(sp, 0);
    int distinct = stream_processor_distinct_count(ws);
    assert(distinct == 3);

    stream_processor_destroy(sp);
    return 0;
}

static int test_watermark(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 10000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 1000;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    /* Push events into two windows */
    stream_processor_push(sp, 5000, 1.0, NULL);    /* window [0, 10000) */
    stream_processor_push(sp, 15000, 2.0, NULL);   /* window [10000, 20000) */

    /* Advance watermark past first window + grace */
    stream_processor_advance_watermark(sp, 12000);

    int closed = stream_processor_close_expired_windows(sp);
    assert(closed >= 1);

    /* Late event test */
    (void)stream_processor_push(sp, 500, 99.0, NULL);  /* Before watermark */
    assert(stream_processor_get_late_event_count(sp) > 0);

    stream_processor_destroy(sp);
    return 0;
}

static int test_percentile(void) {
    WindowConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.type = WINDOW_TUMBLING;
    cfg.window_size_ms = 60000;
    cfg.max_events = 100;
    cfg.grace_period_ms = 0;

    StreamProcessor *sp = stream_processor_create(&cfg);
    assert(sp != NULL);

    /* 0, 10, 20, 30, 40, 50, 60, 70, 80, 90 */
    int i;
    for (i = 0; i < 10; i++) {
        stream_processor_push(sp, 1000, (double)(i * 10), NULL);
    }

    const WindowState *ws = stream_processor_get_window(sp, 0);
    /* P50 (median) in descending sorted [90,80,...,0]: idx = round(0.5*9)=4 => 50 */
    double p50 = stream_processor_percentile(ws, 50.0);
    assert(p50 >= 40.0 && p50 <= 60.0);

    stream_processor_destroy(sp);
    return 0;
}

int main(void) {
    printf("=== Running Stream Processor Tests ===\n");
    test_processor_create();
    test_push_tumbling();
    test_push_session();
    test_push_batch();
    test_aggregation();
    test_top_k();
    test_distinct_count();
    test_watermark();
    test_percentile();
    printf("All stream processor tests passed!\n");
    return 0;
}