#include "btree.h"
#include <stdio.h>
int main(void) {
    BTree tree = btree_create();
    for (int32_t kk = 0; kk < 50; kk++) {
        btree_insert(&tree, kk, kk*10);
    }
    printf("=== Tree after inserts ===\n");
    btree_print_tree(tree);
    
    /* Check specific keys */
    int32_t probe[] = {7, 11, 15, 19, 23, 27, 28};
    for (int i = 0; i < 7; i++) {
        int32_t v;
        if (btree_search(tree, probe[i], &v))
            printf("key=%d val=%d\n", probe[i], v);
        else
            printf("key=%d NOT FOUND\n", probe[i]);
    }
    return 0;
}
