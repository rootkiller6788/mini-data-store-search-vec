#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lsm_tree.h"

#ifdef _WIN32
#include <direct.h>
#define rmdir_p(d) _rmdir(d)
#else
#include <unistd.h>
#define rmdir_p(d) rmdir(d)
#endif

#define NUM_KEYS 1000

int main(void) {
    srand((unsigned int)time(NULL));

    printf("=== LSM Tree Demo ===\n");
    printf("Opening LSM tree with LEVELED compaction...\n");

    LSMTree *tree = lsm_open("./lsm_demo_data", COMPACTION_LEVELED);
    if (!tree) {
        fprintf(stderr, "Failed to open LSM tree\n");
        return 1;
    }

    /* Put 1000 keys */
    printf("Inserting %d keys...\n", NUM_KEYS);
    for (int i = 0; i < NUM_KEYS; i++) {
        char kbuf[32], vbuf[64];
        snprintf(kbuf, sizeof(kbuf), "lsm_key_%06d", i);
        snprintf(vbuf, sizeof(vbuf), "lsm_value_number_%06d_in_lsm_tree", i);
        lsm_put(tree, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                (uint8_t *)vbuf, (uint32_t)strlen(vbuf));
    }

    printf("Checking compaction...\n");
    int n = lsm_maybe_compact(tree);
    printf("Compaction rounds: %d\n", n);

    /* Get test: verify all keys */
    printf("Verifying %d keys...\n", NUM_KEYS);
    uint8_t val_buf[1024];
    uint32_t vlen;
    int found = 0, missed = 0;

    for (int i = 0; i < NUM_KEYS; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "lsm_key_%06d", i);
        if (lsm_get(tree, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                    val_buf, &vlen) > 0) {
            found++;
        } else {
            missed++;
            if (missed <= 5)
                printf("MISSING: %s\n", kbuf);
        }
    }
    printf("Get result: %d found, %d missed\n", found, missed);

    /* Random read speed test */
    printf("Random read test...\n");
    clock_t start = clock();
    int reads = 5000;
    for (int i = 0; i < reads; i++) {
        char kbuf[32];
        int r = rand() % NUM_KEYS;
        snprintf(kbuf, sizeof(kbuf), "lsm_key_%06d", r);
        lsm_get(tree, (uint8_t *)kbuf, (uint32_t)strlen(kbuf), val_buf, &vlen);
    }
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    printf("%d reads in %.3f s (%.0f reads/s)\n", reads, elapsed,
           reads / elapsed);

    /* Level stats */
    printf("\nLevel stats:\n");
    for (int lv = 0; lv < LSM_MAX_LEVELS; lv++) {
        if (tree->levels[lv].num_files > 0)
            printf("  Level %d: %u files\n", lv, tree->levels[lv].num_files);
    }

    lsm_close(tree);

    /* Cleanup */
    remove("./lsm_demo_data/level0_000000.sst");
    remove("./lsm_demo_data/level0_000001.sst");
    remove("./lsm_demo_data/level1_000002.sst");
    remove("./lsm_demo_data/level1_000003.sst");
    remove("./lsm_demo_data/level2_000000.sst");
    remove("./lsm_demo_data/level1_000000.sst");
    remove("./lsm_demo_data/MINI_LOG");
    remove("./lsm_demo_data/MINI_LOG.closed");
    rmdir_p("./lsm_demo_data");

    return 0;
}
