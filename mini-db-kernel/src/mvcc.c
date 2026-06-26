#include "mvcc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MVCC_MAX_TXNS 64

static int32_t next_txn_id = 1;
static int32_t active_txns[MVCC_MAX_TXNS];
static int32_t num_active = 0;
static int32_t committed_txns[MVCC_MAX_TXNS];
static int32_t num_committed = 0;
static int32_t aborted_txns[MVCC_MAX_TXNS];
static int32_t num_aborted = 0;

static bool is_active(int32_t txn_id) {
    for (int32_t i = 0; i < num_active; i++) {
        if (active_txns[i] == txn_id) return true;
    }
    return false;
}

static bool is_committed(int32_t txn_id) {
    for (int32_t i = 0; i < num_committed; i++) {
        if (committed_txns[i] == txn_id) return true;
    }
    return false;
}

static bool is_aborted(int32_t txn_id) {
    for (int32_t i = 0; i < num_aborted; i++) {
        if (aborted_txns[i] == txn_id) return true;
    }
    return false;
}

void mvcc_init(void) {
    next_txn_id = 1;
    num_active = 0;
    num_committed = 0;
    num_aborted = 0;
}

int32_t mvcc_begin(void) {
    int32_t txn_id = next_txn_id++;
    if (num_active < MVCC_MAX_TXNS) {
        active_txns[num_active++] = txn_id;
    }
    return txn_id;
}

void mvcc_commit(int32_t txn_id) {
    for (int32_t i = 0; i < num_active; i++) {
        if (active_txns[i] == txn_id) {
            active_txns[i] = active_txns[num_active - 1];
            num_active--;
            break;
        }
    }
    if (num_committed < MVCC_MAX_TXNS) {
        committed_txns[num_committed++] = txn_id;
    }
}

void mvcc_abort(int32_t txn_id) {
    for (int32_t i = 0; i < num_active; i++) {
        if (active_txns[i] == txn_id) {
            active_txns[i] = active_txns[num_active - 1];
            num_active--;
            break;
        }
    }
    if (num_aborted < MVCC_MAX_TXNS) {
        aborted_txns[num_aborted++] = txn_id;
    }
}

MVCCTransaction mvcc_get_snapshot(int32_t txn_id) {
    MVCCTransaction snap;
    snap.txn_id = txn_id;
    snap.snapshot_xmin = next_txn_id - 1;
    snap.snapshot_xmax = next_txn_id + 64;
    snap.num_active = num_active;
    memcpy(snap.snapshot_active_txns, active_txns, num_active * sizeof(int32_t));
    for (int32_t i = 0; i < num_active; i++) {
        if (snap.snapshot_active_txns[i] == txn_id) {
            snap.snapshot_active_txns[i] = snap.snapshot_active_txns[num_active - 1];
            snap.num_active--;
            break;
        }
    }
    return snap;
}

/*
 * L4: MVCC Visibility Rules (Snapshot Isolation)
 *
 * 版本 V 对快照 S 可见的条件:
 *   1. V.txn_begin == S.txn_id → 自己创建，除非被自己删除
 *   2. V.txn_begin 已提交 且 不在 S 的活跃列表中
 *   3. V.txn_begin 未中止 (is_aborted)
 *   4. V.txn_end == 0 或 V.txn_end == S.txn_id (自己删的)
 *   5. V.txn_end 未提交 (其他人删但未提交)
 *
 * 参考: Berenson et al., "A Critique of ANSI SQL Isolation Levels", SIGMOD 1995
 */
static bool is_visible(int32_t version_txn_begin, int32_t version_txn_end,
                       MVCCTransaction *snap) {
    if (version_txn_begin == snap->txn_id) {
        return version_txn_end == 0;
    }
    if (is_aborted(version_txn_begin)) {
        return false;
    }
    if (!is_committed(version_txn_begin)) {
        return false;
    }
    if (version_txn_end != 0 && version_txn_end != snap->txn_id) {
        if (is_committed(version_txn_end)) return false;
    }
    return true;
}

bool mvcc_read(Tuple *tuple, int32_t txn_id, uint8_t *data_out, int32_t *size_out) {
    if (!tuple || !tuple->version_chain) return false;
    MVCCTransaction snap = mvcc_get_snapshot(txn_id);
    TupleVersion *ver = tuple->version_chain;
    while (ver) {
        if (is_visible(ver->txn_id_begin, ver->txn_id_end, &snap)) {
            if (data_out && ver->data_size > 0) {
                memcpy(data_out, ver->data, ver->data_size < MVCC_MAX_DATA_SIZE ? ver->data_size : MVCC_MAX_DATA_SIZE);
            }
            if (size_out) *size_out = ver->data_size;
            return true;
        }
        ver = ver->next_version;
    }
    return false;
}

bool mvcc_update(Tuple *tuple, int32_t txn_id, const uint8_t *new_data, int32_t new_size) {
    if (!tuple) return false;
    if (!is_active(txn_id)) return false;
    TupleVersion *head = tuple->version_chain;
    while (head) {
        if (head->txn_id_begin == txn_id && head->txn_id_end == 0) {
            head->txn_id_end = next_txn_id;
            break;
        }
        head = head->next_version;
    }
    if (head && head->txn_id_begin == txn_id && head->txn_id_end != 0) {
        head->txn_id_end = 0;
    }
    TupleVersion *new_ver = malloc(sizeof(TupleVersion));
    memset(new_ver, 0, sizeof(*new_ver));
    new_ver->txn_id_begin = txn_id;
    new_ver->txn_id_end = 0;
    new_ver->data_size = new_size < MVCC_MAX_DATA_SIZE ? new_size : MVCC_MAX_DATA_SIZE;
    if (new_data && new_size > 0) {
        memcpy(new_ver->data, new_data, new_ver->data_size);
    }
    new_ver->next_version = tuple->version_chain;
    tuple->version_chain = new_ver;
    return true;
}

int32_t mvcc_gc(Tuple *tuples, int32_t num_tuples) {
    int32_t cleaned = 0;
    int32_t min_active = next_txn_id;
    for (int32_t i = 0; i < num_active; i++) {
        if (active_txns[i] < min_active) min_active = active_txns[i];
    }
    for (int32_t t = 0; t < num_tuples; t++) {
        Tuple *tuple = &tuples[t];
        TupleVersion *prev = NULL;
        TupleVersion *ver = tuple->version_chain;
        TupleVersion *keep = NULL;
        while (ver) {
            if (ver->txn_id_end != 0 && ver->txn_id_end < min_active && is_committed(ver->txn_id_end)) {
                TupleVersion *next = ver->next_version;
                if (prev) prev->next_version = next;
                else tuple->version_chain = next;
                free(ver);
                ver = next;
                cleaned++;
                continue;
            }
            if (!keep) keep = ver;
            prev = ver;
            ver = ver->next_version;
        }
    }
    return cleaned;
}

void mvcc_print_tuple(Tuple *tuple) {
    if (!tuple) { printf("NULL tuple\n"); return; }
    printf("Tuple id=%d versions:\n", tuple->tuple_id);
    TupleVersion *ver = tuple->version_chain;
    while (ver) {
        printf("  [txn_begin=%d, txn_end=%d] size=%d data=",
               ver->txn_id_begin, ver->txn_id_end, ver->data_size);
        for (int32_t i = 0; i < ver->data_size && i < 20; i++) {
            printf("%02x ", ver->data[i]);
        }
        printf("\n");
        ver = ver->next_version;
    }
}
