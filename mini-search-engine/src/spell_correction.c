#include "spell_correction.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/*
 * spell_correction.c -- Spelling Correction and Fuzzy Search
 *
 * Reference: Levenshtein (1966), Damerau (1964)
 *            Jurafsky & Martin "Speech and Language Processing" (3e)
 *            Norvig "How to Write a Spelling Corrector" (2007)
 *
 * Knowledge Coverage:
 *   L5: Levenshtein distance (DP), Damerau-Levenshtein, Soundex
 *   L5: N-gram Jaccard similarity, spell correction pipeline
 *   L4: Edit distance metric properties
 */

static int32_t min3(int32_t a, int32_t b, int32_t c) {
    int32_t m = (a < b) ? a : b;
    return (m < c) ? m : c;
}

/* ===== L5: Levenshtein Edit Distance =====
 *
 * d[i][j] = min( d[i-1][j]+1, d[i][j-1]+1, d[i-1][j-1]+cost )
 *   cost = 0 if s1[i-1]==s2[j-1], else 1.
 *
 * Space-optimized with two rows: O(min(m,n)).
 * Time: O(m*n).
 *
 * L4 Theorem: Levenshtein distance is a proper metric:
 *   non-negativity, identity, symmetry, triangle inequality. */
int32_t levenshtein_distance(const char *s1, const char *s2) {
    if (!s1 || !s2) {
        if (!s1 && !s2) return 0;
        return !s1 ? (int32_t)strlen(s2) : (int32_t)strlen(s1);
    }

    int32_t len1 = (int32_t)strlen(s1);
    int32_t len2 = (int32_t)strlen(s2);

    if (len1 > len2) {
        const char *ts = s1; s1 = s2; s2 = ts;
        int32_t tl = len1; len1 = len2; len2 = tl;
    }

    int32_t *prev = (int32_t *)calloc((size_t)(len1 + 1), sizeof(int32_t));
    int32_t *curr = (int32_t *)calloc((size_t)(len1 + 1), sizeof(int32_t));
    if (!prev || !curr) { free(prev); free(curr); return -1; }

    int32_t i, j;
    for (i = 0; i <= len1; i++) prev[i] = i;

    for (j = 1; j <= len2; j++) {
        curr[0] = j;
        for (i = 1; i <= len1; i++) {
            int32_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            curr[i] = min3(prev[i] + 1, curr[i - 1] + 1, prev[i - 1] + cost);
        }
        { int32_t *sw = prev; prev = curr; curr = sw; }
    }

    int32_t result = prev[len1];
    free(prev); free(curr);
    return result;
}

/* ===== L5: Damerau-Levenshtein Distance =====
 * Extends Levenshtein with transposition as a single operation.
 * d[i][j] = min( del, ins, sub, trans )
 *   trans = d[i-2][j-2] + 1 if s1[i-1]==s2[j-2] AND s1[i-2]==s2[j-1] */
int32_t damerau_levenshtein(const char *s1, const char *s2) {
    if (!s1 || !s2) {
        if (!s1 && !s2) return 0;
        return !s1 ? (int32_t)strlen(s2) : (int32_t)strlen(s1);
    }

    int32_t len1 = (int32_t)strlen(s1);
    int32_t len2 = (int32_t)strlen(s2);

    int32_t **d = (int32_t **)malloc((size_t)(len1 + 1) * sizeof(int32_t *));
    if (!d) return -1;
    int32_t i;
    for (i = 0; i <= len1; i++) {
        d[i] = (int32_t *)calloc((size_t)(len2 + 1), sizeof(int32_t));
        if (!d[i]) {
            int32_t k; for (k = 0; k < i; k++) free(d[k]);
            free(d); return -1;
        }
    }

    int32_t j;
    for (i = 0; i <= len1; i++) {
        for (j = 0; j <= len2; j++) d[i][j] = 0;
        d[i][0] = i;
    }
    for (j = 0; j <= len2; j++) d[0][j] = j;

    for (i = 1; i <= len1; i++) {
        for (j = 1; j <= len2; j++) {
            int32_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            d[i][j] = min3(d[i - 1][j] + 1, d[i][j - 1] + 1,
                           d[i - 1][j - 1] + cost);
            if (i > 1 && j > 1 &&
                s1[i - 1] == s2[j - 2] &&
                s1[i - 2] == s2[j - 1]) {
                int32_t trans = d[i - 2][j - 2] + 1;
                if (trans < d[i][j]) d[i][j] = trans;
            }
        }
    }

    int32_t result = 0;
    if (len1 >= 0 && len2 >= 0) {
        result = d[len1][len2];
    }
    for (i = 0; i <= len1; i++) free(d[i]);
    free(d);
    return result;
}

/* ===== L5: Soundex Phonetic Encoding =====
 * US Census Soundex (1918):
 *   1. Retain first letter (uppercase)
 *   2. Replace consonants: BFPV��1, CGJKQSXZ��2, DT��3, L��4, MN��5, R��6
 *   3. Drop vowels and H,W,Y (unless first letter)
 *   4. Collapse adjacent identical digits
 *   5. Pad/truncate to letter + 3 digits
 * Example: Washington��W252, Robert��R163 */
void soundex_encode(const char *word, char *code) {
    if (!word || !code) return;

    static const char sdx_map[26] = {
        '0','1','2','3','0','1','2','0',  /* A-H */
        '0','2','2','4','5','5','0','1',  /* I-P */
        '2','6','2','3','0','1','0','2',  /* Q-X */
        '0','2'                            /* Y-Z */
    };

    int32_t len = (int32_t)strlen(word);
    if (len == 0) { code[0] = '\0'; return; }

    char first = (char)toupper((unsigned char)word[0]);
    if (first < 'A' || first > 'Z') { code[0] = '\0'; return; }
    code[0] = first;

    int32_t pos = 1;
    char prev = sdx_map[first - 'A'];
    int32_t i;

    for (i = 1; i < len && pos < 4; i++) {
        char c = (char)toupper((unsigned char)word[i]);
        if (c < 'A' || c > 'Z') continue;
        char d = sdx_map[c - 'A'];
        if (d == '0') { prev = '0'; continue; }
        if (d == prev) continue;
        code[pos++] = d;
        prev = d;
    }
    while (pos < 4) code[pos++] = '0';
    code[4] = '\0';
}

/* ===== L5: N-gram Jaccard Similarity =====
 * J(A,B) = |S(A) intersect S(B)| / |S(A) union S(B)|
 * where S(word) = set of character n-grams with boundary padding.
 * Time: O(|A|*|B|), Space: O(|A|+|B|). */
double ngram_jaccard_similarity(const char *s1, const char *s2, int32_t n) {
    if (!s1 || !s2 || n <= 0) return 0.0;
    if (strcmp(s1, s2) == 0) return 1.0;

    int32_t len1 = (int32_t)strlen(s1);
    int32_t len2 = (int32_t)strlen(s2);
    int32_t pad = n - 1;
    int32_t pl1 = pad + len1 + pad;
    int32_t pl2 = pad + len2 + pad;

    char p1[SPELL_MAX_WORD_LEN + 4];
    char p2[SPELL_MAX_WORD_LEN + 4];

    memset(p1, '_', (size_t)pad);
    memcpy(p1 + pad, s1, (size_t)len1);
    memset(p1 + pad + len1, '_', (size_t)pad);
    p1[pl1] = '\0';

    memset(p2, '_', (size_t)pad);
    memcpy(p2 + pad, s2, (size_t)len2);
    memset(p2 + pad + len2, '_', (size_t)pad);
    p2[pl2] = '\0';

    int32_t count1 = pl1 - n + 1;
    int32_t count2 = pl2 - n + 1;
    if (count1 <= 0 || count2 <= 0) return 0.0;
    if (count1 > 256 || count2 > 256) return 0.0;

    char ng1[256][8], ng2[256][8];
    int32_t p;
    for (p = 0; p < count1; p++) {
        memcpy(ng1[p], p1 + p, (size_t)n);
        ng1[p][n] = '\0';
    }
    for (p = 0; p < count2; p++) {
        memcpy(ng2[p], p2 + p, (size_t)n);
        ng2[p][n] = '\0';
    }

    int32_t intersect = 0;
    int32_t *used = (int32_t *)calloc((size_t)count2, sizeof(int32_t));
    if (!used) return 0.0;

    int32_t a;
    for (a = 0; a < count1; a++) {
        int32_t b;
        for (b = 0; b < count2; b++) {
            if (!used[b] && strcmp(ng1[a], ng2[b]) == 0) {
                intersect++; used[b] = 1; break;
            }
        }
    }
    free(used);

    int32_t un = count1 + count2 - intersect;
    return (un > 0) ? (double)intersect / (double)un : 0.0;
}

/* ===== L5: Spell Correction by Edit Distance =====
 * Brute-force scan of dictionary, keeping candidates within max_distance.
 * Candidates are sorted by increasing edit distance.
 * Reference: Norvig (2007). */
int32_t spell_correct(const char *word, const char **dictionary,
                      int32_t dict_size, SpellCandidate *candidates,
                      int32_t max_candidates, int32_t max_distance) {
    if (!word || !dictionary || dict_size <= 0 || !candidates ||
        max_candidates <= 0) return 0;

    int32_t count = 0;
    int32_t i;

    for (i = 0; i < dict_size && count < max_candidates; i++) {
        if (!dictionary[i]) continue;
        int32_t dist = levenshtein_distance(word, dictionary[i]);
        if (dist < 0 || dist > max_distance) continue;

        /* Insert sorted by distance (insertion sort) */
        int32_t pos = count;
        while (pos > 0 && candidates[pos - 1].distance > dist) {
            candidates[pos] = candidates[pos - 1];
            pos--;
        }
        if (pos < max_candidates) {
            strncpy(candidates[pos].word, dictionary[i],
                    SPELL_MAX_WORD_LEN - 1);
            candidates[pos].word[SPELL_MAX_WORD_LEN - 1] = '\0';
            candidates[pos].distance = dist;
            if (count < max_candidates) count++;
        }
    }
    return count;
}

/* ===== L5: Generate Edit-Distance-1 Variants =====
 * Deletions + Insertions + Substitutions + Transpositions.
 * Total variants for word of length L:
 *   Del: L, Ins: 26*(L+1), Sub: 25*L, Trans: L-1
 *   = 54*L + 25 variants (e.g., 295 for L=5).
 * Used in spell-checkers for candidate generation. */
int32_t spell_generate_variants(const char *word,
                                spell_variant_callback cb, void *ctx) {
    if (!word || !cb) return 0;

    int32_t len = (int32_t)strlen(word);
    if (len <= 0 || len >= SPELL_MAX_WORD_LEN) return 0;

    char variant[SPELL_MAX_WORD_LEN + 2];
    int32_t count = 0, i;
    char c;

    /* Deletions */
    for (i = 0; i < len; i++) {
        memcpy(variant, word, (size_t)i);
        memcpy(variant + i, word + i + 1, (size_t)(len - i - 1));
        variant[len - 1] = '\0';
        cb(variant, ctx); count++;
    }

    /* Insertions */
    for (i = 0; i <= len; i++) {
        for (c = 'a'; c <= 'z'; c++) {
            memcpy(variant, word, (size_t)i);
            variant[i] = c;
            memcpy(variant + i + 1, word + i, (size_t)(len - i));
            variant[len + 1] = '\0';
            cb(variant, ctx); count++;
        }
    }

    /* Substitutions */
    for (i = 0; i < len; i++) {
        for (c = 'a'; c <= 'z'; c++) {
            if (c == (char)tolower((unsigned char)word[i])) continue;
            memcpy(variant, word, (size_t)len + 1);
            variant[i] = c;
            cb(variant, ctx); count++;
        }
    }

    /* Transpositions */
    for (i = 0; i < len - 1; i++) {
        memcpy(variant, word, (size_t)len + 1);
        { char t = variant[i]; variant[i] = variant[i + 1]; variant[i + 1] = t; }
        cb(variant, ctx); count++;
    }

    return count;
}