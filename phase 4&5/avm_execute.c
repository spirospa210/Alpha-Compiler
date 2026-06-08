#include "avm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>

/* ============================================================
 *  errors / warnings
 * ============================================================ */
void avm_error(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "Runtime error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, " (line %u)\n", currLine);
    va_end(ap);
    executionFinished = 1;
}
void avm_warning(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "Runtime warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ============================================================
 *  environment values on the stack
 * ============================================================ */
unsigned totalActuals = 0;

void avm_dec_top(void) {
    if (top == 0) { avm_error("stack overflow"); }
    else --top;
}
static void avm_push_envvalue_impl(unsigned val) {
    stack[top].type = number_m;
    stack[top].data.numVal = (double)val;
    avm_dec_top();
}
void avm_push_envvalue(unsigned val) { avm_push_envvalue_impl(val); }

unsigned avm_get_envvalue(unsigned i) {
    assert(stack[i].type == number_m);
    unsigned val = (unsigned)stack[i].data.numVal;
    assert((double)val == stack[i].data.numVal);
    return val;
}

void avm_callsaveenvironment(void) {
    avm_push_envvalue(totalActuals);
    avm_push_envvalue(pc + 1);                 /* return address */
    avm_push_envvalue(top + totalActuals + 2); /* saved top (before call seq) */
    avm_push_envvalue(topsp);
}

unsigned avm_totalactuals(void) {
    return avm_get_envvalue(topsp + AVM_NUMACTUALS_OFFSET);
}
avm_memcell* avm_getactual(unsigned i) {
    assert(i < avm_totalactuals());
    return &stack[topsp + AVM_STACKENV_SIZE + 1 + i];
}

/* ============================================================
 *  call / funcenter / funcexit / pusharg
 * ============================================================ */
static userfunc* avm_getfuncinfo(unsigned addr) {
    extern userfunc* avm_getfuncinfo_addr(unsigned);
    return avm_getfuncinfo_addr(addr);
}

void avm_call_functor(avm_table* t) {
    /* Functor convention: the CALLER has already pushed the table as the first
       actual (the `this`/self argument) before issuing `call <table>`, exactly
       as the method-call desugaring obj..m(args) pushes obj as first actual.
       Here we only locate the table's "()" member and transfer control to it. */
    avm_memcell key; key.type = string_m; key.data.strVal = "()";
    avm_memcell* fn = avm_tablegetelem(t, &key);
    if (!fn) { avm_error("illegal call: table has no '()' member"); return; }
    if (fn->type == userfunc_m) {
        avm_callsaveenvironment();
        pc = fn->data.funcVal;
        assert(pc < AVM_ENDING_PC);
        assert(code[pc].opcode == funcenter_v);
    } else if (fn->type == libfunc_m) {
        avm_callsaveenvironment();
        extern void avm_calllibfunc(char*);
        avm_calllibfunc(fn->data.libfuncVal);
    } else if (fn->type == table_m) {
        avm_call_functor(fn->data.tableVal);
    } else {
        avm_error("illegal '()' member type in functor call");
    }
}

void execute_call(instruction* instr) {
    avm_memcell* func = avm_translate_operand(&instr->arg1, &ax);
    assert(func);
    switch (func->type) {
        case userfunc_m: {
            avm_callsaveenvironment();
            pc = func->data.funcVal;
            assert(pc < AVM_ENDING_PC);
            assert(code[pc].opcode == funcenter_v);
            break;
        }
        case string_m:  /* calling a string => treated as libfunc name? spec: runtime error */
            avm_error("cannot call something that is not a function (string)");
            break;
        case libfunc_m:
            avm_callsaveenvironment();
            { extern void avm_calllibfunc(char*); avm_calllibfunc(func->data.libfuncVal); }
            break;
        case table_m:
            avm_call_functor(func->data.tableVal);
            break;
        default:
            avm_error("call: cannot bind '%s' to a callable", typeStrings[func->type]);
    }
}

void execute_pusharg(instruction* instr) {
    avm_memcell* arg = avm_translate_operand(&instr->arg1, &ax);
    avm_assign(&stack[top], arg);
    ++totalActuals;
    avm_dec_top();
}

void execute_funcenter(instruction* instr) {
    avm_memcell* func = avm_translate_operand(&instr->result, &ax);
    assert(func->type == userfunc_m);
    assert(pc == func->data.funcVal);

    totalActuals = 0;
    userfunc* f = avm_getfuncinfo(pc);
    topsp = top;
    top = top - (f ? f->localSize : 0);
    if ((int)top < 0) { avm_error("stack overflow"); }
}

void execute_funcexit(instruction* unused) {
    (void)unused;
    unsigned oldTop = top;
    top   = avm_get_envvalue(topsp + AVM_SAVEDTOP_OFFSET);
    pc    = avm_get_envvalue(topsp + AVM_SAVEDPC_OFFSET);
    topsp = avm_get_envvalue(topsp + AVM_SAVEDTOPSP_OFFSET);

    while (++oldTop <= top)         /* intentionally skipping first */
        avm_memcellclear(&stack[oldTop]);
}

/* ============================================================
 *  assign / getretval
 * ============================================================ */
void execute_assign(instruction* instr) {
    avm_memcell* lv = avm_translate_operand(&instr->result, NULL);
    avm_memcell* rv = avm_translate_operand(&instr->arg1, &ax);
    assert(lv && (&stack[0] <= lv && lv <= &stack[AVM_STACKSIZE-1] || lv == &retval));
    assert(rv);
    avm_assign(lv, rv);
}

/* ============================================================
 *  arithmetic
 * ============================================================ */
static double add_impl(double x,double y){return x+y;}
static double sub_impl(double x,double y){return x-y;}
static double mul_impl(double x,double y){return x*y;}
static double div_impl(double x,double y){return x/y;}
static double mod_impl(double x,double y){return (double)(((long long)x) % ((long long)y));}
typedef double (*arithmetic_func_t)(double,double);
static arithmetic_func_t arithmeticFuncs[] = { add_impl, sub_impl, mul_impl, div_impl, mod_impl };

void execute_arithmetic(instruction* instr) {
    avm_memcell* lv  = avm_translate_operand(&instr->result, NULL);
    avm_memcell* rv1 = avm_translate_operand(&instr->arg1, &ax);
    avm_memcell* rv2 = avm_translate_operand(&instr->arg2, &bx);

    if (rv1->type != number_m || rv2->type != number_m) {
        avm_error("not a number in arithmetic!");
        return;
    }
    if ((instr->opcode == div_v || instr->opcode == mod_v) && rv2->data.numVal == 0) {
        avm_error("division by zero");
        return;
    }
    arithmetic_func_t op = arithmeticFuncs[instr->opcode - add_v];
    avm_memcellclear(lv);
    lv->type = number_m;
    lv->data.numVal = op(rv1->data.numVal, rv2->data.numVal);
}

/* ============================================================
 *  relational (ordering)
 * ============================================================ */
static unsigned char jle_impl(double x,double y){return x<=y;}
static unsigned char jge_impl(double x,double y){return x>=y;}
static unsigned char jlt_impl(double x,double y){return x< y;}
static unsigned char jgt_impl(double x,double y){return x> y;}

static void execute_cmp(instruction* instr, unsigned char (*cmp)(double,double)) {
    avm_memcell* rv1 = avm_translate_operand(&instr->arg1, &ax);
    avm_memcell* rv2 = avm_translate_operand(&instr->arg2, &bx);
    if (rv1->type != number_m || rv2->type != number_m) {
        avm_error("not a number in comparison!");
        return;
    }
    if (cmp(rv1->data.numVal, rv2->data.numVal))
        pc = instr->result.val;
}
void execute_jle(instruction* i){ execute_cmp(i, jle_impl); }
void execute_jge(instruction* i){ execute_cmp(i, jge_impl); }
void execute_jlt(instruction* i){ execute_cmp(i, jlt_impl); }
void execute_jgt(instruction* i){ execute_cmp(i, jgt_impl); }

/* ============================================================
 *  equality (jeq / jne)
 * ============================================================ */
static void execute_eq(instruction* instr, int wantEqual) {
    assert(instr->result.type == label_a);
    avm_memcell* rv1 = avm_translate_operand(&instr->arg1, &ax);
    avm_memcell* rv2 = avm_translate_operand(&instr->arg2, &bx);

    unsigned char result = 0;

    if (rv1->type == undef_m || rv2->type == undef_m) {
        avm_error("'undef' involved in equality!");
        return;
    }
    if (rv1->type == nil_m || rv2->type == nil_m) {
        result = (rv1->type == nil_m && rv2->type == nil_m);
    }
    else if (rv1->type == bool_m || rv2->type == bool_m) {
        result = (avm_tobool(rv1) == avm_tobool(rv2));
    }
    else if (rv1->type != rv2->type) {
        avm_error("%s == %s is illegal!", typeStrings[rv1->type], typeStrings[rv2->type]);
        return;
    }
    else {
        switch (rv1->type) {
            case number_m:   result = rv1->data.numVal == rv2->data.numVal; break;
            case string_m:   result = strcmp(rv1->data.strVal, rv2->data.strVal) == 0; break;
            case table_m:    result = rv1->data.tableVal == rv2->data.tableVal; break;
            case userfunc_m: result = rv1->data.funcVal == rv2->data.funcVal; break;
            case libfunc_m:  result = strcmp(rv1->data.libfuncVal, rv2->data.libfuncVal) == 0; break;
            default: break;
        }
    }
    if (!executionFinished) {
        unsigned char take = wantEqual ? result : !result;
        if (take) pc = instr->result.val;
    }
}
void execute_jeq(instruction* i){ execute_eq(i, 1); }
void execute_jne(instruction* i){ execute_eq(i, 0); }

/* ============================================================
 *  jump / nop
 * ============================================================ */
void execute_jump(instruction* instr) { pc = instr->result.val; }
void execute_nop(instruction* unused) { (void)unused; }

/* and/or/not should not occur (lowered), but keep stubs for dispatcher safety */
void execute_and(instruction* i){ (void)i; avm_error("unexpected 'and' opcode"); }
void execute_or (instruction* i){ (void)i; avm_error("unexpected 'or' opcode"); }
void execute_not(instruction* i){ (void)i; avm_error("unexpected 'not' opcode"); }
void execute_uminus(instruction* i){ (void)i; avm_error("unexpected 'uminus' opcode"); }

/* ============================================================
 *  getretval (assign result, retval) is emitted as assign_v, so no separate op.
 *  But keep handler in case getretval_v is used directly.
 * ============================================================ */
void execute_getretval(instruction* instr) {
    avm_memcell* lv = avm_translate_operand(&instr->result, NULL);
    avm_assign(lv, &retval);
}

/* ============================================================
 *  tables
 * ============================================================ */
void execute_newtable(instruction* instr) {
    avm_memcell* lv = avm_translate_operand(&instr->result, NULL);
    avm_memcellclear(lv);
    lv->type = table_m;
    lv->data.tableVal = avm_tablenew();
    avm_tableincrefcounter(lv->data.tableVal);
}

void execute_tablegetelem(instruction* instr) {
    avm_memcell* lv = avm_translate_operand(&instr->result, NULL);
    avm_memcell* t  = avm_translate_operand(&instr->arg1, NULL);
    avm_memcell* i  = avm_translate_operand(&instr->arg2, &ax);

    avm_memcellclear(lv);
    lv->type = nil_m;

    if (t->type != table_m) {
        avm_error("illegal use of type %s as table!", typeStrings[t->type]);
        return;
    }
    avm_memcell* content = avm_tablegetelem(t->data.tableVal, i);
    if (content) {
        avm_assign(lv, content);
    } else {
        char* ts = avm_tostring(t);
        char* is = avm_tostring(i);
        avm_warning("%s[%s] not found!", ts, is);
        free(ts); free(is);
    }
}

void execute_tablesetelem(instruction* instr) {
    avm_memcell* t = avm_translate_operand(&instr->result, NULL);
    avm_memcell* i = avm_translate_operand(&instr->arg1, &ax);
    avm_memcell* c = avm_translate_operand(&instr->arg2, &bx);

    if (t->type != table_m) {
        avm_error("illegal use of type %s as table!", typeStrings[t->type]);
        return;
    }
    if (i->type == nil_m || i->type == undef_m) {
        avm_error("nil/undef cannot be used as table index!");
        return;
    }
    avm_tablesetelem(t->data.tableVal, i, c);
}

/* ============================================================
 *  dispatcher (1-1 with vmopcode enum)
 * ============================================================ */
typedef void (*execute_func_t)(instruction*);

static execute_func_t executeFuncs[] = {
    execute_assign,        /* assign_v */
    execute_arithmetic,    /* add_v */
    execute_arithmetic,    /* sub_v */
    execute_arithmetic,    /* mul_v */
    execute_arithmetic,    /* div_v */
    execute_arithmetic,    /* mod_v */
    execute_uminus,        /* uminus_v */
    execute_and,           /* and_v */
    execute_or,            /* or_v */
    execute_not,           /* not_v */
    execute_jeq,           /* jeq_v */
    execute_jne,           /* jne_v */
    execute_jle,           /* jle_v */
    execute_jge,           /* jge_v */
    execute_jlt,           /* jlt_v */
    execute_jgt,           /* jgt_v */
    execute_call,          /* call_v */
    execute_pusharg,       /* pusharg_v */
    execute_funcenter,     /* funcenter_v */
    execute_funcexit,      /* funcexit_v */
    execute_newtable,      /* newtable_v */
    execute_tablegetelem,  /* tablegetelem_v */
    execute_tablesetelem,  /* tablesetelem_v */
    execute_nop,           /* nop_v */
    execute_jump,          /* jump_v */
    execute_getretval      /* getretval_v */
};

#define AVM_MAX_INSTRUCTIONS (getretval_v)

void execute_cycle(void) {
    if (executionFinished) return;
    if (pc == AVM_ENDING_PC) { executionFinished = 1; return; }

    assert(pc < AVM_ENDING_PC);
    instruction* instr = code + pc;
    if(getenv("VMTRACE")) fprintf(stderr,"[pc=%u op=%d top=%u topsp=%u]\n",pc,instr->opcode,top,topsp);
    assert(instr->opcode >= 0 && instr->opcode <= AVM_MAX_INSTRUCTIONS);
    if (instr->srcLine) currLine = instr->srcLine;
    unsigned oldPC = pc;
    (*executeFuncs[instr->opcode])(instr);
    if (pc == oldPC) ++pc;
}

void avm_initialize(void) {
    top = AVM_STACKSIZE - 1 - vm_total_globals;
    topsp = 0;
    executionFinished = 0;
    pc = 0;
    AVM_WIPEOUT(ax); AVM_WIPEOUT(bx); AVM_WIPEOUT(cx); AVM_WIPEOUT(retval);
    ax.type = bx.type = cx.type = retval.type = undef_m;
    extern void avm_initlibfuncs(void);
    avm_initlibfuncs();
}

void avm_run(void) {
    while (!executionFinished) execute_cycle();
}
