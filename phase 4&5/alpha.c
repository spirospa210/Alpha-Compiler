/* alpha.c - Phase 4/5 driver.
 *
 * Pipeline:  source.asc --(scanner+parser, Phase 1-3)--> quads
 *            --(Phase 4 generate_target_code)--> instructions + const tables
 *            --(write binary)--> a.abc
 *            --(Phase 5 VM load + run)--> program output
 *
 * Build (see Makefile): linked together with the parser, scanner, symtable,
 * quads, actions (Phase 1-3) plus targetcode_* and avm_* (Phase 4-5).
 *
 * The Phase 1-3 front-end provides: yyparse(), the token list, the populated
 * quads[]/currQuad, and the symbol table. We invoke it via the existing main
 * flow, but to keep a single entry point we expose run_frontend() from parser.y
 * OR call the same steps here. To avoid duplicating parser.y's main, this file
 * is only compiled into the VM-side binary; the compiler-side `parser` binary
 * keeps its own main and additionally calls generate+write when error_count==0.
 */

#include "targetcode.h"
#include "avm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Standalone VM entry: load a .abc binary and run it. */
int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file.abc>\n", argv[0]);
        return 1;
    }
    avm_load_binary(argv[1]);
    avm_initialize();
    avm_run();
    return 0;
}
