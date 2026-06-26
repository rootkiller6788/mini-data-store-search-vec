#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "nosql_verify.h"

int main(void) {
    /* Hash function */
    const char *msg = "The quick brown fox jumps over the lazy dog";
    MerkleHash h1, h2;
    merkle_hash_data((const uint8_t *)msg, strlen(msg), &h1);
    merkle_hash_data((const uint8_t *)msg, strlen(msg), &h2);
    assert(merkle_hash_equal(&h1, &h2));

    /* Combined hash */
    MerkleHash combined;
    merkle_hash_combine(&h1, &h2, &combined);
    assert(!merkle_hash_equal(&h1, &combined));

    /* Different messages → different hashes */
    MerkleHash h3;
    merkle_hash_data((const uint8_t *)"different", 9, &h3);
    assert(!merkle_hash_equal(&h1, &h3));

    /* Tree construction */
    MerkleTree *tree = merkle_tree_create();
    assert(tree != NULL);

    assert(merkle_tree_add_leaf(tree, "d1", (const uint8_t *)"data1", 5) == 1);
    assert(merkle_tree_add_leaf(tree, "d2", (const uint8_t *)"data2", 5) == 2);
    assert(merkle_tree_add_leaf(tree, "d3", (const uint8_t *)"data3", 5) == 3);
    assert(merkle_tree_add_leaf(tree, "d4", (const uint8_t *)"data4", 5) == 4);

    assert(merkle_tree_build(tree) == 0);

    const MerkleHash *root = merkle_tree_root(tree);
    assert(root != NULL);

    /* Generate and verify proof */
    MerkleProof proof;
    assert(merkle_tree_generate_proof(tree, 0, &proof) == 0);
    assert(proof.path_len > 0);
    assert(merkle_tree_verify_proof(root, &tree->leaves[0], &proof) == 1);

    /* Tampered proof fails */
    MerkleHash fake_hash;
    merkle_hash_data((const uint8_t *)"fake", 4, &fake_hash);
    proof.path_hashes[0] = fake_hash;
    assert(merkle_tree_verify_proof(root, &tree->leaves[0], &proof) == 0);

    /* Tree diff */
    MerkleTree *tree2 = merkle_tree_create();
    merkle_tree_add_leaf(tree2, "d1", (const uint8_t *)"data1", 5);
    merkle_tree_add_leaf(tree2, "d2", (const uint8_t *)"DIFF", 4);
    merkle_tree_add_leaf(tree2, "d3", (const uint8_t *)"data3", 5);
    merkle_tree_add_leaf(tree2, "d4", (const uint8_t *)"data4", 5);
    merkle_tree_build(tree2);

    int diffs[16];
    int nd = merkle_tree_diff(tree, tree2, diffs, 16);
    assert(nd >= 1);
    assert(diffs[0] == 1);  /* d2 differs */

    merkle_tree_destroy(tree);
    merkle_tree_destroy(tree2);

    printf("test_verify: PASSED\n");
    return 0;
}
