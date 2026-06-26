# mini-file-storage Makefile
# C99, libc+libm only

CC      := gcc
CFLAGS  := -std=c99 -Wall -Wextra -pedantic -O2 -g
INC     := -Iinclude
LDFLAGS := -lm

BUILD_DIR := build
SRC_DIR   := src
EXA_DIR   := examples

SRCS := $(SRC_DIR)/sstable.c $(SRC_DIR)/lsm_tree.c $(SRC_DIR)/skiplist.c \
        $(SRC_DIR)/wal_file.c $(SRC_DIR)/compaction.c
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

EXAS := $(EXA_DIR)/sstable_demo.c $(EXA_DIR)/lsm_demo.c $(EXA_DIR)/skiplist_demo.c
EXABINS := $(patsubst $(EXA_DIR)/%.c,$(BUILD_DIR)/%,$(EXAS))

.PHONY: all clean dirs

all: dirs $(BUILD_DIR)/libmini-fs.a $(EXABINS)

dirs:
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

# Static library
$(BUILD_DIR)/libmini-fs.a: $(OBJS)
	$(AR) rcs $@ $^

# Object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c include/*.h
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

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
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
	@if exist demo_output.sst del demo_output.sst
	@if exist lsm_demo_data rmdir /s /q lsm_demo_data
