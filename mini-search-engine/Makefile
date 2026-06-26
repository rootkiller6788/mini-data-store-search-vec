CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -g -Wno-unused-function
LDFLAGS  = -lm

INCDIR   = include
SRCDIR   = src
EXAMDIR  = examples
TESTDIR  = tests
BUILDDIR = build

SRCS     = $(SRCDIR)/inverted_index.c $(SRCDIR)/tokenizer.c \
           $(SRCDIR)/scoring.c $(SRCDIR)/query_parser.c \
           $(SRCDIR)/search_engine.c $(SRCDIR)/index_compression.c \
           $(SRCDIR)/ranking.c $(SRCDIR)/language_model.c \
           $(SRCDIR)/spell_correction.c
OBJS     = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

EXAMPLES = $(BUILDDIR)/index_demo $(BUILDDIR)/scoring_demo $(BUILDDIR)/boolean_query_demo
TESTS    = $(BUILDDIR)/test_runner

.PHONY: all clean test run-index-demo run-scoring-demo run-boolean-demo

all: $(EXAMPLES)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(BUILDDIR)/index_demo: $(EXAMDIR)/index_demo.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILDDIR)/scoring_demo: $(EXAMDIR)/scoring_demo.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILDDIR)/boolean_query_demo: $(EXAMDIR)/boolean_query_demo.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILDDIR)/test_runner: $(TESTDIR)/test_runner.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

test: $(BUILDDIR)/test_runner
	$(BUILDDIR)/test_runner

run-index-demo: $(BUILDDIR)/index_demo
	$(BUILDDIR)/index_demo

run-scoring-demo: $(BUILDDIR)/scoring_demo
	$(BUILDDIR)/scoring_demo

run-boolean-demo: $(BUILDDIR)/boolean_query_demo
	$(BUILDDIR)/boolean_query_demo

clean:
	rm -rf $(BUILDDIR)
