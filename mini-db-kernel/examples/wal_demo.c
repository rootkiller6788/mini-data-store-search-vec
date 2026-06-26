#include "wal.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("=== WAL Demo ===\n\n");

    WALManager wm;
    wal_init(&wm);

    uint8_t data_a[] = "Account A: $100";
    uint8_t data_b[] = "Account B: $200";
    uint8_t data_a_new[] = "Account A: $50";
    uint8_t data_b_new[] = "Account B: $250";
    uint8_t empty[4096] = {0};

    printf("1. Transaction T1 (committed): Transfer $50 from A to B\n");
    wal_write(&wm, WAL_UPDATE, 1, 0, 1,
              data_a, (int32_t)strlen((char *)data_a),
              data_a_new, (int32_t)strlen((char *)data_a_new));
    wal_write(&wm, WAL_UPDATE, 2, 0, 1,
              data_b, (int32_t)strlen((char *)data_b),
              data_b_new, (int32_t)strlen((char *)data_b_new));
    wal_write(&wm, WAL_COMMIT, -1, 0, 1, NULL, 0, NULL, 0);
    printf("   T1 committed\n");

    printf("\n2. Transaction T2 (uncommitted): Insert new record\n");
    wal_write(&wm, WAL_INSERT, 3, 0, 2, NULL, 0, data_a, (int32_t)strlen((char *)data_a));

    printf("\n3. Transaction T3 (committed): Delete record\n");
    wal_write(&wm, WAL_DELETE, 4, 0, 3, data_b, (int32_t)strlen((char *)data_b), NULL, 0);
    wal_write(&wm, WAL_COMMIT, -1, 0, 3, NULL, 0, NULL, 0);

    printf("\n4. Transaction T4 (uncommitted): Update record\n");
    wal_write(&wm, WAL_UPDATE, 1, 0, 4,
              data_a_new, (int32_t)strlen((char *)data_a_new),
              data_b_new, (int32_t)strlen((char *)data_b_new));

    wal_print(&wm);

    printf("\n5. Flush all records\n");
    wal_flush(&wm, wm.next_lsn - 1);
    printf("   flush_lsn = %lld\n", (long long)wm.flush_lsn);

    printf("\n6. Checkpoint\n");
    wal_checkpoint(&wm);

    printf("\n7. Simulate crash & recovery:\n");
    wal_recover(&wm);

    printf("\n=== WAL Demo Complete ===\n");
    return 0;
}
