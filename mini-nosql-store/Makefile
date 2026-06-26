CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -g
INCLUDES = -Iinclude
LDFLAGS  = -lm

SRCDIR   = src
BUILDDIR = build
TESTDIR  = tests
EXAMDIR  = examples

SRCS     = $(SRCDIR)/kv_store.c \
           $(SRCDIR)/lsm_engine.c \
           $(SRCDIR)/document_store.c \
           $(SRCDIR)/col_family.c \
           $(SRCDIR)/redis_model.c \
           $(SRCDIR)/nosql_wal.c \
           $(SRCDIR)/nosql_hash.c \
           $(SRCDIR)/nosql_cache.c \
           $(SRCDIR)/nosql_verify.c

OBJS     = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

TEST_SRCS = $(TESTDIR)/test_kv.c \
            $(TESTDIR)/test_lsm.c \
            $(TESTDIR)/test_doc.c \
            $(TESTDIR)/test_redis.c \
            $(TESTDIR)/test_wal.c \
            $(TESTDIR)/test_hash.c \
            $(TESTDIR)/test_cache.c \
            $(TESTDIR)/test_verify.c

TEST_BINS = $(TEST_SRCS:$(TESTDIR)/%.c=$(BUILDDIR)/%)

TARGETS  = $(BUILDDIR)/kv_demo \
           $(BUILDDIR)/lsm_demo \
           $(BUILDDIR)/doc_store_demo \
           $(BUILDDIR)/wal_demo \
           $(BUILDDIR)/hash_demo \
           $(BUILDDIR)/cache_demo

.PHONY: all test clean builddir

all: builddir $(TARGETS)
	@echo === mini-nosql-store built successfully ===

test: builddir $(TEST_BINS)
	@echo === Running all tests ===
	$(BUILDDIR)/test_kv
	$(BUILDDIR)/test_lsm
	$(BUILDDIR)/test_doc
	$(BUILDDIR)/test_redis
	$(BUILDDIR)/test_wal
	$(BUILDDIR)/test_hash
	$(BUILDDIR)/test_cache
	$(BUILDDIR)/test_verify
	@echo === All tests passed ===

builddir:
	@mkdir -p "$(BUILDDIR)"

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILDDIR)/kv_demo: $(EXAMDIR)/kv_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/lsm_demo: $(EXAMDIR)/lsm_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/doc_store_demo: $(EXAMDIR)/doc_store_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/wal_demo: $(EXAMDIR)/wal_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/hash_demo: $(EXAMDIR)/hash_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/cache_demo: $(EXAMDIR)/cache_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/test_kv: $(TESTDIR)/test_kv.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_lsm: $(TESTDIR)/test_lsm.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_doc: $(TESTDIR)/test_doc.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_redis: $(TESTDIR)/test_redis.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_wal: $(TESTDIR)/test_wal.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_hash: $(TESTDIR)/test_hash.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_cache: $(TESTDIR)/test_cache.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)
$(BUILDDIR)/test_verify: $(TESTDIR)/test_verify.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

clean:
	@rm -rf "$(BUILDDIR)"
