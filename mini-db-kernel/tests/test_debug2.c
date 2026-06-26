#include "kv_store.h"
#include <stdio.h>
#include <string.h>
int main(void) {
    printf("=== test 1: init/destroy ===\n");
    fflush(stdout);
    { KVStore s; kvs_init(&s); kvs_destroy(&s); printf("ok\n"); fflush(stdout); }
    
    printf("=== test 2: begin/commit ===\n");
    fflush(stdout);
    { KVStore s; kvs_init(&s); 
      int32_t t = kvs_begin_txn(&s); printf("txn=%d\n",t); fflush(stdout);
      kvs_commit_txn(&s, t); kvs_destroy(&s); printf("ok\n"); fflush(stdout); }
    
    printf("=== test 3: put/get ===\n");
    fflush(stdout);
    { KVStore s; kvs_init(&s);
      int32_t t = kvs_begin_txn(&s);
      uint8_t k[] = "mykey"; int32_t v=42;
      kvs_put(&s, t, k, 6, (uint8_t*)&v, 4);
      kvs_commit_txn(&s, t);
      int32_t t2 = kvs_begin_txn(&s);
      int32_t ov=0, ol=0;
      kvs_get(&s, t2, k, 6, (uint8_t*)&ov, &ol);
      printf("val=%d len=%d\n", ov, ol); fflush(stdout);
      kvs_commit_txn(&s, t2);
      kvs_destroy(&s); printf("ok\n"); fflush(stdout); }
    
    printf("=== test 4: scan ===\n");
    fflush(stdout);
    { KVStore s; kvs_init(&s);
      int32_t t = kvs_begin_txn(&s);
      for (int32_t kk = 0; kk < 50; kk++) {
        uint8_t kb[8]; memset(kb,0,8); kb[0]=(uint8_t)(kk&0xFF);
        int32_t v = kk*10;
        kvs_put(&s, t, kb, 4, (uint8_t*)&v, 4);
      }
      kvs_commit_txn(&s, t);
      int32_t t2 = kvs_begin_txn(&s);
      KVCursor c;
      kvs_scan_open(&s, t2, 0, 0x7fffffff, &c);
      KVEntry e; int sc=0;
      while(kvs_scan_next(&c,&e)) sc++;
      printf("scanned=%d\n", sc); fflush(stdout);
      kvs_scan_close(&c);
      kvs_commit_txn(&s, t2);
      kvs_destroy(&s); printf("ok\n"); fflush(stdout); }
    
    printf("ALL OK\n");
    return 0;
}
