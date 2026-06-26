#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "wire_protocol.h"

/* ================================================================
 * L3: Protocol Buffer - Binary serialization utility
 *
 * Kafka's wire protocol is a binary protocol with fixed-width
 * integers in big-endian, length-prefixed strings, and variable
 * message bodies.
 *
 * Design principles (from Kafka protocol guide):
 *   - Big-endian (network byte order) for all integers
 *   - Strings: [int16 len][UTF-8 bytes]
 *   - Arrays: [int32 count][elements...]
 *   - Request/Response framing: [int32 total_size][payload...]
 *
 * Reference: Kafka Protocol Guide (kafka.apache.org/protocol)
 * Course: Stanford CS 144 (Networking) - Binary protocol design
 *         CMU 15-441 (Computer Networks) - Application protocols
 * ================================================================ */

/*
 * Host-to-network byte order conversions.
 * On little-endian systems (x86), swap bytes.
 * On big-endian systems, no-op.
 */
static int16_t htons_i16(int16_t v) {
    return (int16_t)(((uint16_t)v >> 8) | ((uint16_t)v << 8));
}
static int32_t htonl_i32(int32_t v) {
    return (int32_t)(((uint32_t)v >> 24) |
                     (((uint32_t)v >> 8) & 0xFF00) |
                     (((uint32_t)v << 8) & 0xFF0000) |
                     ((uint32_t)v << 24));
}
static int64_t htonll_i64(int64_t v) {
    return (int64_t)(((uint64_t)v >> 56) |
                     (((uint64_t)v >> 40) & 0xFF00ULL) |
                     (((uint64_t)v >> 24) & 0xFF0000ULL) |
                     (((uint64_t)v >> 8) & 0xFF000000ULL) |
                     (((uint64_t)v << 8) & 0xFF00000000ULL) |
                     (((uint64_t)v << 24) & 0xFF0000000000ULL) |
                     (((uint64_t)v << 40) & 0xFF000000000000ULL) |
                     ((uint64_t)v << 56));
}
static int16_t ntohs_i16(int16_t v) { return htons_i16(v); }
static int32_t ntohl_i32(int32_t v) { return htonl_i32(v); }
static int64_t ntohll_i64(int64_t v) { return htonll_i64(v); }

ProtocolBuffer* protocol_buffer_create(int capacity)
{
    ProtocolBuffer *pb;
    if (capacity < 1) capacity = 256;
    pb = (ProtocolBuffer*)calloc(1, sizeof(ProtocolBuffer));
    if (!pb) return NULL;
    pb->data = (uint8_t*)calloc((size_t)capacity, 1);
    if (!pb->data) {
        free(pb);
        return NULL;
    }
    pb->capacity = capacity;
    pb->position = 0;
    pb->limit = capacity;
    return pb;
}

void protocol_buffer_destroy(ProtocolBuffer *pb)
{
    if (!pb) return;
    free(pb->data);
    free(pb);
}

/* Write int16 in big-endian.
 * Returns 0 on success, -1 if no space. */
int protocol_buffer_write_i16(ProtocolBuffer *pb, int16_t value)
{
    int16_t net_val;
    if (!pb) return -1;
    if (pb->position + 2 > pb->capacity) return -1;
    net_val = htons_i16(value);
    memcpy(pb->data + pb->position, &net_val, 2);
    pb->position += 2;
    return 0;
}

int protocol_buffer_write_i32(ProtocolBuffer *pb, int32_t value)
{
    int32_t net_val;
    if (!pb) return -1;
    if (pb->position + 4 > pb->capacity) return -1;
    net_val = htonl_i32(value);
    memcpy(pb->data + pb->position, &net_val, 4);
    pb->position += 4;
    return 0;
}

int protocol_buffer_write_i64(ProtocolBuffer *pb, int64_t value)
{
    int64_t net_val;
    if (!pb) return -1;
    if (pb->position + 8 > pb->capacity) return -1;
    net_val = htonll_i64(value);
    memcpy(pb->data + pb->position, &net_val, 8);
    pb->position += 8;
    return 0;
}

/* Write Kafka-style string: [int16 len][UTF-8 bytes]
 * Returns 0 on success, -1 on overflow. */
int protocol_buffer_write_string(ProtocolBuffer *pb, const char *str)
{
    int len;
    if (!pb || !str) return -1;
    len = (int)strlen(str);
    if (len > 32767) len = 32767;
    if (pb->position + 2 + len > pb->capacity) return -1;
    if (protocol_buffer_write_i16(pb, (int16_t)len) != 0) return -1;
    memcpy(pb->data + pb->position, str, (size_t)len);
    pb->position += len;
    return 0;
}

int protocol_buffer_write_bytes(ProtocolBuffer *pb,
                                  const uint8_t *data, int len)
{
    if (!pb || !data) return -1;
    if (pb->position + len > pb->capacity) return -1;
    memcpy(pb->data + pb->position, data, (size_t)len);
    pb->position += len;
    return 0;
}

int protocol_buffer_read_i16(ProtocolBuffer *pb, int16_t *value)
{
    int16_t net_val;
    if (!pb || !value) return -1;
    if (pb->position + 2 > pb->limit) return -1;
    memcpy(&net_val, pb->data + pb->position, 2);
    *value = ntohs_i16(net_val);
    pb->position += 2;
    return 0;
}

int protocol_buffer_read_i32(ProtocolBuffer *pb, int32_t *value)
{
    int32_t net_val;
    if (!pb || !value) return -1;
    if (pb->position + 4 > pb->limit) return -1;
    memcpy(&net_val, pb->data + pb->position, 4);
    *value = ntohl_i32(net_val);
    pb->position += 4;
    return 0;
}

int protocol_buffer_read_i64(ProtocolBuffer *pb, int64_t *value)
{
    int64_t net_val;
    if (!pb || !value) return -1;
    if (pb->position + 8 > pb->limit) return -1;
    memcpy(&net_val, pb->data + pb->position, 8);
    *value = ntohll_i64(net_val);
    pb->position += 8;
    return 0;
}

int protocol_buffer_read_string(ProtocolBuffer *pb,
                                  char *out, int max_len)
{
    int16_t str_len;
    if (!pb || !out) return -1;
    if (protocol_buffer_read_i16(pb, &str_len) != 0) return -1;
    if (str_len >= max_len) str_len = (int16_t)(max_len - 1);
    if (pb->position + str_len > pb->limit) return -1;
    memcpy(out, pb->data + pb->position, (size_t)str_len);
    out[str_len] = '\0';
    pb->position += str_len;
    return 0;
}

/* ================================================================
 * L6: Request Serialization
 *
 * Each request has the format:
 *   [message_size:4B][api_key:2B][api_version:2B][correlation_id:4B]
 *   [client_id:string][request_body...]
 *
 * The message_size is the total size from api_key onwards.
 * ================================================================ */

int produce_request_serialize(const ProduceRequest *req,
                                uint8_t *out, int out_max, int *out_len)
{
    ProtocolBuffer *pb;
    int body_start, total_size;
    (void)body_start;

    if (!req || !out || !out_len || out_max < 50) return -1;

    pb = protocol_buffer_create(out_max);
    if (!pb) return -1;

    /* Skip 4 bytes for message_size (filled at end) */
    pb->position = 4;

    /* Header */
    protocol_buffer_write_i16(pb, req->header.api_key);
    protocol_buffer_write_i16(pb, req->header.api_version);
    protocol_buffer_write_i32(pb, req->header.correlation_id);
    protocol_buffer_write_string(pb, req->header.client_id);

    /* Produce-specific fields */
    protocol_buffer_write_i16(pb, req->required_acks);
    protocol_buffer_write_i32(pb, req->timeout_ms);
    protocol_buffer_write_string(pb, req->topic);
    protocol_buffer_write_i32(pb, req->partition);
    protocol_buffer_write_i32(pb, req->message_count);
    /* In real implementation, message data follows here */

    /* Compute total size (excluding the 4-byte size prefix) */
    total_size = pb->position - 4;

    /* Write message_size at the beginning */
    {
        int32_t net_size = htonl_i32((int32_t)total_size);
        memcpy(pb->data, &net_size, 4);
    }

    /* Copy to output */
    *out_len = pb->position;
    if (*out_len <= out_max) {
        memcpy(out, pb->data, (size_t)*out_len);
    } else {
        protocol_buffer_destroy(pb);
        return -1;
    }

    protocol_buffer_destroy(pb);
    return 0;
}

int produce_response_serialize(const ProduceResponse *resp,
                                uint8_t *out, int out_max, int *out_len)
{
    ProtocolBuffer *pb;
    int total_size;

    if (!resp || !out || !out_len || out_max < 30) return -1;

    pb = protocol_buffer_create(out_max);
    if (!pb) return -1;

    pb->position = 4;

    /* Header */
    protocol_buffer_write_i32(pb, resp->header.correlation_id);

    /* Response body */
    protocol_buffer_write_string(pb, resp->topic);
    protocol_buffer_write_i32(pb, resp->partition);
    protocol_buffer_write_i16(pb, resp->error_code);
    protocol_buffer_write_i64(pb, resp->base_offset);
    protocol_buffer_write_i64(pb, resp->log_append_time_ms);

    total_size = pb->position - 4;
    {
        int32_t net_size = htonl_i32((int32_t)total_size);
        memcpy(pb->data, &net_size, 4);
    }

    *out_len = pb->position;
    if (*out_len <= out_max) {
        memcpy(out, pb->data, (size_t)*out_len);
    } else {
        protocol_buffer_destroy(pb);
        return -1;
    }

    protocol_buffer_destroy(pb);
    return 0;
}

int fetch_request_serialize(const FetchRequest *req,
                             uint8_t *out, int out_max, int *out_len)
{
    ProtocolBuffer *pb;
    int total_size;

    if (!req || !out || !out_len || out_max < 60) return -1;

    pb = protocol_buffer_create(out_max);
    if (!pb) return -1;

    pb->position = 4;

    /* Header */
    protocol_buffer_write_i16(pb, req->header.api_key);
    protocol_buffer_write_i16(pb, req->header.api_version);
    protocol_buffer_write_i32(pb, req->header.correlation_id);
    protocol_buffer_write_string(pb, req->header.client_id);

    /* Fetch-specific fields */
    protocol_buffer_write_i32(pb, req->replica_id);
    protocol_buffer_write_i32(pb, req->max_wait_ms);
    protocol_buffer_write_i32(pb, req->min_bytes);
    protocol_buffer_write_string(pb, req->topic);
    protocol_buffer_write_i32(pb, req->partition);
    protocol_buffer_write_i64(pb, req->fetch_offset);
    protocol_buffer_write_i32(pb, req->max_bytes);

    total_size = pb->position - 4;
    {
        int32_t net_size = htonl_i32((int32_t)total_size);
        memcpy(pb->data, &net_size, 4);
    }

    *out_len = pb->position;
    if (*out_len <= out_max) {
        memcpy(out, pb->data, (size_t)*out_len);
    } else {
        protocol_buffer_destroy(pb);
        return -1;
    }

    protocol_buffer_destroy(pb);
    return 0;
}

int fetch_response_serialize(const FetchResponse *resp,
                              uint8_t *out, int out_max, int *out_len)
{
    ProtocolBuffer *pb;
    int total_size;

    if (!resp || !out || !out_len || out_max < 30) return -1;

    pb = protocol_buffer_create(out_max);
    if (!pb) return -1;

    pb->position = 4;

    protocol_buffer_write_i32(pb, resp->header.correlation_id);
    protocol_buffer_write_string(pb, resp->topic);
    protocol_buffer_write_i32(pb, resp->partition);
    protocol_buffer_write_i16(pb, resp->error_code);
    protocol_buffer_write_i64(pb, resp->high_watermark);
    protocol_buffer_write_i32(pb, resp->record_count);

    total_size = pb->position - 4;
    {
        int32_t net_size = htonl_i32((int32_t)total_size);
        memcpy(pb->data, &net_size, 4);
    }

    *out_len = pb->position;
    if (*out_len <= out_max) {
        memcpy(out, pb->data, (size_t)*out_len);
    } else {
        protocol_buffer_destroy(pb);
        return -1;
    }

    protocol_buffer_destroy(pb);
    return 0;
}

/* Serialize OffsetCommitRequest.
 * L6: The OffsetCommit RPC is critical for consumer group progress tracking.
 * Format: [group_id][topic][partition][offset][timestamp][metadata] */
int offset_commit_request_serialize(const OffsetCommitRequest *req,
                                      uint8_t *out, int out_max, int *out_len)
{
    ProtocolBuffer *pb;
    int total_size;

    if (!req || !out || !out_len || out_max < 80) return -1;

    pb = protocol_buffer_create(out_max);
    if (!pb) return -1;

    pb->position = 4;

    protocol_buffer_write_i16(pb, req->header.api_key);
    protocol_buffer_write_i16(pb, req->header.api_version);
    protocol_buffer_write_i32(pb, req->header.correlation_id);
    protocol_buffer_write_string(pb, req->header.client_id);

    protocol_buffer_write_string(pb, req->group_id);
    protocol_buffer_write_string(pb, req->topic);
    protocol_buffer_write_i32(pb, req->partition);
    protocol_buffer_write_i64(pb, req->committed_offset);
    protocol_buffer_write_i64(pb, req->commit_timestamp);
    protocol_buffer_write_string(pb, req->metadata);

    total_size = pb->position - 4;
    {
        int32_t net_size = htonl_i32((int32_t)total_size);
        memcpy(pb->data, &net_size, 4);
    }

    *out_len = pb->position;
    if (*out_len <= out_max) {
        memcpy(out, pb->data, (size_t)*out_len);
    } else {
        protocol_buffer_destroy(pb);
        return -1;
    }

    protocol_buffer_destroy(pb);
    return 0;
}

int offset_fetch_response_serialize(const OffsetFetchResponse *resp,
                                      uint8_t *out, int out_max, int *out_len)
{
    ProtocolBuffer *pb;
    int total_size;

    if (!resp || !out || !out_len || out_max < 40) return -1;

    pb = protocol_buffer_create(out_max);
    if (!pb) return -1;

    pb->position = 4;

    protocol_buffer_write_i32(pb, resp->header.correlation_id);
    protocol_buffer_write_string(pb, resp->topic);
    protocol_buffer_write_i32(pb, resp->partition);
    protocol_buffer_write_i64(pb, resp->offset);
    protocol_buffer_write_i16(pb, resp->error_code);

    total_size = pb->position - 4;
    {
        int32_t net_size = htonl_i32((int32_t)total_size);
        memcpy(pb->data, &net_size, 4);
    }

    *out_len = pb->position;
    if (*out_len <= out_max) {
        memcpy(out, pb->data, (size_t)*out_len);
    } else {
        protocol_buffer_destroy(pb);
        return -1;
    }

    protocol_buffer_destroy(pb);
    return 0;
}

/* ================================================================
 * L7: API Version Negotiation
 *
 * Kafka's protocol supports versioning: each API key can have
 * multiple versions. Clients discover supported versions via
 * ApiVersionsRequest (API key 18).
 *
 * This is a real-world example of protocol evolution:
 * - v0: basic produce with no timestamp
 * - v2: added timestamp field
 * - v3: added transactional ID
 * - v7: added record batch format v2 (KIP-98)
 * - v9: added flexible versions (KIP-482)
 *
 * Reference: Kafka KIP-35 (ApiVersionRequest), KIP-97 (Kafka protocol)
 * ================================================================ */

int wire_api_versions_supported(ApiKey key, int16_t version)
{
    switch (key) {
    case API_KEY_PRODUCE:
        return (version >= 0 && version <= 7) ? 1 : 0;
    case API_KEY_FETCH:
        return (version >= 0 && version <= 7) ? 1 : 0;
    case API_KEY_METADATA:
        return (version >= 0 && version <= 5) ? 1 : 0;
    case API_KEY_OFFSET_COMMIT:
        return (version >= 0 && version <= 5) ? 1 : 0;
    case API_KEY_OFFSET_FETCH:
        return (version >= 0 && version <= 3) ? 1 : 0;
    case API_KEY_FIND_COORDINATOR:
        return (version >= 0 && version <= 2) ? 1 : 0;
    case API_KEY_JOIN_GROUP:
        return (version >= 0 && version <= 4) ? 1 : 0;
    case API_KEY_HEARTBEAT:
        return (version >= 0 && version <= 3) ? 1 : 0;
    case API_KEY_LEAVE_GROUP:
        return (version >= 0 && version <= 2) ? 1 : 0;
    case API_KEY_SYNC_GROUP:
        return (version >= 0 && version <= 3) ? 1 : 0;
    case API_KEY_API_VERSIONS:
        return (version >= 0 && version <= 2) ? 1 : 0;
    case API_KEY_CREATE_TOPICS:
        return (version >= 0 && version <= 3) ? 1 : 0;
    case API_KEY_DELETE_TOPICS:
        return (version >= 0 && version <= 2) ? 1 : 0;
    default:
        return 0;
    }
}

int16_t wire_get_max_supported_version(ApiKey key)
{
    switch (key) {
    case API_KEY_PRODUCE:           return 7;
    case API_KEY_FETCH:             return 7;
    case API_KEY_METADATA:          return 5;
    case API_KEY_OFFSET_COMMIT:     return 5;
    case API_KEY_OFFSET_FETCH:      return 3;
    case API_KEY_FIND_COORDINATOR:  return 2;
    case API_KEY_JOIN_GROUP:        return 4;
    case API_KEY_HEARTBEAT:         return 3;
    case API_KEY_LEAVE_GROUP:       return 2;
    case API_KEY_SYNC_GROUP:        return 3;
    case API_KEY_API_VERSIONS:      return 2;
    case API_KEY_CREATE_TOPICS:     return 3;
    case API_KEY_DELETE_TOPICS:     return 2;
    default:                        return -1;
    }
}

/* Map error codes to human-readable strings.
 * L7: Used by Kafka clients for error reporting. */
const char* wire_error_string(ProtocolError error)
{
    switch (error) {
    case ERROR_NONE:                       return "No error";
    case ERROR_UNKNOWN:                    return "Unknown server error";
    case ERROR_OFFSET_OUT_OF_RANGE:        return "Offset out of range";
    case ERROR_CORRUPT_MESSAGE:            return "Corrupt message";
    case ERROR_UNKNOWN_TOPIC_OR_PART:      return "Unknown topic or partition";
    case ERROR_LEADER_NOT_AVAILABLE:       return "Leader not available";
    case ERROR_NOT_LEADER_FOR_PARTITION:   return "Not leader for partition";
    case ERROR_REQUEST_TIMED_OUT:          return "Request timed out";
    case ERROR_BROKER_NOT_AVAILABLE:       return "Broker not available";
    case ERROR_MESSAGE_TOO_LARGE:          return "Message too large";
    case ERROR_NETWORK_EXCEPTION:          return "Network exception";
    case ERROR_NOT_ENOUGH_REPLICAS:        return "Not enough replicas";
    case ERROR_ILLEGAL_GENERATION:         return "Illegal generation";
    case ERROR_UNKNOWN_MEMBER_ID:          return "Unknown member ID";
    case ERROR_REBALANCE_IN_PROGRESS:      return "Rebalance in progress";
    case ERROR_UNSUPPORTED_VERSION:        return "Unsupported version";
    case ERROR_TOPIC_ALREADY_EXISTS:       return "Topic already exists";
    case ERROR_INVALID_PARTITIONS:         return "Invalid partitions";
    default:                               return "Unknown error code";
    }
}

/* ================================================================
 * L8: Request Validation
 *
 * Validate raw bytes as a Kafka request header before full parsing.
 * This is a defense-in-depth measure for protocol security.
 *
 * Checks:
 *   - Minimum length (>= 12 bytes for header)
 *   - Message size <= max request size
 *   - API key in known range
 *   - Client ID is non-empty
 * ================================================================ */

int wire_validate_request_header(const uint8_t *data, int len)
{
    int32_t msg_size, correlation_id;
    int16_t api_key;
    uint8_t client_id_len;

    if (!data || len < 12) {
        printf("wire: request too short (%d bytes, min 12)\n", len);
        return -1;
    }

    /* Parse message_size (4 bytes, big-endian) */
    msg_size = (int32_t)(((uint32_t)data[0] << 24) |
                         ((uint32_t)data[1] << 16) |
                         ((uint32_t)data[2] << 8) |
                         (uint32_t)data[3]);

    if (msg_size < 0 || msg_size > WIRE_MAX_REQUEST_SIZE) {
        printf("wire: invalid message size %d\n", msg_size);
        return -1;
    }

    if (len < msg_size + 4) {
        printf("wire: truncated request (have %d, need %d)\n", len, msg_size + 4);
        return -1;
    }

    /* API key (2 bytes) at offset 4 */
    api_key = (int16_t)(((uint16_t)data[4] << 8) | (uint16_t)data[5]);
    if (api_key < 0 || api_key > 50) {
        printf("wire: unknown API key %d\n", api_key);
        return -1;
    }

    /* Correlation ID (4 bytes) at offset 8 */
    correlation_id = (int32_t)(((uint32_t)data[8] << 24) |
                               ((uint32_t)data[9] << 16) |
                               ((uint32_t)data[10] << 8) |
                               (uint32_t)data[11]);
    if (correlation_id < 0) {
        printf("wire: invalid correlation ID %d\n", correlation_id);
        return -1;
    }

    /* Client ID string length should be reasonable */
    if (len > 14) {
        client_id_len = data[12];
        if ((int)client_id_len + 14 > len) {
            printf("wire: client ID too long (%d)\n", client_id_len);
            return -1;
        }
    }

    return 0;
}

/* L8: Estimate response buffer size based on API and record count.
 *
 * This is used for buffer pre-allocation to avoid reallocations.
 * Accurate estimates prevent buffer overflow and fragmentation. */
int wire_estimate_response_size(ApiKey key, int record_count)
{
    int base;

    switch (key) {
    case API_KEY_PRODUCE:
        base = 60;  /* Header + topic + partition + offset + timestamp */
        break;
    case API_KEY_FETCH:
        base = 60;  /* Header + topic + partition + HW + record_count */
        /* Each record header: ~30 bytes + avg record size (~256 bytes) */
        base += record_count * (30 + 256);
        break;
    case API_KEY_METADATA:
        base = 80;  /* Broker list + topic list */
        break;
    case API_KEY_OFFSET_COMMIT:
        base = 40;
        break;
    case API_KEY_OFFSET_FETCH:
        base = 50;
        break;
    case API_KEY_FIND_COORDINATOR:
        base = 40;
        break;
    case API_KEY_JOIN_GROUP:
        base = 200;  /* Member list */
        break;
    case API_KEY_HEARTBEAT:
        base = 20;
        break;
    case API_KEY_SYNC_GROUP:
        base = 200;  /* Assignment data */
        break;
    default:
        base = 100;
    }

    if (base > WIRE_MAX_RESPONSE_SIZE) base = WIRE_MAX_RESPONSE_SIZE;
    return base;
}