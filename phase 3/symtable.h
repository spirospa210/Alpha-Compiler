#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdbool.h>

/* ============================================================
 *  PHASE 2 (FROZEN) - unchanged enum and core declarations.
 *  Everything below the "PHASE 3 ADDITIONS" banner is new and
 *  purely additive: it does not alter any existing field's
 *  meaning or any existing function's behaviour.
 * ============================================================ */

typedef enum {
    GLOBAL_VAR,
    LOCAL_VAR,
    FORMAL_ARG,
    USER_FUNC,
    LIB_FUNC
} SymbolType;

/* ------------------------------------------------------------
 *  PHASE 3 ADDITIONS
 * ------------------------------------------------------------ */

/* The three scope-spaces of the spec (program vars / function
 * locals / formal arguments). A dedicated counter is kept per
 * space; entering a function saves/restores them on a stack. */
typedef enum {
    PROGRAM_SCOPE,
    FUNCTION_LOCAL,
    FORMAL_ARGUMENT
} ScopeSpace;

typedef struct SymbolTableEntry {
    char* name;
    SymbolType type;
    int line;
    int scope;
    bool isActive;
    struct SymbolTableEntry* next_in_hash;
    struct SymbolTableEntry* next_in_scope;

    /* ---- PHASE 3 additive fields (default-initialised on insert) ---- */
    ScopeSpace space;        /* which scope-space this symbol lives in   */
    unsigned   offset;       /* order of appearance within that space    */
    unsigned   totalLocals;  /* (functions) number of local variables    */
    unsigned   totalFormals; /* (functions) number of formal arguments   */
    unsigned   iaddress;     /* (functions) quad index of its funcstart  */
    bool       isTemp;       /* true for compiler-generated temporaries  */
} SymbolTableEntry;

/* The intermediate-code layer refers to symbol-table entries as
 * `symbol` (matches the lecture `symbol*` type in struct expr). */
typedef struct SymbolTableEntry symbol;

/* ---- Phase 2 (frozen) prototypes ---- */
void init_symtable();
SymbolTableEntry* insert_symbol(char* name, SymbolType type, int line, int scope);
SymbolTableEntry* lookup_scope(char* name, int scope);
SymbolTableEntry* lookup_all(char* name, int current_scope);
void hide_scope(int scope);
void print_symtable();
void insert_library_functions();

/* ============================================================
 *  PHASE 3 ADDITIONS - scope-space / offset bookkeeping
 * ============================================================ */

ScopeSpace currscopespace(void);
unsigned   currscopeoffset(void);
void       incurrscopeoffset(void);

/* Function entry/exit: save current space+offsets, start fresh
 * local/formal spaces, restore on exit. */
void enterscopespace(void);
void exitscopespace(void);

void resetformalargsoffset(void);
void resetfunctionlocalsoffset(void);
void restorecurrscopeoffset(unsigned n);

/* Hidden-variable (temporary) generator: real symbol-table entry
 * named _t0,_t1,... in the current scope, with space+offset. */
symbol* newtemp(int line, int scope);

/* Anonymous-function name generator: _f0,_f1,... */
char* newfuncname(void);

/* temp-counter save/restore (per-function temp reuse) */
unsigned gettempcounter(void);
void     settempcounter(unsigned n);

#endif
