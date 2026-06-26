CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS  = -lm
OUTDIR   = bin

SRCS     = src/buffer_pool.c src/btree.c src/wal.c src/lock_manager.c src/mvcc.c
OBJS     = $(patsubst src/%.c,$(OUTDIR)/%.o,$(SRCS))

EXAMPLES = examples/buffer_pool_demo.c examples/btree_demo.c examples/wal_demo.c
BINS     = $(patsubst examples/%.c,$(OUTDIR)/%,$(EXAMPLES))

.PHONY: all clean demo_a demo_b demo_c

all: $(OUTDIR) $(BINS)

$(OUTDIR):
	mkdir -p "$(OUTDIR)"

$(OUTDIR)/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUTDIR)/buffer_pool_demo: examples/buffer_pool_demo.c $(OUTDIR)/buffer_pool.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/btree_demo: examples/btree_demo.c $(OUTDIR)/btree.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/wal_demo: examples/wal_demo.c $(OUTDIR)/wal.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf "$(OUTDIR)"

demo_a:
	$(OUTDIR)/buffer_pool_demo

demo_b:
	$(OUTDIR)/btree_demo

demo_c:
	$(OUTDIR)/wal_demo
