#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "wire_protocol.h"

static int test_buffer_create(void) {
    ProtocolBuffer *pb = protocol_buffer_create(256);
    assert(pb != NULL);
    assert(pb->capacity == 256);
    assert(pb->position == 0);
    protocol_buffer_destroy(pb);
    return 0;
}

static int test_buffer_write_read_i16(void) {
    ProtocolBuffer *pb = protocol_buffer_create(256);
    assert(pb != NULL);

    assert(protocol_buffer_write_i16(pb, 42) == 0);
    assert(pb->position == 2);

    int16_t val;
    /* Reset position for read */
    int saved_pos = pb->position;
    pb->position = 0;
    assert(protocol_buffer_read_i16(pb, &val) == 0);
    assert(val == 42);

    pb->position = saved_pos;
    protocol_buffer_destroy(pb);
    return 0;
}

static int test_buffer_write_read_i32(void) {
    ProtocolBuffer *pb = protocol_buffer_create(256);
    assert(pb != NULL);

    assert(protocol_buffer_write_i32(pb, 1234567) == 0);
    assert(pb->position == 4);

    pb->position = 0;
    int32_t val;
    assert(protocol_buffer_read_i32(pb, &val) == 0);
    assert(val == 1234567);

    protocol_buffer_destroy(pb);
    return 0;
}

static int test_buffer_write_read_i64(void) {
    ProtocolBuffer *pb = protocol_buffer_create(256);
    assert(pb != NULL);

    assert(protocol_buffer_write_i64(pb, 9876543210LL) == 0);
    assert(pb->position == 8);

    pb->position = 0;
    int64_t val;
    assert(protocol_buffer_read_i64(pb, &val) == 0);
    assert(val == 9876543210LL);

    protocol_buffer_destroy(pb);
    return 0;
}

static int test_buffer_write_read_string(void) {
    ProtocolBuffer *pb = protocol_buffer_create(256);
    assert(pb != NULL);

    assert(protocol_buffer_write_string(pb, "hello") == 0);

    pb->position = 0;
    char buf[64];
    assert(protocol_buffer_read_string(pb, buf, sizeof(buf)) == 0);
    assert(strcmp(buf, "hello") == 0);

    protocol_buffer_destroy(pb);
    return 0;
}

static int test_produce_request_serialize(void) {
    ProduceRequest req;
    memset(&req, 0, sizeof(req));
    req.header.api_key = API_KEY_PRODUCE;
    req.header.api_version = 3;
    req.header.correlation_id = 100;
    strncpy(req.header.client_id, "test-client", sizeof(req.header.client_id) - 1);
    req.required_acks = 1;
    req.timeout_ms = 3000;
    strncpy(req.topic, "my-topic", sizeof(req.topic) - 1);
    req.partition = 0;
    req.message_count = 5;

    uint8_t buf[512];
    int len;
    assert(produce_request_serialize(&req, buf, (int)sizeof(buf), &len) == 0);
    assert(len > 4);

    return 0;
}

static int test_produce_response_serialize(void) {
    ProduceResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.correlation_id = 100;
    strncpy(resp.topic, "my-topic", sizeof(resp.topic) - 1);
    resp.partition = 0;
    resp.error_code = ERROR_NONE;
    resp.base_offset = 42;
    resp.log_append_time_ms = 1234567890;

    uint8_t buf[512];
    int len;
    assert(produce_response_serialize(&resp, buf, (int)sizeof(buf), &len) == 0);
    assert(len > 4);

    return 0;
}

static int test_fetch_request_serialize(void) {
    FetchRequest req;
    memset(&req, 0, sizeof(req));
    req.header.api_key = API_KEY_FETCH;
    req.header.api_version = 4;
    req.header.correlation_id = 200;
    strncpy(req.header.client_id, "consumer-1", sizeof(req.header.client_id) - 1);
    req.replica_id = -1;
    req.max_wait_ms = 500;
    req.min_bytes = 1;
    strncpy(req.topic, "events", sizeof(req.topic) - 1);
    req.partition = 0;
    req.fetch_offset = 0;
    req.max_bytes = 1048576;

    uint8_t buf[512];
    int len;
    assert(fetch_request_serialize(&req, buf, (int)sizeof(buf), &len) == 0);
    assert(len > 4);

    return 0;
}

static int test_offset_commit_serialize(void) {
    OffsetCommitRequest req;
    memset(&req, 0, sizeof(req));
    req.header.api_key = API_KEY_OFFSET_COMMIT;
    req.header.api_version = 2;
    req.header.correlation_id = 300;
    strncpy(req.header.client_id, "consumer-1", sizeof(req.header.client_id) - 1);
    strncpy(req.group_id, "my-group", sizeof(req.group_id) - 1);
    strncpy(req.topic, "my-topic", sizeof(req.topic) - 1);
    req.partition = 0;
    req.committed_offset = 999;
    req.commit_timestamp = 1234567890;

    uint8_t buf[512];
    int len;
    assert(offset_commit_request_serialize(&req, buf, (int)sizeof(buf), &len) == 0);
    assert(len > 4);

    return 0;
}

static int test_offset_fetch_response_serialize(void) {
    OffsetFetchResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.header.correlation_id = 300;
    strncpy(resp.topic, "my-topic", sizeof(resp.topic) - 1);
    resp.partition = 0;
    resp.offset = 999;
    resp.error_code = ERROR_NONE;

    uint8_t buf[512];
    int len;
    assert(offset_fetch_response_serialize(&resp, buf, (int)sizeof(buf), &len) == 0);
    assert(len > 4);

    return 0;
}

static int test_api_versions(void) {
    assert(wire_api_versions_supported(API_KEY_PRODUCE, 3) == 1);
    assert(wire_api_versions_supported(API_KEY_PRODUCE, 8) == 0);
    assert(wire_api_versions_supported(API_KEY_PRODUCE, -1) == 0);

    assert(wire_get_max_supported_version(API_KEY_PRODUCE) == 7);
    assert(wire_get_max_supported_version(API_KEY_FETCH) == 7);
    assert(wire_get_max_supported_version((ApiKey)999) == -1);

    return 0;
}

static int test_error_strings(void) {
    assert(strcmp(wire_error_string(ERROR_NONE), "No error") == 0);
    assert(strcmp(wire_error_string(ERROR_UNKNOWN_TOPIC_OR_PART),
                  "Unknown topic or partition") == 0);
    assert(strcmp(wire_error_string(ERROR_REBALANCE_IN_PROGRESS),
                  "Rebalance in progress") == 0);
    assert(strstr(wire_error_string((ProtocolError)9999), "Unknown") != NULL);
    return 0;
}

static int test_request_validation(void) {
    /* Build a minimal valid request */
    uint8_t valid[20];
    memset(valid, 0, sizeof(valid));
    /* message_size = 12 (4+2+2+4) */
    valid[3] = 12;  /* big-endian */
    /* api_key = 3 (METADATA) */
    valid[5] = 3;
    /* correlation_id = 1 */
    valid[11] = 1;
    /* client_id length = 0 */
    valid[12] = 0;
    valid[13] = 0;

    /* Note: wire_validate_request_header checks client_id length at position 12 */
    /* Our test data has 0-length client_id which is valid */

    /* Actually test with NULL/empty */
    assert(wire_validate_request_header(NULL, 0) == -1);
    assert(wire_validate_request_header(valid, 5) == -1);  /* too short */

    return 0;
}

static int test_response_size_estimation(void) {
    int sz = wire_estimate_response_size(API_KEY_FETCH, 100);
    assert(sz > 0 && sz <= WIRE_MAX_RESPONSE_SIZE);

    sz = wire_estimate_response_size(API_KEY_PRODUCE, 0);
    assert(sz > 0);

    sz = wire_estimate_response_size((ApiKey)999, 10);
    assert(sz > 0);

    return 0;
}

int main(void) {
    printf("=== Running Wire Protocol Tests ===\n");
    test_buffer_create();
    test_buffer_write_read_i16();
    test_buffer_write_read_i32();
    test_buffer_write_read_i64();
    test_buffer_write_read_string();
    test_produce_request_serialize();
    test_produce_response_serialize();
    test_fetch_request_serialize();
    test_offset_commit_serialize();
    test_offset_fetch_response_serialize();
    test_api_versions();
    test_error_strings();
    test_request_validation();
    test_response_size_estimation();
    printf("All wire protocol tests passed!\n");
    return 0;
}