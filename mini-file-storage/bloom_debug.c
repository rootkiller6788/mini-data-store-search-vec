#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sstable.h"
int main() {
    uint8_t *keys[10]; uint32_t kl[10]; uint8_t *vals[10]; uint32_t vl[10];
    for (int i=0; i<10; i++) {
        char b[32]; snprintf(b,32,"bk_%02d",i);
        kl[i]=strlen(b); keys[i]=strdup((uint8_t*)b);
        snprintf(b,32,"bv_%02d",i);
        vl[i]=strlen(b); vals[i]=strdup((uint8_t*)b);
    }
    sstable_write("bdebug.sst", (const uint8_t**)keys, kl, (const uint8_t**)vals, vl, 10);
    SSTable *t = sstable_read("bdebug.sst");
    printf("bloom bytes=%u bit_array=%p\n", t->bloom.num_bytes, (void*)t->bloom.bit_array);
    for (int i=0; i<10; i++) {
        int bf = bloom_maybe_contain(&t->bloom, keys[i], kl[i]);
        uint8_t vb[64]; uint32_t vln;
        int rc = sstable_get(t, keys[i], kl[i], vb, &vln);
        printf("key=bk_%02d bloom=%d get=%d\n", i, bf, rc);
    }
    sstable_destroy(t);
    remove("bdebug.sst");
    for (int i=0; i<10; i++) { free(keys[i]); free(vals[i]); }
    return 0;
}
