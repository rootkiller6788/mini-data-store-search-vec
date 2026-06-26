#include "kv_store.h"
#include <stdio.h>
int main(void) {
    printf("test start\n");
    fflush(stdout);
    KVStore store;
    printf("calling kvs_init\n");
    fflush(stdout);
    kvs_init(&store);
    printf("kvs_init done, initialized=%d\n", store.initialized);
    fflush(stdout);
    kvs_destroy(&store);
    printf("kvs_destroy done\n");
    return 0;
}
