#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sstable.h"

#define NUM_RECORDS 100

int main(void) {
    srand((unsigned int)time(NULL));

    printf("=== SSTable Demo: Write %d records ===\n", NUM_RECORDS);

    /* Generate test data */
    uint8_t  *keys[NUM_RECORDS];
    uint32_t  key_lens[NUM_RECORDS];
    uint8_t  *values[NUM_RECORDS];
    uint32_t  val_lens[NUM_RECORDS];

    for (int i = 0; i < NUM_RECORDS; i++) {
        char kbuf[32], vbuf[64];
        snprintf(kbuf, sizeof(kbuf), "key_%06d", i);
        snprintf(vbuf, sizeof(vbuf), "value_for_key_%06d_in_sstable", i);
        key_lens[i] = (uint32_t)strlen(kbuf);
        keys[i]    = (uint8_t *)strdup(kbuf);
        val_lens[i] = (uint32_t)strlen(vbuf);
        values[i]  = (uint8_t *)strdup(vbuf);
    }

    /* Write SSTable */
    const char *filename = "demo_output.sst";
    if (sstable_write(filename,
                      (const uint8_t **)keys, key_lens,
                      (const uint8_t **)values, val_lens,
                      NUM_RECORDS) != 0) {
        fprintf(stderr, "Failed to write SSTable\n");
        goto cleanup;
    }
    printf("SSTable written: %s\n", filename);

    /* Read back */
    SSTable *table = sstable_read(filename);
    if (!table) {
        fprintf(stderr, "Failed to read SSTable\n");
        goto cleanup;
    }
    printf("SSTable loaded: %u data blocks, %u index entries\n",
           table->num_data_blocks, table->index_block.num_entries);

    /* Verify all keys can be retrieved */
    int found = 0, missing = 0;
    uint8_t val_buf[MAX_VALUE_SIZE];
    uint32_t vlen;
    for (int i = 0; i < NUM_RECORDS; i++) {
        if (sstable_get(table, keys[i], key_lens[i], val_buf, &vlen) > 0) {
            found++;
        } else {
            missing++;
            printf("MISSING: key_%06d\n", i);
        }
    }
    printf("Lookup result: %d found, %d missing\n", found, missing);

    /* Test bloom filter false positive rate */
    int fp = 0;
    int test_n = 10000;
    for (int i = 0; i < test_n; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "absent_key_%06d", i);
        uint32_t klen = (uint32_t)strlen(kbuf);
        /* Bloom may say "maybe" but actual lookup says no */
        if (bloom_maybe_contain(&table->bloom, (uint8_t *)kbuf, klen)) {
            uint8_t dummy[MAX_VALUE_SIZE];
            uint32_t dlen;
            if (sstable_get(table, (uint8_t *)kbuf, klen, dummy, &dlen) <= 0) {
                fp++;
            }
        }
    }
    printf("Bloom filter: %d false positives out of %d tests (%.4f%%)\n",
           fp, test_n, (fp * 100.0) / test_n);

    sstable_destroy(table);
    remove(filename);

cleanup:
    for (int i = 0; i < NUM_RECORDS; i++) {
        free(keys[i]);
        free(values[i]);
    }
    return 0;
}
