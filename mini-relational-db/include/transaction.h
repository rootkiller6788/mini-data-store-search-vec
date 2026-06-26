#ifndef TRANSACTION_H
#define TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

/*
 * Transaction Manager — L2 Core Concept: ACID.
 *
 * Reference: CMU 15-445 Lecture 13-16: Transaction Management
 *
 * ACID Properties (Haerder & Reuter, 1983):
 *   A: Atomicity   — all-or-nothing execution
 *   C: Consistency — database invariants maintained
 *   I: Isolation   — concurrent transactions don't interfere
 *   D: Durability  — committed changes survive failures
 *
 * This module implements a simplified transaction manager with:
 *   - Strict Two-Phase Locking (S2PL) for serializability
 *   - Write-Ahead Logging (WAL) for atomicity/durability
 *   - READ_COMMITTED isolation level
 */

#define TXN_MAX_LOCKS  16
#define TXN_MAX_LOG    16
#define TXN_MAX_ACTIVE  8

/* Lock modes (compatible with strict 2PL). */
typedef enum {
    LOCK_SHARED    = 0,          /* S-lock: read */
    LOCK_EXCLUSIVE = 1           /* X-lock: write */
} LockMode;

/* Lock on a database resource (identified by rid). */
typedef struct {
    int       rid;               /* resource ID */
    LockMode  mode;
    int       txn_id;            /* which transaction holds it */
    int       granted;
} TxnLock;

/* WAL entry types. */
typedef enum {
    WAL_BEGIN   = 0,
    WAL_INSERT  = 1,
    WAL_UPDATE  = 2,
    WAL_DELETE  = 3,
    WAL_COMMIT  = 4,
    WAL_ABORT   = 5
} WALType;

/* A single WAL record. */
typedef struct {
    WALType type;
    int     txn_id;
    int     rid;
    char    before[64];
    char    after[64];
    int     lsn;                 /* log sequence number */
} WALRecord;

/* Transaction state machine. */
typedef enum {
    TXN_ACTIVE   = 0,
    TXN_COMMITTED = 1,
    TXN_ABORTED  = 2
} TXNStatus;

/* Transaction descriptor. */
typedef struct {
    int        txn_id;
    TXNStatus  status;
    int        lock_count;
    TxnLock    locks[TXN_MAX_LOCKS];
    int        wal_count;
    WALRecord  wal_entries[TXN_MAX_LOG];
} Transaction;

/* Global lock table. */
typedef struct {
    TxnLock  entries[TXN_MAX_LOCKS * TXN_MAX_ACTIVE];
    int      count;
} LockTable;

/* Global WAL log. */
typedef struct {
    WALRecord entries[TXN_MAX_LOG * TXN_MAX_ACTIVE];
    int       count;
    int       next_lsn;
} WALLog;

/* Transaction manager. */
typedef struct {
    Transaction  txns[TXN_MAX_ACTIVE];
    int          num_txns;
    LockTable    lock_table;
    WALLog       wal_log;
    int          next_txn_id;
} TXNManager;

/* --- Lock Table API (L4: Serializability Theorem) --- */

/* Initialize the transaction manager. */
void      txn_mgr_init(TXNManager *mgr);

/*
 * Begin a new transaction. Returns transaction ID >= 0, or -1.
 * Writes WAL_BEGIN record.
 */
int       txn_begin(TXNManager *mgr);

/*
 * Acquire a lock with strict 2PL protocol.
 * All locks are held until commit/abort.
 * Returns 0 on success, -1 on deadlock/conflict.
 */
int       txn_lock_acquire(TXNManager *mgr, int txn_id, int rid, LockMode mode);

/*
 * Release all locks for a transaction (called at commit/abort).
 */
void      txn_lock_release(TXNManager *mgr, int txn_id);

/*
 * Write-Ahead Log appends. Invariant: WAL record is flushed before data page.
 */
int       txn_wal_append(TXNManager *mgr, int txn_id, WALType type,
                          int rid, const char *before, const char *after);

/*
 * Commit a transaction: write WAL_COMMIT, release locks, mark committed.
 * Returns 0 on success.
 */
int       txn_commit(TXNManager *mgr, int txn_id);

/*
 * Abort a transaction: UNDO all changes using WAL before-images,
 * write WAL_ABORT, release locks.
 */
void      txn_abort(TXNManager *mgr, int txn_id);

/*
 * Crash recovery: replay WAL to restore consistent state.
 *   - REDO committed transactions
 *   - UNDO uncommitted transactions
 */
void      txn_recover(TXNManager *mgr);

/* Get transaction status. */
TXNStatus txn_get_status(const TXNManager *mgr, int txn_id);

/* Print WAL log for debugging. */
void      txn_wal_dump(const TXNManager *mgr);

#endif
