#include <stdio.h>
#include "bptree.h"
int main(void) {
    printf("=== STEP 1: bptree ===\n");
    fflush(stdout);
    BPTree *t = bptree_create();
    printf("create OK\n");
    bptree_insert(t, "a", "1");
    printf("insert OK\n");
    bptree_destroy(t);
    printf("destroy OK\n");
    return 0;
}
