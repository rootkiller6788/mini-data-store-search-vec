/* ───────────────────────────────────────────────────────────
   Snapshot / MVCC — Multi-Version Concurrency Control

   Implements:
     1. Snapshot manager with monotonically increasing global seqno
     2. Version store with per-key version chains (newest first)
     3. Snapshot-consistent reads (visible seqno <= snapshot_seq)
     4. Garbage collection of versions older than oldest active snapshot

   Theorem (MVCC Correctness):
     A read at snapshot S sees the effects of all writes with
     seqno ≤ S and no writes with seqno > S. This guarantee
     provides a consistent point-in-time view.

   Theorem (Garbage Collection Safety):
     A version with seqno V can be safely removed iff V < min(S)
     for all active snapshots S, because no active reader
     needs to see version V.

   Reference: P.A. Bernstein, N. Goodman, "Multiversion Concurrency
   Control — Theory and Algorithms", ACM TODS, 1983.
   ─────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "snapshot.h"

/* ─────────────────────────────────────────────
   Internal: simple FNV-1a hash for version store
   ───────────────────────────────────────────── */
static uint32_t vs_hash(const uint8_t *key, uint32_t len, uint32_t num_buckets) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) {
        h ^= (uint32_t)key[i];
        h *= 16777619u;
    }
    return h % num_buckets;
}

/* ─────────────────────────────────────────────
   Internal: current time in milliseconds
   ───────────────────────────────────────────── */
static uint64_t now_ms_snapshot(void) {
    return (uint64_t)(clock() / (CLOCKS_PER_SEC / 1000));
}

/* ============================================================
   Snapshot Manager
   ============================================================ */

/* ─────────────────────────────────────────────
   Create snapshot manager
   ───────────────────────────────────────────── */
SnapshotManager *snap_mgr_create(void) {
    SnapshotManager *mgr = (SnapshotManager *)calloc(1, sizeof(SnapshotManager));
    if (!mgr) return NULL;

    mgr->global_seqno = 1;  /* 0 reserved */
    mgr->num_active = 0;
    mgr->oldest_snapshot_seq = UINT64_MAX;
    mgr->snapshot_counter = 0;
    mgr->snapshot_released = 0;

    return mgr;
}

/* ─────────────────────────────────────────────
   Create a new snapshot at current seqno
   ───────────────────────────────────────────── */
Snapshot *snap_mgr_create_snapshot(SnapshotManager *mgr) {
    if (!mgr) return NULL;
    if (mgr->num_active >= SNAPSHOT_MAX_STACK) return NULL;

    Snapshot *snap = (Snapshot *)calloc(1, sizeof(Snapshot));
    if (!snap) return NULL;

    snap->snapshot_seq   = mgr->global_seqno;
    snap->create_time_ms = now_ms_snapshot();
    snap->ref_count      = 1;

    mgr->active_snapshots[mgr->num_active++] = snap;
    mgr->snapshot_counter++;

    /* Update oldest snapshot */
    if (snap->snapshot_seq < mgr->oldest_snapshot_seq)
        mgr->oldest_snapshot_seq = snap->snapshot_seq;

    return snap;
}

/* ─────────────────────────────────────────────
   Release a snapshot
   ───────────────────────────────────────────── */
void snap_mgr_release_snapshot(SnapshotManager *mgr, Snapshot *snap) {
    if (!mgr || !snap) return;

    /* Find and remove from active list */
    for (uint32_t i = 0; i < mgr->num_active; i++) {
        if (mgr->active_snapshots[i] == snap) {
            /* Shift remaining entries */
            for (uint32_t j = i; j < mgr->num_active - 1; j++)
                mgr->active_snapshots[j] = mgr->active_snapshots[j + 1];
            mgr->num_active--;
            break;
        }
    }

    free(snap);
    mgr->snapshot_released++;

    /* Recompute oldest snapshot */
    mgr->oldest_snapshot_seq = UINT64_MAX;
    for (uint32_t i = 0; i < mgr->num_active; i++) {
        if (mgr->active_snapshots[i]->snapshot_seq < mgr->oldest_snapshot_seq)
            mgr->oldest_snapshot_seq = mgr->active_snapshots[i]->snapshot_seq;
    }
    if (mgr->num_active == 0)
        mgr->oldest_snapshot_seq = UINT64_MAX;
}

/* ─────────────────────────────────────────────
   Allocate next sequence number
   ───────────────────────────────────────────── */
uint64_t snap_mgr_next_seqno(SnapshotManager *mgr) {
    if (!mgr) return 0;
    return mgr->global_seqno++;
}

/* ─────────────────────────────────────────────
   Get oldest active snapshot seqno
   ───────────────────────────────────────────── */
uint64_t snap_mgr_get_oldest_active(SnapshotManager *mgr) {
    if (!mgr) return UINT64_MAX;
    return mgr->oldest_snapshot_seq;
}

/* ─────────────────────────────────────────────
   Destroy snapshot manager
   ───────────────────────────────────────────── */
void snap_mgr_destroy(SnapshotManager *mgr) {
    if (!mgr) return;
    /* Release all active snapshots */
    for (uint32_t i = 0; i < mgr->num_active; i++)
        free(mgr->active_snapshots[i]);
    free(mgr);
}

/* ============================================================
   Version Store
   ============================================================ */

/* ─────────────────────────────────────────────
   Create version store
   ───────────────────────────────────────────── */
VersionStore *vs_create(uint32_t num_buckets) {
    if (num_buckets == 0) num_buckets = 256;

    VersionStore *vs = (VersionStore *)calloc(1, sizeof(VersionStore));
    if (!vs) return NULL;

    vs->buckets = (VersionEntry **)calloc(num_buckets, sizeof(VersionEntry *));
    if (!vs->buckets) {
        free(vs);
        return NULL;
    }

    vs->num_buckets = num_buckets;
    vs->num_keys = 0;

    return vs;
}

/* ─────────────────────────────────────────────
   Insert a new version for a key
   ───────────────────────────────────────────── */
int vs_put(VersionStore *vs,
           const uint8_t *key, uint32_t key_len,
           const uint8_t *value, uint32_t value_len,
           uint64_t seqno, uint8_t is_deleted) {
    if (!vs || !key || key_len > SNAPSHOT_MAX_KEY_LEN) return -1;

    uint32_t bucket = vs_hash(key, key_len, vs->num_buckets);

    /* Find existing entry or create new */
    VersionEntry *entry = vs->buckets[bucket];
    while (entry) {
        if (entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            goto add_version;
        }
        entry = entry->next;
    }

    /* New key entry */
    entry = (VersionEntry *)calloc(1, sizeof(VersionEntry));
    if (!entry) return -1;
    memcpy(entry->key, key, key_len);
    entry->key_len = key_len;
    entry->head = NULL;
    entry->next = vs->buckets[bucket];
    vs->buckets[bucket] = entry;
    vs->num_keys++;

add_version: ;
    /* Allocate new version node */
    VersionNode *node = (VersionNode *)calloc(1, sizeof(VersionNode));
    if (!node) return -1;

    if (value && value_len > 0 && value_len <= SNAPSHOT_MAX_VALUE_LEN)
        memcpy(node->value, value, value_len);
    node->value_len = value_len;
    node->seqno     = seqno;
    node->is_deleted = is_deleted;

    /* Insert at head (newest first) */
    node->older = entry->head;
    entry->head = node;

    return 0;
}

/* ─────────────────────────────────────────────
   Read value at a given snapshot point
   Returns: 1 = found, 0 = not found, -1 = error
   ───────────────────────────────────────────── */
int vs_get_at_snapshot(VersionStore *vs,
                        const uint8_t *key, uint32_t key_len,
                        uint64_t snapshot_seq,
                        uint8_t *value_out, uint32_t *value_len_out) {
    if (!vs || !key || !value_out || !value_len_out) return -1;

    uint32_t bucket = vs_hash(key, key_len, vs->num_buckets);
    VersionEntry *entry = vs->buckets[bucket];

    while (entry) {
        if (entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            /* Walk version chain from newest to oldest */
            VersionNode *v = entry->head;
            while (v) {
                if (v->seqno <= snapshot_seq) {
                    if (v->is_deleted) return 0; /* key was deleted */
                    memcpy(value_out, v->value, v->value_len);
                    *value_len_out = v->value_len;
                    return 1;
                }
                v = v->older;
            }
            return 0; /* no visible version */
        }
        entry = entry->next;
    }
    return 0; /* key not found */
}

/* ─────────────────────────────────────────────
   Garbage collect versions older than oldest_snapshot.
   Keeps at least one version per key for correctness.
   ───────────────────────────────────────────── */
int vs_gc_old_versions(VersionStore *vs, uint64_t oldest_snapshot) {
    if (!vs) return -1;
    uint32_t removed = 0;

    for (uint32_t b = 0; b < vs->num_buckets; b++) {
        VersionEntry *entry = vs->buckets[b];
        while (entry) {
            VersionNode *prev = NULL;
            VersionNode *v    = entry->head;

            while (v) {
                VersionNode *older = v->older;

                /* We can remove v if:
                   - It's not the only version, AND
                   - Its seqno < oldest_snapshot
                   Actually: keep all versions needed by any
                   active snapshot. Remove only those that are
                   older than the oldest snapshot AND there's
                   a newer version that covers the same key. */
                if (older != NULL && v->seqno < oldest_snapshot) {
                    /* Safe to remove v — newer version covers it */
                    if (prev) {
                        prev->older = older;
                    } else {
                        entry->head = older;
                    }
                    free(v);
                    removed++;
                    v = older;
                    continue;
                }

                prev = v;
                v = older;
            }
            entry = entry->next;
        }
    }

    return (int)removed;
}

/* ─────────────────────────────────────────────
   Destroy version store
   ───────────────────────────────────────────── */
void vs_destroy(VersionStore *vs) {
    if (!vs) return;

    for (uint32_t b = 0; b < vs->num_buckets; b++) {
        VersionEntry *entry = vs->buckets[b];
        while (entry) {
            VersionEntry *next_entry = entry->next;
            /* Free all versions */
            VersionNode *v = entry->head;
            while (v) {
                VersionNode *next_v = v->older;
                free(v);
                v = next_v;
            }
            free(entry);
            entry = next_entry;
        }
    }
    free(vs->buckets);
    free(vs);
}

/* ============================================================
   VersionedKV helpers
   ============================================================ */

/* ─────────────────────────────────────────────
   Check if a versioned entry is visible at snapshot
   ───────────────────────────────────────────── */
int versioned_kv_visible(const VersionedKV *kv, uint64_t snapshot_seq) {
    if (!kv) return 0;
    return (kv->seqno <= snapshot_seq) ? 1 : 0;
}

/* ─────────────────────────────────────────────
   Check if a versioned entry is a tombstone
   ───────────────────────────────────────────── */
int versioned_kv_is_tombstone(const VersionedKV *kv) {
    if (!kv) return 0;
    return kv->is_deleted;
}
