#include "targetcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* ============================================================
 *  Constant tables - unique values, lookup-then-insert (FAQ #11)
 * ============================================================ */

#define CT_INIT 64

static double*   numConsts   = NULL;  static unsigned numConsts_total   = 0, numConsts_cap   = 0;
static char**    strConsts   = NULL;  static unsigned strConsts_total   = 0, strConsts_cap   = 0;
static char**    namedLibfuncs= NULL; static unsigned libfuncs_total    = 0, libfuncs_cap    = 0;
static userfunc* userFuncs   = NULL;  static unsigned userFuncs_total   = 0, userFuncs_cap   = 0;

unsigned consts_newnumber(double n) {
    for (unsigned i = 0; i < numConsts_total; i++)
        if (numConsts[i] == n) return i;
    if (numConsts_total == numConsts_cap) {
        numConsts_cap = numConsts_cap ? numConsts_cap * 2 : CT_INIT;
        numConsts = realloc(numConsts, numConsts_cap * sizeof(double));
    }
    numConsts[numConsts_total] = n;
    return numConsts_total++;
}

unsigned consts_newstring(char* s) {
    for (unsigned i = 0; i < strConsts_total; i++)
        if (strcmp(strConsts[i], s) == 0) return i;
    if (strConsts_total == strConsts_cap) {
        strConsts_cap = strConsts_cap ? strConsts_cap * 2 : CT_INIT;
        strConsts = realloc(strConsts, strConsts_cap * sizeof(char*));
    }
    strConsts[strConsts_total] = strdup(s);
    return strConsts_total++;
}

unsigned libfuncs_newused(char* s) {
    for (unsigned i = 0; i < libfuncs_total; i++)
        if (strcmp(namedLibfuncs[i], s) == 0) return i;
    if (libfuncs_total == libfuncs_cap) {
        libfuncs_cap = libfuncs_cap ? libfuncs_cap * 2 : CT_INIT;
        namedLibfuncs = realloc(namedLibfuncs, libfuncs_cap * sizeof(char*));
    }
    namedLibfuncs[libfuncs_total] = strdup(s);
    return libfuncs_total++;
}

/* user functions are keyed by symbol identity (unique definitions). */
unsigned userfuncs_newfunc(symbol* sym) {
    for (unsigned i = 0; i < userFuncs_total; i++)
        if (userFuncs[i].address == sym->taddress &&
            strcmp(userFuncs[i].id, sym->name) == 0) return i;
    if (userFuncs_total == userFuncs_cap) {
        userFuncs_cap = userFuncs_cap ? userFuncs_cap * 2 : CT_INIT;
        userFuncs = realloc(userFuncs, userFuncs_cap * sizeof(userfunc));
    }
    userFuncs[userFuncs_total].address   = sym->taddress;
    userFuncs[userFuncs_total].localSize = sym->totalLocals;
    userFuncs[userFuncs_total].id        = strdup(sym->name ? sym->name : "");
    return userFuncs_total++;
}

double    consts_getnumber(unsigned i){ assert(i < numConsts_total); return numConsts[i]; }
char*     consts_getstring(unsigned i){ assert(i < strConsts_total); return strConsts[i]; }
char*     libfuncs_getused(unsigned i){ assert(i < libfuncs_total);  return namedLibfuncs[i]; }
userfunc* userfuncs_getfunc(unsigned i){ assert(i < userFuncs_total); return &userFuncs[i]; }

unsigned total_numConsts(void){ return numConsts_total; }
unsigned total_strConsts(void){ return strConsts_total; }
unsigned total_libfuncs (void){ return libfuncs_total; }
unsigned total_userfuncs(void){ return userFuncs_total; }

/* ============================================================
 *  Target-code instruction array
 * ============================================================ */

instruction* instructions      = NULL;
unsigned     totalInstructions = 0;
unsigned     currInstruction   = 0;
static unsigned instr_cap      = 0;

unsigned nextinstructionlabel(void) { return currInstruction; }

void emit_instr(instruction* t) {
    if (currInstruction == instr_cap) {
        instr_cap = instr_cap ? instr_cap * 2 : 1024;
        instructions = realloc(instructions, instr_cap * sizeof(instruction));
    }
    instructions[currInstruction++] = *t;
    totalInstructions = currInstruction;
}

/* ============================================================
 *  expr* -> vmarg  (slide 19 make_operand)
 * ============================================================ */

void make_operand(expr* e, vmarg* arg) {
    if (!e) { arg->type = nil_a; arg->val = 0; return; }  /* defensive */

    switch (e->type) {
        /* variable-like: stored in a memory cell via space+offset */
        case var_e:
        case tableitem_e:
        case arithexpr_e:
        case boolexpr_e:
        case assignexpr_e:
        case newtable_e: {
            assert(e->sym);
            arg->val = e->sym->offset;
            switch (e->sym->space) {
                case PROGRAM_SCOPE:   arg->type = global_a; break;
                case FUNCTION_LOCAL:  arg->type = local_a;  break;
                case FORMAL_ARGUMENT: arg->type = formal_a; break;
                default: assert(0);
            }
            break;
        }

        /* constants */
        case constbool_e:
            arg->val  = e->boolConst;
            arg->type = bool_a;
            break;
        case conststring_e:
            arg->val  = consts_newstring(e->strConst);
            arg->type = string_a;
            break;
        case constnum_e:
            arg->val  = consts_newnumber(e->numConst);
            arg->type = number_a;
            break;
        case nil_e:
            arg->type = nil_a;
            break;

        /* functions */
        case programfunc_e:
            arg->type = userfunc_a;
            arg->val  = userfuncs_newfunc(e->sym);
            break;
        case libraryfunc_e:
            arg->type = libfunc_a;
            arg->val  = libfuncs_newused(e->sym->name);
            break;

        default: assert(0);
    }
}

void make_numberoperand(vmarg* arg, double val) {
    arg->val  = consts_newnumber(val);
    arg->type = number_a;
}
void make_booloperand(vmarg* arg, unsigned val) {
    arg->val  = val;
    arg->type = bool_a;
}
void make_retvaloperand(vmarg* arg) {
    arg->type = retval_a;
}

/* ============================================================
 *  Incomplete jumps (slide 21)
 * ============================================================ */

static incomplete_jump* ij_head  = NULL;
static unsigned         ij_total = 0;

void add_incomplete_jump(unsigned instrNo, unsigned iaddress) {
    incomplete_jump* n = malloc(sizeof(incomplete_jump));
    n->instrNo  = instrNo;
    n->iaddress = iaddress;
    n->next     = ij_head;
    ij_head     = n;
    ij_total++;
}

void patch_incomplete_jumps(void) {
    for (incomplete_jump* x = ij_head; x; x = x->next) {
        if (x->iaddress == currQuad)            /* jump to end of i-code */
            instructions[x->instrNo].result.val = totalInstructions;
        else
            instructions[x->instrNo].result.val = quads[x->iaddress].taddress;
    }
}
