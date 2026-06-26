#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include "message_codec.h"

static int test_varint_encode_decode_u32(void) {
    uint8_t buf[10];
    uint32_t decoded;
    int n;

    /* Small value (1 byte) */
    n = varint_encode_u32(buf, 5);
    assert(n == 1);
    assert(buf[0] == 5);
    assert(varint_decode_u32(buf, 10, &decoded) == 1);
    assert(decoded == 5);

    /* 2-byte value */
    n = varint_encode_u32(buf, 300);
    assert(n == 2);
    assert(varint_decode_u32(buf, 10, &decoded) == 2);
    assert(decoded == 300);

    /* Max uint32 */
    uint8_t big_buf[10];
    n = varint_encode_u32(big_buf, 0xFFFFFFFF);
    assert(n > 0 && n <= 5);
    assert(varint_decode_u32(big_buf, 10, &decoded) > 0);
    assert(decoded == 0xFFFFFFFF);

    return 0;
}

static int test_varint_encode_decode_i64(void) {
    uint8_t buf[10];
    int64_t decoded;
    int n;

    /* Positive value */
    n = varint_encode_i64(buf, 100);
    assert(n > 0);
    assert(varint_decode_i64(buf, 10, &decoded) > 0);
    assert(decoded == 100);

    /* Negative value (ZigZag) */
    n = varint_encode_i64(buf, -100);
    assert(n > 0);
    assert(varint_decode_i64(buf, 10, &decoded) > 0);
    assert(decoded == -100);

    /* Zero */
    n = varint_encode_i64(buf, 0);
    assert(n > 0);
    assert(varint_decode_i64(buf, 10, &decoded) > 0);
    assert(decoded == 0);

    return 0;
}

static int test_crc32c(void) {
    const char *test_str = "hello world";
    uint32_t crc1 = crc32c_compute((const uint8_t*)test_str, strlen(test_str));
    uint32_t crc2 = crc32c_compute((const uint8_t*)test_str, strlen(test_str));
    assert(crc1 == crc2);
    assert(crc1 != 0);

    /* Different data -> different CRC */
    uint32_t crc3 = crc32c_compute((const uint8_t*)"hello world!", 12);
    assert(crc3 != crc1);

    /* CRC verification */
    assert(crc32c_verify((const uint8_t*)test_str, strlen(test_str), crc1) == 0);
    assert(crc32c_verify((const uint8_t*)test_str, strlen(test_str), crc1 + 1) == -1);

    /* Append CRC */
    uint32_t crc_a = crc32c_append(0, (const uint8_t*)"hello ", 6);
    uint32_t crc_b = crc32c_append(crc_a, (const uint8_t*)"world", 5);
    uint32_t crc_full = crc32c_compute((const uint8_t*)"hello world", 11);
    assert(crc_b == crc_full);

    return 0;
}

static int test_message_envelope(void) {
    MessageEnvelope env;
    message_envelope_init(&env);
    assert(env.magic_byte == 2);

    /* Set fields */
    env.timestamp = 1234567890;
    env.key = strdup("test-key");
    env.key_length = (int32_t)strlen(env.key);
    env.value = strdup("test-value");
    env.value_length = (int32_t)strlen(env.value);
    env.headers = strdup("hdr");
    env.headers_length = (int32_t)strlen(env.headers);

    /* Serialize */
    uint8_t buf[512];
    int ser_len;
    assert(message_envelope_serialize(&env, buf, sizeof(buf), &ser_len) == 0);
    assert(ser_len > 14);

    /* Deserialize */
    MessageEnvelope env2;
    int consumed_bytes = 0;
    assert(message_envelope_deserialize(buf, ser_len, &env2, &consumed_bytes) == 0);
    assert(consumed_bytes > 0);
    assert(env2.magic_byte == 2);
    assert(env2.timestamp == 1234567890);
    assert(env2.key != NULL);
    assert(strcmp(env2.key, "test-key") == 0);
    assert(strcmp(env2.value, "test-value") == 0);

    message_envelope_free(&env);
    message_envelope_free(&env2);
    return 0;
}

static int test_message_envelope_null(void) {
    MessageEnvelope env;
    message_envelope_init(&env);

    uint8_t buf[64];
    int len;
    /* NULL key and value */
    assert(message_envelope_serialize(&env, buf, sizeof(buf), &len) == 0);

    MessageEnvelope env2;
    int consumed2 = 0;
    assert(message_envelope_deserialize(buf, len, &env2, &consumed2) == 0);
    assert(env2.key_length == 0);
    assert(env2.value_length == 0);

    message_envelope_free(&env);
    message_envelope_free(&env2);
    return 0;
}

static int test_messageset(void) {
    MessageSet *ms = messageset_create(0);
    assert(ms != NULL);
    assert(ms->base_offset == 0);
    assert(ms->record_count == 0);

    MessageEnvelope env1, env2;
    message_envelope_init(&env1);
    env1.timestamp = 1000;
    env1.key = strdup("k1");
    env1.key_length = 2;
    env1.value = strdup("v1");
    env1.value_length = 2;

    message_envelope_init(&env2);
    env2.timestamp = 2000;
    env2.key = strdup("k2");
    env2.key_length = 2;
    env2.value = strdup("v2");
    env2.value_length = 2;

    assert(messageset_add_record(ms, &env1) == 0);
    assert(messageset_add_record(ms, &env2) == 0);
    assert(ms->record_count == 2);
    assert(ms->first_timestamp == 1000);
    assert(ms->max_timestamp == 2000);

    /* Serialize */
    uint8_t buf[4096];
    int ser_len;
    assert(messageset_serialize(ms, buf, (int)sizeof(buf), &ser_len) == 0);
    assert(ser_len > 61);

    /* Deserialize */
    MessageSet ms2;
    memset(&ms2, 0, sizeof(ms2));
    assert(messageset_deserialize(buf, ser_len, &ms2) == 0);
    assert(ms2.base_offset == 0);
    assert(ms2.record_count == 2);

    messageset_destroy(ms);
    messageset_destroy(&ms2);
    message_envelope_free(&env1);
    message_envelope_free(&env2);
    return 0;
}

static int test_compression(void) {
    uint8_t input[256], compressed[512], decompressed[256];
    size_t comp_len, decomp_len;
    int i;

    /* Create data with repeating pattern (good for RLE) */
    for (i = 0; i < 256; i++) input[i] = (uint8_t)(i / 8);

    /* Test each compression type */
    CompressionType types[] = { COMPRESSION_NONE, COMPRESSION_GZIP,
                                 COMPRESSION_LZ4, COMPRESSION_SNAPPY,
                                 COMPRESSION_ZSTD };
    int num_types = (int)(sizeof(types) / sizeof(types[0]));
    int j;
    for (j = 0; j < num_types; j++) {
        memset(compressed, 0, sizeof(compressed));
        memset(decompressed, 0, sizeof(decompressed));

        assert(compress_data(types[j], input, 256, compressed,
               sizeof(compressed), &comp_len) == 0);
        assert(decompress_data(types[j], compressed, comp_len,
               decompressed, sizeof(decompressed), &decomp_len) == 0);
        assert(decomp_len == 256);
        assert(memcmp(input, decompressed, 256) == 0);
    }

    /* Compression ratio */
    double ratio = compression_ratio(256, comp_len);
    assert(ratio > 0.0);

    /* NULL safety */
    assert(compress_data(COMPRESSION_NONE, NULL, 0, compressed,
           sizeof(compressed), &comp_len) == -1);

    return 0;
}

static int test_compression_utilities(void) {
    assert(strcmp(compression_type_name(COMPRESSION_NONE), "none") == 0);
    assert(strcmp(compression_type_name(COMPRESSION_LZ4), "lz4") == 0);
    assert(strcmp(compression_type_name(COMPRESSION_ZSTD), "zstd") == 0);

    assert(compression_type_from_name("gzip") == COMPRESSION_GZIP);
    assert(compression_type_from_name("lz4") == COMPRESSION_LZ4);
    assert(compression_type_from_name("unknown") == COMPRESSION_NONE);
    assert(compression_type_from_name(NULL) == COMPRESSION_NONE);

    /* Entropy-based recommendation */
    const char *rec = compression_recommendation(1.0);
    assert(rec != NULL && strstr(rec, "gzip") != NULL);

    rec = compression_recommendation(7.0);
    assert(rec != NULL && strstr(rec, "none") != NULL);

    return 0;
}

static int test_header_validation(void) {
    uint8_t valid_hdr[] = { 2, 0x00 };     /* Magic=2, attrs=0 */
    uint8_t bad_magic[] = { 5, 0x00 };     /* Bad magic */
    uint8_t bad_attrs[] = { 2, 0xF0 };     /* Reserved bits set */

    assert(message_envelope_validate_header(valid_hdr, 2) == 0);
    assert(message_envelope_validate_header(bad_magic, 2) == -1);
    assert(message_envelope_validate_header(bad_attrs, 2) == -1);
    assert(message_envelope_validate_header(NULL, 0) == -1);
    assert(message_envelope_validate_header(valid_hdr, 1) == -1);

    return 0;
}

int main(void) {
    printf("=== Running Message Codec Tests ===\n");
    test_varint_encode_decode_u32();
    test_varint_encode_decode_i64();
    test_crc32c();
    test_message_envelope();
    test_message_envelope_null();
    test_messageset();
    test_compression();
    test_compression_utilities();
    test_header_validation();
    printf("All message codec tests passed!\n");
    return 0;
}