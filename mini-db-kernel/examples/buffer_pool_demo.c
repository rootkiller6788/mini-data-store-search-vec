#include "buffer_pool.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    BufferPool bp;
    bp_init(&bp);

    printf("=== Buffer Pool Demo ===\n\n");

    printf("1. Fetch 10 pages (page 0..9)\n");
    for (int32_t i = 0; i < 10; i++) {
        int32_t idx = bp_fetch_page(&bp, i);
        printf("   page %d -> frame %d\n", i, idx);
    }
    bp_print_stats(&bp);

    printf("\n2. Write dummy data to page 5 and mark dirty\n");
    int32_t idx5 = bp_fetch_page(&bp, 5);
    sprintf((char *)bp.frames[idx5].data, "Hello from page 5!");
    bp.frames[idx5].dirty = true;
    bp_unpin_page(&bp, 5);
    printf("   Page 5 dirty, unpinned\n");

    printf("\n3. Fetch 1015 more pages to force LRU eviction\n");
    for (int32_t i = 10; i < 1025; i++) {
        int32_t idx = bp_fetch_page(&bp, i);
        if (idx < 0) printf("   FAIL: no frame available at page %d\n", i);
    }
    bp_print_stats(&bp);

    printf("\n4. Re-fetch page 5 (should be cache miss, reloaded)\n");
    idx5 = bp_fetch_page(&bp, 5);
    printf("   page 5 -> frame %d, data: %s\n", idx5, bp.frames[idx5].data);
    bp_print_stats(&bp);

    printf("\n5. Flush all dirty pages\n");
    bp_flush_all(&bp);
    printf("   All dirty pages flushed.\n");

    printf("\n=== Buffer Pool Demo Complete ===\n");
    return 0;
}
