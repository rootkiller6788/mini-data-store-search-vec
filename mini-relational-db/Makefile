CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -O2
LIBS     = -lm

SRCDIR   = src
INCDIR   = include
BINDIR   = build
EXAMPLES = examples
OBJDIR   = $(BINDIR)/obj

SRCS     = $(SRCDIR)/sql_parser.c \
           $(SRCDIR)/query_optimizer.c \
           $(SRCDIR)/volcano_executor.c \
           $(SRCDIR)/join_algorithms.c \
           $(SRCDIR)/row_store.c

OBJS     = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
LIB      = $(BINDIR)/librelationaldb.a

EXE_SQL       = $(BINDIR)/sql_demo
EXE_JOIN      = $(BINDIR)/join_demo
EXE_OPTIMIZER = $(BINDIR)/optimizer_demo

.PHONY: all lib examples clean run-sql run-join run-optimizer

all: lib examples

lib: $(LIB)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@if not exist "$(OBJDIR)" mkdir "$(OBJDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(LIB): $(OBJS)
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	ar rcs $@ $^

$(EXE_SQL): $(LIB) $(EXAMPLES)/sql_demo.c
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLES)/sql_demo.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

$(EXE_JOIN): $(LIB) $(EXAMPLES)/join_demo.c
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLES)/join_demo.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

$(EXE_OPTIMIZER): $(LIB) $(EXAMPLES)/optimizer_demo.c
	@if not exist "$(BINDIR)" mkdir "$(BINDIR)"
	$(CC) $(CFLAGS) -I$(INCDIR) $(EXAMPLES)/optimizer_demo.c -L$(BINDIR) -lrelationaldb $(LIBS) -o $@

examples: $(EXE_SQL) $(EXE_JOIN) $(EXE_OPTIMIZER)

run-sql: $(EXE_SQL)
	$(EXE_SQL)

run-join: $(EXE_JOIN)
	$(EXE_JOIN)

run-optimizer: $(EXE_OPTIMIZER)
	$(EXE_OPTIMIZER)

clean:
	@if exist "$(OBJDIR)" rmdir /s /q "$(OBJDIR)"
	@if exist "$(BINDIR)" rmdir /s /q "$(BINDIR)"
