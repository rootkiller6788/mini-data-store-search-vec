CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I include
LDFLAGS = -lm
SRCDIR = src
OBJDIR = build
EXAMPLEDIR = examples
TESTDIR = tests
BINDIR = bin

SRCS = $(SRCDIR)/vector_math.c \
       $(SRCDIR)/distance_metrics.c \
       $(SRCDIR)/exact_knn.c \
       $(SRCDIR)/hnsw.c \
       $(SRCDIR)/ivf_pq.c \
       $(SRCDIR)/lsh.c \
       $(SRCDIR)/serialization.c \
       $(SRCDIR)/vector_db.c \
       $(SRCDIR)/index_config.c \
       $(SRCDIR)/pq_full.c \
       $(SRCDIR)/kmeans_pp.c \
       $(SRCDIR)/dimensionality.c \
       $(SRCDIR)/index_eval.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

KNN_SRC = $(EXAMPLEDIR)/knn_brute_demo.c
HNSW_SRC = $(EXAMPLEDIR)/hnsw_demo.c
COMPARE_SRC = $(EXAMPLEDIR)/ann_compare_demo.c
TEST_SRC = $(TESTDIR)/test_all.c

KNN_BIN = $(BINDIR)/knn_brute_demo
HNSW_BIN = $(BINDIR)/hnsw_demo
COMPARE_BIN = $(BINDIR)/ann_compare_demo
TEST_BIN = $(BINDIR)/test_all

.PHONY: all test clean run-knn run-hnsw run-compare

all: $(KNN_BIN) $(HNSW_BIN) $(COMPARE_BIN)

test: $(TEST_BIN)
	$(TEST_BIN)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/knn_brute_demo: $(OBJS) $(KNN_SRC)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(KNN_SRC) $(OBJS) $(LDFLAGS) -o $@

$(BINDIR)/hnsw_demo: $(OBJS) $(HNSW_SRC)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(HNSW_SRC) $(OBJS) $(LDFLAGS) -o $@

$(BINDIR)/ann_compare_demo: $(OBJS) $(COMPARE_SRC)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(COMPARE_SRC) $(OBJS) $(LDFLAGS) -o $@

$(BINDIR)/test_all: $(OBJS) $(TEST_SRC)
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(TEST_SRC) $(OBJS) $(LDFLAGS) -o $@

run-knn: $(KNN_BIN)
	$(KNN_BIN)

run-hnsw: $(HNSW_BIN)
	$(HNSW_BIN)

run-compare: $(COMPARE_BIN)
	$(COMPARE_BIN)

clean:
	rm -rf $(OBJDIR) $(BINDIR)
