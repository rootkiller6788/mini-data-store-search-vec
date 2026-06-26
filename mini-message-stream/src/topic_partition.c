#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "topic_partition.h"

Topic* topic_create(const char *name, int num_partitions, int64_t retention_ms)
{
    Topic *t;
    int i;

    if (num_partitions < 1 || num_partitions > MAX_PARTITIONS) return NULL;

    t = (Topic*)calloc(1, sizeof(Topic));
    if (!t) return NULL;

    strncpy(t->name, name, MAX_TOPIC_NAME_LEN - 1);
    t->name[MAX_TOPIC_NAME_LEN - 1] = '\0';
    t->partition_count = num_partitions;
    t->retention_ms = retention_ms;

    for (i = 0; i < num_partitions; i++) {
        Partition *p = &t->partitions[i];
        p->id = i;
        p->leader_broker = -1;
        p->replica_count = 0;
        p->segment_count = 1;
        p->active_segment_idx = 0;

        p->segments[0].base_offset = 0;
        p->segments[0].size = 0;
        p->segments[0].is_active = 1;
    }

    return t;
}

void topic_destroy(Topic *topic)
{
    free(topic);
}

int64_t partition_append(Partition *p, const char *key, const char *value,
                         const char *headers)
{
    LogSegment *seg;
    Record *rec;
    int64_t offset;

    if (!p || p->segment_count == 0) return -1;

    seg = &p->segments[p->active_segment_idx];

    if (seg->size >= MAX_RECORDS_PER_SEG) {
        partition_roll_segment(p);
        seg = &p->segments[p->active_segment_idx];
    }

    if (seg->size < MAX_RECORDS_PER_SEG && seg->is_active) {
        offset = seg->base_offset + seg->size;
        rec = &seg->records[seg->size];

        rec->offset = offset;
        rec->timestamp = (int64_t)time(NULL);

        if (key) {
            strncpy(rec->key, key, MAX_KEY_LEN - 1);
            rec->key[MAX_KEY_LEN - 1] = '\0';
        } else {
            rec->key[0] = '\0';
        }

        if (value) {
            strncpy(rec->value, value, MAX_VALUE_LEN - 1);
            rec->value[MAX_VALUE_LEN - 1] = '\0';
        } else {
            rec->value[0] = '\0';
        }

        if (headers) {
            strncpy(rec->headers, headers, MAX_HEADER_LEN - 1);
            rec->headers[MAX_HEADER_LEN - 1] = '\0';
        } else {
            rec->headers[0] = '\0';
        }

        seg->size++;
        return offset;
    }

    return -1;
}

int partition_read(Partition *p, int64_t start_offset, int max_records,
                   Record *out_records, int *out_count)
{
    int seg_idx, rec_idx, copied;
    int64_t seg_end;

    if (!p || !out_records || !out_count) return -1;

    *out_count = 0;
    copied = 0;

    for (seg_idx = 0; seg_idx < p->segment_count && copied < max_records; seg_idx++) {
        LogSegment *seg = &p->segments[seg_idx];
        seg_end   = seg->base_offset + seg->size;

        if (start_offset >= seg_end) continue;

        for (rec_idx = 0; rec_idx < seg->size && copied < max_records; rec_idx++) {
            if (seg->records[rec_idx].offset >= start_offset) {
                memcpy(&out_records[copied], &seg->records[rec_idx], sizeof(Record));
                copied++;
            }
        }
    }

    *out_count = copied;
    return 0;
}

void partition_roll_segment(Partition *p)
{
    LogSegment *active_seg;
    int64_t next_base;

    if (!p || p->segment_count >= MAX_SEGMENTS) return;

    active_seg = &p->segments[p->active_segment_idx];
    active_seg->is_active = 0;
    next_base = active_seg->base_offset + active_seg->size;

    p->active_segment_idx = p->segment_count;
    p->segment_count++;

    p->segments[p->active_segment_idx].base_offset = next_base;
    p->segments[p->active_segment_idx].size = 0;
    p->segments[p->active_segment_idx].is_active = 1;
}

int partition_get_segment_count(const Partition *p)
{
    return p ? p->segment_count : 0;
}

int64_t partition_get_end_offset(const Partition *p)
{
    const LogSegment *seg;

    if (!p || p->segment_count == 0) return 0;
    seg = &p->segments[p->active_segment_idx];
    return seg->base_offset + seg->size;
}
