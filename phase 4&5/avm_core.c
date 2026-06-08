#include "avm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

/* ============================================================
 *  VM registers / state
 * ============================================================ */
avm_memcell  stack[AVM_STACKSIZE];
avm_memcell  ax, bx, cx;
avm_memcell  retval;
unsigned     top, topsp;
unsigned     vm_total_globals = 0;
unsigned     pc = 0;
unsigned     codeSize = 0;
unsigned     currLine = 0;
unsigned char executionFinished = 0;
instruction* code = NULL;

char* typeStrings[] = {
    "number", "string", "bool", "table",
    "userfunc", "libfunc", "nil", "undef"
};

/* ---- loaded constant tables (owned by the VM after load) ---- */
static double*   vm_numbers   = NULL; static unsigned vm_numbers_n = 0;
static char**    vm_strings   = NULL; static unsigned vm_strings_n = 0;
static userfunc* vm_userfuncs = NULL; static unsigned vm_userfuncs_n = 0;
static char**    vm_libfuncs  = NULL; static unsigned vm_libfuncs_n = 0;

double consts_getnumber_vm(unsigned i){ assert(i<vm_numbers_n); return vm_numbers[i]; }
char*  consts_getstring_vm(unsigned i){ assert(i<vm_strings_n); return vm_strings[i]; }
char*  libfuncs_getused_vm(unsigned i){ assert(i<vm_libfuncs_n); return vm_libfuncs[i]; }
userfunc* avm_getfuncinfo_addr(unsigned addr) {
    for (unsigned i=0;i<vm_userfuncs_n;i++) if (vm_userfuncs[i].address==addr) return &vm_userfuncs[i];
    return NULL;
}

/* ============================================================
 *  Binary loader  (mirror of write_targetcode_binary)
 * ============================================================ */
static unsigned rd_u(FILE* f){ unsigned v; if(fread(&v,sizeof v,1,f)!=1){avm_error("bad binary (u)");} return v; }
static double   rd_d(FILE* f){ double v; if(fread(&v,sizeof v,1,f)!=1){avm_error("bad binary (d)");} return v; }
static unsigned char rd_b(FILE* f){ unsigned char v; if(fread(&v,sizeof v,1,f)!=1){avm_error("bad binary (b)");} return v; }
static char*    rd_str(FILE* f){
    unsigned len = rd_u(f);
    char* s = malloc(len);
    if (fread(s,1,len,f)!=len){ avm_error("bad binary (str)"); }
    return s;
}
static void rd_arg(FILE* f, vmarg* a){ a->type=(vmarg_t)rd_b(f); a->val=rd_u(f); }

void avm_load_binary(const char* filename) {
    FILE* f = fopen(filename, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", filename); exit(1); }

    unsigned magic = rd_u(f);
    if (magic != MAGICNUMBER) { fprintf(stderr, "bad magic %u\n", magic); exit(1); }

    vm_total_globals = rd_u(f);   /* program globals -> top starts below them */

    vm_strings_n = rd_u(f);
    vm_strings = malloc(vm_strings_n * sizeof(char*));
    for (unsigned i=0;i<vm_strings_n;i++) vm_strings[i] = rd_str(f);

    vm_numbers_n = rd_u(f);
    vm_numbers = malloc(vm_numbers_n * sizeof(double));
    for (unsigned i=0;i<vm_numbers_n;i++) vm_numbers[i] = rd_d(f);

    vm_userfuncs_n = rd_u(f);
    vm_userfuncs = malloc(vm_userfuncs_n * sizeof(userfunc));
    for (unsigned i=0;i<vm_userfuncs_n;i++) {
        vm_userfuncs[i].address   = rd_u(f);
        vm_userfuncs[i].localSize = rd_u(f);
        vm_userfuncs[i].id        = rd_str(f);
    }

    vm_libfuncs_n = rd_u(f);
    vm_libfuncs = malloc(vm_libfuncs_n * sizeof(char*));
    for (unsigned i=0;i<vm_libfuncs_n;i++) vm_libfuncs[i] = rd_str(f);

    codeSize = rd_u(f);
    code = malloc(codeSize * sizeof(instruction));
    for (unsigned i=0;i<codeSize;i++) {
        code[i].opcode = (vmopcode)rd_b(f);
        rd_arg(f, &code[i].result);
        rd_arg(f, &code[i].arg1);
        rd_arg(f, &code[i].arg2);
        code[i].srcLine = rd_u(f);
    }
    fclose(f);
}

/* ============================================================
 *  memcell management
 * ============================================================ */
void avm_memcellclear(avm_memcell* m) {
    if (m->type == string_m && m->data.strVal) { free(m->data.strVal); m->data.strVal = NULL; }
    else if (m->type == table_m && m->data.tableVal) { avm_tabledecrefcounter(m->data.tableVal); m->data.tableVal = NULL; }
    m->type = undef_m;
}

void avm_memcell_inc_refcounter(avm_memcell* m) {
    if (m->type == table_m) avm_tableincrefcounter(m->data.tableVal);
}

void avm_assign(avm_memcell* lv, avm_memcell* rv) {
    if (lv == rv) return;
    if (lv->type == table_m && rv->type == table_m && lv->data.tableVal == rv->data.tableVal) return;

    avm_memcellclear(lv);
    *lv = *rv;
    if (lv->type == string_m) lv->data.strVal = strdup(rv->data.strVal);
    else if (lv->type == table_m) avm_tableincrefcounter(lv->data.tableVal);
}

/* ============================================================
 *  operand translation (slide 30)
 * ============================================================ */
static void load_const_to_reg(vmarg* arg, avm_memcell* reg) {
    switch (arg->type) {
        case number_a: reg->type=number_m; reg->data.numVal=consts_getnumber_vm(arg->val); break;
        case string_a: reg->type=string_m; reg->data.strVal=strdup(consts_getstring_vm(arg->val)); break;
        case bool_a:   reg->type=bool_m;   reg->data.boolVal=arg->val?1:0; break;
        case nil_a:    reg->type=nil_m; break;
        case userfunc_a: reg->type=userfunc_m; reg->data.funcVal=avm_userfunc_addr(arg->val); break;
        case libfunc_a:  reg->type=libfunc_m;  reg->data.libfuncVal=libfuncs_getused_vm(arg->val); break;
        default: fprintf(stderr,"load_const_to_reg: bad type %d val %u\n", arg->type, arg->val); assert(0);
    }
}

/* userfunc operand 'val' is an index into the userfunc table; return its address */
unsigned avm_userfunc_addr(unsigned idx) {
    assert(idx < vm_userfuncs_n);
    return vm_userfuncs[idx].address;
}

avm_memcell* avm_translate_operand(vmarg* arg, avm_memcell* reg) {
    switch (arg->type) {
        /* variables -> stack memory cells */
        case global_a: return &stack[AVM_STACKSIZE - 1 - arg->val];
        case local_a:  return &stack[topsp - arg->val];
        case formal_a: return &stack[topsp + AVM_STACKENV_SIZE + 1 + arg->val];
        case retval_a: return &retval;
        /* constants / functions -> loaded into the passed register */
        default:
            load_const_to_reg(arg, reg);
            return reg;
    }
}

/* ============================================================
 *  tobool / tostring  (slides 41, FAQ #10 format)
 * ============================================================ */
static unsigned char number_tobool(avm_memcell* m){ return m->data.numVal != 0; }
static unsigned char string_tobool(avm_memcell* m){ return m->data.strVal[0] != 0; }
static unsigned char bool_tobool  (avm_memcell* m){ return m->data.boolVal; }
static unsigned char table_tobool (avm_memcell* m){ (void)m; return 1; }
static unsigned char userfunc_tobool(avm_memcell* m){ (void)m; return 1; }
static unsigned char libfunc_tobool(avm_memcell* m){ (void)m; return 1; }
static unsigned char nil_tobool   (avm_memcell* m){ (void)m; return 0; }
static unsigned char undef_tobool (avm_memcell* m){ (void)m; assert(0); return 0; }

static unsigned char (*toboolFuncs[])(avm_memcell*) = {
    number_tobool, string_tobool, bool_tobool, table_tobool,
    userfunc_tobool, libfunc_tobool, nil_tobool, undef_tobool
};
unsigned char avm_tobool(avm_memcell* m) {
    assert(m->type >= 0 && m->type < undef_m + 1);
    return toboolFuncs[m->type](m);
}

/* number formatting: %g-style (integers without decimals) per libFunctions.asc */
static char* number_tostring(double n) {
    char buf[64];
    /* integers: no decimals; else compact %g (default 6 sig digits) */
    if (n == (long long)n && fabs(n) < 1e15)
        snprintf(buf, sizeof buf, "%lld", (long long)n);
    else
        snprintf(buf, sizeof buf, "%g", n);
    return strdup(buf);
}

/* forward: table -> string ( [{key : value}, ...] with trailing ", " ) */
static char* table_tostring(avm_table* t);

char* avm_tostring(avm_memcell* m) {
    switch (m->type) {
        case number_m:   return number_tostring(m->data.numVal);
        case string_m:   return strdup(m->data.strVal);
        case bool_m:     return strdup(m->data.boolVal ? "true" : "false");
        case table_m:    return table_tostring(m->data.tableVal);
        case nil_m:      return strdup("nil");
        case undef_m:    return strdup("undef");
        case userfunc_m: {
            /* libFunctions.asc shows user func printed as "name()" */
            userfunc* u = avm_getfuncinfo_addr(m->data.funcVal);
            char buf[128];
            snprintf(buf, sizeof buf, "%s()", u ? u->id : "");
            return strdup(buf);
        }
        case libfunc_m: {
            char buf[128];
            snprintf(buf, sizeof buf, "lib::%s", m->data.libfuncVal);
            return strdup(buf);
        }
        default: return strdup("?");
    }
}

/* table printing: gather all elements as {key : value}, with trailing ", " */
#include "avm_table_print.inc"
