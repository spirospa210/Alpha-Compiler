#ifndef SYMTABLE_H
#define SYMTABLE_H

#include <stdbool.h>

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

#endif
