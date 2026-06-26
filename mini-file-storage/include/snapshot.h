#ifndef MINI_SNAPSHOT_H
#define MINI_SNAPSHOT_H

#include <stdint.h>
#include <stddef.h>

/* ─────────────────────────────────────────────
   Snapshot / MVCC (Multi-Version Concurrency Control)

   Provides snapshot isolation using global sequence numbers.
   Each write is assigned a monotonically increasing sequence number.
   A snapshot is defined by the maximum sequence number visible to
   the read operation.

   Theorem (Snapshot Isolation):
     Under SI, a transaction T sees a committed snapshot of the
     database as of T's start time. This prevents dirty reads,
     non-repeatable reads, and phantom reads, but allows write skew.

   Reference: H. Berenson et al., "A Critique of ANSI SQL Isolation
   Levels", SIGMOD 1995.

   Key concept: Each key in the LSM tree may have multiple versions
   stored across memtable, immutable memtables, and SSTables.
   A snapshot read ignores all entries with seqno > snapshot_seq.
   ───────────────────────────────────────────── */

#define SNAPSHOT_MAX_KEY_LEN   256
#define SNAPSHOT_MAX_VALUE_LEN 1024
#define SNAPSHOT_MAX_STACK     64     /* max active snapshots */

/* ─────────────────────────────────────────────
   Versioned key-value entry
   ───────────────────────────────────────────── */
typedef struct {
    uint8_t  key[SNAPSHOT_MAX_KEY_LEN];
    uint32_t key_len;
    uint8_t  value[SNAPSHOT_MAX_VALUE_LEN];
    uint32_t value_len;
    uint64_t seqno;          /* write sequence number */
    uint8_t  is_deleted;     /* tombstone flag */
} VersionedKV;

/* ─────────────────────────────────────────────
   Snapshot handle — a read view at a point in time
   ───────────────────────────────────────────── */
typedef struct {
    uint64_t snapshot_seq;   /* max sequence number visible */
    uint64_t create_time_ms;
    uint32_t ref_count;
} Snapshot;

/* ─────────────────────────────────────────────
   MVCC snapshot manager
   ───────────────────────────────────────────── */
typedef struct {
    uint64_t  global_seqno;           /* next sequence number to assign */
    Snapshot *active_snapshots[SNAPSHOT_MAX_STACK];
    uint32_t  num_active;
    uint64_t  oldest_snapshot_seq;    /* for garbage collection */
    uint64_t  snapshot_counter;       /* total snapshots created */
    uint64_t  snapshot_released;      /* total snapshots released */
} SnapshotManager;

/* ─────────────────────────────────────────────
   Snapshot-aware version list (simple linked list
   of versions for a single key, newest first)
   ───────────────────────────────────────────── */
typedef struct VersionNode {
    uint8_t  value[SNAPSHOT_MAX_VALUE_LEN];
    uint32_t value_len;
    uint64_t seqno;
    uint8_t  is_deleted;
    struct VersionNode *older;   /* next older version */
} VersionNode;

/* ─────────────────────────────────────────────
   Version store — maps key → version chain
   ───────────────────────────────────────────── */
typedef struct VersionEntry {
    uint8_t             key[SNAPSHOT_MAX_KEY_LEN];
    uint32_t            key_len;
    VersionNode        *head;   /* newest version first */
    struct VersionEntry *next;  /* hash chain */
} VersionEntry;

typedef struct {
    VersionEntry **buckets;
    uint32_t       num_buckets;
    uint32_t       num_keys;
} VersionStore;

/* ─────────────────────────────────────────────
   API
   ───────────────────────────────────────────── */

/* Snapshot manager */
SnapshotManager *snap_mgr_create(void);
Snapshot        *snap_mgr_create_snapshot(SnapshotManager *mgr);
void             snap_mgr_release_snapshot(SnapshotManager *mgr, Snapshot *snap);
uint64_t         snap_mgr_next_seqno(SnapshotManager *mgr);
uint64_t         snap_mgr_get_oldest_active(SnapshotManager *mgr);
void             snap_mgr_destroy(SnapshotManager *mgr);

/* Version store */
VersionStore *vs_create(uint32_t num_buckets);
int           vs_put(VersionStore *vs,
                     const uint8_t *key, uint32_t key_len,
                     const uint8_t *value, uint32_t value_len,
                     uint64_t seqno, uint8_t is_deleted);
int           vs_get_at_snapshot(VersionStore *vs,
                                  const uint8_t *key, uint32_t key_len,
                                  uint64_t snapshot_seq,
                                  uint8_t *value_out, uint32_t *value_len_out);
int           vs_gc_old_versions(VersionStore *vs, uint64_t oldest_snapshot);
void          vs_destroy(VersionStore *vs);

/* VersionedKV helpers */
int  versioned_kv_visible(const VersionedKV *kv, uint64_t snapshot_seq);
int  versioned_kv_is_tombstone(const VersionedKV *kv);

#endif /* MINI_SNAPSHOT_H */
