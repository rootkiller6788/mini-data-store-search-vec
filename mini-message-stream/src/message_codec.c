#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "message_codec.h"

/* ================================================================
 * L5: Variable-Length Integer Encoding (Protobuf Varint + ZigZag)
 *
 * Reference: Protocol Buffers Encoding (Google, 2008)
 *
 * Varint: Each byte uses 7 bits for data, MSB indicates continuation.
 * This is efficient because small values (< 128) use 1 byte.
 *
 * ZigZag: Maps signed integers to unsigned for efficient varint encoding.
 *   encode(n) = (n << 1) ^ (n >> 63)   [for int64]
 *   decode(u) = (u >> 1) ^ -(u & 1)
 *
 * Course: Berkeley CS 267 (Data Serialization)
 * ================================================================ */

/* Encode uint32 as varint. Returns number of bytes written.
 * L5: Algorithm - while value >= 0x80, emit (value & 0x7F) | 0x80,
 * then value >>= 7. Final byte is just value & 0x7F. */
int varint_encode_u32(uint8_t *out, uint32_t value)
{
    int i;
    i = 0;
    while (value >= 0x80) {
        out[i++] = (uint8_t)((value & 0x7F) | 0x80);
        value >>= 7;
        if (i >= MAX_VARINT_BYTES) return -1;
    }
    out[i++] = (uint8_t)(value & 0x7F);
    return i;
}

/* Decode uint32 varint. Returns number of bytes consumed, or -1 on error.
 * L5: Algorithm - read bytes, accumulate into result using shift.
 * Detect overflow beyond uint32 range. */
int varint_decode_u32(const uint8_t *in, int in_len, uint32_t *out_value)
{
    uint32_t result;
    int shift, i;
    uint8_t b;

    if (!in || !out_value || in_len < 1) return -1;

    result = 0;
    shift = 0;

    for (i = 0; i < in_len && i < MAX_VARINT_BYTES; i++) {
        b = in[i];
        result |= ((uint32_t)(b & 0x7F)) << shift;
        if ((b & 0x80) == 0) {
            *out_value = result;
            return i + 1;
        }
        shift += 7;
        if (shift >= 32) return -1;  /* overflow */
    }

    return -1;  /* truncated */
}

/* ZigZag encode int64 to uint64, then varint encode.
 * L5: ZigZag encoding makes signed integers more compact for varint.
 * Small negative numbers like -1 map to small unsigned numbers like 1. */
int varint_encode_i64(uint8_t *out, int64_t value)
{
    uint64_t zigzag;
    zigzag = (uint64_t)((value << 1) ^ (value >> 63));
    /* Encode as if uint32 for simplicity (limited range in this demo) */
    return varint_encode_u32(out, (uint32_t)zigzag);
}

/* ZigZag + varint decode for int64.
 * The ZigZag formula: decode(u) = (u >> 1) ^ -(u & 1) */
int varint_decode_i64(const uint8_t *in, int in_len, int64_t *out_value)
{
    uint32_t uval;
    int consumed;
    uint64_t zigzag;

    consumed = varint_decode_u32(in, in_len, &uval);
    if (consumed < 0) return -1;

    zigzag = (uint64_t)uval;
    *out_value = (int64_t)((zigzag >> 1) ^ -(int64_t)(zigzag & 1));
    return consumed;
}

/* ================================================================
 * L5: CRC32C Checksum (Castagnoli Polynomial)
 *
 * CRC32C (Castagnoli): polynomial 0x1EDC6F41
 * Used by iSCSI, SCTP, ext4 metadata, Kafka record batches.
 *
 * L4: Error Detection - CRC32C detects all single-bit errors,
 * all double-bit errors, all odd-numbered bit errors, all burst
 * errors up to 32 bits, and 99.9999999% of longer bursts.
 *
 * Algorithm: Table-driven CRC32C (256-entry lookup table).
 * This is O(n) time, O(1) space (256 * 4 = 1KB table).
 *
 * Reference: RFC 3720 (iSCSI), Intel SSE4.2 CRC32 instruction
 * Course: Stanford CS 144 (Networking) - CRC error detection
 *         MIT 6.004 (Computation Structures) - Error correcting codes
 * ================================================================ */

static uint32_t crc32c_table[256];
static int crc32c_table_initialized = 0;

/* Initialize the CRC32C lookup table.
 * L5: Table-driven CRC uses precomputed values for each byte.
 * For each byte value 0..255, compute CRC of that byte shifted through
 * the polynomial. */
static void crc32c_init_table(void)
{
    uint32_t remainder;
    int i, j;

    for (i = 0; i < 256; i++) {
        remainder = (uint32_t)i;
        for (j = 0; j < 8; j++) {
            if (remainder & 1) {
                remainder = (remainder >> 1) ^ CRC32C_POLY;
            } else {
                remainder >>= 1;
            }
        }
        crc32c_table[i] = remainder;
    }
    crc32c_table_initialized = 1;
}

uint32_t crc32c_compute(const uint8_t *data, size_t length)
{
    uint32_t crc;
    size_t i;

    if (!crc32c_table_initialized) crc32c_init_table();

    crc = 0xFFFFFFFF;
    for (i = 0; i < length; i++) {
        crc = crc32c_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t crc32c_append(uint32_t prev_crc, const uint8_t *data, size_t length)
{
    uint32_t crc;
    size_t i;

    if (!crc32c_table_initialized) crc32c_init_table();

    crc = prev_crc ^ 0xFFFFFFFF;
    for (i = 0; i < length; i++) {
        crc = crc32c_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

int crc32c_verify(const uint8_t *data, size_t length, uint32_t expected)
{
    uint32_t computed;
    computed = crc32c_compute(data, length);
    return (computed == expected) ? 0 : -1;
}

/* ================================================================
 * L1: Message Envelope Operations
 * ================================================================ */

void message_envelope_init(MessageEnvelope *env)
{
    if (!env) return;
    memset(env, 0, sizeof(MessageEnvelope));
    env->magic_byte = 2;  /* Kafka v2 message format */
}

void message_envelope_free(MessageEnvelope *env)
{
    if (!env) return;
    free(env->key);
    free(env->value);
    free(env->headers);
    memset(env, 0, sizeof(MessageEnvelope));
}

/* Serialize a single message envelope to binary.
 * L6: Binary serialization with length-delimited fields.
 *
 * Wire format:
 *   [magic:1B][attrs:1B][timestamp:8B]
 *   [key_len:varint][key:key_len bytes]
 *   [value_len:varint][value:value_len bytes]
 *   [headers_len:varint][headers:headers_len bytes]
 */
int message_envelope_serialize(const MessageEnvelope *env,
                                uint8_t *out, int out_max, int *out_len)
{
    int pos, n;

    if (!env || !out || !out_len || out_max < 14) return -1;

    pos = 0;
    out[pos++] = env->magic_byte;
    out[pos++] = env->attributes;

    /* Timestamp: 8 bytes, big-endian */
    out[pos++] = (uint8_t)((env->timestamp >> 56) & 0xFF);
    out[pos++] = (uint8_t)((env->timestamp >> 48) & 0xFF);
    out[pos++] = (uint8_t)((env->timestamp >> 40) & 0xFF);
    out[pos++] = (uint8_t)((env->timestamp >> 32) & 0xFF);
    out[pos++] = (uint8_t)((env->timestamp >> 24) & 0xFF);
    out[pos++] = (uint8_t)((env->timestamp >> 16) & 0xFF);
    out[pos++] = (uint8_t)((env->timestamp >> 8) & 0xFF);
    out[pos++] = (uint8_t)(env->timestamp & 0xFF);

    /* Key */
    n = varint_encode_u32(out + pos, (uint32_t)env->key_length);
    if (n < 0) return -1;
    pos += n;
    if (pos + env->key_length > out_max) return -1;
    if (env->key && env->key_length > 0) {
        memcpy(out + pos, env->key, env->key_length);
        pos += env->key_length;
    }

    /* Value */
    n = varint_encode_u32(out + pos, (uint32_t)env->value_length);
    if (n < 0) return -1;
    pos += n;
    if (pos + env->value_length > out_max) return -1;
    if (env->value && env->value_length > 0) {
        memcpy(out + pos, env->value, env->value_length);
        pos += env->value_length;
    }

    /* Headers */
    n = varint_encode_u32(out + pos, (uint32_t)env->headers_length);
    if (n < 0) return -1;
    pos += n;
    if (pos + env->headers_length > out_max) return -1;
    if (env->headers && env->headers_length > 0) {
        memcpy(out + pos, env->headers, env->headers_length);
        pos += env->headers_length;
    }

    *out_len = pos;
    return 0;
}

/* Deserialize a message envelope from binary. */
int message_envelope_deserialize(const uint8_t *in, int in_len,
                                  MessageEnvelope *out_env, int *consumed)
{
    int pos, consumed_varint;
    uint32_t ulen;

    if (!in || !out_env || in_len < 10) return -1;

    message_envelope_init(out_env);

    pos = 0;
    out_env->magic_byte = in[pos++];
    out_env->attributes  = in[pos++];

    /* Timestamp: 8 bytes big-endian */
    out_env->timestamp = 0;
    out_env->timestamp |= ((int64_t)in[pos++]) << 56;
    out_env->timestamp |= ((int64_t)in[pos++]) << 48;
    out_env->timestamp |= ((int64_t)in[pos++]) << 40;
    out_env->timestamp |= ((int64_t)in[pos++]) << 32;
    out_env->timestamp |= ((int64_t)in[pos++]) << 24;
    out_env->timestamp |= ((int64_t)in[pos++]) << 16;
    out_env->timestamp |= ((int64_t)in[pos++]) << 8;
    out_env->timestamp |= ((int64_t)in[pos++]);

    /* Key length */
    consumed_varint = varint_decode_u32(in + pos, in_len - pos, &ulen);
    if (consumed_varint < 0) return -1;
    pos += consumed_varint;
    out_env->key_length = (int32_t)ulen;
    if (out_env->key_length > 0 && pos + out_env->key_length <= in_len) {
        out_env->key = (char*)malloc(out_env->key_length + 1);
        if (out_env->key) {
            memcpy(out_env->key, in + pos, out_env->key_length);
            out_env->key[out_env->key_length] = '\0';
        }
        pos += out_env->key_length;
    }

    /* Value length */
    consumed_varint = varint_decode_u32(in + pos, in_len - pos, &ulen);
    if (consumed_varint < 0) { message_envelope_free(out_env); return -1; }
    pos += consumed_varint;
    out_env->value_length = (int32_t)ulen;
    if (out_env->value_length > 0 && pos + out_env->value_length <= in_len) {
        out_env->value = (char*)malloc(out_env->value_length + 1);
        if (out_env->value) {
            memcpy(out_env->value, in + pos, out_env->value_length);
            out_env->value[out_env->value_length] = '\0';
        }
        pos += out_env->value_length;
    }

    /* Headers length */
    consumed_varint = varint_decode_u32(in + pos, in_len - pos, &ulen);
    if (consumed_varint < 0) { message_envelope_free(out_env); return -1; }
    pos += consumed_varint;
    out_env->headers_length = (int32_t)ulen;
    if (out_env->headers_length > 0 && pos + out_env->headers_length <= in_len) {
        out_env->headers = (char*)malloc(out_env->headers_length + 1);
        if (out_env->headers) {
            memcpy(out_env->headers, in + pos, out_env->headers_length);
            out_env->headers[out_env->headers_length] = '\0';
        }
    }

    if (consumed) *consumed = pos;
    return 0;
}

/* ================================================================
 * L6: MessageSet (Batch) Operations
 *
 * Batching is the key to Kafka's throughput. A single network round-trip
 * and disk write serves many messages. Kafka v2 batches also support
 * compression at the batch level.
 *
 * Reference: Kafka KIP-98 (Message format v2)
 * ================================================================ */

MessageSet* messageset_create(int64_t base_offset)
{
    MessageSet *ms;
    ms = (MessageSet*)calloc(1, sizeof(MessageSet));
    if (!ms) return NULL;
    ms->base_offset = base_offset;
    ms->magic = 2;
    ms->record_count = 0;
    ms->records = NULL;
    return ms;
}

void messageset_destroy(MessageSet *ms)
{
    int i;
    if (!ms) return;
    if (ms->records) {
        for (i = 0; i < ms->record_count; i++) {
            message_envelope_free(&ms->records[i]);
        }
        free(ms->records);
        ms->records = NULL;
    }
    ms->record_count = 0;
    /* Note: does not free(ms) — caller manages the struct memory */
}

int messageset_add_record(MessageSet *ms, const MessageEnvelope *env)
{
    MessageEnvelope *new_records;
    int new_count;

    if (!ms || !env) return -1;

    new_count = ms->record_count + 1;
    new_records = (MessageEnvelope*)realloc(ms->records,
                                             new_count * sizeof(MessageEnvelope));
    if (!new_records) return -1;

    ms->records = new_records;
    /* Deep copy: allocate new memory for key/value/headers */
    MessageEnvelope *dst = &ms->records[ms->record_count];
    memset(dst, 0, sizeof(MessageEnvelope));
    dst->magic_byte = env->magic_byte;
    dst->attributes = env->attributes;
    dst->timestamp = env->timestamp;
    dst->key_length = env->key_length;
    dst->value_length = env->value_length;
    dst->headers_length = env->headers_length;
    if (env->key && env->key_length > 0) {
        dst->key = (char*)malloc((size_t)env->key_length + 1);
        if (dst->key) { memcpy(dst->key, env->key, (size_t)env->key_length);
                        dst->key[env->key_length] = '\0'; }
    }
    if (env->value && env->value_length > 0) {
        dst->value = (char*)malloc((size_t)env->value_length + 1);
        if (dst->value) { memcpy(dst->value, env->value, (size_t)env->value_length);
                          dst->value[env->value_length] = '\0'; }
    }
    if (env->headers && env->headers_length > 0) {
        dst->headers = (char*)malloc((size_t)env->headers_length + 1);
        if (dst->headers) { memcpy(dst->headers, env->headers,
                                    (size_t)env->headers_length);
                            dst->headers[env->headers_length] = '\0'; }
    }
    ms->record_count = new_count;

    /* Update batch metadata */
    if (ms->record_count == 1) {
        ms->first_timestamp = env->timestamp;
        ms->max_timestamp = env->timestamp;
    } else {
        if (env->timestamp < ms->first_timestamp)
            ms->first_timestamp = env->timestamp;
        if (env->timestamp > ms->max_timestamp)
            ms->max_timestamp = env->timestamp;
    }
    ms->last_offset_delta = ms->record_count - 1;

    return 0;
}

/* Serialize a MessageSet to binary with CRC protection.
 * L6: This is the on-disk format for Kafka record batches. */
int messageset_serialize(const MessageSet *ms, uint8_t *out,
                          int out_max, int *out_len)
{
    int pos, i, rec_len;
    uint32_t crc;

    if (!ms || !out || !out_len || out_max < 61) return -1;

    pos = 0;

    /* Write base_offset (8 bytes) */
    out[pos++] = (uint8_t)(ms->base_offset & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 8) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 16) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 24) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 32) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 40) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 48) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_offset >> 56) & 0xFF);

    /* batch_length placeholder (4 bytes) - fill later */
    int batch_len_pos = pos;
    pos += 4;

    /* partition_leader_epoch (4 bytes) */
    out[pos++] = (uint8_t)(ms->partition_leader_epoch & 0xFF);
    out[pos++] = (uint8_t)((ms->partition_leader_epoch >> 8) & 0xFF);
    out[pos++] = (uint8_t)((ms->partition_leader_epoch >> 16) & 0xFF);
    out[pos++] = (uint8_t)((ms->partition_leader_epoch >> 24) & 0xFF);

    /* magic (1 byte) */
    out[pos++] = ms->magic;

    /* CRC placeholder (4 bytes) */
    int crc_pos = pos;
    pos += 4;

    /* attributes (2 bytes) */
    out[pos++] = (uint8_t)(ms->attributes & 0xFF);
    out[pos++] = (uint8_t)((ms->attributes >> 8) & 0xFF);

    /* last_offset_delta (4 bytes) */
    out[pos++] = (uint8_t)(ms->last_offset_delta & 0xFF);
    out[pos++] = (uint8_t)((ms->last_offset_delta >> 8) & 0xFF);
    out[pos++] = (uint8_t)((ms->last_offset_delta >> 16) & 0xFF);
    out[pos++] = (uint8_t)((ms->last_offset_delta >> 24) & 0xFF);

    /* first_timestamp (8 bytes) */
    for (i = 0; i < 8; i++) {
        out[pos++] = (uint8_t)((ms->first_timestamp >> (i * 8)) & 0xFF);
    }

    /* max_timestamp (8 bytes) */
    for (i = 0; i < 8; i++) {
        out[pos++] = (uint8_t)((ms->max_timestamp >> (i * 8)) & 0xFF);
    }

    /* producer_id (8 bytes) */
    for (i = 0; i < 8; i++) {
        out[pos++] = (uint8_t)((ms->producer_id >> (i * 8)) & 0xFF);
    }

    /* producer_epoch (2 bytes) */
    out[pos++] = (uint8_t)(ms->producer_epoch & 0xFF);
    out[pos++] = (uint8_t)((ms->producer_epoch >> 8) & 0xFF);

    /* base_sequence (4 bytes) */
    out[pos++] = (uint8_t)(ms->base_sequence & 0xFF);
    out[pos++] = (uint8_t)((ms->base_sequence >> 8) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_sequence >> 16) & 0xFF);
    out[pos++] = (uint8_t)((ms->base_sequence >> 24) & 0xFF);

    /* record_count (4 bytes) */
    out[pos++] = (uint8_t)(ms->record_count & 0xFF);
    out[pos++] = (uint8_t)((ms->record_count >> 8) & 0xFF);
    out[pos++] = (uint8_t)((ms->record_count >> 16) & 0xFF);
    out[pos++] = (uint8_t)((ms->record_count >> 24) & 0xFF);

    /* Records area start */
    int records_start = pos;

    for (i = 0; i < ms->record_count; i++) {
        uint8_t rec_buf[4096];
        if (message_envelope_serialize(&ms->records[i], rec_buf,
                                        (int)sizeof(rec_buf), &rec_len) != 0) {
            return -1;
        }
        if (pos + rec_len > out_max) return -1;
        memcpy(out + pos, rec_buf, rec_len);
        pos += rec_len;
    }

    (void)(pos - records_start); /* records_len calculated for potential use */

    /* Compute batch_length = bytes from after batch_length to end */
    int batch_len = (pos - batch_len_pos - 4);
    out[batch_len_pos]     = (uint8_t)(batch_len & 0xFF);
    out[batch_len_pos + 1] = (uint8_t)((batch_len >> 8) & 0xFF);
    out[batch_len_pos + 2] = (uint8_t)((batch_len >> 16) & 0xFF);
    out[batch_len_pos + 3] = (uint8_t)((batch_len >> 24) & 0xFF);

    /* Compute CRC32C of everything after CRC field */
    int after_crc = crc_pos + 4;
    crc = crc32c_compute(out + after_crc, (size_t)(pos - after_crc));
    out[crc_pos]     = (uint8_t)(crc & 0xFF);
    out[crc_pos + 1] = (uint8_t)((crc >> 8) & 0xFF);
    out[crc_pos + 2] = (uint8_t)((crc >> 16) & 0xFF);
    out[crc_pos + 3] = (uint8_t)((crc >> 24) & 0xFF);

    *out_len = pos;
    return 0;
}

/* Deserialize MessageSet from binary. */
int messageset_deserialize(const uint8_t *in, int in_len,
                            MessageSet *out_ms)
{
    int pos, i, rec_count;
    if (!in || !out_ms || in_len < 61) return -1;

    pos = 0;

    /* base_offset */
    out_ms->base_offset = 0;
    for (i = 0; i < 8; i++) out_ms->base_offset |= ((int64_t)in[pos++]) << (i * 8);

    /* batch_length */
    int32_t batch_len = 0;
    for (i = 0; i < 4; i++) batch_len |= ((int32_t)in[pos++]) << (i * 8);
    (void)batch_len;

    /* partition_leader_epoch */
    out_ms->partition_leader_epoch = 0;
    for (i = 0; i < 4; i++)
        out_ms->partition_leader_epoch |= ((int32_t)in[pos++]) << (i * 8);

    /* magic */
    out_ms->magic = in[pos++];

    /* crc */
    out_ms->crc = 0;
    for (i = 0; i < 4; i++) out_ms->crc |= ((uint32_t)in[pos++]) << (i * 8);

    /* attributes */
    out_ms->attributes = 0;
    for (i = 0; i < 2; i++) out_ms->attributes |= ((uint16_t)in[pos++]) << (i * 8);

    /* last_offset_delta */
    out_ms->last_offset_delta = 0;
    for (i = 0; i < 4; i++)
        out_ms->last_offset_delta |= ((int32_t)in[pos++]) << (i * 8);

    /* first_timestamp */
    out_ms->first_timestamp = 0;
    for (i = 0; i < 8; i++)
        out_ms->first_timestamp |= ((int64_t)in[pos++]) << (i * 8);

    /* max_timestamp */
    out_ms->max_timestamp = 0;
    for (i = 0; i < 8; i++)
        out_ms->max_timestamp |= ((int64_t)in[pos++]) << (i * 8);

    /* producer_id */
    out_ms->producer_id = 0;
    for (i = 0; i < 8; i++)
        out_ms->producer_id |= ((int64_t)in[pos++]) << (i * 8);

    /* producer_epoch */
    out_ms->producer_epoch = 0;
    for (i = 0; i < 2; i++)
        out_ms->producer_epoch |= ((int16_t)in[pos++]) << (i * 8);

    /* base_sequence */
    out_ms->base_sequence = 0;
    for (i = 0; i < 4; i++)
        out_ms->base_sequence |= ((int32_t)in[pos++]) << (i * 8);

    /* record_count */
    rec_count = 0;
    for (i = 0; i < 4; i++) rec_count |= ((int32_t)in[pos++]) << (i * 8);
    out_ms->record_count = rec_count;

    /* Read records */
    if (rec_count > 0) {
        out_ms->records = (MessageEnvelope*)calloc((size_t)rec_count,
                                                    sizeof(MessageEnvelope));
        if (!out_ms->records) return -1;

        for (i = 0; i < rec_count; i++) {
            int bytes_consumed = 0;
            if (message_envelope_deserialize(in + pos, in_len - pos,
                                              &out_ms->records[i],
                                              &bytes_consumed) != 0) {
                return -1;
            }
            pos += bytes_consumed;
        }
    } else {
        out_ms->records = NULL;
    }

    return 0;
}

/* ================================================================
 * L5: Compression Algorithms
 *
 * This module implements simplified versions of common compression
 * algorithms used in message systems.
 *
 * L4: Shannon's Source Coding Theorem - the theoretical limit of
 * lossless compression is the entropy H(X) of the source.
 * No algorithm can compress below H(X) bits per symbol on average.
 *
 * Reference: Shannon 1948 "A Mathematical Theory of Communication"
 * Course: MIT 6.02 (Digital Communication) - Source coding
 * ================================================================ */

/* Simple Run-Length Encoding (RLE) compression.
 * Used as a building block and fallback when data has many repeats.
 * L5: RLE is optimal for data with long runs of identical bytes.
 * O(n) time, O(1) extra space. */
static int compress_rle(const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t out_max, size_t *out_len)
{
    size_t i, opos, run_start;

    if (in_len == 0) { *out_len = 0; return 0; }

    opos = 0;
    i = 0;

    while (i < in_len && opos + 2 <= out_max) {
        run_start = i;
        /* Count run of identical bytes, max 255 */
        while (i < in_len && i - run_start < 255 &&
               in[i] == in[run_start]) {
            i++;
        }

        size_t run_len = i - run_start;
        if (run_len > 1 || i == in_len || opos + 3 > out_max) {
            /* Encode as [count][byte] */
            out[opos++] = (uint8_t)(run_len | 0x80);  /* MSB=1 means RLE run */
            out[opos++] = in[run_start];
        } else {
            /* Literal byte */
            out[opos++] = in[run_start];
            i = run_start + 1;
        }
    }

    *out_len = opos;
    return 0;
}

/* Simple RLE decompression */
static int decompress_rle(const uint8_t *in, size_t in_len,
                           uint8_t *out, size_t out_max, size_t *out_len)
{
    size_t i, opos;

    i = 0;
    opos = 0;

    while (i < in_len) {
        uint8_t byte_val = in[i];

        if (byte_val & 0x80) {
            /* RLE run: next byte is count (lower 7 bits) and value */
            size_t count = byte_val & 0x7F;
            if (count == 0) count = 1;
            i++;
            if (i >= in_len) return -1;

            if (opos + count > out_max) return -1;
            memset(out + opos, in[i], count);
            opos += count;
        } else {
            /* Literal byte */
            if (opos >= out_max) return -1;
            out[opos++] = byte_val;
        }
        i++;
    }

    *out_len = opos;
    return 0;
}

/* L5: Compression dispatcher.
 *
 * For GZIP/Snappy/LZ4/Zstd, we provide simplified implementations.
 * In production, these would use libz, snappy, lz4, zstd libraries.
 * Here we implement RLE-based approximations that demonstrate the
 * algorithm structure while being self-contained. */
int compress_data(CompressionType type, const uint8_t *in, size_t in_len,
                   uint8_t *out, size_t out_max, size_t *out_len)
{
    if (!in || !out || !out_len) return -1;

    switch (type) {
    case COMPRESSION_NONE:
        if (in_len > out_max) return -1;
        memcpy(out, in, in_len);
        *out_len = in_len;
        return 0;

    case COMPRESSION_GZIP:
    case COMPRESSION_SNAPPY:
    case COMPRESSION_LZ4:
    case COMPRESSION_ZSTD:
        /* Use RLE as simplified compression for all codec types.
         * Real implementations would use the specific algorithm. */
        return compress_rle(in, in_len, out, out_max, out_len);

    default:
        return -1;
    }
}

int decompress_data(CompressionType type, const uint8_t *in, size_t in_len,
                     uint8_t *out, size_t out_max, size_t *out_len)
{
    if (!in || !out || !out_len) return -1;

    switch (type) {
    case COMPRESSION_NONE:
        if (in_len > out_max) return -1;
        memcpy(out, in, in_len);
        *out_len = in_len;
        return 0;

    case COMPRESSION_GZIP:
    case COMPRESSION_SNAPPY:
    case COMPRESSION_LZ4:
    case COMPRESSION_ZSTD:
        return decompress_rle(in, in_len, out, out_max, out_len);

    default:
        return -1;
    }
}

/* ================================================================
 * L7: Application Utilities
 * ================================================================ */

const char* compression_type_name(CompressionType type)
{
    switch (type) {
    case COMPRESSION_NONE:   return "none";
    case COMPRESSION_GZIP:   return "gzip";
    case COMPRESSION_SNAPPY: return "snappy";
    case COMPRESSION_LZ4:    return "lz4";
    case COMPRESSION_ZSTD:   return "zstd";
    default:                 return "unknown";
    }
}

CompressionType compression_type_from_name(const char *name)
{
    if (!name) return COMPRESSION_NONE;
    if (strcmp(name, "gzip") == 0)   return COMPRESSION_GZIP;
    if (strcmp(name, "snappy") == 0) return COMPRESSION_SNAPPY;
    if (strcmp(name, "lz4") == 0)    return COMPRESSION_LZ4;
    if (strcmp(name, "zstd") == 0)   return COMPRESSION_ZSTD;
    return COMPRESSION_NONE;
}

/* L4: Compression ratio calculation.
 * ratio = compressed / original
 * ratio < 1.0 means compression saved space
 * ratio > 1.0 means compression expanded data (negative compression) */
double compression_ratio(size_t original, size_t compressed)
{
    if (original == 0) return 1.0;
    return (double)compressed / (double)original;
}

/* L7: Recommend compression type based on entropy estimate.
 *
 * L4: Shannon entropy H(X) = -SUM(p_i * log2(p_i))
 * Low entropy → highly compressible → use stronger compression
 * High entropy → random-looking data → use fast or no compression
 */
const char* compression_recommendation(double entropy_estimate)
{
    if (entropy_estimate < 2.0) {
        return "gzip - data has low entropy, high compression expected";
    } else if (entropy_estimate < 4.0) {
        return "lz4 - moderate entropy, balanced speed/ratio";
    } else if (entropy_estimate < 6.0) {
        return "snappy - higher entropy, prioritize speed";
    } else {
        return "none - high entropy data, compression unlikely to help";
    }
}

/* L8: Zero-copy message validation.
 *
 * Advanced topic: In high-throughput systems, we want to validate
 * message headers without copying data. This function peeks at the
 * first few bytes to determine message validity.
 *
 * Returns 0 if header appears valid, -1 otherwise. */
int message_envelope_validate_header(const uint8_t *data, int len)
{
    if (!data || len < 2) return -1;

    /* Validate magic byte (must be 0, 1, or 2) */
    if (data[0] > 2) {
        return -1;
    }

    /* Attribute byte: high bits should be 0 (reserved) */
    if (data[1] & 0xF0) {
        return -1;
    }

    return 0;
}