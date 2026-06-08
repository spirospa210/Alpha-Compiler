#include "avm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>

extern unsigned totalActuals;
extern void execute_funcexit(instruction*);

/* ---- library-func registry (simple name->fn map) ---- */
typedef struct lib_entry { char* id; library_func_t fn; struct lib_entry* next; } lib_entry;
static lib_entry* lib_head = NULL;

void avm_registerlibfunc(char* id, library_func_t addr) {
    lib_entry* e = malloc(sizeof(lib_entry));
    e->id = strdup(id); e->fn = addr; e->next = lib_head; lib_head = e;
}
library_func_t avm_getlibraryfunc(char* id) {
    for (lib_entry* e = lib_head; e; e = e->next)
        if (strcmp(e->id, id) == 0) return e->fn;
    return NULL;
}

void avm_calllibfunc(char* id) {
    library_func_t f = avm_getlibraryfunc(id);
    if (!f) { avm_error("unsupported lib func '%s' called!", id); return; }
    /* manual enter: no stack locals */
    topsp = top;
    totalActuals = 0;
    (*f)();
    if (!executionFinished)
        execute_funcexit((instruction*)0);
}

/* ============================================================
 *  print
 * ============================================================ */
static void libfunc_print(void) {
    unsigned n = avm_totalactuals();
    for (unsigned i = 0; i < n; i++) {
        char* s = avm_tostring(avm_getactual(i));
        fputs(s, stdout);
        free(s);
    }
}

/* ============================================================
 *  typeof
 * ============================================================ */
static const char* typeof_strings[] = {
    "number", "string", "boolean", "table",
    "userfunction", "libraryfunction", "nil", "undef"
};
static void libfunc_typeof(void) {
    unsigned n = avm_totalactuals();
    if (n != 1) { avm_error("one argument (not %u) expected in 'typeof'!", n); return; }
    avm_memcellclear(&retval);
    retval.type = string_m;
    retval.data.strVal = strdup(typeof_strings[avm_getactual(0)->type]);
}

/* ============================================================
 *  input
 * ============================================================ */
static void libfunc_input(void) {
    char buf[1024];
    if (!fgets(buf, sizeof buf, stdin)) {
        avm_memcellclear(&retval); retval.type = nil_m; return;
    }
    size_t L = strlen(buf);
    while (L && (buf[L-1]=='\n' || buf[L-1]=='\r')) buf[--L] = 0;

    avm_memcellclear(&retval);
    /* number? */
    char* end; double d = strtod(buf, &end);
    if (end != buf && *end == 0) { retval.type = number_m; retval.data.numVal = d; return; }
    if (strcmp(buf,"true")==0)  { retval.type = bool_m; retval.data.boolVal = 1; return; }
    if (strcmp(buf,"false")==0) { retval.type = bool_m; retval.data.boolVal = 0; return; }
    if (strcmp(buf,"nil")==0)   { retval.type = nil_m; return; }
    retval.type = string_m; retval.data.strVal = strdup(buf);
}

/* ============================================================
 *  object* family
 * ============================================================ */
static void libfunc_objecttotalmembers(void) {
    unsigned n = avm_totalactuals();
    if (n != 1 || avm_getactual(0)->type != table_m) {
        avm_error("one table argument expected in 'objecttotalmembers'!");
        avm_memcellclear(&retval); retval.type = nil_m; return;
    }
    avm_memcellclear(&retval);
    retval.type = number_m;
    retval.data.numVal = avm_getactual(0)->data.tableVal->total;
}

static void libfunc_objectmemberkeys(void) {
    unsigned n = avm_totalactuals();
    if (n != 1 || avm_getactual(0)->type != table_m) {
        avm_error("one table argument expected in 'objectmemberkeys'!");
        avm_memcellclear(&retval); retval.type = nil_m; return;
    }
    avm_table* src = avm_getactual(0)->data.tableVal;
    avm_table* keys = avm_tablenew();
    unsigned idx = 0;
    for (avm_table_bucket* b = src->orderHead; b; b = b->orderNext) {
        avm_memcell k; k.type = number_m; k.data.numVal = idx++;
        avm_tablesetelem(keys, &k, &b->key);
    }
    avm_memcellclear(&retval);
    retval.type = table_m;
    retval.data.tableVal = keys;
    avm_tableincrefcounter(keys);
}

static void table_copy_into(avm_table* dst, avm_table* src) {
    for (avm_table_bucket* b = src->orderHead; b; b = b->orderNext)
        avm_tablesetelem(dst, &b->key, &b->value);   /* shallow: refs copied */
}
static void libfunc_objectcopy(void) {
    unsigned n = avm_totalactuals();
    if (n != 1 || avm_getactual(0)->type != table_m) {
        avm_error("one table argument expected in 'objectcopy'!");
        avm_memcellclear(&retval); retval.type = nil_m; return;
    }
    avm_table* src = avm_getactual(0)->data.tableVal;
    avm_table* cp  = avm_tablenew();
    table_copy_into(cp, src);
    avm_memcellclear(&retval);
    retval.type = table_m;
    retval.data.tableVal = cp;
    avm_tableincrefcounter(cp);
}

/* ============================================================
 *  totalarguments / argument(i)  - step ONE activation record down
 * ============================================================ */
static void libfunc_totalarguments(void) {
    unsigned p_topsp = avm_get_envvalue(topsp + AVM_SAVEDTOPSP_OFFSET);
    avm_memcellclear(&retval);
    if (!p_topsp) {                       /* no previous activation record */
        avm_warning("'totalarguments' called outside a function!");
        retval.type = nil_m;
    } else {
        retval.type = number_m;
        retval.data.numVal = avm_get_envvalue(p_topsp + AVM_NUMACTUALS_OFFSET);
    }
}
static void libfunc_argument(void) {
    unsigned p_topsp = avm_get_envvalue(topsp + AVM_SAVEDTOPSP_OFFSET);
    unsigned n = avm_totalactuals();
    avm_memcellclear(&retval);
    if (!p_topsp) { avm_warning("'argument' called outside a function!"); retval.type = nil_m; return; }
    if (n != 1 || avm_getactual(0)->type != number_m) { avm_error("argument(i): integer expected"); retval.type=nil_m; return; }
    unsigned i = (unsigned)avm_getactual(0)->data.numVal;
    unsigned p_total = avm_get_envvalue(p_topsp + AVM_NUMACTUALS_OFFSET);
    if (i >= p_total) { retval.type = nil_m; return; }
    avm_assign(&retval, &stack[p_topsp + AVM_STACKENV_SIZE + 1 + i]);
}

/* ============================================================
 *  strtonum / sqrt / cos / sin
 * ============================================================ */
static void libfunc_strtonum(void) {
    if (avm_totalactuals()!=1 || avm_getactual(0)->type != string_m) {
        avm_error("one string expected in 'strtonum'!"); avm_memcellclear(&retval); retval.type=nil_m; return;
    }
    char* s = avm_getactual(0)->data.strVal;
    char* end; double d = strtod(s, &end);
    avm_memcellclear(&retval);
    if (end != s && *end == 0) { retval.type = number_m; retval.data.numVal = d; }
    else retval.type = nil_m;
}
static void libfunc_sqrt(void) {
    if (avm_totalactuals()!=1 || avm_getactual(0)->type != number_m) { avm_error("number expected in 'sqrt'!"); avm_memcellclear(&retval); retval.type=nil_m; return; }
    double x = avm_getactual(0)->data.numVal;
    avm_memcellclear(&retval);
    if (x < 0) retval.type = nil_m;
    else { retval.type = number_m; retval.data.numVal = sqrt(x); }
}
static void libfunc_cos(void) {
    if (avm_totalactuals()!=1 || avm_getactual(0)->type != number_m) { avm_error("number expected in 'cos'!"); avm_memcellclear(&retval); retval.type=nil_m; return; }
    double deg = avm_getactual(0)->data.numVal;
    avm_memcellclear(&retval); retval.type = number_m;
    retval.data.numVal = cos(deg * M_PI / 180.0);
}
static void libfunc_sin(void) {
    if (avm_totalactuals()!=1 || avm_getactual(0)->type != number_m) { avm_error("number expected in 'sin'!"); avm_memcellclear(&retval); retval.type=nil_m; return; }
    double deg = avm_getactual(0)->data.numVal;
    avm_memcellclear(&retval); retval.type = number_m;
    retval.data.numVal = sin(deg * M_PI / 180.0);
}

void avm_initlibfuncs(void) {
    avm_registerlibfunc("print",              libfunc_print);
    avm_registerlibfunc("typeof",             libfunc_typeof);
    avm_registerlibfunc("input",              libfunc_input);
    avm_registerlibfunc("objecttotalmembers", libfunc_objecttotalmembers);
    avm_registerlibfunc("objectmemberkeys",   libfunc_objectmemberkeys);
    avm_registerlibfunc("objectcopy",         libfunc_objectcopy);
    avm_registerlibfunc("totalarguments",     libfunc_totalarguments);
    avm_registerlibfunc("argument",           libfunc_argument);
    avm_registerlibfunc("strtonum",           libfunc_strtonum);
    avm_registerlibfunc("sqrt",               libfunc_sqrt);
    avm_registerlibfunc("cos",                libfunc_cos);
    avm_registerlibfunc("sin",                libfunc_sin);
}
