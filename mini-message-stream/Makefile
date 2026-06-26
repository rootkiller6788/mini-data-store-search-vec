CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -Iinclude
LDFLAGS = -lm

SRC_DIR  = src
INC_DIR  = include
EXAM_DIR = examples
BUILD_DIR = build

SRCS = $(SRC_DIR)/topic_partition.c \
       $(SRC_DIR)/producer_client.c \
       $(SRC_DIR)/consumer_group.c \
       $(SRC_DIR)/broker.c \
       $(SRC_DIR)/offset_manager.c

OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

EXAMPLES = producer_demo consumer_demo broker_demo

all: $(BUILD_DIR) $(EXAMPLES)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(INC_DIR)/topic_partition.h $(INC_DIR)/producer_client.h $(INC_DIR)/consumer_group.h $(INC_DIR)/broker.h $(INC_DIR)/offset_manager.h
	$(CC) $(CFLAGS) -c $< -o $@

producer_demo: $(OBJS) $(EXAM_DIR)/producer_demo.c
	$(CC) $(CFLAGS) $(OBJS) $(EXAM_DIR)/producer_demo.c -o $(BUILD_DIR)/producer_demo $(LDFLAGS)

consumer_demo: $(OBJS) $(EXAM_DIR)/consumer_demo.c
	$(CC) $(CFLAGS) $(OBJS) $(EXAM_DIR)/consumer_demo.c -o $(BUILD_DIR)/consumer_demo $(LDFLAGS)

broker_demo: $(OBJS) $(EXAM_DIR)/broker_demo.c
	$(CC) $(CFLAGS) $(OBJS) $(EXAM_DIR)/broker_demo.c -o $(BUILD_DIR)/broker_demo $(LDFLAGS)

run-producer: producer_demo
	$(BUILD_DIR)/producer_demo

run-consumer: consumer_demo
	$(BUILD_DIR)/consumer_demo

run-broker: broker_demo
	$(BUILD_DIR)/broker_demo

run-all: run-producer run-consumer run-broker

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean run-producer run-consumer run-broker run-all
