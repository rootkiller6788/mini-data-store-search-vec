#include "btree.h"
#include <stdio.h>
int main(void) {
    BTree tree = btree_create();
    for (int32_t kk = 0; kk < 50; kk++) {
        printf("put %d\n", kk); fflush(stdout);
        btree_insert(&tree, kk, kk*10);
    }
    int32_t errs = 0;
    for (int32_t kk = 0; kk < 50; kk++) {
        int32_t v;
        if (btree_search(tree, kk, &v)) {
            if (v != kk*10) { printf("  ERR: key=%d val=%d expect=%d\n", kk, v, kk*10); errs++; }
        } else {
            printf("  NOT FOUND: key=%d\n", kk); errs++;
        }
    }
    printf("errors=%d\n", errs);
    return errs;
}
