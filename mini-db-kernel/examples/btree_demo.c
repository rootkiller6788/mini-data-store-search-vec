#include "btree.h"
#include <stdio.h>

int main(void) {
    printf("=== B+Tree Demo ===\n\n");

    BTree tree = btree_create();

    printf("1. Insert keys 1..50\n");
    for (int32_t i = 1; i <= 50; i++) {
        btree_insert(&tree, i, i * 10);
    }
    printf("   After insertions:\n");
    btree_print_tree(tree);

    printf("\n2. Point queries:\n");
    for (int32_t i = 10; i <= 15; i++) {
        int32_t value = 0;
        if (btree_search(tree, i, &value)) {
            printf("   key=%d -> value=%d\n", i, value);
        } else {
            printf("   key=%d -> NOT FOUND\n", i);
        }
    }

    printf("\n3. Range scan [20, 30]:\n");
    int32_t keys_out[50], values_out[50];
    int32_t count = btree_search_range(tree, 20, 30, keys_out, values_out, 50);
    for (int32_t i = 0; i < count; i++) {
        printf("   key=%d -> value=%d\n", keys_out[i], values_out[i]);
    }
    printf("   Total: %d results\n", count);

    printf("\n4. Delete keys 5, 10, 15, 25, 35, 45\n");
    btree_delete(&tree, 5);
    btree_delete(&tree, 10);
    btree_delete(&tree, 15);
    btree_delete(&tree, 25);
    btree_delete(&tree, 35);
    btree_delete(&tree, 45);
    printf("   After deletions:\n");
    btree_print_tree(tree);

    printf("\n5. Verify deleted keys are gone:\n");
    int32_t v = 0;
    printf("   search 5: %s\n", btree_search(tree, 5, &v) ? "FOUND (error!)" : "not found (good)");
    printf("   search 10: %s\n", btree_search(tree, 10, &v) ? "FOUND (error!)" : "not found (good)");
    printf("   search 20: %s\n", btree_search(tree, 20, &v) ? "found (good)" : "not found (error!)");

    printf("\n=== B+Tree Demo Complete ===\n");
    return 0;
}
