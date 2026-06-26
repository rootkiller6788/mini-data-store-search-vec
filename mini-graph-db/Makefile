CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -pedantic -O2
INCLUDES = -Iinclude
LDFLAGS  = -lm

SRCDIR   = src
OBJDIR   = build
BINDIR   = bin
EXAMDIR  = examples

OBJS = $(OBJDIR)/property_graph.o \
       $(OBJDIR)/graph_traversal.o \
       $(OBJDIR)/graph_algo.o \
       $(OBJDIR)/cypher_like.o \
       $(OBJDIR)/graph_index.o

TRAVERSAL_DEMO = $(BINDIR)/graph_traversal_demo
PAGERANK_DEMO  = $(BINDIR)/pagerank_demo
CYPHER_DEMO    = $(BINDIR)/cypher_demo

.PHONY: all clean run-traversal run-pagerank run-cypher

all: $(OBJDIR) $(BINDIR) $(TRAVERSAL_DEMO) $(PAGERANK_DEMO) $(CYPHER_DEMO)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR)/property_graph.o: $(SRCDIR)/property_graph.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRCDIR)/property_graph.c -o $@

$(OBJDIR)/graph_traversal.o: $(SRCDIR)/graph_traversal.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRCDIR)/graph_traversal.c -o $@

$(OBJDIR)/graph_algo.o: $(SRCDIR)/graph_algo.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRCDIR)/graph_algo.c -o $@

$(OBJDIR)/cypher_like.o: $(SRCDIR)/cypher_like.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRCDIR)/cypher_like.c -o $@

$(OBJDIR)/graph_index.o: $(SRCDIR)/graph_index.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $(SRCDIR)/graph_index.c -o $@

$(TRAVERSAL_DEMO): $(OBJS) $(EXAMDIR)/graph_traversal_demo.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(EXAMDIR)/graph_traversal_demo.c $(OBJS) $(LDFLAGS)

$(PAGERANK_DEMO): $(OBJS) $(EXAMDIR)/pagerank_demo.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(EXAMDIR)/pagerank_demo.c $(OBJS) $(LDFLAGS)

$(CYPHER_DEMO): $(OBJS) $(EXAMDIR)/cypher_demo.c
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(EXAMDIR)/cypher_demo.c $(OBJS) $(LDFLAGS)

run-traversal: $(TRAVERSAL_DEMO)
	$(TRAVERSAL_DEMO)

run-pagerank: $(PAGERANK_DEMO)
	$(PAGERANK_DEMO)

run-cypher: $(CYPHER_DEMO)
	$(CYPHER_DEMO)

clean:
	rm -rf $(OBJDIR) $(BINDIR)
