CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I include
LDFLAGS = -lm
SRCDIR = src
OBJDIR = build
EXAMPLEDIR = examples
BINDIR = bin

SRCS = $(SRCDIR)/vector_math.c $(SRCDIR)/exact_knn.c $(SRCDIR)/hnsw.c \
       $(SRCDIR)/ivf_pq.c $(SRCDIR)/lsh.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

KNN_SRC = $(EXAMPLEDIR)/knn_brute_demo.c
HNSW_SRC = $(EXAMPLEDIR)/hnsw_demo.c
COMPARE_SRC = $(EXAMPLEDIR)/ann_compare_demo.c

KNN_BIN = $(BINDIR)/knn_brute_demo
HNSW_BIN = $(BINDIR)/hnsw_demo
COMPARE_BIN = $(BINDIR)/ann_compare_demo

.PHONY: all clean run-knn run-hnsw run-compare

all: $(KNN_BIN) $(HNSW_BIN) $(COMPARE_BIN)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	if not exist $(OBJDIR) mkdir $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/knn_brute_demo: $(OBJS) $(KNN_SRC)
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) $(KNN_SRC) $(OBJS) $(LDFLAGS) -o $@

$(BINDIR)/hnsw_demo: $(OBJS) $(HNSW_SRC)
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) $(HNSW_SRC) $(OBJS) $(LDFLAGS) -o $@

$(BINDIR)/ann_compare_demo: $(OBJS) $(COMPARE_SRC)
	if not exist $(BINDIR) mkdir $(BINDIR)
	$(CC) $(CFLAGS) $(COMPARE_SRC) $(OBJS) $(LDFLAGS) -o $@

run-knn: $(KNN_BIN)
	$(KNN_BIN)

run-hnsw: $(HNSW_BIN)
	$(HNSW_BIN)

run-compare: $(COMPARE_BIN)
	$(COMPARE_BIN)

clean:
	if exist $(OBJDIR) rmdir /s /q $(OBJDIR)
	if exist $(BINDIR) rmdir /s /q $(BINDIR)
