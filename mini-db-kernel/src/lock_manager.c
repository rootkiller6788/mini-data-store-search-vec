#include "lock_manager.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t hash_resource(int32_t resource_id) {
    return (uint32_t)(resource_id * 2654435761ULL) % LM_NUM_BUCKETS;
}

static LockEntry *alloc_entry(LockManager *lm) {
    if (lm->pool_idx >= (LM_NUM_BUCKETS * 16)) return NULL;
    LockEntry *entry = &lm->entries_pool[lm->pool_idx++];
    memset(entry, 0, sizeof(*entry));
    return entry;
}

void lm_init(LockManager *lm) {
    memset(lm->buckets, 0, sizeof(lm->buckets));
    memset(lm->entries_pool, 0, sizeof(lm->entries_pool));
    lm->pool_idx = 0;
}

static bool has_conflict(LockManager *lm, int32_t resource_id, LockMode mode, int32_t txn_id) {
    uint32_t bucket = hash_resource(resource_id);
    LockEntry *entry = lm->buckets[bucket];
    while (entry) {
        if (entry->resource_id == resource_id && entry->granted) {
            if (entry->txn_id == txn_id) {
                if (entry->mode == LOCK_EXCLUSIVE) return false;
                if (mode == LOCK_SHARED) return false;
            }
            if (entry->mode == LOCK_EXCLUSIVE) return true;
            if (entry->mode == LOCK_SHARED && mode == LOCK_EXCLUSIVE) return true;
        }
        entry = entry->next;
    }
    return false;
}

bool lm_lock_acquire(LockManager *lm, int32_t txn_id, int32_t resource_id, LockMode mode) {
    uint32_t bucket = hash_resource(resource_id);
    if (!has_conflict(lm, resource_id, mode, txn_id)) {
        LockEntry *entry = alloc_entry(lm);
        if (!entry) return false;
        entry->txn_id = txn_id;
        entry->resource_id = resource_id;
        entry->mode = mode;
        entry->granted = true;
        entry->next = lm->buckets[bucket];
        lm->buckets[bucket] = entry;
        return true;
    }
    LockEntry *entry = alloc_entry(lm);
    if (!entry) return false;
    entry->txn_id = txn_id;
    entry->resource_id = resource_id;
    entry->mode = mode;
    entry->granted = false;
    LockEntry *head = lm->buckets[bucket];
    while (head) {
        if (head->resource_id == resource_id) {
            LockEntry *tail = head;
            while (tail->queue_next) tail = tail->queue_next;
            tail->queue_next = entry;
            entry->queue_prev = tail;
            entry->queue_next = NULL;
            return false;
        }
        head = head->next;
    }
    entry->next = lm->buckets[bucket];
    lm->buckets[bucket] = entry;
    return false;
}

void lm_lock_release(LockManager *lm, int32_t txn_id, int32_t resource_id) {
    uint32_t bucket = hash_resource(resource_id);
    LockEntry *entry = lm->buckets[bucket];
    LockEntry *prev = NULL;
    while (entry) {
        if (entry->resource_id == resource_id && entry->txn_id == txn_id && entry->granted) {
            if (prev) prev->next = entry->next;
            else lm->buckets[bucket] = entry->next;
            LockEntry *next = entry->queue_next;
            if (next && !next->granted) {
                next->granted = true;
                next->next = lm->buckets[bucket];
                lm->buckets[bucket] = next;
            }
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

bool lm_detect_deadlock(LockManager *lm) {
    for (int32_t b = 0; b < LM_NUM_BUCKETS; b++) {
        LockEntry *entry = lm->buckets[b];
        while (entry) {
            if (!entry->granted) {
                LockEntry *holder = lm->buckets[hash_resource(entry->resource_id)];
                while (holder) {
                    if (holder->resource_id == entry->resource_id && holder->granted && holder->txn_id != entry->txn_id) {
                        LockEntry *waits = lm->buckets[hash_resource(holder->txn_id)];
                        while (waits) {
                            if (!waits->granted && waits->txn_id == holder->txn_id) {
                                LockEntry *waits2 = lm->buckets[hash_resource(waits->resource_id)];
                                while (waits2) {
                                    if (waits2->granted && waits2->resource_id == waits->resource_id && waits2->txn_id == entry->txn_id) {
                                        return true;
                                    }
                                    waits2 = waits2->next;
                                }
                            }
                            waits = waits->next;
                        }
                    }
                    holder = holder->next;
                }
            }
            entry = entry->next;
        }
    }
    return false;
}

static LockEntry *find_granted_entry(LockManager *lm, int32_t txn_id, int32_t resource_id) {
    uint32_t bucket = hash_resource(resource_id);
    LockEntry *entry = lm->buckets[bucket];
    while (entry) {
        if (entry->txn_id == txn_id && entry->resource_id == resource_id && entry->granted) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

bool lm_lock_upgrade(LockManager *lm, int32_t txn_id, int32_t resource_id) {
    LockEntry *existing = find_granted_entry(lm, txn_id, resource_id);
    if (!existing) return false;
    if (existing->mode == LOCK_EXCLUSIVE) return true;
    if (has_conflict(lm, resource_id, LOCK_EXCLUSIVE, txn_id)) {
        LockEntry *conflicting = lm->buckets[hash_resource(resource_id)];
        int32_t other_holders = 0;
        while (conflicting) {
            if (conflicting->resource_id == resource_id && conflicting->granted &&
                conflicting->txn_id != txn_id && conflicting->mode == LOCK_SHARED) {
                other_holders++;
            }
            conflicting = conflicting->next;
        }
        if (other_holders > 0) return false;
    }
    existing->mode = LOCK_EXCLUSIVE;
    return true;
}

void lm_release_all(LockManager *lm, int32_t txn_id) {
    for (int32_t b = 0; b < LM_NUM_BUCKETS; b++) {
        LockEntry *entry = lm->buckets[b];
        LockEntry *prev = NULL;
        while (entry) {
            if (entry->txn_id == txn_id && entry->granted) {
                LockEntry *next = entry->next;
                if (prev) prev->next = next;
                else lm->buckets[b] = next;
                LockEntry *waiter = entry->queue_next;
                if (waiter && !waiter->granted) {
                    waiter->granted = true;
                    waiter->next = lm->buckets[b];
                    lm->buckets[b] = waiter;
                }
                entry = next;
                continue;
            }
            prev = entry;
            entry = entry->next;
        }
    }
}

int32_t lm_count_waiters(LockManager *lm, int32_t resource_id) {
    uint32_t bucket = hash_resource(resource_id);
    LockEntry *entry = lm->buckets[bucket];
    int32_t count = 0;
    while (entry) {
        if (entry->resource_id == resource_id && !entry->granted) {
            count++;
        }
        entry = entry->next;
    }
    return count;
}

void lm_print_locks(LockManager *lm) {
    printf("Lock Manager State:\n");
    for (int32_t b = 0; b < LM_NUM_BUCKETS; b++) {
        LockEntry *entry = lm->buckets[b];
        while (entry) {
            printf("  txn=%d resource=%d mode=%s granted=%s %s\n",
                   entry->txn_id, entry->resource_id,
                   entry->mode == LOCK_SHARED ? "S" : "X",
                   entry->granted ? "yes" : "no",
                   entry->granted ? "" : "(WAITING)");
            entry = entry->next;
        }
    }
}
