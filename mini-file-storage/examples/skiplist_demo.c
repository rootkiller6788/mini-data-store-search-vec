#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "skiplist.h"

#define NUM_KEYS 50

/* Simple shuffle */
static void shuffle(int *arr, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
    }
}

int main(void) {
    srand((unsigned int)time(NULL));

    printf("=== Skip List Demo ===\n\n");

    /* Create skip list */
    SkipList *list = skiplist_create();
    if (!list) {
        fprintf(stderr, "Failed to create skip list\n");
        return 1;
    }
    printf("Skip list created (max level: %d)\n", SKIPLIST_MAX_LEVEL);

    /* Insert 50 random keys in shuffled order */
    int indices[NUM_KEYS];
    for (int i = 0; i < NUM_KEYS; i++) indices[i] = i;
    shuffle(indices, NUM_KEYS);

    printf("Inserting %d keys (shuffled)...\n", NUM_KEYS);
    for (int i = 0; i < NUM_KEYS; i++) {
        char kbuf[32], vbuf[64];
        int idx = indices[i];
        snprintf(kbuf, sizeof(kbuf), "skiplist_key_%06d", idx);
        snprintf(vbuf, sizeof(vbuf), "value_%06d_for_%06d", idx, idx);
        skiplist_insert(list, (uint8_t *)kbuf, (uint32_t)strlen(kbuf),
                        (uint8_t *)vbuf, (uint32_t)strlen(vbuf));
    }
    printf("Inserted %u keys (size=%u, level=%u)\n\n",
           NUM_KEYS, list->size, list->level);

    /* In-order traversal via iterator */
    printf("--- In-order traversal (first 15 entries) ---\n");
    SkipListIterator *it = skiplist_iterator(list);
    SkipNode *node;
    int count = 0;
    while ((node = skiplist_iterator_next(it)) != NULL && count < 15) {
        printf("  %.*s => %.*s\n",
               (int)node->key_len, node->key,
               (int)node->value_len, node->value);
        count++;
    }
    if (count >= 15) printf("  ... (total %u entries)\n\n", list->size);
    skiplist_iterator_destroy(it);

    /* Search for the middle key */
    {
        int mid = NUM_KEYS / 2;
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "skiplist_key_%06d", mid);
        printf("--- Searching for '%s' ---\n", kbuf);
        node = skiplist_search(list, (uint8_t *)kbuf, (uint32_t)strlen(kbuf));
        if (node) {
            printf("  Found: %.*s => %.*s\n",
                   (int)node->key_len, node->key,
                   (int)node->value_len, node->value);
        } else {
            printf("  NOT FOUND!\n");
        }
    }

    /* Search for a non-existent key */
    {
        char *nk = "skiplist_key_999999";
        printf("\n--- Searching for '%s' (absent) ---\n", nk);
        node = skiplist_search(list, (uint8_t *)nk, (uint32_t)strlen(nk));
        printf("  %s\n", node ? "Found (unexpected)" : "Not found (expected)");
    }

    /* Full iterator walk to count entries */
    printf("\n--- Full iterator walk ---\n");
    it = skiplist_iterator(list);
    int total = 0;
    const char *prev = "";
    int order_ok = 1;
    while ((node = skiplist_iterator_next(it)) != NULL) {
        if (strncmp(prev, (char *)node->key, strlen(prev)) > 0)
            order_ok = 0;
        prev = (char *)node->key;
        total++;
    }
    skiplist_iterator_destroy(it);
    printf("  Total entries: %d, order %s\n",
           total, order_ok ? "OK" : "BROKEN");

    /* Performance: 10000 random searches */
    printf("\n--- Performance: 10000 random searches ---\n");
    clock_t start = clock();
    for (int i = 0; i < 10000; i++) {
        char kbuf[32];
        snprintf(kbuf, sizeof(kbuf), "skiplist_key_%06d", rand() % NUM_KEYS);
        skiplist_search(list, (uint8_t *)kbuf, (uint32_t)strlen(kbuf));
    }
    clock_t end = clock();
    double secs = (double)(end - start) / CLOCKS_PER_SEC;
    printf("  10000 searches in %.4f s (%.0f ops/s)\n", secs, 10000.0 / secs);

    skiplist_destroy(list);
    printf("\nSkip list demo complete.\n");
    return 0;
}
