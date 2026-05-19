#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdbool.h>

typedef enum {
    program_var,
    function_local,
    formal_arg
} scopespace_t;

typedef enum {
    GLOBAL_VAR,
    LOCAL_VAR,
    FORMAL_ARG,
    USER_FUNC,
    LIB_FUNC
} SymbolType;

typedef struct SymbolTableEntry {
    char* name;
    SymbolType type;
    int line;
    int scope;
    bool isActive;
    
    // --- Phase 3 Additions ---
    scopespace_t space;
    unsigned offset;
    unsigned taddress;      // Function jump address
    unsigned totalLocals;   // Function total locals
    
    struct SymbolTableEntry* next_in_hash;
    struct SymbolTableEntry* next_in_scope;
} SymbolTableEntry;

void init_symtable();
SymbolTableEntry* insert_symbol(char* name, SymbolType type, int line, int scope);
SymbolTableEntry* lookup_scope(char* name, int scope);
SymbolTableEntry* lookup_all(char* name, int current_scope);
void hide_scope(int scope);
void print_symtable();
void insert_library_functions();

// --- Phase 3 Scope & Offset API ---
scopespace_t currscopespace();
unsigned currscopeoffset();
void inccurrscopeoffset();
void entering_function();
void exiting_function();
void resetformalargsoffset();
void resetfunctionlocalsoffset();

// --- Phase 3 Temporary Variables ---
SymbolTableEntry* newtemp(int line, int scope);
void resettemp();

#endif