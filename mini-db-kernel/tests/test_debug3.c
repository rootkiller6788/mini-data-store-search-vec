#include "kv_store.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    printf("=== scan test ===\n");
    fflush(stdout);
    KVStore s; 
    printf("a. kvs_init...\n"); fflush(stdout);
    kvs_init(&s);
    printf("b. begin_txn...\n"); fflush(stdout);
    int32_t t = kvs_begin_txn(&s);
    printf("c. inserting 50 keys...\n"); fflush(stdout);
    for (int32_t kk = 0; kk < 50; kk++) {
        uint8_t kb[8]; memset(kb,0,8); kb[0]=(uint8_t)(kk&0xFF);
        int32_t v = kk*10;
        if (!kvs_put(&s, t, kb, 4, (uint8_t*)&v, 4)) {
            printf("PUT FAILED at kk=%d\n", kk);
        }
    }
    printf("d. commit...\n"); fflush(stdout);
    kvs_commit_txn(&s, t);
    printf("e. begin_txn2...\n"); fflush(stdout);
    int32_t t2 = kvs_begin_txn(&s);
    printf("f. scan_open...\n"); fflush(stdout);
    KVCursor c; memset(&c, 0, sizeof(c));
    kvs_scan_open(&s, t2, 0, 0x7fffffff, &c);
    printf("g. scan_next...\n"); fflush(stdout);
    KVEntry e; int sc=0;
    while(kvs_scan_next(&c,&e)) sc++;
    printf("h. scanned=%d\n", sc); fflush(stdout);
    kvs_scan_close(&c);
    kvs_commit_txn(&s, t2);
    kvs_destroy(&s);
    printf("i. done\n");
    return 0;
}
