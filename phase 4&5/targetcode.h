#ifndef TARGETCODE_H
#define TARGETCODE_H

#include "quads.h"

/* ============================================================
 *  Phase 4 - Target (virtual-machine) code
 * ============================================================ */

/* VM opcodes. Order is 1-1 with the dispatcher executeFuncs[] in the VM,
 * but here it only needs to be a stable encoding. We follow the lecture
 * set (slide 18/26/31). */
typedef enum vmopcode {
    assign_v = 0,
    add_v, sub_v, mul_v, div_v, mod_v,
    uminus_v,                /* present in enum; emitted as mul by -1 in practice */
    and_v, or_v, not_v,      /* present in enum; logicals lowered to jumps */
    jeq_v, jne_v,
    jle_v, jge_v, jlt_v, jgt_v,
    call_v, pusharg_v,
    funcenter_v, funcexit_v,
    newtable_v, tablegetelem_v, tablesetelem_v,
    nop_v, jump_v,
    getretval_v              /* materialises a call's return value into an lvalue */
} vmopcode;

/* Operand types (slide 19). */
typedef enum vmarg_t {
    label_a    = 0,
    global_a   = 1,
    formal_a   = 2,
    local_a    = 3,
    number_a   = 4,
    string_a   = 5,
    bool_a     = 6,
    nil_a      = 7,
    userfunc_a = 8,
    libfunc_a  = 9,
    retval_a   = 10
} vmarg_t;

typedef struct vmarg {
    vmarg_t  type;
    unsigned val;
} vmarg;

typedef struct instruction {
    vmopcode opcode;
    vmarg    result;
    vmarg    arg1;
    vmarg    arg2;
    unsigned srcLine;
} instruction;

/* userfunc record for the user-function constant table (slide 28 grammar). */
typedef struct userfunc {
    unsigned address;    /* target-code address of the funcenter */
    unsigned localSize;  /* number of local variables */
    char*    id;         /* function name */
} userfunc;

/* ---- constant tables (unique values; lookup-then-insert, FAQ #11) ---- */
unsigned consts_newstring (char* s);
unsigned consts_newnumber (double n);
unsigned libfuncs_newused (char* s);
unsigned userfuncs_newfunc(symbol* sym);

/* read-back accessors (used by the VM loader / debug dumps) */
double   consts_getnumber (unsigned i);
char*    consts_getstring (unsigned i);
char*    libfuncs_getused (unsigned i);
userfunc*userfuncs_getfunc(unsigned i);

unsigned total_numConsts (void);
unsigned total_strConsts (void);
unsigned total_libfuncs  (void);
unsigned total_userfuncs (void);

/* ---- target-code instruction array ---- */
extern instruction* instructions;
extern unsigned     totalInstructions;
extern unsigned     currInstruction;   /* == next emit index */

unsigned nextinstructionlabel(void);
void     emit_instr(instruction* t);

/* ---- expr* -> vmarg conversion (slide 19) ---- */
void make_operand (expr* e, vmarg* arg);
void make_numberoperand (vmarg* arg, double val);
void make_booloperand   (vmarg* arg, unsigned val);
void make_retvaloperand (vmarg* arg);

/* ---- incomplete jumps (slide 21) ---- */
typedef struct incomplete_jump {
    unsigned                 instrNo;   /* the jump instruction number */
    unsigned                 iaddress;  /* the i-code jump-target (quad) address */
    struct incomplete_jump*  next;
} incomplete_jump;

void add_incomplete_jump  (unsigned instrNo, unsigned iaddress);
void patch_incomplete_jumps(void);

/* ---- the generator dispatcher ---- */
void generate_target_code(void);   /* iterate quads, fill instructions[] */

/* ---- debug + binary output ---- */
void write_targetcode_text  (const char* filename);  /* human-readable dump */
void write_targetcode_binary(const char* filename);   /* VM binary (magic 340200501) */

#define MAGICNUMBER 340200501u

#endif
