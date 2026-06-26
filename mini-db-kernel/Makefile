CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -I include -std=c99
LDFLAGS  = -lm
OUTDIR   = bin

SRCS     = src/buffer_pool.c src/btree.c src/wal.c src/lock_manager.c src/mvcc.c \
           src/slotted_page.c src/disk_manager.c src/lsm_tree.c src/query_exec.c src/kv_store.c
OBJS     = $(patsubst src/%.c,$(OUTDIR)/%.o,$(SRCS))

EXAMPLES = examples/buffer_pool_demo.c examples/btree_demo.c examples/wal_demo.c
BINS     = $(patsubst examples/%.c,$(OUTDIR)/%,$(EXAMPLES))

TESTS    = test_slotted_page test_disk_manager test_lsm_tree test_query_exec test_kv_store
TESTBINS = $(addprefix $(OUTDIR)/,$(TESTS))

.PHONY: all clean test demo_a demo_b demo_c

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

# --- Test targets ---

$(OUTDIR)/test_slotted_page: tests/test_slotted_page.c $(OUTDIR)/slotted_page.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/test_disk_manager: tests/test_disk_manager.c $(OUTDIR)/disk_manager.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/test_lsm_tree: tests/test_lsm_tree.c $(OUTDIR)/lsm_tree.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/test_query_exec: tests/test_query_exec.c $(OUTDIR)/query_exec.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OUTDIR)/test_kv_store: tests/test_kv_store.c $(OUTDIR)/kv_store.o $(OUTDIR)/btree.o \
                          $(OUTDIR)/buffer_pool.o $(OUTDIR)/wal.o $(OUTDIR)/lock_manager.o \
                          $(OUTDIR)/mvcc.o $(OUTDIR)/disk_manager.o $(OUTDIR)/slotted_page.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: $(OUTDIR) $(TESTBINS)
	@echo "=== Running Tests ==="
	@fail=0; \
	for t in $(TESTBINS); do \
		if [ -f "$$t.exe" ]; then t="$$t.exe"; fi; \
		echo "--- Running $$t ---"; \
		"$$t" || fail=1; \
		echo ""; \
	done; \
	if [ $$fail -eq 0 ]; then \
		echo "ALL TESTS PASSED"; \
	else \
		echo "SOME TESTS FAILED"; \
		exit 1; \
	fi

clean:
	rm -rf "$(OUTDIR)"

demo_a:
	$(OUTDIR)/buffer_pool_demo

demo_b:
	$(OUTDIR)/btree_demo

demo_c:
	$(OUTDIR)/wal_demo
