#include "btree.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int32_t hash_key(const uint8_t *key, int32_t key_len) {
    uint32_t hash = 5381;
    for (int32_t i = 0; i < key_len; i++) {
        hash = ((hash << 5) + hash) + (uint32_t)key[i];
    }
    return (int32_t)(hash & 0x7FFFFFFF);
}
int main(void) {
    BTree tree = btree_create();
    for (int32_t kk = 0; kk < 50; kk++) {
        uint8_t kb[8]; memset(kb,0,8); kb[0]=(uint8_t)(kk&0xFF);
        int32_t key = hash_key(kb, 4);
        printf("put %d: key=%d\n", kk, key); fflush(stdout);
        btree_insert(&tree, key, kk*10);
    }
    /* verify */
    for (int32_t kk = 0; kk < 50; kk++) {
        uint8_t kb[8]; memset(kb,0,8); kb[0]=(uint8_t)(kk&0xFF);
        int32_t key = hash_key(kb, 4);
        int32_t v;
        if (btree_search(tree, key, &v)) {
            printf("get %d: key=%d val=%d\n", kk, key, v);
        } else {
            printf("get %d: key=%d NOT FOUND\n", kk, key);
        }
    }
    printf("done\n");
    return 0;
}
