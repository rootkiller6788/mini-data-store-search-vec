CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -Iinclude
LDFLAGS = -lm

SRC_DIR  = src
INC_DIR  = include
EXAM_DIR = examples
TEST_DIR = tests
BUILD_DIR = build

SRCS = $(SRC_DIR)/topic_partition.c \
       $(SRC_DIR)/producer_client.c \
       $(SRC_DIR)/consumer_group.c \
       $(SRC_DIR)/broker.c \
       $(SRC_DIR)/offset_manager.c \
       $(SRC_DIR)/replication.c \
       $(SRC_DIR)/message_codec.c \
       $(SRC_DIR)/stream_processor.c \
       $(SRC_DIR)/log_cleaner.c \
       $(SRC_DIR)/wire_protocol.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

ALL_HEADERS = $(wildcard $(INC_DIR)/*.h)

EXAMPLES = producer_demo consumer_demo broker_demo

TESTS = test_topic_partition test_producer test_consumer_group \
        test_offset_manager test_broker test_replication \
        test_message_codec test_stream_processor test_log_cleaner \
        test_wire_protocol

all: $(BUILD_DIR) $(EXAMPLES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(ALL_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# === Examples ===
producer_demo: $(OBJS) $(EXAM_DIR)/producer_demo.c
	$(CC) $(CFLAGS) $(OBJS) $(EXAM_DIR)/producer_demo.c -o $(BUILD_DIR)/producer_demo $(LDFLAGS)

consumer_demo: $(OBJS) $(EXAM_DIR)/consumer_demo.c
	$(CC) $(CFLAGS) $(OBJS) $(EXAM_DIR)/consumer_demo.c -o $(BUILD_DIR)/consumer_demo $(LDFLAGS)

broker_demo: $(OBJS) $(EXAM_DIR)/broker_demo.c
	$(CC) $(CFLAGS) $(OBJS) $(EXAM_DIR)/broker_demo.c -o $(BUILD_DIR)/broker_demo $(LDFLAGS)

# === Tests ===
test_topic_partition: $(OBJS) $(TEST_DIR)/test_topic_partition.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_topic_partition.c -o $(BUILD_DIR)/test_topic_partition $(LDFLAGS)
	$(BUILD_DIR)/test_topic_partition

test_producer: $(OBJS) $(TEST_DIR)/test_producer.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_producer.c -o $(BUILD_DIR)/test_producer $(LDFLAGS)
	$(BUILD_DIR)/test_producer

test_consumer_group: $(OBJS) $(TEST_DIR)/test_consumer_group.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_consumer_group.c -o $(BUILD_DIR)/test_consumer_group $(LDFLAGS)
	$(BUILD_DIR)/test_consumer_group

test_offset_manager: $(OBJS) $(TEST_DIR)/test_offset_manager.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_offset_manager.c -o $(BUILD_DIR)/test_offset_manager $(LDFLAGS)
	$(BUILD_DIR)/test_offset_manager

test_broker: $(OBJS) $(TEST_DIR)/test_broker.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_broker.c -o $(BUILD_DIR)/test_broker $(LDFLAGS)
	$(BUILD_DIR)/test_broker

test_replication: $(OBJS) $(TEST_DIR)/test_replication.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_replication.c -o $(BUILD_DIR)/test_replication $(LDFLAGS)
	$(BUILD_DIR)/test_replication

test_message_codec: $(OBJS) $(TEST_DIR)/test_message_codec.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_message_codec.c -o $(BUILD_DIR)/test_message_codec $(LDFLAGS)
	$(BUILD_DIR)/test_message_codec

test_stream_processor: $(OBJS) $(TEST_DIR)/test_stream_processor.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_stream_processor.c -o $(BUILD_DIR)/test_stream_processor $(LDFLAGS)
	$(BUILD_DIR)/test_stream_processor

test_log_cleaner: $(OBJS) $(TEST_DIR)/test_log_cleaner.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_log_cleaner.c -o $(BUILD_DIR)/test_log_cleaner $(LDFLAGS)
	$(BUILD_DIR)/test_log_cleaner

test_wire_protocol: $(OBJS) $(TEST_DIR)/test_wire_protocol.c
	$(CC) $(CFLAGS) $(OBJS) $(TEST_DIR)/test_wire_protocol.c -o $(BUILD_DIR)/test_wire_protocol $(LDFLAGS)
	$(BUILD_DIR)/test_wire_protocol

test: $(TESTS)
	@echo ""
	@echo "============================================"
	@echo "  ALL TESTS PASSED"
	@echo "============================================"

# === Runners ===
run-producer: producer_demo
	$(BUILD_DIR)/producer_demo

run-consumer: consumer_demo
	$(BUILD_DIR)/consumer_demo

run-broker: broker_demo
	$(BUILD_DIR)/broker_demo

run-all: run-producer run-consumer run-broker

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean test run-producer run-consumer run-broker run-all $(TESTS)
