/*
 * nosql_hash.c — Consistent Hashing Ring for distributed KV
 *
 * Knowledge layers covered:
 *   L1: HashRing/HashNode/HashVNode struct, API declaration
 *   L2: Consistent hashing — key distribution with minimal remapping
 *   L3: Virtual node ring + sorted ring with binary search
 *   L4: Karger's theorem — O(K/N) remapping on node change
 *   L5: MurmurHash3 32-bit, binary search, ring construction
 *   L7: Dynamo quorum (R+W>N), hinted handoff
 */
#include "nosql_hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ================================================================
 * L5: MurmurHash3-inspired 32-bit hash function
 *
 * Properties required for consistent hashing:
 *   - Uniform distribution (avalanche effect)
 *   - Deterministic (same key → same hash)
 *   - Fast computation (no multiplication loops)
 *
 * Algorithm: simplified MurmurHash3 32-bit
 * Reference: Austin Appleby, 2008
 * ================================================================ */

uint32_t hash_ring_hash32(const char *data, size_t len) {
    uint32_t h = 0x811C9DC5u;  /* FNV offset basis */
    uint32_t prime = 0x01000193u;  /* FNV prime */
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= prime;
    }
    /* Finalization mix (MurmurHash3 fmix32) */
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

/* ================================================================
 * L3: Ring Construction — virtual nodes for load balance
 *
 * Each physical node gets HASH_RING_VNODES virtual positions
 * on the ring. Virtual nodes solve the "hot spot" problem:
 * without them, key distribution variance is O(N).
 * With them, variance is O(1/VNODES).
 * ================================================================ */

static int vnode_compare(const void *a, const void *b) {
    const HashVNode *va = (const HashVNode *)a;
    const HashVNode *vb = (const HashVNode *)b;
    if (va->hash < vb->hash) return -1;
    if (va->hash > vb->hash) return 1;
    return 0;
}

static void ring_rebuild(HashRing *ring) {
    int idx = 0;
    for (int n = 0; n < ring->node_count; n++) {
        if (!ring->nodes[n].is_active) continue;
        for (int v = 0; v < HASH_RING_VNODES && idx < HASH_RING_TOTAL_SLOTS; v++) {
            char buf[128];
            int len = snprintf(buf, sizeof(buf), "%s:vn%d",
                               ring->nodes[n].name, v);
            ring->ring[idx].hash = hash_ring_hash32(buf, (size_t)len);
            ring->ring[idx].node_idx = n;
            idx++;
        }
    }
    ring->ring_size = idx;
    qsort(ring->ring, (size_t)ring->ring_size, sizeof(HashVNode), vnode_compare);
}

HashRing *hash_ring_create(int replication_factor) {
    HashRing *ring = (HashRing *)calloc(1, sizeof(HashRing));
    if (!ring) return NULL;
    ring->replication_factor = (replication_factor > 0) ? replication_factor : 3;
    return ring;
}

void hash_ring_destroy(HashRing *ring) {
    free(ring);
}

int hash_ring_add_node(HashRing *ring, const char *node_name) {
    if (!ring || !node_name || ring->node_count >= HASH_RING_MAX_NODES)
        return -1;

    /* Check duplicate */
    for (int i = 0; i < ring->node_count; i++) {
        if (strcmp(ring->nodes[i].name, node_name) == 0) return 0;
    }

    HashNode *n = &ring->nodes[ring->node_count];
    strncpy(n->name, node_name, sizeof(n->name) - 1);
    n->name[sizeof(n->name) - 1] = '\0';
    n->is_active = 1;
    n->key_count = 0;
    n->hash = hash_ring_hash32(node_name, strlen(node_name));
    ring->node_count++;
    ring_rebuild(ring);
    return ring->node_count;
}

int hash_ring_remove_node(HashRing *ring, const char *node_name) {
    if (!ring || !node_name) return -1;

    int found = -1;
    for (int i = 0; i < ring->node_count; i++) {
        if (strcmp(ring->nodes[i].name, node_name) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) return -2;

    /* Compact array: move last into removed position */
    if (found < ring->node_count - 1) {
        ring->nodes[found] = ring->nodes[ring->node_count - 1];
    }
    memset(&ring->nodes[ring->node_count - 1], 0, sizeof(HashNode));
    ring->node_count--;
    ring_rebuild(ring);
    return ring->node_count;
}

int hash_ring_set_node_active(HashRing *ring, const char *node_name, int active) {
    if (!ring || !node_name) return -1;
    for (int i = 0; i < ring->node_count; i++) {
        if (strcmp(ring->nodes[i].name, node_name) == 0) {
            ring->nodes[i].is_active = active;
            ring_rebuild(ring);
            return 0;
        }
    }
    return -2;
}

/* ================================================================
 * L5: Key→Node mapping via binary search on sorted ring
 *
 * Algorithm: Find the first virtual node whose hash >= key_hash.
 * If key_hash > all ring hashes, wrap around to ring[0].
 * This is the "clockwise walk" property of consistent hashing.
 *
 * Complexity: O(log V) where V = total virtual nodes
 * ================================================================ */

int hash_ring_locate(HashRing *ring, const char *key) {
    if (!ring || !key || ring->ring_size == 0) return -1;

    uint32_t kh = hash_ring_hash32(key, strlen(key));

    /* Binary search for first vnode with hash >= kh */
    int lo = 0, hi = ring->ring_size - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (ring->ring[mid].hash < kh) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    /* lo points to first element >= kh, or ring_size if none */
    int slot = (lo < ring->ring_size) ? lo : 0;
    return ring->ring[slot].node_idx;
}

/*
 * L7: Replica selection — walk clockwise to find N distinct nodes
 *
 * For replication factor N, we walk the ring clockwise collecting
 * the first N distinct physical nodes (skipping duplicates from
 * virtual nodes mapping to the same physical node).
 */
int hash_ring_get_replicas(HashRing *ring, const char *key,
                            int replica_indices[], int max_replicas) {
    if (!ring || !key || !replica_indices || ring->ring_size == 0) return 0;

    uint32_t kh = hash_ring_hash32(key, strlen(key));
    int seen[HASH_RING_MAX_NODES] = {0};
    int found = 0;

    /* Find start position */
    int lo = 0, hi = ring->ring_size - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (ring->ring[mid].hash < kh) lo = mid + 1;
        else hi = mid - 1;
    }
    int start = (lo < ring->ring_size) ? lo : 0;

    for (int i = 0; i < ring->ring_size && found < max_replicas; i++) {
        int slot = (start + i) % ring->ring_size;
        int nidx = ring->ring[slot].node_idx;
        if (!seen[nidx]) {
            seen[nidx] = 1;
            replica_indices[found++] = nidx;
        }
    }
    return found;
}

/* ================================================================
 * L7: Dynamo-style Quorum Read/Write
 *
 * Quorum condition (Dynamo): R + W > N
 *   N = replication_factor (total replicas per key)
 *   R = read_quorum (minimum successful reads for consistency)
 *   W = write_quorum (minimum acks for write success)
 *
 * Default: N=3, R=2, W=2  (R+W=4 > 3, strong consistency)
 *          N=3, R=1, W=3  (fast reads, strong writes)
 *          N=3, R=3, W=1  (strong reads, fast writes)
 * ================================================================ */

int hash_ring_quorum_write(HashRing *ring, const char *key,
                            const char *value, int w_quorum) {
    if (!ring || !key || !value) return -1;
    if (w_quorum <= 0) w_quorum = ring->replication_factor / 2 + 1;

    int replicas[HASH_RING_MAX_NODES];
    int n_replicas = hash_ring_get_replicas(ring, key, replicas,
                                             ring->replication_factor);
    if (n_replicas < w_quorum) return -2;

    int written = 0;
    for (int i = 0; i < n_replicas; i++) {
        if (ring->nodes[replicas[i]].is_active) {
            ring->nodes[replicas[i]].key_count++;
            written++;
        }
    }
    return (written >= w_quorum) ? written : -3;
}

int hash_ring_quorum_read(HashRing *ring, const char *key,
                           char *value_out, size_t max_len, int r_quorum) {
    if (!ring || !key || !value_out) return -1;
    if (r_quorum <= 0) r_quorum = ring->replication_factor / 2 + 1;

    int replicas[HASH_RING_MAX_NODES];
    int n_replicas = hash_ring_get_replicas(ring, key, replicas,
                                             ring->replication_factor);
    if (n_replicas < r_quorum) return -2;

    int read_ok = 0;
    for (int i = 0; i < n_replicas; i++) {
        if (ring->nodes[replicas[i]].is_active) {
            read_ok++;
        }
    }
    if (read_ok < r_quorum) return -3;

    /* Simple: return a marker indicating quorum success */
    snprintf(value_out, max_len, "quorum_read_ok:%d", read_ok);
    return read_ok;
}

/* ================================================================
 * L7: Hinted Handoff — temporary write delegation
 *
 * When a node N is unavailable, a peer temporarily stores N-bound
 * writes. When N recovers, the peer "hands off" those writes back.
 *
 * This is a key Dynamo feature that maintains write availability
 * during transient failures (the "A" in CAP's AP choice).
 * ================================================================ */

HandoffStore *handoff_store_create(void) {
    return (HandoffStore *)calloc(1, sizeof(HandoffStore));
}

void handoff_store_destroy(HandoffStore *store) {
    if (!store) return;
    HintedHandoff *cur = store->head;
    while (cur) {
        HintedHandoff *tmp = cur;
        cur = cur->next;
        free(tmp);
    }
    free(store);
}

int handoff_store_add(HandoffStore *store, const char *target_node,
                       const char *key, const char *value) {
    if (!store || !target_node || !key || !value) return -1;
    HintedHandoff *h = (HintedHandoff *)calloc(1, sizeof(HintedHandoff));
    if (!h) return -1;
    strncpy(h->target_node, target_node, sizeof(h->target_node) - 1);
    strncpy(h->key, key, HASH_MAX_KEY_LEN - 1);
    strncpy(h->value, value, sizeof(h->value) - 1);
    h->timestamp = (uint64_t)time(NULL);
    h->next = store->head;
    store->head = h;
    store->count++;
    return 0;
}

int handoff_store_drain(HandoffStore *store, const char *target_node,
                         char (*keys)[HASH_MAX_KEY_LEN],
                         char (*values)[256], int max_count) {
    if (!store || !target_node || !keys || !values) return 0;

    int drained = 0;
    HintedHandoff *cur = store->head;
    HintedHandoff *prev = NULL;

    while (cur && drained < max_count) {
        if (strcmp(cur->target_node, target_node) == 0) {
            size_t klen = strlen(cur->key);
            if (klen >= HASH_MAX_KEY_LEN) klen = HASH_MAX_KEY_LEN - 1;
            memcpy(keys[drained], cur->key, klen);
            keys[drained][klen] = '\0';
            size_t vlen = strlen(cur->value);
            if (vlen >= 256) vlen = 255;
            memcpy(values[drained], cur->value, vlen);
            values[drained][vlen] = '\0';
            drained++;
            HintedHandoff *tmp = cur;
            if (prev) prev->next = cur->next;
            else store->head = cur->next;
            cur = cur->next;
            free(tmp);
            store->count--;
        } else {
            prev = cur;
            cur = cur->next;
        }
    }
    return drained;
}

/* ================================================================
 * L4: Load balance statistics — verify Karger's theorem empirically
 *
 * Computes standard deviation of key distribution across nodes.
 * Good consistent hashing: stddev ≤ mean * 0.3
 * ================================================================ */

HashRingStats hash_ring_compute_stats(HashRing *ring) {
    HashRingStats stats;
    memset(&stats, 0, sizeof(stats));
    if (!ring) return stats;

    stats.total_nodes = ring->node_count;
    stats.total_slots = ring->ring_size;

    int active = 0;
    int total_keys = 0;
    for (int i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].is_active) {
            active++;
            total_keys += ring->nodes[i].key_count;
        }
    }
    stats.active_nodes = active;
    stats.total_keys = total_keys;

    if (active == 0 || total_keys == 0) return stats;

    double mean = (double)total_keys / active;
    double sum_sq = 0.0;
    for (int i = 0; i < ring->node_count; i++) {
        if (ring->nodes[i].is_active) {
            double diff = ring->nodes[i].key_count - mean;
            sum_sq += diff * diff;
        }
    }
    stats.load_stddev = sqrt(sum_sq / active);
    return stats;
}
