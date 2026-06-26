#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define LM_NUM_BUCKETS 256
#define LM_MAX_QUEUE   64

typedef enum {
    LOCK_SHARED    = 0,
    LOCK_EXCLUSIVE = 1
} LockMode;

typedef struct LockEntry {
    int32_t          txn_id;
    int32_t          resource_id;
    LockMode         mode;
    bool             granted;
    struct LockEntry *next;
    struct LockEntry *queue_next;
    struct LockEntry *queue_prev;
} LockEntry;

typedef struct {
    LockEntry *buckets[LM_NUM_BUCKETS];
    LockEntry  entries_pool[LM_NUM_BUCKETS * 16];
    int32_t    pool_idx;
} LockManager;

void    lm_init(LockManager *lm);
bool    lm_lock_acquire(LockManager *lm, int32_t txn_id, int32_t resource_id, LockMode mode);
void    lm_lock_release(LockManager *lm, int32_t txn_id, int32_t resource_id);
bool    lm_detect_deadlock(LockManager *lm);
bool    lm_lock_upgrade(LockManager *lm, int32_t txn_id, int32_t resource_id);
void    lm_release_all(LockManager *lm, int32_t txn_id);
int32_t lm_count_waiters(LockManager *lm, int32_t resource_id);
void    lm_print_locks(LockManager *lm);

#endif
