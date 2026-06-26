#include "index_compression.h"
#include <string.h>
#include <stdio.h>

/* ===================================================================
 * index_compression.c — Posting List Compression Implementations
 *
 * Each encoding scheme exploits different statistical properties of
 * posting list gaps. The key insight (L4): gap distribution in
 * posting lists is highly skewed — most gaps are small (1-3), making
 * variable-length codes far more efficient than fixed-width storage.
 *
 * L4 Theorem (Entropy Lower Bound): The expected code length cannot
 * be less than the entropy H = -sum p(x)*log2(p(x)). A good code
 * achieves average length close to H.
 * =================================================================== */

/* ===== VByte Encoding =====
 * L5: VByte (varint) splits each 32-bit integer into 7-bit groups.
 * Each byte: 7 data bits + 1 continuation bit (MSB).
 * MSB=1 means "more bytes follow", MSB=0 = "last byte".
 *
 * Example: 300 decimal = 0b100101100
 *   Byte 0: 1_0101100 (0xAC), Byte 1: 0_0000010 (0x02)
 *
 * L4: VByte achieves ~2-4x compression on posting lists since
 * small doc_id gaps (<128) use 1 byte instead of 4.
 * Used in Lucene/Elasticsearch .tim and .doc files. */
int32_t vbyte_encode(const uint32_t *input, int32_t num_vals,
                     uint8_t *output, int32_t output_capacity) {
    int32_t out_pos = 0;
    int32_t i;

    for (i = 0; i < num_vals; i++) {
        uint32_t val = input[i];
        while (val >= 0x80) {
            if (out_pos >= output_capacity) return -1;
            output[out_pos++] = (uint8_t)((val & 0x7F) | 0x80);
            val >>= 7;
        }
        if (out_pos >= output_capacity) return -1;
        output[out_pos++] = (uint8_t)(val & 0x7F);
    }
    return out_pos;
}

int32_t vbyte_decode(const uint8_t *input, int32_t input_len,
                     uint32_t *output, int32_t output_capacity) {
    int32_t out_pos = 0, in_pos = 0;

    while (in_pos < input_len && out_pos < output_capacity) {
        uint32_t val = 0;
        int32_t shift = 0;

        while (in_pos < input_len) {
            uint8_t byte = input[in_pos++];
            val |= (uint32_t)(byte & 0x7F) << shift;
            shift += 7;
            if (!(byte & 0x80)) break;
        }
        output[out_pos++] = val;
    }
    return out_pos;
}

/* ===== Delta/Gap Encoding =====
 * L5: Store the first value, then successive differences.
 * For sorted posting lists, gaps are typically 1-10.
 *
 * L4: gap distribution follows Zipf: P(gap=k) ∝ 1/k^α, α≈1.5-2.
 * Sum of gaps = last_id - first_id (telescoping). */
int32_t delta_encode(const uint32_t *sorted_input, int32_t num_vals,
                     uint32_t *output, int32_t output_capacity) {
    if (num_vals <= 0 || !sorted_input || !output) return 0;
    if (num_vals > output_capacity) return -1;

    output[0] = sorted_input[0];
    int32_t i;
    for (i = 1; i < num_vals; i++) {
        if (sorted_input[i] < sorted_input[i - 1]) return -2;
        output[i] = sorted_input[i] - sorted_input[i - 1];
    }
    return num_vals;
}

int32_t delta_decode(const uint32_t *gaps, int32_t num_vals,
                     uint32_t *output, int32_t output_capacity) {
    if (num_vals <= 0 || !gaps || !output) return 0;
    if (num_vals > output_capacity) return -1;

    output[0] = gaps[0];
    int32_t i;
    for (i = 1; i < num_vals; i++) {
        output[i] = output[i - 1] + gaps[i];
    }
    return num_vals;
}

/* ===== Simple9 Encoding =====
 * L5: Simple9 packs integers into 32-bit words. First 4 bits (28-31)
 * select packing mode; remaining 28 bits hold values.
 *
 * Selector modes:
 *   0: 28x1-bit, 1: 14x2, 2: 9x3, 3: 7x4, 4: 5x5,
 *   5: 4x7, 6: 3x9, 7: 2x14, 8: 1x28
 *
 * L4: Word-aligned → no bit-level shifting overhead.
 * Decompression uses a lookup table: very fast. */

static const int32_t s9_num_vals[]  = { 28, 14, 9, 7, 5, 4, 3, 2, 1 };
static const int32_t s9_bits[]      = {  1,  2, 3, 4, 5, 7, 9, 14, 28 };
static const uint32_t s9_mask[]     = {
    0x00000001, 0x00000003, 0x00000007, 0x0000000F,
    0x0000001F, 0x0000007F, 0x000001FF, 0x00003FFF, 0x0FFFFFFF
};

int32_t simple9_encode(const uint32_t *input, int32_t num_vals,
                       uint32_t *output, int32_t output_capacity) {
    int32_t in_pos = 0, out_pos = 0;

    while (in_pos < num_vals && out_pos < output_capacity) {
        int32_t best_sel = -1, best_count = 0;
        int32_t s;

        /* Find the selector that packs the most remaining values */
        for (s = 0; s < 9; s++) {
            int32_t count = s9_num_vals[s];
            if (in_pos + count > num_vals) continue;

            int32_t fits = 1, j;
            for (j = 0; j < count; j++) {
                if (input[in_pos + j] > s9_mask[s]) {
                    fits = 0;
                    break;
                }
            }
            if (fits && count > best_count) {
                best_count = count;
                best_sel = s;
            }
        }

        if (best_sel < 0) { best_sel = 8; best_count = 1; }

        uint32_t word = ((uint32_t)best_sel << 28);
        int32_t j;
        for (j = 0; j < best_count; j++) {
            word |= (input[in_pos + j] & s9_mask[best_sel])
                    << (j * s9_bits[best_sel]);
        }
        output[out_pos++] = word;
        in_pos += best_count;
    }
    return out_pos;
}

int32_t simple9_decode(const uint32_t *input, int32_t num_words,
                       uint32_t *output, int32_t output_capacity) {
    int32_t out_pos = 0;
    int32_t w;

    for (w = 0; w < num_words && out_pos < output_capacity; w++) {
        uint32_t word = input[w];
        int32_t selector = (int32_t)(word >> 28);
        if (selector < 0 || selector > 8) return -1;

        int32_t count = s9_num_vals[selector];
        int32_t bits  = s9_bits[selector];
        uint32_t mask = s9_mask[selector];
        int32_t j;

        for (j = 0; j < count && out_pos < output_capacity; j++) {
            uint32_t val = (word >> (j * bits)) & mask;
            output[out_pos++] = val;
        }
    }
    return out_pos;
}

/* ===== Elias Gamma Coding =====
 * L5: For integer n >= 1:
 *   L = floor(log2(n))
 *   Code = L zeros, then (L+1)-bit binary of n.
 * Example: n=5 (binary 101), L=2 → 00101
 *
 * L4: Gamma code length = 2*floor(log2(n)) + 1 bits.
 * Gamma is universally optimal for p(n) ∝ 1/(2n^2). */

static void bit_write(uint8_t *buf, int32_t *bit_pos,
                      uint32_t val, int32_t nbits) {
    while (nbits > 0) {
        int32_t byte_idx = *bit_pos >> 3;
        int32_t bit_idx  = *bit_pos & 7;
        int32_t room = 8 - bit_idx;
        int32_t chunk = (nbits < room) ? nbits : room;
        uint32_t bits = (val >> (nbits - chunk)) & ((1U << chunk) - 1);
        buf[byte_idx] |= (uint8_t)(bits << (room - chunk));
        *bit_pos += chunk;
        nbits -= chunk;
    }
}

static uint32_t bit_read(const uint8_t *buf, int32_t *bit_pos, int32_t nbits) {
    uint32_t val = 0;
    while (nbits > 0) {
        int32_t byte_idx = *bit_pos >> 3;
        int32_t bit_idx  = *bit_pos & 7;
        int32_t room = 8 - bit_idx;
        int32_t chunk = (nbits < room) ? nbits : room;
        uint32_t bits = (buf[byte_idx] >> (room - chunk)) &
                        ((1U << chunk) - 1);
        val = (val << chunk) | bits;
        *bit_pos += chunk;
        nbits -= chunk;
    }
    return val;
}

int32_t elias_gamma_encode(const uint32_t *input, int32_t num_vals,
                           uint8_t *output, int32_t output_capacity) {
    if (!input || !output || output_capacity <= 0) return -1;
    memset(output, 0, (size_t)output_capacity);

    int32_t bit_pos = 0;
    int32_t i;

    for (i = 0; i < num_vals; i++) {
        uint32_t n = input[i];
        if (n == 0) n = 1;

        int32_t L = 0;
        uint32_t temp = n;
        while (temp > 1) { temp >>= 1; L++; }

        /* L zeros */
        bit_write(output, &bit_pos, 0, L);
        /* (L+1)-bit binary of n */
        bit_write(output, &bit_pos, n, L + 1);
    }
    return (bit_pos + 7) / 8;
}

int32_t elias_gamma_decode(const uint8_t *input, int32_t input_len_bits,
                           uint32_t *output, int32_t output_capacity) {
    if (!input || !output || output_capacity <= 0 || input_len_bits <= 0)
        return 0;

    int32_t bit_pos = 0, out_pos = 0;

    while (bit_pos < input_len_bits && out_pos < output_capacity) {
        /* Count leading zeros; the terminating 1 is consumed */
        int32_t L = 0;
        while (bit_pos < input_len_bits &&
               bit_read(input, &bit_pos, 1) == 0)
            L++;
        if (bit_pos >= input_len_bits) break;

        /* The leading 1 was consumed by the zero-count loop.
         * Read remaining L bits and reconstruct: n = (1 << L) | low_bits */
        uint32_t low_bits = 0;
        if (L > 0)
            low_bits = bit_read(input, &bit_pos, L);
        uint32_t n = (1U << (uint32_t)L) | low_bits;
        output[out_pos++] = n;
    }
    return out_pos;
}

/* ===== Elias Delta Coding =====
 * L5: Encodes n as:
 *   1. Gamma(L+1) where L = floor(log2(n))
 *   2. L-bit binary of n without leading 1
 * Length ≈ log2(n) + 2*log2(log2(n)) + O(1) bits.
 *
 * L4: Delta is asymptotically optimal for distributions where
 * p(n) ∝ 1/(n * (log n)^2). */

int32_t elias_delta_encode(const uint32_t *input, int32_t num_vals,
                           uint8_t *output, int32_t output_capacity) {
    if (!input || !output || output_capacity <= 0) return -1;
    memset(output, 0, (size_t)output_capacity);

    int32_t bit_pos = 0;
    int32_t i;

    for (i = 0; i < num_vals; i++) {
        uint32_t n = input[i];
        if (n == 0) n = 1;

        int32_t L = 0;
        uint32_t temp = n;
        while (temp > 1) { temp >>= 1; L++; }

        /* Gamma-encode (L+1) */
        int32_t Lp1 = L + 1;
        int32_t L2 = 0;
        uint32_t t2 = (uint32_t)Lp1;
        while (t2 > 1) { t2 >>= 1; L2++; }
        bit_write(output, &bit_pos, 0, L2);
        bit_write(output, &bit_pos, (uint32_t)Lp1, L2 + 1);

        /* L bits of n (drop leading 1) */
        if (L > 0) {
            uint32_t low_bits = n & ((1U << L) - 1);
            bit_write(output, &bit_pos, low_bits, L);
        }
    }
    return (bit_pos + 7) / 8;
}

int32_t elias_delta_decode(const uint8_t *input, int32_t input_len_bits,
                           uint32_t *output, int32_t output_capacity) {
    if (!input || !output || output_capacity <= 0 || input_len_bits <= 0)
        return 0;

    int32_t bit_pos = 0, out_pos = 0;

    while (bit_pos < input_len_bits && out_pos < output_capacity) {
        /* Gamma-decode L+1: count leading zeros (terminating 1 consumed) */
        int32_t L2 = 0;
        while (bit_pos < input_len_bits &&
               bit_read(input, &bit_pos, 1) == 0)
            L2++;
        if (bit_pos >= input_len_bits) break;

        /* The terminating 1 was consumed; read L2 more bits for full value */
        uint32_t low_Lp1 = 0;
        if (L2 > 0)
            low_Lp1 = bit_read(input, &bit_pos, L2);
        uint32_t Lp1 = (1U << (uint32_t)L2) | low_Lp1;
        int32_t L = (int32_t)Lp1 - 1;

        /* Read L bits for remainder */
        uint32_t low_bits = 0;
        if (L > 0) {
            if (bit_pos + L > input_len_bits) break;
            low_bits = bit_read(input, &bit_pos, L);
        }

        uint32_t n = (1U << L) | low_bits;
        output[out_pos++] = n;
    }
    return out_pos;
}

/* ===== Golomb Coding (Rice variant) =====
 * L5: For M=2^k: q=n/M, r=n%M. Code: unary(q) + k-bit binary(r).
 * Unary(q): q ones then a zero.
 *
 * L4 (Golomb 1966): For geometric distribution with parameter p,
 * Golomb with M = ceil(ln(2-p)/(-ln(1-p))) is optimal. */

int32_t golomb_encode(const uint32_t *input, int32_t num_vals,
                      uint8_t *output, int32_t output_capacity, int32_t k) {
    if (!input || !output || output_capacity <= 0 || k < 0 || k > 30)
        return -1;
    memset(output, 0, (size_t)output_capacity);

    int32_t bit_pos = 0;
    uint32_t mask = (uint32_t)((1 << k) - 1);
    int32_t i;

    for (i = 0; i < num_vals; i++) {
        uint32_t n = input[i];
        uint32_t q = n >> (uint32_t)k;
        uint32_t r = n & mask;

        /* Unary encode quotient */
        uint32_t j;
        for (j = 0; j < q; j++)
            bit_write(output, &bit_pos, 1, 1);
        bit_write(output, &bit_pos, 0, 1);

        /* Remainder in k bits */
        bit_write(output, &bit_pos, r, k);
    }
    return (bit_pos + 7) / 8;
}

int32_t golomb_decode(const uint8_t *input, int32_t input_len,
                      uint32_t *output, int32_t output_capacity, int32_t k) {
    if (!input || !output || output_capacity <= 0 || k < 0)
        return 0;

    int32_t bit_pos = 0, out_pos = 0;
    int32_t total_bits = input_len * 8;

    while (bit_pos < total_bits && out_pos < output_capacity) {
        /* Decode unary quotient */
        uint32_t q = 0;
        while (bit_pos < total_bits &&
               bit_read(input, &bit_pos, 1) == 1)
            q++;

        /* Decode remainder */
        if (bit_pos + k > total_bits) break;
        uint32_t r = bit_read(input, &bit_pos, k);

        output[out_pos++] = (q << (uint32_t)k) | r;
    }
    return out_pos;
}

/* ===== Utility Functions ===== */

double compression_ratio(int32_t original_bytes, int32_t compressed_bytes) {
    if (original_bytes <= 0 || compressed_bytes <= 0) return 0.0;
    return (double)original_bytes / (double)compressed_bytes;
}

int32_t golomb_optimal_k(double mean_gap) {
    if (mean_gap <= 0.0) return 0;
    /* Heuristic: M ≈ mean_gap * ln(2); k = ceil(log2(M)) */
    double m_opt = mean_gap * 0.693147;
    if (m_opt < 1.0) return 0;
    int32_t k = 0;
    while ((1 << k) < (int32_t)(m_opt + 0.5) && k < 30)
        k++;
    return k;
}