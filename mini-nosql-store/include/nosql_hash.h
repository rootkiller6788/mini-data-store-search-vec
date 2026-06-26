#ifndef NOSQL_HASH_H
#define NOSQL_HASH_H

#include <stdint.h>
#include <stddef.h>

/*
 * Consistent Hashing Ring — Distributed KV placement (Dynamo-style)
 *
 * Theorem (Karger et al., STOC 1997):
 *   Consistent hashing distributes keys across N nodes such that
 *   adding/removing a node remaps only K/N keys (minimal disruption).
 *
 * Reference: Amazon Dynamo Paper (DeCandia et al., SOSP 2007)
 *
 * Key properties:
 *   - Balance: Keys are evenly distributed (with virtual nodes)
 *   - Monotonicity: Adding nodes only shifts keys TO the new node
 *   - Spread: Each key maps to exactly one primary node
 *   - Load: Each node handles roughly 1/N of keys
 *
 * Quorum (Dynamo): R + W > N ensures strong consistency
 *   N = replication factor
 *   R = read quorum (minimum nodes to read)
 *   W = write quorum (minimum nodes to ack write)
 */

#define HASH_RING_MAX_NODES    64
#define HASH_RING_VNODES      128   /* Virtual nodes per physical node */
#define HASH_RING_TOTAL_SLOTS (HASH_RING_MAX_NODES * HASH_RING_VNODES)
#define HASH_MAX_KEY_LEN       64

typedef struct hash_node_t {
    char     name[64];
    uint32_t hash;
    int      is_active;
    int      key_count;
} HashNode;

typedef struct hash_vnode_t {
    uint32_t hash;
    int      node_idx;        /* Index into nodes[] */
} HashVNode;

typedef struct hash_ring_t {
    HashNode   nodes[HASH_RING_MAX_NODES];
    int        node_count;
    HashVNode  ring[HASH_RING_TOTAL_SLOTS];
    int        ring_size;
    int        replication_factor;  /* N in Dynamo quorum */
} HashRing;

typedef struct hash_ring_stats_t {
    int    total_nodes;
    int    active_nodes;
    int    total_slots;
    int    total_keys;
    double load_stddev;       /* Standard deviation of key distribution */
} HashRingStats;

/*
 * L5: MurmurHash3-style 32-bit hash (simplified)
 *
 * Used for both node placement and key-to-node mapping.
 * Good avalanche effect for uniform distribution.
 */
uint32_t hash_ring_hash32(const char *data, size_t len);

/* Ring lifecycle */
HashRing *hash_ring_create(int replication_factor);
void      hash_ring_destroy(HashRing *ring);

/* Node management */
int  hash_ring_add_node(HashRing *ring, const char *node_name);
int  hash_ring_remove_node(HashRing *ring, const char *node_name);
int  hash_ring_set_node_active(HashRing *ring, const char *node_name, int active);

/*
 * Key→Node mapping (L5: Ring Lookup)
 *
 * Binary search on sorted ring to find the first virtual node
 * whose hash >= key_hash (clockwise walk).
 */
int  hash_ring_locate(HashRing *ring, const char *key);
int  hash_ring_get_replicas(HashRing *ring, const char *key,
                             int replica_indices[], int max_replicas);

/*
 * L7: Dynamo-style Quorum Writes/Reads
 *
 * Writes to W replicas, reads from R replicas.
 * R + W > N ensures read-repair can resolve conflicts.
 */
int  hash_ring_quorum_write(HashRing *ring, const char *key,
                             const char *value, int w_quorum);
int  hash_ring_quorum_read(HashRing *ring, const char *key,
                            char *value_out, size_t max_len, int r_quorum);

/*
 * L7: Hinted Handoff
 *
 * When a node is down, a peer temporarily holds its writes
 * and forwards them when the node recovers.
 */
typedef struct hinted_handoff_t {
    char     target_node[64];
    char     key[HASH_MAX_KEY_LEN];
    char     value[256];
    uint64_t timestamp;
    struct hinted_handoff_t *next;
} HintedHandoff;

typedef struct handoff_store_t {
    HintedHandoff *head;
    int            count;
} HandoffStore;

HandoffStore *handoff_store_create(void);
void          handoff_store_destroy(HandoffStore *store);
int           handoff_store_add(HandoffStore *store, const char *target_node,
                                const char *key, const char *value);
int           handoff_store_drain(HandoffStore *store, const char *target_node,
                                  char (*keys)[HASH_MAX_KEY_LEN],
                                  char (*values)[256], int max_count);

/* Ring statistics for load balancing analysis */
HashRingStats hash_ring_compute_stats(HashRing *ring);

#endif
