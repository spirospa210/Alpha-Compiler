CC      = gcc
CFLAGS  = -O2 -w

# Phase 1-3 front-end + Phase 4 generation -> the `alpha` compiler
COMPILER_SRC = parser.tab.c lex.yy.c symtable.c quads.c actions.c \
              targetcode_core.c targetcode_gen.c targetcode_out.c
# Phase 5 virtual machine -> the `avm` executable (uses only targetcode.h defs)
VM_SRC      = alpha.c avm_core.c avm_table.c avm_execute.c avm_libfuncs.c

all: alpha avm

parser.tab.c: parser.y
	bison -d parser.y

lex.yy.c: scanner.l
	flex scanner.l

alpha: parser.tab.c lex.yy.c
	$(CC) $(CFLAGS) -o alpha $(COMPILER_SRC) -lm

avm: $(VM_SRC)
	$(CC) $(CFLAGS) -o avm $(VM_SRC) -lm

clean:
	rm -f alpha avm parser.tab.c parser.tab.h lex.yy.c *.o \
	      quads.txt targetcode.txt out.abc
