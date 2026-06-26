#include "kv_store.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    printf("1. Test overwrite\n");
    KVStore store;
    kvs_init(&store);
    int32_t txn = kvs_begin_txn(&store);
    uint8_t key[] = "overwrite";
    int32_t v1 = 100, v2 = 200;
    printf("2. first put\n"); fflush(stdout);
    kvs_put(&store, txn, key, 10, (uint8_t*)&v1, 4);
    printf("3. second put\n"); fflush(stdout);
    kvs_put(&store, txn, key, 10, (uint8_t*)&v2, 4);
    printf("4. commit\n"); fflush(stdout);
    kvs_commit_txn(&store, txn);
    printf("5. begin txn2\n"); fflush(stdout);
    int32_t txn2 = kvs_begin_txn(&store);
    int32_t out_val = 0;
    int32_t out_len = 0;
    printf("6. get\n"); fflush(stdout);
    kvs_get(&store, txn2, key, 10, (uint8_t*)&out_val, &out_len);
    printf("7. got value=%d\n", out_val); fflush(stdout);
    kvs_commit_txn(&store, txn2);
    kvs_destroy(&store);
    printf("8. done\n");
    return 0;
}
