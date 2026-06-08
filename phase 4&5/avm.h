#ifndef AVM_H
#define AVM_H

#include "targetcode.h"

/* ============================================================
 *  Phase 5 - Virtual Machine
 * ============================================================ */

#define AVM_STACKSIZE       4096
#define AVM_WIPEOUT(m)      memset(&(m), 0, sizeof(m))
#define AVM_STACKENV_SIZE   4
#define AVM_ENDING_PC       codeSize

/* environment-value offsets within an activation record (relative to topsp) */
#define AVM_NUMACTUALS_OFFSET  4
#define AVM_SAVEDPC_OFFSET     3
#define AVM_SAVEDTOP_OFFSET    2
#define AVM_SAVEDTOPSP_OFFSET  1

typedef enum avm_memcell_t {
    number_m = 0,
    string_m,
    bool_m,
    table_m,
    userfunc_m,
    libfunc_m,
    nil_m,
    undef_m
} avm_memcell_t;

typedef struct avm_table avm_table;

typedef struct avm_memcell {
    avm_memcell_t type;
    union {
        double          numVal;
        char*           strVal;
        unsigned char   boolVal;
        avm_table*      tableVal;
        unsigned        funcVal;     /* userfunc: target address */
        char*           libfuncVal;  /* libfunc: name */
    } data;
} avm_memcell;

/* ---- table (hash buckets for numeric + string + misc keys) ---- */
#define AVM_TABLE_HASHSIZE 211

typedef struct avm_table_bucket {
    avm_memcell key;
    avm_memcell value;
    struct avm_table_bucket* next;       /* hash chain */
    struct avm_table_bucket* orderNext;  /* insertion order chain */
} avm_table_bucket;

struct avm_table {
    unsigned          refCounter;
    avm_table_bucket* numIndexed [AVM_TABLE_HASHSIZE];  /* numeric keys */
    avm_table_bucket* strIndexed [AVM_TABLE_HASHSIZE];  /* string keys  */
    avm_table_bucket* boolIndexed[2];                   /* bool keys    */
    avm_table_bucket* userIndexed[AVM_TABLE_HASHSIZE];  /* userfunc keys*/
    avm_table_bucket* libIndexed [AVM_TABLE_HASHSIZE];  /* libfunc keys */
    avm_table_bucket* tableIndexed[AVM_TABLE_HASHSIZE]; /* table keys (by addr) */
    avm_table_bucket* orderHead;                        /* insertion order */
    avm_table_bucket* orderTail;
    unsigned          total;
};

/* ---- VM registers / state ---- */
extern avm_memcell  stack[AVM_STACKSIZE];
extern avm_memcell  ax, bx, cx;
extern avm_memcell  retval;
extern unsigned     top, topsp;
extern unsigned     vm_total_globals;

extern unsigned     pc;
extern unsigned     codeSize;
extern unsigned     currLine;
extern unsigned char executionFinished;
extern instruction* code;

/* ---- loader ---- */
void avm_load_binary(const char* filename);   /* fills constant tables + code[] */

/* ---- table API ---- */
avm_table*   avm_tablenew(void);
void         avm_tabledestroy(avm_table* t);
avm_memcell* avm_tablegetelem(avm_table* t, avm_memcell* key);
void         avm_tablesetelem(avm_table* t, avm_memcell* key, avm_memcell* value);
void         avm_tableincrefcounter(avm_table* t);
void         avm_tabledecrefcounter(avm_table* t);

/* ---- memcell management ---- */
void avm_memcellclear(avm_memcell* m);
void avm_assign(avm_memcell* lv, avm_memcell* rv);
void avm_memcell_inc_refcounter(avm_memcell* m);

/* ---- operand translation ---- */
avm_memcell* avm_translate_operand(vmarg* arg, avm_memcell* reg);
unsigned     avm_userfunc_addr(unsigned idx);   /* userfunc table index -> address */

/* ---- conversions / display ---- */
unsigned char avm_tobool(avm_memcell* m);
char*         avm_tostring(avm_memcell* m);          /* malloc'd, caller frees */
extern char*  typeStrings[];

/* ---- execution ---- */
void avm_initialize(void);
void execute_cycle(void);
void avm_run(void);

/* ---- errors / warnings ---- */
void avm_error(const char* fmt, ...);
void avm_warning(const char* fmt, ...);

/* ---- environment helpers ---- */
unsigned avm_get_envvalue(unsigned i);
void     avm_push_envvalue(unsigned val);
unsigned avm_totalactuals(void);
avm_memcell* avm_getactual(unsigned i);
void     avm_dec_top(void);
void     avm_call_functor(avm_table* t);
void     avm_callsaveenvironment(void);

/* ---- library functions ---- */
typedef void (*library_func_t)(void);
void           avm_registerlibfunc(char* id, library_func_t addr);
library_func_t avm_getlibraryfunc(char* id);
void           avm_calllibfunc(char* id);
void           avm_initlibfuncs(void);

#endif
