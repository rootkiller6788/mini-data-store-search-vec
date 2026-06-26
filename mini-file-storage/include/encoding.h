#ifndef MINI_ENCODING_H
#define MINI_ENCODING_H

#include <stdint.h>
#include <stddef.h>

/* ─────────────────────────────────────────────
   Key-Value Encoding Utilities

   Implements variable-length integer encoding (varint) and
   composite key encoding for ordered storage.

   Varint Encoding (LEB128):
     Each byte uses 7 bits for data and 1 continuation bit (MSB).
     This is more efficient than fixed-width encoding for small
     integers (which are common in database workloads).

     Space: 1 byte for values [0, 127], 2 bytes for [128, 16383], ...
     Worst case: 5 bytes for 32-bit, 10 bytes for 64-bit.

   Reference: Google Protocol Buffers Encoding,
   https://protobuf.dev/programming-guides/encoding/

   Big-Endian Key Encoding:
     For integer keys, store in big-endian order to preserve
     bytewise comparison semantics. This allows memcmp() to
     correctly order integer-valued keys.
   ───────────────────────────────────────────── */

/* ─────────────────────────────────────────────
   Varint 32-bit encoding/decoding
   ───────────────────────────────────────────── */

/**
 * Encode a uint32_t as varint.
 * Returns number of bytes written (1-5).
 * buf must have at least 5 bytes available.
 */
int  varint32_encode(uint32_t value, uint8_t *buf);

/**
 * Decode a varint to uint32_t.
 * Returns number of bytes consumed, or 0 on error.
 * Advances *p by the number of bytes read.
 */
int  varint32_decode(const uint8_t **p, const uint8_t *end, uint32_t *value_out);

/* ─────────────────────────────────────────────
   Varint 64-bit encoding/decoding
   ───────────────────────────────────────────── */

int  varint64_encode(uint64_t value, uint8_t *buf);
int  varint64_decode(const uint8_t **p, const uint8_t *end, uint64_t *value_out);

/* ─────────────────────────────────────────────
   Composite key builder (tuple encoding)
   Used for encoding compound keys (e.g., user_id + timestamp)
   in a manner that preserves bytewise ordering.
   ───────────────────────────────────────────── */

#define COMPOSITE_KEY_MAX_PARTS 4
#define COMPOSITE_KEY_MAX_LEN   256

typedef struct {
    uint8_t  *parts[COMPOSITE_KEY_MAX_PARTS];
    uint32_t  part_lens[COMPOSITE_KEY_MAX_PARTS];
    uint32_t  num_parts;
} CompositeKeyBuilder;

void composite_key_init(CompositeKeyBuilder *builder);

/**
 * Add a raw byte component.
 */
int  composite_key_add_part(CompositeKeyBuilder *builder,
                             const uint8_t *data, uint32_t len);

/**
 * Add a big-endian unsigned integer component (32-bit).
 * Uses fixed-width encoding for ordering preservation.
 */
int  composite_key_add_uint32(CompositeKeyBuilder *builder, uint32_t value);

/**
 * Add a string component with escaping to avoid separator conflicts.
 */
int  composite_key_add_string(CompositeKeyBuilder *builder,
                               const uint8_t *str, uint32_t len);

/**
 * Build the composite key into buf. buf must have at least
 * COMPOSITE_KEY_MAX_LEN bytes.
 * Returns total key length.
 */
uint32_t composite_key_build(CompositeKeyBuilder *builder, uint8_t *buf);

/* ─────────────────────────────────────────────
   Integer key encoding/decoding (big-endian, fixed-width)
   ───────────────────────────────────────────── */

/**
 * Encode uint32_t as 4-byte big-endian.
 */
void int32_be_encode(uint32_t val, uint8_t *buf);

/**
 * Decode uint32_t from 4-byte big-endian.
 */
uint32_t int32_be_decode(const uint8_t *buf);

/**
 * Encode uint64_t as 8-byte big-endian.
 */
void int64_be_encode(uint64_t val, uint8_t *buf);

/**
 * Decode uint64_t from 8-byte big-endian.
 */
uint64_t int64_be_decode(const uint8_t *buf);

/* ─────────────────────────────────────────────
   Delta encoding for sorted key sequences
   ───────────────────────────────────────────── */

/**
 * Compute the length of the common prefix between two byte arrays.
 */
uint32_t common_prefix_len(const uint8_t *a, uint32_t a_len,
                           const uint8_t *b, uint32_t b_len);

/**
 * Compute the length of the common prefix between two keys
 * separated by a gap (for restart-point delta encoding).
 * max_gap: maximum number of entries between the two keys.
 */
uint32_t common_prefix_skip(const uint8_t **keys, const uint32_t *key_lens,
                            uint32_t from, uint32_t to, uint32_t max_gap);

#endif /* MINI_ENCODING_H */
