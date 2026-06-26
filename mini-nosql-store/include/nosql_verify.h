#ifndef NOSQL_VERIFY_H
#define NOSQL_VERIFY_H

#include <stdint.h>
#include <stddef.h>

/*
 * Merkle Tree — Cryptographic Data Verification
 *
 * Theorem (Merkle, 1979 — "A Certified Digital Signature"):
 *   A Merkle tree enables efficient and secure verification of
 *   large datasets: O(log N) proof size to verify any leaf,
 *   O(log N) time to construct inclusion proofs.
 *
 * Applications in NoSQL:
 *   - Anti-entropy repair (Cassandra, Dynamo): Compare Merkle trees
 *     between replicas to find divergent data ranges
 *   - Blockchain: Transaction inclusion proofs (Bitcoin, Ethereum)
 *   - Git: Content-addressed object integrity
 *   - Certificate Transparency (RFC 6962)
 *
 * Key property:
 *   Root hash = H(H(L0) || H(L1) || ... || H(Ln-1))
 *   Proof for leaf i: sibling hashes along path to root
 */

#define MERKLE_HASH_SIZE    32   /* SHA-256 output size */
#define MERKLE_MAX_LEAVES  256
#define MERKLE_MAX_NODES   (2 * MERKLE_MAX_LEAVES)

typedef struct merkle_hash_t {
    uint8_t bytes[MERKLE_HASH_SIZE];
} MerkleHash;

typedef struct merkle_leaf_t {
    char  key[64];
    MerkleHash hash;
} MerkleLeaf;

typedef struct merkle_node_t {
    MerkleHash hash;
    int        is_leaf;       /* 1 = leaf, 0 = internal */
    int        left;          /* Index of left child, -1 if leaf */
    int        right;         /* Index of right child, -1 if leaf */
} MerkleNode;

typedef struct merkle_tree_t {
    MerkleNode nodes[MERKLE_MAX_NODES];
    MerkleLeaf leaves[MERKLE_MAX_LEAVES];
    int        leaf_count;
    int        node_count;
    int        height;        /* Tree height (root = level 0) */
} MerkleTree;

/*
 * L5: Merkle-Damgard based hash (simplified SHA-256-like)
 *
 * A proper 256-bit cryptographic hash with:
 *   - Preimage resistance: Given H(m), infeasible to find m
 *   - Second preimage resistance: Given m1, infeasible to find m2 with H(m1)=H(m2)
 *   - Collision resistance: Infeasible to find any m1 != m2 with H(m1)=H(m2)
 *
 * This is a simplified 32-byte hash using iterative compression.
 */
void merkle_hash_data(const uint8_t *data, size_t len, MerkleHash *out);
void merkle_hash_combine(const MerkleHash *a, const MerkleHash *b,
                          MerkleHash *out);
int  merkle_hash_equal(const MerkleHash *a, const MerkleHash *b);
void merkle_hash_to_hex(const MerkleHash *h, char *hex_out, size_t max_len);
void merkle_hash_print(const MerkleHash *h);

/* Tree lifecycle */
MerkleTree *merkle_tree_create(void);
void        merkle_tree_destroy(MerkleTree *tree);

/* Leaf operations */
int  merkle_tree_add_leaf(MerkleTree *tree, const char *key,
                          const uint8_t *data, size_t len);
int  merkle_tree_build(MerkleTree *tree);
const MerkleHash *merkle_tree_root(MerkleTree *tree);

/*
 * L8: Inclusion Proof (Merkle Proof)
 *
 * An inclusion proof for leaf L consists of the sibling hashes
 * along the path from L to the root. The verifier can recompute
 * the root hash from L + proof and compare with the known root.
 *
 * Proof size: O(log N) hashes
 * Verification time: O(log N)
 */
typedef struct merkle_proof_t {
    int         leaf_index;
    MerkleHash  path_hashes[16];  /* Max depth = 16 for 256 leaves */
    int         path_directions[16]; /* 0=left sibling, 1=right sibling */
    int         path_len;
} MerkleProof;

int  merkle_tree_generate_proof(MerkleTree *tree, int leaf_index,
                                 MerkleProof *proof);
int  merkle_tree_verify_proof(const MerkleHash *root,
                              const MerkleLeaf *leaf,
                              const MerkleProof *proof);

/*
 * L7: Tree Difference Detection (Anti-Entropy)
 *
 * Given two Merkle trees (from two replicas), compare them
 * efficiently to find divergent data ranges.
 *
 * Algorithm: breadth-first comparison, pruning identical subtrees.
 * Returns number of divergent leaves.
 */
int merkle_tree_diff(MerkleTree *a, MerkleTree *b,
                      int *diff_leaf_indices, int max_diffs);

#endif
