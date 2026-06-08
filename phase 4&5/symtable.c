#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"

#define HASH_SIZE 512
#define MAX_SCOPES 100

SymbolTableEntry* hash_table[HASH_SIZE];
SymbolTableEntry* scope_lists[MAX_SCOPES];
int max_scope_reached = 0;

unsigned int hash(char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
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

    /* PHASE 3: default-initialise additive fields (overwritten as needed). */
    new_entry->space       = PROGRAM_SCOPE;
    new_entry->offset      = 0;
    new_entry->totalLocals = 0;
    new_entry->totalFormals = 0;
    new_entry->iaddress    = 0;
    new_entry->taddress    = 0;
    new_entry->isTemp      = false;
    
    // Insert into hash table (LIFO is fine here, it's just for fast lookup)
    new_entry->next_in_hash = hash_table[index];
    hash_table[index] = new_entry;
    
    // Insert into scope list (FIFO - append to tail to preserve insertion order!)
    new_entry->next_in_scope = NULL;
    if (scope_lists[scope] == NULL) {
        scope_lists[scope] = new_entry;
    } else {
        SymbolTableEntry* curr = scope_lists[scope];
        while (curr->next_in_scope != NULL) {
            curr = curr->next_in_scope;
        }
        curr->next_in_scope = new_entry;
    }
    
    if (scope > max_scope_reached) max_scope_reached = scope;
    
    return new_entry;
}

SymbolTableEntry* lookup_scope(char* name, int scope) {
    SymbolTableEntry* curr = scope_lists[scope];
    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0 && curr->isActive) {
            return curr;
        }
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
            printf("\"%s\" %s (line %d) (scope %d)\n", 
                curr->name, 
                get_type_string(curr->type), 
                curr->line, 
                curr->scope);
            curr = curr->next_in_scope;
        }
    }
}

/* ============================================================
 *  PHASE 3 ADDITIONS (appended) - scope-space / offset / temps
 *  None of the code above this banner was changed in behaviour;
 *  only the additive field initialisation in insert_symbol().
 * ============================================================ */

/* --- per-scope-space offset counters --- */
static unsigned programVarOffset   = 0;
static unsigned functionLocalOffset = 0;
static unsigned formalArgOffset    = 0;

/* current active scope-space (PROGRAM_SCOPE at top level) */
static ScopeSpace scopeSpaceCounter = PROGRAM_SCOPE;

/* stack used to save/restore the local-offset across nested functions */
#define SCOPE_STACK_MAX 1024
static unsigned scopeOffsetStack[SCOPE_STACK_MAX];
static int      scopeOffsetStackTop = 0;

ScopeSpace currscopespace(void) {
    return scopeSpaceCounter;
}

unsigned currscopeoffset(void) {
    switch (scopeSpaceCounter) {
        case PROGRAM_SCOPE:    return programVarOffset;
        case FUNCTION_LOCAL:   return functionLocalOffset;
        case FORMAL_ARGUMENT:  return formalArgOffset;
        default:               return 0;
    }
}

void incurrscopeoffset(void) {
    switch (scopeSpaceCounter) {
        case PROGRAM_SCOPE:    ++programVarOffset;    break;
        case FUNCTION_LOCAL:   ++functionLocalOffset; break;
        case FORMAL_ARGUMENT:  ++formalArgOffset;     break;
    }
}

/* Entering a function definition: the locals of the enclosing context
 * must be preserved.  We push the current function-local offset and
 * switch the active space to FUNCTION_LOCAL with a fresh counter. */
void enterscopespace(void) {
    if (scopeOffsetStackTop < SCOPE_STACK_MAX)
        scopeOffsetStack[scopeOffsetStackTop++] = functionLocalOffset;
    scopeSpaceCounter = FUNCTION_LOCAL;
}

void exitscopespace(void) {
    if (scopeOffsetStackTop > 0)
        functionLocalOffset = scopeOffsetStack[--scopeOffsetStackTop];
    /* On exit we are no longer inside this function's local space.
     * The caller decides which space becomes active next via the
     * funcdef action (it restores PROGRAM_SCOPE at global level or the
     * enclosing FUNCTION_LOCAL).  We default back to PROGRAM_SCOPE when
     * the stack is empty (i.e. we returned to global scope). */
    if (scopeOffsetStackTop == 0)
        scopeSpaceCounter = PROGRAM_SCOPE;
    else
        scopeSpaceCounter = FUNCTION_LOCAL;
}

void resetformalargsoffset(void) {
    formalArgOffset = 0;
}

void resetfunctionlocalsoffset(void) {
    functionLocalOffset = 0;
}

void restorecurrscopeoffset(unsigned n) {
    switch (scopeSpaceCounter) {
        case PROGRAM_SCOPE:    programVarOffset    = n; break;
        case FUNCTION_LOCAL:   functionLocalOffset = n; break;
        case FORMAL_ARGUMENT:  formalArgOffset     = n; break;
    }
}

/* --- temporary (hidden) variable generation --- */

static unsigned tempCounter = 0;  /* current next-temp number */

unsigned gettempcounter(void) { return tempCounter; }
void     settempcounter(unsigned n) { tempCounter = n; }

char* newtempname(void) {
    char buf[32];
    sprintf(buf, "_t%u", tempCounter++);
    return strdup(buf);
}

/* newtemp: creates (or reuses) a real symbol named _tN in the current
 * scope.  Temps count in the active scope-space's offset. */
symbol* newtemp(int line, int scope) {
    char* name = newtempname();
    SymbolTableEntry* sym = lookup_scope(name, scope);
    if (sym == NULL || !sym->isActive) {
        sym = insert_symbol(name, (scope == 0) ? GLOBAL_VAR : LOCAL_VAR, line, scope);
        sym->isTemp = true;
        sym->space  = currscopespace();
        sym->offset = currscopeoffset();
        incurrscopeoffset();
    }
    free(name);
    return sym;
}

/* --- anonymous function naming: _f0, _f1, ... --- */

static unsigned anonFuncCounter = 0;

char* newfuncname(void) {
    char buf[32];
    sprintf(buf, "_f%u", anonFuncCounter++);
    return strdup(buf);
}
