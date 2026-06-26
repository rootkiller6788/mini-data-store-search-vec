#ifndef INDEX_COMPRESSION_H
#define INDEX_COMPRESSION_H

#include <stdint.h>
#include <stddef.h>

/*
 * index_compression.h — Posting List Compression for Inverted Indexes
 *
 * Reference: Witten, Moffat, Bell. "Managing Gigabytes" (1999), Ch. 3-4
 *            Zobel & Moffat. "Inverted files for text search engines" (2006)
 *
 * Knowledge Coverage:
 *   L5: VByte, Simple9, Elias Gamma/Delta, Golomb coding
 *   L4: Information-theoretic lower bounds (Entropy, Kraft inequality)
 *   L3: Word-aligned vs bit-aligned coding, posting list delta encoding
 */

#define COMPRESS_BUFFER_SIZE 4096
#define SIMPLE9_MAX_VAL      0x0FFFFFFF
#define SIMPLE9_SELECTOR_BITS 4
#define SIMPLE9_DATA_BITS     28

/* VByte: 7-bit payload + 1 continuation bit per byte.
 * Small ints (<128) use 1 byte; widely used in Lucene. */
int32_t vbyte_encode(const uint32_t *input, int32_t num_vals,
                     uint8_t *output, int32_t output_capacity);
int32_t vbyte_decode(const uint8_t *input, int32_t input_len,
                     uint32_t *output, int32_t output_capacity);

/* Delta encoding: store gaps between sorted doc_ids.
 * Gap distribution is Zipf-like; small gaps dominate. */
int32_t delta_encode(const uint32_t *sorted_input, int32_t num_vals,
                     uint32_t *output, int32_t output_capacity);
int32_t delta_decode(const uint32_t *gaps, int32_t num_vals,
                     uint32_t *output, int32_t output_capacity);

/* Simple9: packs integers into 32-bit words using 4-bit selectors.
 * Selector 0:28x1, 1:14x2, 2:9x3, 3:7x4, 4:5x5, 5:4x7, 6:3x9, 7:2x14, 8:1x28 */
int32_t simple9_encode(const uint32_t *input, int32_t num_vals,
                       uint32_t *output, int32_t output_capacity);
int32_t simple9_decode(const uint32_t *input, int32_t num_words,
                       uint32_t *output, int32_t output_capacity);

/* Elias gamma: 2*floor(log2(n))+1 bits. Universal code for positive ints. */
int32_t elias_gamma_encode(const uint32_t *input, int32_t num_vals,
                           uint8_t *output, int32_t output_capacity);
int32_t elias_gamma_decode(const uint8_t *input, int32_t input_len_bits,
                           uint32_t *output, int32_t output_capacity);

/* Elias delta: gamma-coded length prefix. Better for larger ints.
 * Asymptotically optimal for power-law distributions. */
int32_t elias_delta_encode(const uint32_t *input, int32_t num_vals,
                           uint8_t *output, int32_t output_capacity);
int32_t elias_delta_decode(const uint8_t *input, int32_t input_len_bits,
                           uint32_t *output, int32_t output_capacity);

/* Golomb/Rice coding: optimal for geometric distributions.
 * k = log2(M) for Rice variant (M = 2^k).
 * Reference: Golomb, "Run-length encodings" (1966) */
int32_t golomb_encode(const uint32_t *input, int32_t num_vals,
                      uint8_t *output, int32_t output_capacity, int32_t k);
int32_t golomb_decode(const uint8_t *input, int32_t input_len,
                      uint32_t *output, int32_t output_capacity, int32_t k);

double compression_ratio(int32_t original_bytes, int32_t compressed_bytes);
int32_t golomb_optimal_k(double mean_gap);

#endif
