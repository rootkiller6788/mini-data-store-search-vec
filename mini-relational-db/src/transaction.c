#include "transaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Transaction Manager Implementation - L2 Core Concept: ACID
 *
 * Reference: CMU 15-445 Lecture 13-16
 *            Gray & Reuter, "Transaction Processing: Concepts and Techniques"
 *
 * Strict Two-Phase Locking (S2PL) Theorem:
 *   Any schedule produced by S2PL is conflict-serializable.
 *   Proof (sketch): The precedence graph of conflicting lock acquisitions
 *   is acyclic because locks are held until commit, creating a total order
 *   on transactions based on commit time.
 *
 * Write-Ahead Logging (WAL) Invariant (ARIES):
 *   Before a data page is written to disk, all WAL records up to the
 *   page's LSN must be flushed to stable storage.
 *   This ensures: Atomicity (undo uncommitted) + Durability (redo committed).
 */

/* Lock compatibility matrix: [LOCK_SHARED][LOCK_SHARED] etc.
 * 1 = compatible, 0 = conflict.
 *   | S | X |
 * --+---+---+
 * S | 1 | 0 |
 * X | 0 | 0 |
 */
static int lock_compatible(LockMode a, LockMode b) {
    return (a == LOCK_SHARED && b == LOCK_SHARED) ? 1 : 0;
}

void txn_mgr_init(TXNManager *mgr) {
    if (!mgr) return;
    memset(mgr, 0, sizeof(TXNManager));
    mgr->next_txn_id = 1;
    mgr->wal_log.next_lsn = 1;
}

int txn_begin(TXNManager *mgr) {
    if (!mgr || mgr->num_txns >= TXN_MAX_ACTIVE) return -1;

    int tid = mgr->next_txn_id++;
    Transaction *txn = &mgr->txns[mgr->num_txns++];
    memset(txn, 0, sizeof(Transaction));
    txn->txn_id = tid;
    txn->status = TXN_ACTIVE;

    txn_wal_append(mgr, tid, WAL_BEGIN, 0, "", "");

    return tid;
}

static int check_deadlock(TXNManager *mgr, int txn_id, int rid,
                           LockMode mode) {
    /* Simple deadlock detection: check if any active transaction holds
     * an incompatible lock on this resource.
     *
     * Formal deadlock detection requires a Wait-For Graph (WFG) and
     * cycle detection (Tarjan's SCC). Here we use conservative timeout
     * approximation: fail if conflict and conflicting txn started after us.
     */
    for (int i = 0; i < mgr->lock_table.count; i++) {
        TxnLock *l = &mgr->lock_table.entries[i];
        if (l->rid == rid && l->granted && l->txn_id != txn_id) {
            if (!lock_compatible(l->mode, mode)) {
                /* Potential deadlock - check ages */
                for (int j = 0; j < mgr->num_txns; j++) {
                    if (mgr->txns[j].txn_id == l->txn_id) {
                        /* If holder has higher id (newer), we wait.
                         * Simple wound-wait variant for deadlock prevention. */
                        if (l->txn_id > txn_id) {
                            return 0; /* wait */
                        } else {
                            return -1; /* abort us (wound) */
                        }
                    }
                }
                return -1; /* conflict */
            }
        }
    }
    return 0;
}

int txn_lock_acquire(TXNManager *mgr, int txn_id, int rid, LockMode mode) {
    if (!mgr || txn_id < 0 || rid < 0) return -1;

    /* Find transaction */
    Transaction *txn = NULL;
    for (int i = 0; i < mgr->num_txns; i++) {
        if (mgr->txns[i].txn_id == txn_id && mgr->txns[i].status == TXN_ACTIVE) {
            txn = &mgr->txns[i];
            break;
        }
    }
    if (!txn) return -1;

    /* Check if we already hold a lock on this resource */
    for (int i = 0; i < txn->lock_count; i++) {
        if (txn->locks[i].rid == rid) {
            /* Lock upgrade: S -> X */
            if (mode == LOCK_EXCLUSIVE && txn->locks[i].mode == LOCK_SHARED) {
                txn->locks[i].mode = LOCK_EXCLUSIVE;
                return 0;
            }
            return 0; /* already held at sufficient level */
        }
    }

    /* Check conflicts and potential deadlock */
    if (check_deadlock(mgr, txn_id, rid, mode) < 0)
        return -1;

    /* Acquire lock */
    if (txn->lock_count >= TXN_MAX_LOCKS) return -1;
    if (mgr->lock_table.count >= TXN_MAX_LOCKS * TXN_MAX_ACTIVE) return -1;

    TxnLock *l = &mgr->lock_table.entries[mgr->lock_table.count++];
    l->rid     = rid;
    l->mode    = mode;
    l->txn_id  = txn_id;
    l->granted = 1;

    txn->locks[txn->lock_count++] = *l;
    return 0;
}

void txn_lock_release(TXNManager *mgr, int txn_id) {
    if (!mgr) return;

    /* Remove from global lock table */
    int write = 0;
    for (int i = 0; i < mgr->lock_table.count; i++) {
        if (mgr->lock_table.entries[i].txn_id != txn_id) {
            if (write != i)
                mgr->lock_table.entries[write] = mgr->lock_table.entries[i];
            write++;
        }
    }
    mgr->lock_table.count = write;

    /* Clear transaction locks */
    for (int i = 0; i < mgr->num_txns; i++) {
        if (mgr->txns[i].txn_id == txn_id) {
            mgr->txns[i].lock_count = 0;
            break;
        }
    }
}

int txn_wal_append(TXNManager *mgr, int txn_id, WALType type,
                    int rid, const char *before, const char *after) {
    if (!mgr || mgr->wal_log.count >= TXN_MAX_LOG * TXN_MAX_ACTIVE)
        return -1;

    WALRecord *r = &mgr->wal_log.entries[mgr->wal_log.count++];
    r->type   = type;
    r->txn_id = txn_id;
    r->rid    = rid;
    r->lsn    = mgr->wal_log.next_lsn++;
    if (before) strncpy(r->before, before, sizeof(r->before) - 1);
    if (after)  strncpy(r->after,  after,  sizeof(r->after)  - 1);

    /* Append to transaction's own WAL list */
    for (int i = 0; i < mgr->num_txns; i++) {
        if (mgr->txns[i].txn_id == txn_id) {
            if (mgr->txns[i].wal_count < TXN_MAX_LOG) {
                mgr->txns[i].wal_entries[mgr->txns[i].wal_count++] = *r;
            }
            break;
        }
    }

    return 0;
}

int txn_commit(TXNManager *mgr, int txn_id) {
    if (!mgr) return -1;

    Transaction *txn = NULL;
    for (int i = 0; i < mgr->num_txns; i++) {
        if (mgr->txns[i].txn_id == txn_id) {
            txn = &mgr->txns[i];
            break;
        }
    }
    if (!txn || txn->status != TXN_ACTIVE) return -1;

    /* Write WAL_COMMIT record */
    txn_wal_append(mgr, txn_id, WAL_COMMIT, 0, "", "");

    /* Release all locks (S2PL: held until commit) */
    txn_lock_release(mgr, txn_id);

    txn->status = TXN_COMMITTED;
    return 0;
}

void txn_abort(TXNManager *mgr, int txn_id) {
    if (!mgr) return;

    Transaction *txn = NULL;
    for (int i = 0; i < mgr->num_txns; i++) {
        if (mgr->txns[i].txn_id == txn_id) {
            txn = &mgr->txns[i];
            break;
        }
    }
    if (!txn) return;

    /* UNDO: rollback changes using WAL before-images */
    for (int i = txn->wal_count - 1; i >= 0; i--) {
        WALRecord *r = &txn->wal_entries[i];
        if (r->type == WAL_INSERT || r->type == WAL_UPDATE || r->type == WAL_DELETE) {
            /* In a real system, apply before-image to undo the change.
             * Here we track the undo in the WAL. */
            char before[128];
            memset(before, 0, sizeof(before));
            txn_wal_append(mgr, txn_id, WAL_ABORT, r->rid, r->before, "");
        }
    }

    /* Write WAL_ABORT record */
    txn_wal_append(mgr, txn_id, WAL_ABORT, 0, "", "");

    /* Release locks */
    txn_lock_release(mgr, txn_id);

    txn->status = TXN_ABORTED;
}

void txn_recover(TXNManager *mgr) {
    if (!mgr) return;

    printf("=== Crash Recovery (ARIES-style) ===\n");

    /* Phase 1: Analysis - identify committed and active transactions */
    int committed[TXN_MAX_ACTIVE] = {0};
    int active[TXN_MAX_ACTIVE] = {0};

    for (int i = 0; i < mgr->wal_log.count; i++) {
        WALRecord *r = &mgr->wal_log.entries[i];
        /* Map txn_id to slot */
        int slot = -1;
        for (int j = 0; j < mgr->num_txns; j++) {
            if (mgr->txns[j].txn_id == r->txn_id) { slot = j; break; }
        }
        if (slot < 0 || slot >= TXN_MAX_ACTIVE) continue;

        switch (r->type) {
        case WAL_BEGIN:   active[slot] = 1; committed[slot] = 0;  break;
        case WAL_COMMIT:  committed[slot] = 1; active[slot] = 0; break;
        case WAL_ABORT:   active[slot] = 0; break;
        default: break;
        }
    }

    /* Phase 2: REDO - redo all committed transactions */
    printf("REDO phase:\n");
    for (int i = 0; i < mgr->wal_log.count; i++) {
        WALRecord *r = &mgr->wal_log.entries[i];
        int slot = -1;
        for (int j = 0; j < mgr->num_txns; j++) {
            if (mgr->txns[j].txn_id == r->txn_id) { slot = j; break; }
        }
        if (slot < 0 || slot >= TXN_MAX_ACTIVE) continue;

        if (committed[slot] &&
            (r->type == WAL_INSERT || r->type == WAL_UPDATE)) {
            printf("  REDO txn=%d rid=%d lsn=%d\n", r->txn_id, r->rid, r->lsn);
        }
    }

    /* Phase 3: UNDO - undo all active (uncommitted) transactions */
    printf("UNDO phase:\n");
    for (int i = mgr->wal_log.count - 1; i >= 0; i--) {
        WALRecord *r = &mgr->wal_log.entries[i];
        int slot = -1;
        for (int j = 0; j < mgr->num_txns; j++) {
            if (mgr->txns[j].txn_id == r->txn_id) { slot = j; break; }
        }
        if (slot < 0 || slot >= TXN_MAX_ACTIVE) continue;

        if (active[slot] &&
            (r->type == WAL_INSERT || r->type == WAL_UPDATE || r->type == WAL_DELETE)) {
            printf("  UNDO txn=%d rid=%d lsn=%d before=%s\n",
                   r->txn_id, r->rid, r->lsn, r->before);
        }
    }

    /* Abort any remaining active transactions */
    for (int i = 0; i < mgr->num_txns; i++) {
        if (active[i])
            txn_abort(mgr, mgr->txns[i].txn_id);
    }
}

TXNStatus txn_get_status(const TXNManager *mgr, int txn_id) {
    if (!mgr) return TXN_ABORTED;
    for (int i = 0; i < mgr->num_txns; i++) {
        if (mgr->txns[i].txn_id == txn_id)
            return mgr->txns[i].status;
    }
    return TXN_ABORTED;
}

void txn_wal_dump(const TXNManager *mgr) {
    if (!mgr) { printf("WAL: (null)\n"); return; }
    printf("WAL: %d records, next_lsn=%d\n", mgr->wal_log.count, mgr->wal_log.next_lsn);
    const char *type_names[] = { "BEGIN", "INSERT", "UPDATE", "DELETE", "COMMIT", "ABORT" };
    for (int i = 0; i < mgr->wal_log.count; i++) {
        const WALRecord *r = &mgr->wal_log.entries[i];
        printf("  [LSN %d] txn=%d %s rid=%d",
               r->lsn, r->txn_id, type_names[r->type], r->rid);
        if (r->before[0]) printf(" before=[%s]", r->before);
        if (r->after[0])  printf(" after=[%s]", r->after);
        printf("\n");
    }
}
