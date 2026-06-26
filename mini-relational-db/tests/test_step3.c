#include <stdio.h>
#include "bptree.h"
#include "buffer_pool.h"
#include "transaction.h"
int main(void) {
    printf("=== STEP 3: all three ===\n");
    fflush(stdout);
    
    printf("bptree...\n"); fflush(stdout);
    BPTree *t = bptree_create();
    
    printf("buffer_pool...\n"); fflush(stdout);
    BufferPool bp;
    bp_init(&bp, 4);
    
    printf("TXNManager size=%zu\n", sizeof(TXNManager)); fflush(stdout);
    TXNManager mgr;
    printf("TXNManager on stack OK\n"); fflush(stdout);
    txn_mgr_init(&mgr);
    
    int tid = txn_begin(&mgr);
    printf("txn_begin=%d\n", tid); fflush(stdout);
    
    txn_commit(&mgr, tid);
    printf("commit OK\n"); fflush(stdout);
    
    bptree_destroy(t);
    bp_destroy(&bp);
    printf("=== DONE ===\n");
    return 0;
}
