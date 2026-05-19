#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"

#define HASH_SIZE 512
#define MAX_SCOPES 100

SymbolTableEntry* hash_table[HASH_SIZE];
SymbolTableEntry* scope_lists[MAX_SCOPES];
int max_scope_reached = 0;

/* --- Phase 3 Globals --- */
unsigned programVarOffset = 0;
unsigned functionLocalOffset = 0;
unsigned formalArgOffset = 0;
unsigned scopeSpaceCounter = 1;
int tempCounter = 0;

/* --- Phase 3 Scope API --- */
scopespace_t currscopespace() {
    if (scopeSpaceCounter == 1) return program_var;
    if (scopeSpaceCounter % 2 == 0) return formal_arg;
    return function_local;
}

unsigned currscopeoffset() {
    switch (currscopespace()) {
        case program_var: return programVarOffset;
        case function_local: return functionLocalOffset;
        case formal_arg: return formalArgOffset;
        default: return 0;
    }
}

void inccurrscopeoffset() {
    switch (currscopespace()) {
        case program_var: programVarOffset++; break;
        case function_local: functionLocalOffset++; break;
        case formal_arg: formalArgOffset++; break;
    }
}

void entering_function() { scopeSpaceCounter++; }
void exiting_function() { scopeSpaceCounter--; }
void resetformalargsoffset() { formalArgOffset = 0; }
void resetfunctionlocalsoffset() { functionLocalOffset = 0; }

/* --- Phase 3 Temps --- */
char* newtempname() {
    char name[16];
    sprintf(name, "_t%d", tempCounter++);
    return strdup(name);
}
void resettemp() { tempCounter = 0; }

SymbolTableEntry* newtemp(int line, int scope) {
    char* name = newtempname();
    SymbolTableEntry* sym = lookup_scope(name, scope);
    if (!sym) {
        // Pass to insert_symbol which will auto-assign offsets
        sym = insert_symbol(name, LOCAL_VAR, line, scope); 
    }
    return sym;
}

/* --- Hash Table Functions --- */
unsigned int hash(char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) { hash = ((hash << 5) + hash) + c; }
    return hash % HASH_SIZE;
}

void init_symtable() {
    for (int i = 0; i < HASH_SIZE; i++) hash_table[i] = NULL;
    for (int i = 0; i < MAX_SCOPES; i++) scope_lists[i] = NULL;
    insert_library_functions();
}

void insert_library_functions() {
    char* lib_funcs[] = {
        "print", "input", "objectmemberkeys", "objecttotalmembers",
        "objectcopy", "totalarguments", "argument", "typeof",
        "strtonum", "sqrt", "cos", "sin"
    };
    for (int i = 0; i < 12; i++) {
        insert_symbol(strdup(lib_funcs[i]), LIB_FUNC, 0, 0);
    }
}

SymbolTableEntry* insert_symbol(char* name, SymbolType type, int line, int scope) {
    unsigned int index = hash(name);
    SymbolTableEntry* new_entry = (SymbolTableEntry*)malloc(sizeof(SymbolTableEntry));
    
    new_entry->name = strdup(name);
    new_entry->type = type;
    new_entry->line = line;
    new_entry->scope = scope;
    new_entry->isActive = true;
    
    // --- Phase 3: Assign Offset and Space ---
    new_entry->space = currscopespace();
    new_entry->offset = currscopeoffset();
    inccurrscopeoffset();
    
    new_entry->next_in_hash = hash_table[index];
    hash_table[index] = new_entry;
    
    new_entry->next_in_scope = NULL;
    if (scope_lists[scope] == NULL) {
        scope_lists[scope] = new_entry;
    } else {
        SymbolTableEntry* curr = scope_lists[scope];
        while (curr->next_in_scope != NULL) { curr = curr->next_in_scope; }
        curr->next_in_scope = new_entry;
    }
    
    if (scope > max_scope_reached) max_scope_reached = scope;
    return new_entry;
}

SymbolTableEntry* lookup_scope(char* name, int scope) {
    SymbolTableEntry* curr = scope_lists[scope];
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0 && curr->isActive) { return curr; }
        curr = curr->next_in_scope;
    }
    return NULL;
}

SymbolTableEntry* lookup_all(char* name, int current_scope) {
    for (int i = current_scope; i >= 0; i--) {
        SymbolTableEntry* found = lookup_scope(name, i);
        if (found) return found;
    }
    return NULL;
}

void hide_scope(int scope) {
    SymbolTableEntry* curr = scope_lists[scope];
    while (curr != NULL) {
        curr->isActive = false;
        curr = curr->next_in_scope;
    }
}

const char* get_type_string(SymbolType type) {
    switch(type) {
        case GLOBAL_VAR: return "[global variable]";
        case LOCAL_VAR: return "[local variable]";
        case FORMAL_ARG: return "[formal argument]";
        case USER_FUNC: return "[user function]";
        case LIB_FUNC: return "[library function]";
        default: return "[unknown]";
    }
}

void print_symtable() {
    for (int i = 0; i <= max_scope_reached; i++) {
        if (scope_lists[i] == NULL) continue;
        printf("\n-----------    Scope #%d    -----------\n", i);
        SymbolTableEntry* curr = scope_lists[i];
        while (curr != NULL) {
            printf("\"%s\" %s (line %d) (scope %d) (offset %d)\n", 
                curr->name, get_type_string(curr->type), 
                curr->line, curr->scope, curr->offset);
            curr = curr->next_in_scope;
        }
    }
}