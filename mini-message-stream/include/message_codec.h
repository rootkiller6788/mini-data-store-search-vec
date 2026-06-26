#ifndef MESSAGE_CODEC_H
#define MESSAGE_CODEC_H

#include <stdint.h>
#include <stddef.h>

/* === L1: Core Definitions - Message Format === */

#define MAX_COMPRESSED_SIZE    (1024 * 1024)   /* 1 MB max compressed */
#define MAX_VARINT_BYTES       10
#define CRC32C_POLY            0x1EDC6F41      /* Castagnoli polynomial */

/**
 * CompressionType - supported compression algorithms
 * L5: Each type represents a different space/time tradeoff
 */
typedef enum {
    COMPRESSION_NONE   = 0,
    COMPRESSION_GZIP   = 1,
    COMPRESSION_SNAPPY = 2,
    COMPRESSION_LZ4    = 3,
    COMPRESSION_ZSTD   = 4   /* L8: Zstandard - modern compression */
} CompressionType;

/**
 * MessageEnvelope - binary message format
 * L1: Core wire format definition for Kafka-like messages
 *
 * Format: [magic:1B][attrs:1B][timestamp:8B][key_len:varint][key:bytes]
 *         [value_len:varint][value:bytes][headers_len:varint][headers:bytes]
 */
typedef struct {
    uint8_t  magic_byte;
    uint8_t  attributes;       /* bit 0-2: compression type, bit 3: timestamp type */
    int64_t  timestamp;
    int32_t  key_length;
    char*    key;
    int32_t  value_length;
    char*    value;
    int32_t  headers_length;
    char*    headers;
} MessageEnvelope;

/**
 * MessageSet - batch of messages (Kafka's RecordBatch equivalent)
 * L3: Engineering structure - batched messages for throughput optimization
 */
typedef struct {
    int64_t  base_offset;
    int32_t  batch_length;     /* total bytes in this batch */
    int32_t  partition_leader_epoch;
    uint8_t  magic;
    uint32_t crc;              /* CRC32C of everything after this field */
    uint8_t  attributes;
    int32_t  last_offset_delta;
    int64_t  first_timestamp;
    int64_t  max_timestamp;
    int32_t  producer_id;
    int16_t  producer_epoch;
    int32_t  base_sequence;

    int32_t  record_count;
    MessageEnvelope* records;
} MessageSet;

/**
 * VarIntBuffer - buffer for reading/writing variable-length integers
 * L3: Engineering structure for ZigZag + varint encoding
 */
typedef struct {
    uint8_t  buf[MAX_VARINT_BYTES];
    int      pos;
    int      len;
} VarIntBuffer;

/**
 * CRCContext - rolling CRC32C computation context
 * L5: Hardware-accelerated CRC (uses CRC32 instruction on x86 if available)
 */
typedef struct {
    uint32_t crc;
    int64_t  bytes_processed;
} CRCContext;

/* === L1: API Declarations === */

/* L5: Variable-length integer encoding (protobuf varint + ZigZag) */
int         varint_encode_u32(uint8_t *out, uint32_t value);
int         varint_decode_u32(const uint8_t *in, int in_len, uint32_t *out_value);
int         varint_encode_i64(uint8_t *out, int64_t value);
int         varint_decode_i64(const uint8_t *in, int in_len, int64_t *out_value);

/* L5: CRC32C checksum computation (Castagnoli polynomial) */
uint32_t    crc32c_compute(const uint8_t *data, size_t length);
uint32_t    crc32c_append(uint32_t crc, const uint8_t *data, size_t length);
int         crc32c_verify(const uint8_t *data, size_t length, uint32_t expected);

/* Message envelope API */
void        message_envelope_init(MessageEnvelope *env);
void        message_envelope_free(MessageEnvelope *env);
int         message_envelope_serialize(const MessageEnvelope *env,
                                        uint8_t *out, int out_max, int *out_len);
int         message_envelope_deserialize(const uint8_t *in, int in_len,
                                          MessageEnvelope *out_env, int *consumed);

/* L6: MessageSet (batch) operations */
MessageSet* messageset_create(int64_t base_offset);
void        messageset_destroy(MessageSet *ms);
int         messageset_add_record(MessageSet *ms, const MessageEnvelope *env);
int         messageset_serialize(const MessageSet *ms, uint8_t *out,
                                  int out_max, int *out_len);
int         messageset_deserialize(const uint8_t *in, int in_len,
                                    MessageSet *out_ms);

/* L5: Compression */
int         compress_data(CompressionType type, const uint8_t *in, size_t in_len,
                           uint8_t *out, size_t out_max, size_t *out_len);
int         decompress_data(CompressionType type, const uint8_t *in, size_t in_len,
                             uint8_t *out, size_t out_max, size_t *out_len);

/* L7: Application utilities */
const char* compression_type_name(CompressionType type);
CompressionType compression_type_from_name(const char *name);
double      compression_ratio(size_t original, size_t compressed);
const char* compression_recommendation(double entropy_estimate);

/* L8: Zero-copy design principles */
int         message_envelope_validate_header(const uint8_t *data, int len);

#endif /* MESSAGE_CODEC_H */