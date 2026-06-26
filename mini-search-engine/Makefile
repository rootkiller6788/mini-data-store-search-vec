CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -g
LDFLAGS  = -lm

INCDIR   = include
SRCDIR   = src
EXAMDIR  = examples
BUILDDIR = build

SRCS     = $(SRCDIR)/inverted_index.c $(SRCDIR)/tokenizer.c \
           $(SRCDIR)/scoring.c $(SRCDIR)/query_parser.c \
           $(SRCDIR)/search_engine.c
OBJS     = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

EXAMPLES = $(BUILDDIR)/index_demo $(BUILDDIR)/scoring_demo $(BUILDDIR)/boolean_query_demo

.PHONY: all clean run-index-demo run-scoring-demo run-boolean-demo

all: $(EXAMPLES)

$(BUILDDIR):
	if not exist "$(BUILDDIR)" mkdir "$(BUILDDIR)"

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(BUILDDIR)/index_demo: $(EXAMDIR)/index_demo.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILDDIR)/scoring_demo: $(EXAMDIR)/scoring_demo.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

$(BUILDDIR)/boolean_query_demo: $(EXAMDIR)/boolean_query_demo.c $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(OBJS) $(LDFLAGS) -o $@

run-index-demo: $(BUILDDIR)/index_demo
	$(BUILDDIR)/index_demo

run-scoring-demo: $(BUILDDIR)/scoring_demo
	$(BUILDDIR)/scoring_demo

run-boolean-demo: $(BUILDDIR)/boolean_query_demo
	$(BUILDDIR)/boolean_query_demo

clean:
	if exist "$(BUILDDIR)" rmdir /s /q "$(BUILDDIR)"
