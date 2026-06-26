#include <stdio.h>
#include "bptree.h"
#include "buffer_pool.h"
int main(void) {
    printf("=== STEP 2: bptree + buffer_pool ===\n");
    fflush(stdout);
    BPTree *t = bptree_create();
    printf("bptree OK\n");
    fflush(stdout);
    BufferPool bp;
    printf("stack OK (BP size=%zu)\n", sizeof(bp));
    fflush(stdout);
    bp_init(&bp, 4);
    printf("init OK\n");
    PageID p = {1, 0};
    int f = bp_fetch(&bp, p);
    printf("fetch OK frame=%d\n", f);
    bptree_destroy(t);
    bp_destroy(&bp);
    printf("=== DONE ===\n");
    return 0;
}
