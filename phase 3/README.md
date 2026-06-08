# Alpha Compiler — Phase 3 (Intermediate Code Generation)

## Build
    make            # bison -d parser.y ; flex scanner.l ; gcc ... -> ./parser
    make clean

Run:
    ./parser path/to/file.alpha
On a successful parse it writes the intermediate code to **quads.txt** (one quad
per line) and prints the symbol table (Phase 2 behaviour, unchanged).

## Files
- scanner.l, token.h      — Phase 1 (frozen, unchanged).
- parser.y                — Phase 2 grammar (structure unchanged) + Phase 3 ICG
                            semantic actions. %union/%type extended; mid-rule
                            markers use file-scope marker stacks (robust).
- symtable.c/.h           — Phase 2 symbol table + additive scope-space/offset/
                            temp/funcname helpers. No existing logic changed.
- quads.c/.h              — quad array, emit, expr ctors, emit_iftableitem,
                            member_item, makelist/merge/backpatch, quads.txt writer.
- actions.c/.h            — semantic-action helpers the grammar calls (do_arith,
                            do_relop, do_and/or/not, to_value/to_bool, do_assign,
                            do_call*, do_methodcall*, do_objectdef*, do_funcstart/
                            end, do_return, inc/dec, for/while marker stacks).

## Verification status (important, read VALIDATION_REPORT.md)
The code-generation engine and EVERY construct in the provided semantic test files
were verified BYTE-FOR-BYTE by compiling with gcc and diffing output:
arith_ops, inc_dec, members, rel_bool_ops, uminus, object_def, calls, chained_call,
function_declare, if_stmt, for_stmt, while_stmt, return_stmt, break_continue.

NOT verifiable in the dev sandbox: the `bison`/`flex` step itself (those tools and
m4 were unavailable, network disabled). The grammar structure is unchanged from
your working Phase 2, and all actions call the byte-verified helpers, but you must
run `make` once on a machine with bison+flex and spot-check a few outputs before
relying on it for Phase 4. If bison reports more than the expected 1 shift/reduce
conflict, adjust `%expect`; the mid-rule actions in `expr AND/OR` are the only
likely source of an extra conflict and are standard backpatch markers.

## quads.txt format (matches the provided expected outputs exactly)
Columns: quad#  opcode  result  arg1  arg2  label  line
- assign:        assign  result  arg1
- arithmetic:    add/sub/mul/div/mod  result  arg1  arg2
- relational:    if_*  arg1  arg2  (label = target)
- jump:          jump  (label = target)
- ret value in the RESULT column; booleans print true/false; strings "quoted";
  temps _t0.., anonymous functions _f0..; constant arithmetic is folded.

## validation_harnesses/
One h_*.c per construct: each drives actions.c in the exact order the matching
grammar rule does, and reproduces that test file byte-for-byte. Use them as the
authoritative reference for what each rule's action must emit.
