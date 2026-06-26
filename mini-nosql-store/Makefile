CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2 -g
INCLUDES = -Iinclude
LDFLAGS  = -lm

SRCDIR   = src
BUILDDIR = build
EXAMDIR  = examples

SRCS     = $(SRCDIR)/kv_store.c \
           $(SRCDIR)/lsm_engine.c \
           $(SRCDIR)/document_store.c \
           $(SRCDIR)/col_family.c \
           $(SRCDIR)/redis_model.c

OBJS     = $(SRCS:$(SRCDIR)/%.c=$(BUILDDIR)/%.o)

TARGETS  = $(BUILDDIR)/kv_demo \
           $(BUILDDIR)/lsm_demo \
           $(BUILDDIR)/doc_store_demo

.PHONY: all clean builddir

all: builddir $(TARGETS)
	@echo === mini-nosql-store built successfully ===

builddir:
	@if not exist "$(BUILDDIR)" mkdir "$(BUILDDIR)"

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILDDIR)/kv_demo: $(EXAMDIR)/kv_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/lsm_demo: $(EXAMDIR)/lsm_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/doc_store_demo: $(EXAMDIR)/doc_store_demo.c $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDFLAGS)

clean:
	@if exist "$(BUILDDIR)" rmdir /s /q "$(BUILDDIR)"
