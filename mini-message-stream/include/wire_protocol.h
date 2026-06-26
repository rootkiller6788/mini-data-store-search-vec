#ifndef WIRE_PROTOCOL_H
#define WIRE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* === L1: Core Definitions - Kafka Wire Protocol === */

#define WIRE_MAX_REQUEST_SIZE   (1024 * 1024)  /* 1 MB */
#define WIRE_MAX_RESPONSE_SIZE  (1024 * 1024)

/* API Keys from Kafka protocol */
typedef enum {
    API_KEY_PRODUCE           = 0,
    API_KEY_FETCH             = 1,
    API_KEY_LIST_OFFSETS      = 2,
    API_KEY_METADATA          = 3,
    API_KEY_OFFSET_COMMIT     = 8,
    API_KEY_OFFSET_FETCH      = 9,
    API_KEY_FIND_COORDINATOR  = 10,
    API_KEY_JOIN_GROUP        = 11,
    API_KEY_HEARTBEAT         = 12,
    API_KEY_LEAVE_GROUP       = 13,
    API_KEY_SYNC_GROUP        = 14,
    API_KEY_DESCRIBE_GROUPS   = 15,
    API_KEY_LIST_GROUPS       = 16,
    API_KEY_API_VERSIONS      = 18,
    API_KEY_CREATE_TOPICS     = 19,
    API_KEY_DELETE_TOPICS     = 20,
} ApiKey;

/* Error codes */
typedef enum {
    ERROR_NONE                     = 0,
    ERROR_UNKNOWN                  = -1,
    ERROR_OFFSET_OUT_OF_RANGE      = 1,
    ERROR_CORRUPT_MESSAGE          = 2,
    ERROR_UNKNOWN_TOPIC_OR_PART    = 3,
    ERROR_INVALID_FETCH_SIZE       = 4,
    ERROR_LEADER_NOT_AVAILABLE     = 5,
    ERROR_NOT_LEADER_FOR_PARTITION = 6,
    ERROR_REQUEST_TIMED_OUT        = 7,
    ERROR_BROKER_NOT_AVAILABLE     = 8,
    ERROR_REPLICA_NOT_AVAILABLE    = 9,
    ERROR_MESSAGE_TOO_LARGE        = 10,
    ERROR_STALE_CONTROLLER_EPOCH   = 11,
    ERROR_OFFSET_METADATA_TOO_LARGE = 12,
    ERROR_NETWORK_EXCEPTION        = 13,
    ERROR_COORDINATOR_LOADING      = 14,
    ERROR_COORDINATOR_NOT_AVAILABLE = 15,
    ERROR_NOT_COORDINATOR          = 16,
    ERROR_INVALID_TOPIC_EXCEPTION  = 17,
    ERROR_RECORD_LIST_TOO_LARGE    = 18,
    ERROR_NOT_ENOUGH_REPLICAS      = 19,
    ERROR_NOT_ENOUGH_REPLICAS_AFTER_APPEND = 20,
    ERROR_INVALID_REQUIRED_ACKS    = 21,
    ERROR_ILLEGAL_GENERATION       = 22,
    ERROR_INCONSISTENT_GROUP_PROTOCOL = 23,
    ERROR_INVALID_GROUP_ID         = 24,
    ERROR_UNKNOWN_MEMBER_ID        = 25,
    ERROR_INVALID_SESSION_TIMEOUT  = 26,
    ERROR_REBALANCE_IN_PROGRESS    = 27,
    ERROR_INVALID_COMMIT_OFFSET_SIZE = 28,
    ERROR_TOPIC_AUTHORIZATION_FAILED = 29,
    ERROR_GROUP_AUTHORIZATION_FAILED = 30,
    ERROR_CLUSTER_AUTHORIZATION_FAILED = 31,
    ERROR_UNSUPPORTED_VERSION      = 35,
    ERROR_TOPIC_ALREADY_EXISTS     = 36,
    ERROR_INVALID_PARTITIONS       = 37,
} ProtocolError;

/*
 * RequestHeader - common header for all Kafka requests
 * L1: Core wire format definition
 *
 * Format: [api_key:2B][api_version:2B][correlation_id:4B][client_id:str]
 */
typedef struct {
    int16_t  api_key;
    int16_t  api_version;
    int32_t  correlation_id;
    char     client_id[128];
} RequestHeader;

/*
 * ResponseHeader - common header for all Kafka responses
 */
typedef struct {
    int32_t  correlation_id;
} ResponseHeader;

/*
 * ProduceRequest - produce message batch to topic/partition
 */
typedef struct {
    RequestHeader header;
    int16_t       required_acks;
    int32_t       timeout_ms;
    char          topic[128];
    int32_t       partition;
    int32_t       message_count;
    /* Message data follows */
} ProduceRequest;

/*
 * ProduceResponse - response to produce request
 */
typedef struct {
    ResponseHeader header;
    char           topic[128];
    int32_t        partition;
    int16_t        error_code;
    int64_t        base_offset;
    int64_t        log_append_time_ms;
} ProduceResponse;

/*
 * FetchRequest - fetch messages from a topic/partition
 */
typedef struct {
    RequestHeader header;
    int32_t       replica_id;
    int32_t       max_wait_ms;
    int32_t       min_bytes;
    char          topic[128];
    int32_t       partition;
    int64_t       fetch_offset;
    int32_t       max_bytes;
} FetchRequest;

/*
 * FetchResponse - response to fetch request
 */
typedef struct {
    ResponseHeader header;
    char           topic[128];
    int32_t        partition;
    int16_t        error_code;
    int64_t        high_watermark;
    int32_t        record_count;
    /* Records follow */
} FetchResponse;

/*
 * MetadataRequest - request cluster/topic metadata
 */
typedef struct {
    RequestHeader header;
    int32_t       topic_count;
    char          topics[16][128];   /* Topic names to describe */
} MetadataRequest;

/*
 * MetadataResponse - cluster/topic metadata
 */
typedef struct {
    ResponseHeader header;
    int32_t        broker_count;
    int32_t        topic_count;
    /* Broker and topic metadata follows */
} MetadataResponse;

/*
 * OffsetCommitRequest - commit consumer offsets
 */
typedef struct {
    RequestHeader header;
    char          group_id[64];
    char          topic[128];
    int32_t       partition;
    int64_t        committed_offset;
    int64_t        commit_timestamp;
    char          metadata[256];
} OffsetCommitRequest;

/*
 * OffsetFetchRequest - fetch committed offsets for a consumer group
 */
typedef struct {
    RequestHeader header;
    char          group_id[64];
    char          topic[128];
    int32_t       partition;
} OffsetFetchRequest;

/*
 * OffsetFetchResponse - offset fetch response
 */
typedef struct {
    ResponseHeader header;
    char           topic[128];
    int32_t        partition;
    int64_t         offset;
    int16_t         error_code;
} OffsetFetchResponse;

/*
 * JoinGroupRequest - consumer joins a group
 */
typedef struct {
    RequestHeader header;
    char          group_id[64];
    int32_t       session_timeout_ms;
    char          member_id[64];
    char          protocol_type[32];
} JoinGroupRequest;

/*
 * SyncGroupRequest - consumer syncs partition assignments
 */
typedef struct {
    RequestHeader header;
    char          group_id[64];
    int32_t       generation_id;
    char          member_id[64];
} SyncGroupRequest;

/*
 * HeartbeatRequest - consumer heartbeat
 */
typedef struct {
    RequestHeader header;
    char          group_id[64];
    int32_t       generation_id;
    char          member_id[64];
} HeartbeatRequest;

/*
 * ProtocolBuffer - structured buffer for serialization
 * L3: Engineering structure for building/parsing protocol messages
 */
typedef struct {
    uint8_t*  data;
    int       capacity;
    int       position;
    int       limit;
} ProtocolBuffer;

/* === L1: API Declarations === */

/* L5: Protocol buffer management */
ProtocolBuffer* protocol_buffer_create(int capacity);
void            protocol_buffer_destroy(ProtocolBuffer *pb);
int             protocol_buffer_write_i16(ProtocolBuffer *pb, int16_t value);
int             protocol_buffer_write_i32(ProtocolBuffer *pb, int32_t value);
int             protocol_buffer_write_i64(ProtocolBuffer *pb, int64_t value);
int             protocol_buffer_write_string(ProtocolBuffer *pb, const char *str);
int             protocol_buffer_write_bytes(ProtocolBuffer *pb,
                                              const uint8_t *data, int len);
int             protocol_buffer_read_i16(ProtocolBuffer *pb, int16_t *value);
int             protocol_buffer_read_i32(ProtocolBuffer *pb, int32_t *value);
int             protocol_buffer_read_i64(ProtocolBuffer *pb, int64_t *value);
int             protocol_buffer_read_string(ProtocolBuffer *pb,
                                              char *out, int max_len);

/* L6: Request/Response serialization */
int             produce_request_serialize(const ProduceRequest *req,
                                            uint8_t *out, int out_max, int *out_len);
int             produce_response_serialize(const ProduceResponse *resp,
                                            uint8_t *out, int out_max, int *out_len);
int             fetch_request_serialize(const FetchRequest *req,
                                         uint8_t *out, int out_max, int *out_len);
int             fetch_response_serialize(const FetchResponse *resp,
                                          uint8_t *out, int out_max, int *out_len);
int             offset_commit_request_serialize(const OffsetCommitRequest *req,
                                                  uint8_t *out, int out_max,
                                                  int *out_len);
int             offset_fetch_response_serialize(const OffsetFetchResponse *resp,
                                                  uint8_t *out, int out_max,
                                                  int *out_len);

/* L7: API version negotiation */
int             wire_api_versions_supported(ApiKey key, int16_t version);
int16_t         wire_get_max_supported_version(ApiKey key);
const char*     wire_error_string(ProtocolError error);

/* L8: Request size limits and validation */
int             wire_validate_request_header(const uint8_t *data, int len);
int             wire_estimate_response_size(ApiKey key, int record_count);

#endif /* WIRE_PROTOCOL_H */