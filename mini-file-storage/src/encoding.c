/* ───────────────────────────────────────────────────────────
   Key-Value Encoding Utilities

   Implements standard encoding schemes used in storage engines:
     1. Varint (LEB128) — variable-length integer encoding
     2. Big-endian fixed-width encoding for ordered integer keys
     3. Composite key builder for tuple encoding with ordering
     4. Common-prefix computation for delta encoding

   Theorem (Varint Correctness):
     For any uint32_t v, varint32_decode(varint32_encode(v)) = v.
     Encoding size s(v) = ceil(log_128(v+1)) for v > 0, s(0) = 1.

   Theorem (String Escape in Composite Keys):
     To prevent byte 0x00 from appearing within a component
     (since it's used as separator), we escape 0x00 as 0x00 0xFF
     and escape 0xFF as 0xFF 0xFF.

   Reference: H. Garcia-Molina, J.D. Ullman, J. Widom,
   "Database Systems: The Complete Book", 2nd Ed., Ch. 14.
   ─────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "encoding.h"

/* ============================================================
   Varint 32-bit
   ============================================================ */

/* ─────────────────────────────────────────────
   Encode uint32_t as LEB128 varint.
   ───────────────────────────────────────────── */
int varint32_encode(uint32_t value, uint8_t *buf) {
    int pos = 0;
    while (value >= 0x80u) {
        buf[pos++] = (uint8_t)((value & 0x7Fu) | 0x80u);
        value >>= 7;
    }
    buf[pos++] = (uint8_t)(value & 0x7Fu);
    return pos;
}

/* ─────────────────────────────────────────────
   Decode LEB128 varint to uint32_t.
   Increments *p by bytes consumed.
   ───────────────────────────────────────────── */
int varint32_decode(const uint8_t **p, const uint8_t *end, uint32_t *value_out) {
    if (!p || !*p || !value_out) return 0;

    uint32_t value = 0;
    int shift = 0;
    int bytes = 0;
    const uint8_t *q = *p;

    while (q < end) {
        uint8_t byte = *q++;
        bytes++;
        value |= ((uint32_t)(byte & 0x7Fu)) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0) {
            *value_out = value;
            *p = q;
            return bytes;
        }
        if (bytes >= 5) return 0; /* overflow / too many bytes */
    }
    return 0; /* truncated */
}

/* ============================================================
   Varint 64-bit
   ============================================================ */

/* ─────────────────────────────────────────────
   Encode uint64_t as LEB128 varint.
   ───────────────────────────────────────────── */
int varint64_encode(uint64_t value, uint8_t *buf) {
    int pos = 0;
    while (value >= 0x80uLL) {
        buf[pos++] = (uint8_t)((value & 0x7FuLL) | 0x80u);
        value >>= 7;
    }
    buf[pos++] = (uint8_t)(value & 0x7FuLL);
    return pos;
}

/* ─────────────────────────────────────────────
   Decode LEB128 varint to uint64_t.
   ───────────────────────────────────────────── */
int varint64_decode(const uint8_t **p, const uint8_t *end, uint64_t *value_out) {
    if (!p || !*p || !value_out) return 0;

    uint64_t value = 0;
    int shift = 0;
    int bytes = 0;
    const uint8_t *q = *p;

    while (q < end) {
        uint8_t byte = *q++;
        bytes++;
        value |= ((uint64_t)(byte & 0x7Fu)) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0) {
            *value_out = value;
            *p = q;
            return bytes;
        }
        if (bytes >= 10) return 0; /* overflow */
    }
    return 0; /* truncated */
}

/* ============================================================
   Big-Endian Integer Encoding
   ============================================================ */

/* ─────────────────────────────────────────────
   uint32_t → 4-byte big-endian
   ───────────────────────────────────────────── */
void int32_be_encode(uint32_t val, uint8_t *buf) {
    buf[0] = (uint8_t)((val >> 24) & 0xFFu);
    buf[1] = (uint8_t)((val >> 16) & 0xFFu);
    buf[2] = (uint8_t)((val >>  8) & 0xFFu);
    buf[3] = (uint8_t)(val & 0xFFu);
}

/* ─────────────────────────────────────────────
   4-byte big-endian → uint32_t
   ───────────────────────────────────────────── */
uint32_t int32_be_decode(const uint8_t *buf) {
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] <<  8) |
           ((uint32_t)buf[3]);
}

/* ─────────────────────────────────────────────
   uint64_t → 8-byte big-endian
   ───────────────────────────────────────────── */
void int64_be_encode(uint64_t val, uint8_t *buf) {
    buf[0] = (uint8_t)((val >> 56) & 0xFFu);
    buf[1] = (uint8_t)((val >> 48) & 0xFFu);
    buf[2] = (uint8_t)((val >> 40) & 0xFFu);
    buf[3] = (uint8_t)((val >> 32) & 0xFFu);
    buf[4] = (uint8_t)((val >> 24) & 0xFFu);
    buf[5] = (uint8_t)((val >> 16) & 0xFFu);
    buf[6] = (uint8_t)((val >>  8) & 0xFFu);
    buf[7] = (uint8_t)(val & 0xFFu);
}

/* ─────────────────────────────────────────────
   8-byte big-endian → uint64_t
   ───────────────────────────────────────────── */
uint64_t int64_be_decode(const uint8_t *buf) {
    return ((uint64_t)buf[0] << 56) |
           ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) |
           ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) |
           ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] <<  8) |
           ((uint64_t)buf[7]);
}

/* ============================================================
   Composite Key Builder
   ============================================================ */

/* ─────────────────────────────────────────────
   Initialize composite key builder
   ───────────────────────────────────────────── */
void composite_key_init(CompositeKeyBuilder *builder) {
    if (!builder) return;
    memset(builder, 0, sizeof(CompositeKeyBuilder));
}

/* ─────────────────────────────────────────────
   Add a raw byte component to the composite key
   ───────────────────────────────────────────── */
int composite_key_add_part(CompositeKeyBuilder *builder,
                            const uint8_t *data, uint32_t len) {
    if (!builder || !data) return -1;
    if (builder->num_parts >= COMPOSITE_KEY_MAX_PARTS) return -1;

    uint32_t idx = builder->num_parts;
    builder->parts[idx] = (uint8_t *)malloc(len + 1);
    if (!builder->parts[idx]) return -1;
    memcpy(builder->parts[idx], data, len);
    builder->parts[idx][len] = 0; /* null-terminate for safety */
    builder->part_lens[idx] = len;
    builder->num_parts++;
    return 0;
}

/* ─────────────────────────────────────────────
   Add a uint32_t component (big-endian encoded)
   ───────────────────────────────────────────── */
int composite_key_add_uint32(CompositeKeyBuilder *builder, uint32_t value) {
    if (!builder) return -1;
    if (builder->num_parts >= COMPOSITE_KEY_MAX_PARTS) return -1;

    uint32_t idx = builder->num_parts;
    builder->parts[idx] = (uint8_t *)malloc(4);
    if (!builder->parts[idx]) return -1;
    int32_be_encode(value, builder->parts[idx]);
    builder->part_lens[idx] = 4;
    builder->num_parts++;
    return 0;
}

/* ─────────────────────────────────────────────
   Add a string component with byte-escape encoding.
   Escape strategy: 0x00 → 0x00 0xFF, 0xFF → 0xFF 0xFF
   This ensures the separator byte 0x00 never appears
   unescaped within a component.
   ───────────────────────────────────────────── */
int composite_key_add_string(CompositeKeyBuilder *builder,
                              const uint8_t *str, uint32_t len) {
    if (!builder || !str) return -1;
    if (builder->num_parts >= COMPOSITE_KEY_MAX_PARTS) return -1;

    /* Worst case: every byte needs escaping → 2*len */
    uint8_t *escaped = (uint8_t *)malloc(len * 2 + 1);
    if (!escaped) return -1;

    uint32_t outpos = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (str[i] == 0x00) {
            escaped[outpos++] = 0x00;
            escaped[outpos++] = 0xFF;
        } else if (str[i] == 0xFF) {
            escaped[outpos++] = 0xFF;
            escaped[outpos++] = 0xFF;
        } else {
            escaped[outpos++] = str[i];
        }
    }

    uint32_t idx = builder->num_parts;
    builder->parts[idx] = escaped;
    builder->part_lens[idx] = outpos;
    builder->num_parts++;
    return 0;
}

/* ─────────────────────────────────────────────
   Build composite key: part1 || 0x00 || part2 || 0x00 || ...
   Returns total length of the composite key.
   ───────────────────────────────────────────── */
uint32_t composite_key_build(CompositeKeyBuilder *builder, uint8_t *buf) {
    if (!builder || !buf) return 0;

    uint32_t pos = 0;
    for (uint32_t i = 0; i < builder->num_parts; i++) {
        if (i > 0) {
            buf[pos++] = 0x00; /* separator */
        }
        memcpy(buf + pos, builder->parts[i], builder->part_lens[i]);
        pos += builder->part_lens[i];
        free(builder->parts[i]);
        builder->parts[i] = NULL;
    }
    builder->num_parts = 0;
    return pos;
}

/* ============================================================
   Common Prefix Computation (for delta encoding)
   ============================================================ */

/* ─────────────────────────────────────────────
   Compute common prefix length between two byte arrays.
   O(min(a_len, b_len))
   ───────────────────────────────────────────── */
uint32_t common_prefix_len(const uint8_t *a, uint32_t a_len,
                           const uint8_t *b, uint32_t b_len) {
    if (!a || !b) return 0;
    uint32_t min_len = a_len < b_len ? a_len : b_len;
    uint32_t i = 0;
    while (i < min_len && a[i] == b[i]) i++;
    return i;
}

/* ─────────────────────────────────────────────
   Compute common prefix between key at index `from`
   and key at index `to` in a sorted key array,
   limited by max_gap.
   ───────────────────────────────────────────── */
uint32_t common_prefix_skip(const uint8_t **keys, const uint32_t *key_lens,
                            uint32_t from, uint32_t to, uint32_t max_gap) {
    if (!keys || !key_lens) return 0;
    if (to <= from || to - from > max_gap) {
        /* Fall back to prefix with immediate predecessor */
        if (to > 0) {
            return common_prefix_len(keys[to - 1], key_lens[to - 1],
                                     keys[to],     key_lens[to]);
        }
        return 0;
    }
    /* For restart points (within max_gap), compute with `from` */
    return common_prefix_len(keys[from], key_lens[from],
                             keys[to],   key_lens[to]);
}
