#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "nosql_hash.h"

int main(void) {
    /* Hash function determinism */
    uint32_t h1 = hash_ring_hash32("hello", 5);
    uint32_t h2 = hash_ring_hash32("hello", 5);
    assert(h1 == h2);

    /* Ring creation */
    HashRing *ring = hash_ring_create(3);
    assert(ring != NULL);

    /* Add nodes */
    assert(hash_ring_add_node(ring, "node-a") >= 1);
    assert(hash_ring_add_node(ring, "node-b") >= 2);
    assert(hash_ring_add_node(ring, "node-c") >= 3);
    assert(ring->node_count == 3);

    /* Locate keys */
    int nidx = hash_ring_locate(ring, "my-key-1");
    assert(nidx >= 0 && nidx < 3);

    int nidx2 = hash_ring_locate(ring, "my-key-1");
    assert(nidx == nidx2);  /* Deterministic */

    /* Get replicas */
    int replicas[8];
    int nr = hash_ring_get_replicas(ring, "my-key-1", replicas, 3);
    assert(nr >= 1);

    /* Quorum write/read */
    assert(hash_ring_quorum_write(ring, "qkey", "qval", 2) >= 2);
    char buf[256];
    assert(hash_ring_quorum_read(ring, "qkey", buf, sizeof(buf), 2) >= 2);

    /* Remove node */
    assert(hash_ring_remove_node(ring, "node-b") == 2);

    /* Statistics */
    HashRingStats st = hash_ring_compute_stats(ring);
    assert(st.active_nodes >= 2);

    /* Hinted handoff */
    HandoffStore *hs = handoff_store_create();
    assert(hs != NULL);
    assert(handoff_store_add(hs, "node-b", "hk1", "hv1") == 0);
    assert(handoff_store_add(hs, "node-b", "hk2", "hv2") == 0);
    char keys[16][HASH_MAX_KEY_LEN];
    char values[16][256];
    int drained = handoff_store_drain(hs, "node-b", keys, values, 16);
    assert(drained == 2);

    handoff_store_destroy(hs);
    hash_ring_destroy(ring);

    printf("test_hash: PASSED\n");
    return 0;
}
