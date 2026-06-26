#ifndef MVCC_H
#define MVCC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MVCC_MAX_DATA_SIZE 256

typedef struct TupleVersion {
    int32_t              txn_id_begin;
    int32_t              txn_id_end;
    uint8_t              data[MVCC_MAX_DATA_SIZE];
    int32_t              data_size;
    struct TupleVersion  *next_version;
} TupleVersion;

typedef struct {
    int32_t       tuple_id;
    TupleVersion  *version_chain;
} Tuple;

typedef struct {
    int32_t  txn_id;
    int32_t  snapshot_xmin;
    int32_t  snapshot_xmax;
    int32_t  snapshot_active_txns[64];
    int32_t  num_active;
} MVCCTransaction;

void mvcc_init(void);
int32_t mvcc_begin(void);
void    mvcc_commit(int32_t txn_id);
void    mvcc_abort(int32_t txn_id);
MVCCTransaction mvcc_get_snapshot(int32_t txn_id);

bool mvcc_read(Tuple *tuple, int32_t txn_id, uint8_t *data_out, int32_t *size_out);
bool mvcc_update(Tuple *tuple, int32_t txn_id, const uint8_t *new_data, int32_t new_size);
int32_t mvcc_gc(Tuple *tuples, int32_t num_tuples);

void mvcc_print_tuple(Tuple *tuple);

#endif
