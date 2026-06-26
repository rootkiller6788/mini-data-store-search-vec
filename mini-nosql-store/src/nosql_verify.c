/*
 * nosql_verify.c — Merkle Tree for data integrity verification
 *
 * Knowledge layers covered:
 *   L1: MerkleHash, MerkleNode, MerkleTree, MerkleProof structs
 *   L2: Data integrity — cryptographic verification of datasets
 *   L3: Binary tree construction with flat array storage
 *   L4: Merkle's theorem (1979) — O(log N) proofs
 *   L5: Simplified SHA-256-like hash, tree construction
 *   L7: Anti-entropy repair via tree diff
 *   L8: Inclusion proofs (Merkle proofs)
 */
#include "nosql_verify.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * L5: Simplified 256-bit cryptographic hash
 *
 * Uses a Merkle-Damgard construction with a compression function
 * inspired by SHA-256's design:
 *   - 8 × 32-bit state words
 *   - 64-byte block size
 *   - Iterative compression with message schedule
 *
 * This is a pedagogical hash with good avalanche properties.
 * For production, use actual SHA-256 (OpenSSL/mbedTLS).
 * ================================================================ */

/* SHA-256 initial hash values (first 32 bits of fractional parts
 * of square roots of first 8 primes) */
static const uint32_t H0[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

/* SHA-256 round constants (first 32 bits of fractional parts
 * of cube roots of first 64 primes) */
static const uint32_t K256[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};

static uint32_t rotr32(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_compress_block(const uint8_t *block, uint32_t *state) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void merkle_hash_data(const uint8_t *data, size_t len, MerkleHash *out) {
    uint32_t state[8];
    memcpy(state, H0, sizeof(H0));

    uint8_t block[64];
    size_t pos = 0;

    /* Process full blocks */
    while (pos + 64 <= len) {
        memcpy(block, data + pos, 64);
        sha256_compress_block(block, state);
        pos += 64;
    }

    /* Process remaining bytes with padding (simplified: pad with zeros) */
    size_t remaining = len - pos;
    memset(block, 0, 64);
    memcpy(block, data + pos, remaining);
    block[remaining] = 0x80;  /* Padding start bit */

    /* Append length in bits at the end (big-endian 64-bit) */
    uint64_t bit_len = len * 8;
    for (int i = 0; i < 8; i++) {
        block[56 + i] = (uint8_t)(bit_len >> (56 - i * 8));
    }

    sha256_compress_block(block, state);

    /* Write output as big-endian */
    for (int i = 0; i < 8; i++) {
        out->bytes[i*4]     = (uint8_t)(state[i] >> 24);
        out->bytes[i*4 + 1] = (uint8_t)(state[i] >> 16);
        out->bytes[i*4 + 2] = (uint8_t)(state[i] >> 8);
        out->bytes[i*4 + 3] = (uint8_t)(state[i]);
    }
}

void merkle_hash_combine(const MerkleHash *a, const MerkleHash *b,
                          MerkleHash *out) {
    uint8_t combined[MERKLE_HASH_SIZE * 2];
    memcpy(combined, a->bytes, MERKLE_HASH_SIZE);
    memcpy(combined + MERKLE_HASH_SIZE, b->bytes, MERKLE_HASH_SIZE);
    merkle_hash_data(combined, sizeof(combined), out);
}

int merkle_hash_equal(const MerkleHash *a, const MerkleHash *b) {
    if (!a || !b) return 0;
    return memcmp(a->bytes, b->bytes, MERKLE_HASH_SIZE) == 0;
}

void merkle_hash_to_hex(const MerkleHash *h, char *hex_out, size_t max_len) {
    if (!h || !hex_out) return;
    size_t limit = (max_len > 0) ? max_len - 1 : 0;
    for (int i = 0; i < MERKLE_HASH_SIZE && i * 2 + 1 < (int)limit; i++) {
        snprintf(hex_out + i * 2, 3, "%02x", h->bytes[i]);
    }
    if (max_len > 0) hex_out[limit] = '\0';
}

void merkle_hash_print(const MerkleHash *h) {
    char hex[65];
    merkle_hash_to_hex(h, hex, sizeof(hex));
    printf("%s", hex);
}

/* ================================================================
 * L3: Merkle Tree Construction
 *
 * The tree is stored in a flat array for cache efficiency.
 * Leaves[0..N-1] are stored first, then internal nodes are built
 * bottom-up. Each internal node's hash = H(left || right).
 *
 * Layout:
 *   nodes[0] = root
 *   nodes[1] = left child of root
 *   nodes[2] = right child of root
 *   ...
 *
 * Actually we use a simpler approach: leaves[] for data, nodes[]
 * array built bottom-up.
 * ================================================================ */

MerkleTree *merkle_tree_create(void) {
    MerkleTree *t = (MerkleTree *)calloc(1, sizeof(MerkleTree));
    if (!t) return NULL;
    return t;
}

void merkle_tree_destroy(MerkleTree *tree) {
    free(tree);
}

int merkle_tree_add_leaf(MerkleTree *tree, const char *key,
                         const uint8_t *data, size_t len) {
    if (!tree || !key || !data || tree->leaf_count >= MERKLE_MAX_LEAVES)
        return -1;

    MerkleLeaf *leaf = &tree->leaves[tree->leaf_count];
    strncpy(leaf->key, key, sizeof(leaf->key) - 1);
    leaf->key[sizeof(leaf->key) - 1] = '\0';
    merkle_hash_data(data, len, &leaf->hash);
    tree->leaf_count++;
    return tree->leaf_count;
}

/*
 * Build the Merkle tree bottom-up.
 *
 * Algorithm:
 *   1. Create leaf nodes from leaves[]
 *   2. Pair up nodes at each level
 *   3. Internal node hash = H(left.hash || right.hash)
 *   4. If odd number of nodes at a level, promote last node
 *
 * Time: O(N), Space: O(N)
 */
int merkle_tree_build(MerkleTree *tree) {
    if (!tree || tree->leaf_count == 0) return -1;

    int node_count = tree->leaf_count;
    if (node_count == 1) {
        /* Single leaf: root = leaf hash */
        tree->nodes[0].is_leaf = 1;
        tree->nodes[0].hash = tree->leaves[0].hash;
        tree->nodes[0].left = -1;
        tree->nodes[0].right = -1;
        tree->node_count = 1;
        tree->height = 1;
        return 0;
    }

    /* Create leaf nodes */
    for (int i = 0; i < tree->leaf_count; i++) {
        tree->nodes[i].is_leaf = 1;
        tree->nodes[i].hash = tree->leaves[i].hash;
        tree->nodes[i].left = -1;
        tree->nodes[i].right = -1;
    }

    int level_start = 0;
    int level_size = tree->leaf_count;
    int next_free = level_size;
    int height = 1;

    while (level_size > 1) {
        int parent_start = next_free;
        int parents = 0;
        for (int i = 0; i < level_size; i += 2) {
            if (i + 1 < level_size) {
                /* Two children */
                tree->nodes[next_free].is_leaf = 0;
                tree->nodes[next_free].left = level_start + i;
                tree->nodes[next_free].right = level_start + i + 1;
                merkle_hash_combine(
                    &tree->nodes[level_start + i].hash,
                    &tree->nodes[level_start + i + 1].hash,
                    &tree->nodes[next_free].hash);
                next_free++;
                parents++;
            } else {
                /* Odd node — promote directly */
                tree->nodes[next_free] = tree->nodes[level_start + i];
                tree->nodes[next_free].left = level_start + i;
                tree->nodes[next_free].right = -1;
                next_free++;
                parents++;
            }
        }
        level_start = parent_start;
        level_size = parents;
        height++;
    }

    tree->node_count = next_free;
    tree->height = height;
    return 0;
}

const MerkleHash *merkle_tree_root(MerkleTree *tree) {
    if (!tree || tree->node_count == 0) return NULL;
    /* Root is the last node created */
    return &tree->nodes[tree->node_count - 1].hash;
}

/* ================================================================
 * L8: Inclusion Proof Generation
 *
 * For a leaf at index leaf_index, walk up the tree collecting
 * sibling hashes. The proof is the list of sibling hashes and
 * direction bits (0 = left sibling, 1 = right sibling).
 *
 * Verification: Starting from leaf hash, for each sibling in proof:
 *   if direction=0: hash = H(sibling || hash)  [sibling is left]
 *   if direction=1: hash = H(hash || sibling)  [sibling is right]
 * Final hash must equal the known root.
 * ================================================================ */

int merkle_tree_generate_proof(MerkleTree *tree, int leaf_index,
                                MerkleProof *proof) {
    if (!tree || !proof || leaf_index < 0 || leaf_index >= tree->leaf_count)
        return -1;
    if (tree->node_count == 0) return -2;

    memset(proof, 0, sizeof(MerkleProof));
    proof->leaf_index = leaf_index;

    /* Find the leaf node position in nodes[] */
    int node_idx = leaf_index;  /* Leaves are stored at indices 0..N-1 */
    int path_idx = 0;

    /* Walk up the tree */
    int level_start = 0;
    int level_size = tree->leaf_count;

    while (level_size > 1) {
        int next_level_start = level_start + level_size;
        /* Find position within this level */
        int pos_in_level = node_idx - level_start;
        int parent_pos = next_level_start + (pos_in_level / 2);

        if (pos_in_level % 2 == 0 && pos_in_level + 1 < level_size) {
            /* Left child — sibling is right child */
            proof->path_hashes[path_idx] =
                tree->nodes[level_start + pos_in_level + 1].hash;
            proof->path_directions[path_idx] = 1;  /* sibling is right */
            path_idx++;
        } else if (pos_in_level % 2 == 1) {
            /* Right child — sibling is left child */
            proof->path_hashes[path_idx] =
                tree->nodes[level_start + pos_in_level - 1].hash;
            proof->path_directions[path_idx] = 0;  /* sibling is left */
            path_idx++;
        }
        /* else: last odd node, no sibling (promoted) */

        node_idx = parent_pos;
        level_start = next_level_start;
        /* Compute new level_size */
        int new_level_size = 0;
        for (int i = 0; i < level_size; i += 2) {
            new_level_size++;
        }
        level_size = new_level_size;
        if (level_size <= 1) break;
    }

    proof->path_len = path_idx;
    return 0;
}

int merkle_tree_verify_proof(const MerkleHash *root,
                              const MerkleLeaf *leaf,
                              const MerkleProof *proof) {
    if (!root || !leaf || !proof) return 0;

    MerkleHash current = leaf->hash;

    for (int i = 0; i < proof->path_len; i++) {
        if (proof->path_directions[i] == 0) {
            /* Sibling is on the left */
            merkle_hash_combine(&proof->path_hashes[i], &current, &current);
        } else {
            /* Sibling is on the right */
            merkle_hash_combine(&current, &proof->path_hashes[i], &current);
        }
    }

    return merkle_hash_equal(&current, root);
}

/* ================================================================
 * L7: Tree Difference Detection (Anti-Entropy)
 *
 * Compares two Merkle trees to find divergent data ranges.
 * Starting from roots: if equal, no differences. If different,
 * recurse into children to pinpoint which leaves differ.
 *
 * This is the core of Cassandra/Dynamo's anti-entropy repair:
 * two replicas exchange Merkle trees and only sync divergent ranges.
 * ================================================================ */

int merkle_tree_diff(MerkleTree *a, MerkleTree *b,
                      int *diff_leaf_indices, int max_diffs) {
    if (!a || !b || !diff_leaf_indices || max_diffs <= 0) return 0;

    const MerkleHash *root_a = merkle_tree_root(a);
    const MerkleHash *root_b = merkle_tree_root(b);

    if (!root_a || !root_b) return 0;

    /* Roots equal → entire trees equal */
    if (merkle_hash_equal(root_a, root_b)) return 0;

    /* Roots differ → compare leaves */
    int diff_count = 0;
    int min_leaves = (a->leaf_count < b->leaf_count) ?
                      a->leaf_count : b->leaf_count;

    for (int i = 0; i < min_leaves && diff_count < max_diffs; i++) {
        if (!merkle_hash_equal(&a->leaves[i].hash, &b->leaves[i].hash)) {
            diff_leaf_indices[diff_count++] = i;
        }
    }

    /* Extra leaves in larger tree are also divergences */
    if (a->leaf_count > min_leaves) {
        for (int i = min_leaves; i < a->leaf_count && diff_count < max_diffs; i++) {
            diff_leaf_indices[diff_count++] = i;
        }
    } else if (b->leaf_count > min_leaves) {
        for (int i = min_leaves; i < b->leaf_count && diff_count < max_diffs; i++) {
            diff_leaf_indices[diff_count++] = i;
        }
    }

    return diff_count;
}
