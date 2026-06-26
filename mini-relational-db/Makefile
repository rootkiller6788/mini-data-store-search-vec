CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2
LIBS     = -lm

SRCDIR   = src
INCDIR   = include
BINDIR   = build
EXAMPLES = examples
TESTSDIR = tests
OBJDIR   = $(BINDIR)/obj

SRCS     = $(SRCDIR)/sql_parser.c \
           $(SRCDIR)/query_optimizer.c \
           $(SRCDIR)/volcano_executor.c \
           $(SRCDIR)/join_algorithms.c \
           $(SRCDIR)/row_store.c \
           $(SRCDIR)/bptree.c \
           $(SRCDIR)/buffer_pool.c \
           $(SRCDIR)/transaction.c \
           $(SRCDIR)/page_layout.c

OBJS     = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
LIB      = $(BINDIR)/librelationaldb.a

EXE_SQL       = $(BINDIR)/sql_demo
EXE_JOIN      = $(BINDIR)/join_demo
EXE_OPTIMIZER = $(BINDIR)/optimizer_demo
EXE_TEST      = $(BINDIR)/test_runner

.PHONY: all lib examples test clean run-sql run-join run-optimizer

all: lib examples

lib: $(LIB)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p "$(OBJDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(LIB): $(OBJS)
	@mkdir -p "$(BINDIR)"
	ar rcs $@ $^

$(EXE_SQL): $(LIB) $(EXAMPLES)/sql_demo.c
	@mkdir -p "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLES)/sql_demo.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

$(EXE_JOIN): $(LIB) $(EXAMPLES)/join_demo.c
	@mkdir -p "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLES)/join_demo.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

$(EXE_OPTIMIZER): $(LIB) $(EXAMPLES)/optimizer_demo.c
	@mkdir -p "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLES)/optimizer_demo.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

$(EXE_TEST): $(LIB) $(TESTSDIR)/test_main.c
	@mkdir -p "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(TESTSDIR)/test_main.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

examples: $(EXE_SQL) $(EXE_JOIN) $(EXE_OPTIMIZER)

test: $(EXE_TEST)
	$(EXE_TEST)

run-sql: $(EXE_SQL)
	$(EXE_SQL)

run-join: $(EXE_JOIN)
	$(EXE_JOIN)

run-optimizer: $(EXE_OPTIMIZER)
	$(EXE_OPTIMIZER)

clean:
	@rm -rf "$(OBJDIR)"
	@rm -rf "$(BINDIR)"
