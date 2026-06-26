# mini-file-storage Makefile
# C99, libc+libm only

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -pedantic -O2 -g
INC     := -Iinclude
LDFLAGS := -lm

BUILD_DIR := build
SRC_DIR   := src
EXA_DIR   := examples
TST_DIR   := tests

SRCS := $(SRC_DIR)/sstable.c $(SRC_DIR)/lsm_tree.c $(SRC_DIR)/skiplist.c \
        $(SRC_DIR)/wal_file.c $(SRC_DIR)/compaction.c \
        $(SRC_DIR)/block_cache.c $(SRC_DIR)/range_query.c \
        $(SRC_DIR)/snapshot.c $(SRC_DIR)/encoding.c
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

EXAS := $(EXA_DIR)/sstable_demo.c $(EXA_DIR)/lsm_demo.c $(EXA_DIR)/skiplist_demo.c
EXABINS := $(patsubst $(EXA_DIR)/%.c,$(BUILD_DIR)/%,$(EXAS))

TSTS := $(TST_DIR)/test_suite.c
TSTBIN := $(BUILD_DIR)/test_suite

.PHONY: all clean dirs test

all: dirs $(BUILD_DIR)/libmini-fs.a $(EXABINS)

test: dirs $(BUILD_DIR)/libmini-fs.a $(TSTBIN)
	@echo "Running tests..."
	$(TSTBIN)

dirs:
	@mkdir -p "$(BUILD_DIR)"

# Static library
$(BUILD_DIR)/libmini-fs.a: $(OBJS)
	$(AR) rcs $@ $^

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c include/*.h
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

# Test suite
$(BUILD_DIR)/test_suite: $(TST_DIR)/test_suite.c $(BUILD_DIR)/libmini-fs.a
	$(CC) $(CFLAGS) $(INC) $< -L$(BUILD_DIR) -lmini-fs $(LDFLAGS) -o $@

# Example: sstable_demo
$(BUILD_DIR)/sstable_demo: $(EXA_DIR)/sstable_demo.c $(BUILD_DIR)/libmini-fs.a
	$(CC) $(CFLAGS) $(INC) $< -L$(BUILD_DIR) -lmini-fs $(LDFLAGS) -o $@

# Example: lsm_demo
$(BUILD_DIR)/lsm_demo: $(EXA_DIR)/lsm_demo.c $(BUILD_DIR)/libmini-fs.a
	$(CC) $(CFLAGS) $(INC) $< -L$(BUILD_DIR) -lmini-fs $(LDFLAGS) -o $@

# Example: skiplist_demo
$(BUILD_DIR)/skiplist_demo: $(EXA_DIR)/skiplist_demo.c $(BUILD_DIR)/libmini-fs.a
	$(CC) $(CFLAGS) $(INC) $< -L$(BUILD_DIR) -lmini-fs $(LDFLAGS) -o $@

clean:
	@rm -rf "$(BUILD_DIR)"
	@rm -f demo_output.sst
	@rm -rf lsm_demo_data
	@rm -f test_output.sst
	@rm -f test_wal.log
	@rm -f test_wal.log.closed
	@rm -f test_wal_crc.sst
	@rm -f test_wal_crc.sst.closed
	@rm -f test_m1.sst
	@rm -f test_m2.sst
	@rm -f test_m3.sst
	@rm -f test_range.sst
	@rm -f test_cp1.sst
	@rm -f test_cp2.sst
	@rm -rf test_lsm_data
	@rm -rf test_lsm_del
