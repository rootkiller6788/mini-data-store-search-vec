#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nosql_hash.h"

int main(void) {
    printf("=== Consistent Hashing Ring Demo ===\n\n");

    HashRing *ring = hash_ring_create(3);
    if (!ring) { printf("Failed to create ring\n"); return 1; }

    /* Build a 5-node cluster */
    printf("Adding 5 nodes to the ring...\n");
    hash_ring_add_node(ring, "dc1-node1");
    hash_ring_add_node(ring, "dc1-node2");
    hash_ring_add_node(ring, "dc2-node1");
    hash_ring_add_node(ring, "dc2-node2");
    hash_ring_add_node(ring, "dc3-node1");

    printf("Ring: %d nodes, %d virtual slots\n\n",
           ring->node_count, ring->ring_size);

    /* Demonstrate key distribution */
    printf("Key → Node mapping (first 10 keys):\n");
    const char *keys[] = {"user:1001", "user:1002", "user:1003",
                          "session:a", "session:b", "session:c",
                          "product:42", "product:43", "product:44",
                          "order:999"};
    for (int i = 0; i < 10; i++) {
        int nidx = hash_ring_locate(ring, keys[i]);
        printf("  %-20s → %s\n", keys[i], ring->nodes[nidx].name);
    }

    /* Demonstrate replica placement */
    printf("\nReplica placement for 'critical-data':\n");
    int replicas[8];
    int nr = hash_ring_get_replicas(ring, "critical-data", replicas, 3);
    for (int i = 0; i < nr; i++) {
        printf("  Replica %d: %s\n", i, ring->nodes[replicas[i]].name);
    }

    /* Quorum write/read */
    printf("\nDynamo-style quorum (N=3, W=2, R=2):\n");
    int wr = hash_ring_quorum_write(ring, "quorum-key", "quorum-val", 2);
    printf("  Write quorum: %d replicas acked\n", wr);

    char buf[256];
    int rr = hash_ring_quorum_read(ring, "quorum-key", buf, sizeof(buf), 2);
    printf("  Read quorum: %d replicas responded\n", rr);

    /* Simulate node failure with hinted handoff */
    printf("\nSimulating node failure + hinted handoff:\n");
    hash_ring_set_node_active(ring, "dc2-node1", 0);

    HandoffStore *hs = handoff_store_create();
    handoff_store_add(hs, "dc2-node1", "hk1", "handoff_value_1");
    handoff_store_add(hs, "dc2-node1", "hk2", "handoff_value_2");
    printf("  Stored 2 handoff entries for dc2-node1\n");

    /* Node recovers */
    hash_ring_set_node_active(ring, "dc2-node1", 1);
    char keys_out[16][HASH_MAX_KEY_LEN];
    char vals_out[16][256];
    int drained = handoff_store_drain(hs, "dc2-node1", keys_out, vals_out, 16);
    printf("  Drained %d handoff entries back to dc2-node1\n", drained);
    handoff_store_destroy(hs);

    /* Load balance statistics */
    HashRingStats st = hash_ring_compute_stats(ring);
    printf("\nLoad balance: %d active nodes, stddev=%.2f\n",
           st.active_nodes, st.load_stddev);

    hash_ring_destroy(ring);
    printf("\nHash ring demo complete.\n");
    return 0;
}
