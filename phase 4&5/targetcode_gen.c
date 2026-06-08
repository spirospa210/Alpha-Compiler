#include "targetcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Convenience: emit one instruction with given fields. */
static void emit_t(vmopcode op, vmarg* res, vmarg* a1, vmarg* a2, unsigned line) {
    instruction t;
    t.opcode = op;
    if (res) t.result = *res; else { t.result.type = label_a; t.result.val = 0; }
    if (a1)  t.arg1   = *a1;  else { t.arg1.type   = label_a; t.arg1.val   = 0; }
    if (a2)  t.arg2   = *a2;  else { t.arg2.type   = label_a; t.arg2.val   = 0; }
    t.srcLine = line;
    emit_instr(&t);
}

/* A jump-family instruction whose target is a quad label that may not yet be
 * generated: emit with placeholder result, register as incomplete. */
static void emit_jump_like(vmopcode op, vmarg* a1, vmarg* a2, unsigned quadTarget, unsigned line) {
    instruction t;
    t.opcode = op;
    t.result.type = label_a; t.result.val = 0;     /* filled by patch_incomplete_jumps */
    if (a1) t.arg1 = *a1; else { t.arg1.type = label_a; t.arg1.val = 0; }
    if (a2) t.arg2 = *a2; else { t.arg2.type = label_a; t.arg2.val = 0; }
    t.srcLine = line;
    add_incomplete_jump(nextinstructionlabel(), quadTarget);
    emit_instr(&t);
}

/* set current quad's taddress to the next target-instruction index */
#define QUAD_TADDR(qptr) ((qptr)->taddress = nextinstructionlabel())

/* ---- function stack for returnList management (nested funcs) ---- */
typedef struct retlist_node { unsigned instrNo; struct retlist_node* next; } retlist_node;
typedef struct func_stack_node {
    symbol*       sym;
    retlist_node* returnList;   /* return-jumps -> funcexit instruction */
    retlist_node* skipList;     /* funcstart skip-jump -> instr after funcexit */
    struct func_stack_node* next;
} func_stack_node;
static func_stack_node* funcstack = NULL;

static void funcstack_push(symbol* s) {
    func_stack_node* n = malloc(sizeof(func_stack_node));
    n->sym = s; n->returnList = NULL; n->skipList = NULL; n->next = funcstack; funcstack = n;
}
static void func_add_retjump(unsigned instrNo) {
    assert(funcstack);
    retlist_node* n = malloc(sizeof(retlist_node));
    n->instrNo = instrNo; n->next = funcstack->returnList; funcstack->returnList = n;
}
static void func_add_skipjump(unsigned instrNo) {
    assert(funcstack);
    retlist_node* n = malloc(sizeof(retlist_node));
    n->instrNo = instrNo; n->next = funcstack->skipList; funcstack->skipList = n;
}
static void funcstack_pop_and_patch(void) {
    assert(funcstack);
    func_stack_node* top = funcstack;
    unsigned exitInstr = nextinstructionlabel() - 1;   /* the funcexit just emitted */
    unsigned afterExit = nextinstructionlabel();        /* instruction right after funcexit */
    for (retlist_node* r = top->returnList; r; ) {
        instructions[r->instrNo].result.type = label_a;
        instructions[r->instrNo].result.val  = exitInstr;
        retlist_node* nx = r->next; free(r); r = nx;
    }
    for (retlist_node* r = top->skipList; r; ) {
        instructions[r->instrNo].result.type = label_a;
        instructions[r->instrNo].result.val  = afterExit;
        retlist_node* nx = r->next; free(r); r = nx;
    }
    funcstack = top->next;
    free(top);
}

/* ============================================================
 *  Generators (one per i-code opcode)
 * ============================================================ */

static void generate(vmopcode op, quad* q) {
    QUAD_TADDR(q);
    vmarg res, a1, a2;
    int hasRes=0, hasA1=0, hasA2=0;
    if (q->result) { make_operand(q->result, &res); hasRes=1; }
    if (q->arg1)   { make_operand(q->arg1,   &a1);  hasA1=1; }
    if (q->arg2)   { make_operand(q->arg2,   &a2);  hasA2=1; }
    emit_t(op, hasRes?&res:NULL, hasA1?&a1:NULL, hasA2?&a2:NULL, q->line);
}

/* simple arithmetic / assign / table forms map straight across */
void generate_ASSIGN(quad* q)       { generate(assign_v, q); }
void generate_ADD(quad* q)          { generate(add_v, q); }
void generate_SUB(quad* q)          { generate(sub_v, q); }
void generate_MUL(quad* q)          { generate(mul_v, q); }
void generate_DIV(quad* q)          { generate(div_v, q); }
void generate_MOD(quad* q)          { generate(mod_v, q); }
void generate_NEWTABLE(quad* q)     { generate(newtable_v, q); }
void generate_TABLEGETELEM(quad* q) { generate(tablegetelem_v, q); }
void generate_TABLESETELEM(quad* q) { generate(tablesetelem_v, q); }
void generate_NOP(quad* q)          { instruction t; t.opcode=nop_v; t.result.type=t.arg1.type=t.arg2.type=label_a; t.result.val=t.arg1.val=t.arg2.val=0; t.srcLine=q->line; QUAD_TADDR(q); emit_instr(&t); }

/* uminus -> mul by -1 : result = arg1 * (-1) */
void generate_UMINUS(quad* q) {
    QUAD_TADDR(q);
    vmarg res, a1, neg1;
    make_operand(q->result, &res);
    make_operand(q->arg1,   &a1);
    make_numberoperand(&neg1, -1.0);
    emit_t(mul_v, &res, &a1, &neg1, q->line);
}

/* unconditional jump: target is q->label (a quad index) */
void generate_JUMP(quad* q) {
    QUAD_TADDR(q);
    emit_jump_like(jump_v, NULL, NULL, q->label, q->line);
}

/* relational branches: if (arg1 REL arg2) goto label.
 * Target instruction in result(label); operands arg1/arg2. */
static void generate_relational(vmopcode op, quad* q) {
    QUAD_TADDR(q);
    vmarg a1, a2;
    make_operand(q->arg1, &a1);
    make_operand(q->arg2, &a2);
    emit_jump_like(op, &a1, &a2, q->label, q->line);
}
void generate_IF_EQ(quad* q)        { generate_relational(jeq_v, q); }
void generate_IF_NOTEQ(quad* q)     { generate_relational(jne_v, q); }
void generate_IF_LESSEQ(quad* q)    { generate_relational(jle_v, q); }
void generate_IF_GREATEREQ(quad* q) { generate_relational(jge_v, q); }
void generate_IF_LESS(quad* q)      { generate_relational(jlt_v, q); }
void generate_IF_GREATER(quad* q)   { generate_relational(jgt_v, q); }

/* logical and/or/not are lowered in Phase 3 already (short-circuit produced
 * boolexpr with jumps + assign true/false). With that scheme these quad ops
 * should not appear; keep generators 1-1 for the dispatcher and lower defensively. */
void generate_AND(quad* q) {
    /* result = arg1 && arg2, via: jeq arg1,false ->+4 ; jeq arg2,false ->+3 ;
       assign true,result ; jump ->+2 ; assign false,result   (slide 24) */
    QUAD_TADDR(q);
    vmarg res, a1, a2, btrue, bfalse;
    make_operand(q->result, &res);
    make_operand(q->arg1, &a1);
    make_operand(q->arg2, &a2);
    make_booloperand(&bfalse, 0);
    make_booloperand(&btrue, 1);
    /* jeq arg1, false -> (instr +4 from first) */
    { instruction t; t.opcode=jeq_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+4; t.arg1=a1; t.arg2=bfalse; t.srcLine=q->line; emit_instr(&t); }
    { instruction t; t.opcode=jeq_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+3; t.arg1=a2; t.arg2=bfalse; t.srcLine=q->line; emit_instr(&t); }
    emit_t(assign_v, &res, &btrue, NULL, q->line);
    { instruction t; t.opcode=jump_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+2; t.arg1.type=t.arg2.type=label_a; t.arg1.val=t.arg2.val=0; t.srcLine=q->line; emit_instr(&t); }
    emit_t(assign_v, &res, &bfalse, NULL, q->line);
}
void generate_OR(quad* q) {
    QUAD_TADDR(q);
    vmarg res, a1, a2, btrue, bfalse;
    make_operand(q->result, &res);
    make_operand(q->arg1, &a1);
    make_operand(q->arg2, &a2);
    make_booloperand(&btrue, 1);
    make_booloperand(&bfalse, 0);
    { instruction t; t.opcode=jeq_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+4; t.arg1=a1; t.arg2=btrue; t.srcLine=q->line; emit_instr(&t); }
    { instruction t; t.opcode=jeq_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+3; t.arg1=a2; t.arg2=btrue; t.srcLine=q->line; emit_instr(&t); }
    emit_t(assign_v, &res, &bfalse, NULL, q->line);
    { instruction t; t.opcode=jump_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+2; t.arg1.type=t.arg2.type=label_a; t.arg1.val=t.arg2.val=0; t.srcLine=q->line; emit_instr(&t); }
    emit_t(assign_v, &res, &btrue, NULL, q->line);
}
void generate_NOT(quad* q) {
    /* result = !arg1 : jeq arg1,false ->+3 ; assign false,result ; jump ->+2 ; assign true,result (slide 23) */
    QUAD_TADDR(q);
    vmarg res, a1, btrue, bfalse;
    make_operand(q->result, &res);
    make_operand(q->arg1, &a1);
    make_booloperand(&bfalse, 0);
    make_booloperand(&btrue, 1);
    { instruction t; t.opcode=jeq_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+3; t.arg1=a1; t.arg2=bfalse; t.srcLine=q->line; emit_instr(&t); }
    emit_t(assign_v, &res, &bfalse, NULL, q->line);
    { instruction t; t.opcode=jump_v; t.result.type=label_a; t.result.val=nextinstructionlabel()+2; t.arg1.type=t.arg2.type=label_a; t.arg1.val=t.arg2.val=0; t.srcLine=q->line; emit_instr(&t); }
    emit_t(assign_v, &res, &btrue, NULL, q->line);
}

/* param -> pusharg (value in arg1 slot of quad) */
void generate_PARAM(quad* q) {
    QUAD_TADDR(q);
    vmarg a;
    make_operand(q->arg1, &a);
    emit_t(pusharg_v, NULL, &a, NULL, q->line);
}

/* call -> call (callee in arg1 slot of quad) */
void generate_CALL(quad* q) {
    QUAD_TADDR(q);
    vmarg a;
    make_operand(q->arg1, &a);
    emit_t(call_v, NULL, &a, NULL, q->line);
}

/* getretval -> assign result, retval */
void generate_GETRETVAL(quad* q) {
    QUAD_TADDR(q);
    vmarg res, rv;
    make_operand(q->result, &res);
    make_retvaloperand(&rv);
    emit_t(assign_v, &res, &rv, NULL, q->line);
}

/* funcstart -> jump (over body, to funcend) + funcenter.
 * Record the user-function's target address, push on funcstack.
 * The skip-jump's target (instruction after funcexit) is unknown now;
 * record it on the function's returnList so funcend patches it too. */
void generate_FUNCSTART(quad* q) {
    symbol* f = q->result->sym;
    QUAD_TADDR(q);
    funcstack_push(f);
    /* skip-jump over the function body */
    { instruction t; t.opcode=jump_v; t.result.type=label_a; t.result.val=0;
      t.arg1.type=t.arg2.type=label_a; t.arg1.val=t.arg2.val=0; t.srcLine=q->line;
      func_add_skipjump(nextinstructionlabel());
      emit_instr(&t); }
    /* funcenter is the actual entry address */
    f->taddress = nextinstructionlabel();
    userfuncs_newfunc(f);
    vmarg fa; make_operand(q->result, &fa);
    emit_t(funcenter_v, &fa, NULL, NULL, q->line);
}

/* return -> assign retval, value ; jump (to funcend, patched at funcend).
 * do_return emits: emit(ret_op, NULL, NULL, e) => value is in q->result. */
void generate_RETURN(quad* q) {
    QUAD_TADDR(q);
    vmarg rv;
    make_retvaloperand(&rv);
    if (q->result) {
        vmarg val; make_operand(q->result, &val);
        emit_t(assign_v, &rv, &val, NULL, q->line);
    }
    /* jump to funcend; target unknown until funcend -> record on funcstack */
    instruction t; t.opcode=jump_v; t.result.type=label_a; t.result.val=0;
    t.arg1.type=t.arg2.type=label_a; t.arg1.val=t.arg2.val=0; t.srcLine=q->line;
    func_add_retjump(nextinstructionlabel());
    emit_instr(&t);
}

/* funcend -> funcexit ; then patch all return jumps to this funcexit */
void generate_FUNCEND(quad* q) {
    QUAD_TADDR(q);
    vmarg fa; make_operand(q->result, &fa);
    emit_t(funcexit_v, &fa, NULL, NULL, q->line);
    funcstack_pop_and_patch();
}

/* ---- dispatcher: 1-1 with iopcode enum order ---- */
typedef void (*generator_func_t)(quad*);

static generator_func_t generators[] = {
    generate_ASSIGN,        /* assign_op        */
    generate_ADD,           /* add_op           */
    generate_SUB,           /* sub_op           */
    generate_MUL,           /* mul_op           */
    generate_DIV,           /* div_op           */
    generate_MOD,           /* mod_op           */
    generate_UMINUS,        /* uminus_op        */
    generate_AND,           /* and_op           */
    generate_OR,            /* or_op            */
    generate_NOT,           /* not_op           */
    generate_IF_EQ,         /* if_eq_op         */
    generate_IF_NOTEQ,      /* if_noteq_op      */
    generate_IF_LESSEQ,     /* if_lesseq_op     */
    generate_IF_GREATEREQ,  /* if_greatereq_op  */
    generate_IF_LESS,       /* if_less_op       */
    generate_IF_GREATER,    /* if_greater_op    */
    generate_JUMP,          /* jump_op          */
    generate_CALL,          /* call_op          */
    generate_PARAM,         /* param_op         */
    generate_RETURN,        /* ret_op           */
    generate_GETRETVAL,     /* getretval_op     */
    generate_FUNCSTART,     /* funcstart_op     */
    generate_FUNCEND,       /* funcend_op       */
    generate_NEWTABLE,      /* tablecreate_op   */
    generate_TABLEGETELEM,  /* tablegetelem_op  */
    generate_TABLESETELEM   /* tablesetelem_op  */
};

void generate_target_code(void) {
    for (unsigned i = 0; i < currQuad; i++)
        (*generators[quads[i].op])(&quads[i]);
    patch_incomplete_jumps();
}
