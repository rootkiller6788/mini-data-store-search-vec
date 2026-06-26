#include "kv_store.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    KVStore s; 
    kvs_init(&s);
    int32_t t = kvs_begin_txn(&s);
    for (int32_t kk = 0; kk < 50; kk++) {
        printf("  put %d...", kk); fflush(stdout);
        uint8_t kb[8]; memset(kb,0,8); kb[0]=(uint8_t)(kk&0xFF);
        int32_t v = kk*10;
        bool ok = kvs_put(&s, t, kb, 4, (uint8_t*)&v, 4);
        printf("%s\n", ok ? "ok" : "FAIL"); fflush(stdout);
    }
    kvs_commit_txn(&s, t);
    kvs_destroy(&s);
    printf("done\n");
    return 0;
}
