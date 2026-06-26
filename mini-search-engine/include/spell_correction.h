#ifndef SPELL_CORRECTION_H
#define SPELL_CORRECTION_H

#include <stdint.h>

/*
 * spell_correction.h — Spelling Correction and Fuzzy Search
 *
 * Reference: Damerau. "A technique for computer detection and correction
 *            of spelling errors" (CACM 1964)
 *            Levenshtein. "Binary codes capable of correcting deletions,
 *            insertions, and reversals" (1966)
 *            Jurafsky & Martin. "Speech and Language Processing" (3rd ed.), Ch. 2
 *
 * Knowledge Coverage:
 *   L5: Levenshtein distance (DP), Damerau-Levenshtein, Soundex
 *   L5: N-gram Jaccard similarity for fuzzy matching
 *   L4: Edit distance metric properties (non-negative, symmetric, triangle inequality)
 */

#define SPELL_MAX_WORD_LEN  64
#define SPELL_MAX_CANDIDATES 32

typedef struct {
    char    word[SPELL_MAX_WORD_LEN];
    int32_t distance;
} SpellCandidate;

/* ===== L5: Edit Distance Algorithms ===== */

/* Levenshtein edit distance (full DP matrix):
 *   d[i][j] = min(d[i-1][j]+1, d[i][j-1]+1, d[i-1][j-1]+cost)
 *   where cost = 0 if a[i]==b[j], else 1.
 * Time: O(m*n), Space: O(min(m,n)) with two-row optimization.
 * Theorem (L4): Levenshtein distance is a metric satisfying
 * non-negativity, identity of indiscernibles, symmetry, and
 * triangle inequality. */
int32_t levenshtein_distance(const char *s1, const char *s2);

/* Damerau-Levenshtein distance: extends Levenshtein by adding
 * transposition of two adjacent characters as a single operation.
 *   d[i][j] = min( d[i-1][j]+1, d[i][j-1]+1,
 *                  d[i-1][j-1]+cost,
 *                  d[i-2][j-2]+1 if s1[i-1]==s2[j-2] && s1[i-2]==s2[j-1] )
 * Reference: Damerau (1964), refined by Lowrance & Wagner (1975) */
int32_t damerau_levenshtein(const char *s1, const char *s2);

/* ===== L5: Phonetic Encoding ===== */

/* Soundex: maps a word to a 4-character code based on pronunciation.
 * Algorithm:
 *   1. Retain first letter (capitalized)
 *   2. Replace consonants with digits: BFPV→1, CGJKQSXZ→2, DT→3, L→4, MN→5, R→6
 *   3. Remove consecutive duplicates
 *   4. Remove vowels (A,E,I,O,U,Y,H,W) unless first letter
 *   5. Pad/truncate to 4 characters (letter + 3 digits)
 * Reference: US Census Bureau Soundex (1918) */
void soundex_encode(const char *word, char *code);

/* ===== L5: Fuzzy String Matching ===== */

/* N-gram Jaccard similarity: |S_ngram(a) ∩ S_ngram(b)| / |S_ngram(a) ∪ S_ngram(b)|
 * where S_ngram(s) = set of all n-grams in s (with padding).
 * Commonly n=2 (bigram) or n=3 (trigram) for spell correction.
 * Theorem (L4): Jaccard is a proper similarity metric on sets. */
double ngram_jaccard_similarity(const char *s1, const char *s2, int32_t n);

/* Find closest dictionary words by edit distance.
 * Returns number of candidates found (up to max_candidates). */
int32_t spell_correct(const char *word, const char **dictionary,
                      int32_t dict_size, SpellCandidate *candidates,
                      int32_t max_candidates, int32_t max_distance);

/* Generate candidate variants within edit distance 1:
 * deletions, insertions, substitutions, transpositions.
 * Stores up to max_variants via the callback; returns number stored. */
typedef void (*spell_variant_callback)(const char *variant, void *ctx);
int32_t spell_generate_variants(const char *word,
                                spell_variant_callback cb, void *ctx);

#endif
