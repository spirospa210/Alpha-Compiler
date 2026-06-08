#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "actions.h"

int act_current_scope = 0;

/* ---- value coercion: materialize a boolexpr into a temp ---- */
expr* to_value(expr* e, int line) {
    if (e == NULL) return NULL;
    if (e->type != boolexpr_e) return e;

    symbol* t = newtemp(line, act_current_scope);
    expr* res = lvalue_expr(t);

    backpatch(e->truelist, nextquadlabel());
    emit(assign_op, newexpr_constbool(1), NULL, res, 0, line);   /* assign _t true  */
    unsigned skip = nextquadlabel();
    emit(jump_op, NULL, NULL, NULL, 0, line);
    backpatch(e->falselist, nextquadlabel());
    emit(assign_op, newexpr_constbool(0), NULL, res, 0, line);   /* assign _t false */
    backpatch(makelist(skip), nextquadlabel());
    return res;
}

/* ---- arithmetic with constant folding ---- */
expr* do_arith(iopcode op, expr* a, expr* b, int line) {
    a = to_value(a, line);
    b = to_value(b, line);
    if (a && b && a->type == constnum_e && b->type == constnum_e) {
        double x = a->numConst, y = b->numConst, r = 0;
        switch (op) {
            case add_op: r = x + y; break;
            case sub_op: r = x - y; break;
            case mul_op: r = x * y; break;
            case div_op:
                if (y == 0) { fprintf(stderr,
                    "[COMPILER WARNING] - division with 0 (when evaluating division with constants) | line %d\n", line);
                    return newexpr_constnum(0); }
                r = x / y; break;
            case mod_op:
                if (y == 0) { fprintf(stderr,
                    "[COMPILER WARNING] - division with 0 (when evaluating division with constants) | line %d\n", line);
                    return newexpr_constnum(0); }
                r = (double)(((long long)x) % ((long long)y)); break;
            default: break;
        }
        return newexpr_constnum(r);
    }
    if ((op == div_op || op == mod_op) && b && b->type == constnum_e && b->numConst == 0)
        fprintf(stderr, "[COMPILER WARNING] - division with 0 | line %d\n", line);

    expr* res = newexpr(arithexpr_e);
    res->sym = newtemp(line, act_current_scope);
    emit(op, a, b, res, 0, line);
    return res;
}

/* ---- relational -> boolexpr ---- */
expr* do_relop(iopcode ifop, expr* a, expr* b, int line) {
    a = to_value(a, line);
    b = to_value(b, line);
    expr* e = newexpr(boolexpr_e);
    e->truelist  = makelist(nextquadlabel());
    emit(ifop, a, b, NULL, 0, line);            /* truelist quad  */
    e->falselist = makelist(nextquadlabel());
    emit(jump_op, NULL, NULL, NULL, 0, line);   /* falselist quad */
    return e;
}

/* ---- true-test a non-boolean into a boolexpr ---- */
expr* to_bool(expr* e, int line) {
    if (e->type == boolexpr_e) return e;
    expr* b = newexpr(boolexpr_e);
    b->truelist  = makelist(nextquadlabel());
    emit(if_eq_op, e, newexpr_constbool(1), NULL, 0, line);  /* IF_EQ e TRUE _ */
    b->falselist = makelist(nextquadlabel());
    emit(jump_op, NULL, NULL, NULL, 0, line);
    return b;
}

expr* do_and(expr* e1, unsigned M, expr* e2, int line) {
    (void)line;
    e1 = to_bool(e1, line);
    e2 = to_bool(e2, line);
    backpatch(e1->truelist, M);
    expr* e = newexpr(boolexpr_e);
    e->truelist  = e2->truelist;
    e->falselist = merge_list(e1->falselist, e2->falselist);
    return e;
}

expr* do_or(expr* e1, unsigned M, expr* e2, int line) {
    (void)line;
    e1 = to_bool(e1, line);
    e2 = to_bool(e2, line);
    backpatch(e1->falselist, M);
    expr* e = newexpr(boolexpr_e);
    e->truelist  = merge_list(e1->truelist, e2->truelist);
    e->falselist = e2->falselist;
    return e;
}

expr* do_not(expr* e1, int line) {
    e1 = to_bool(e1, line);
    expr* e = newexpr(boolexpr_e);
    e->truelist  = e1->falselist;
    e->falselist = e1->truelist;
    return e;
}

/* ---- assignment ---- */
expr* do_assign(expr* lval, expr* rhs, int line) {
    rhs = to_value(rhs, line);
    if (lval->type == tableitem_e) {
        emit(tablesetelem_op, lval->index, rhs, lvalue_expr(lval->sym), 0, line);
        /* trailing extra-emit for a table item == read the element back */
        return emit_iftableitem(lval, line, act_current_scope);
    } else {
        emit(assign_op, rhs, NULL, lval, 0, line);
        symbol* t = newtemp(line, act_current_scope);
        expr* tmp = lvalue_expr(t);
        emit(assign_op, lval, NULL, tmp, 0, line);
        return tmp;
    }
}

/* ---- unary minus ---- */
expr* do_uminus(expr* e, int line) {
    e = to_value(e, line);
    if (e->type == constnum_e) return newexpr_constnum(-e->numConst);
    expr* res = newexpr(arithexpr_e);
    res->sym = newtemp(line, act_current_scope);
    emit(uminus_op, e, NULL, res, 0, line);
    return res;
}

/* ---- inc/dec ---- */
expr* do_preincdec(expr* lval, int isInc, int line) {
    iopcode op = isInc ? add_op : sub_op;
    if (lval->type == tableitem_e) {
        expr* val = emit_iftableitem(lval, line, act_current_scope);
        emit(op, val, newexpr_constnum(1), val, 0, line);
        emit(tablesetelem_op, lval->index, val, lvalue_expr(lval->sym), 0, line);
        return val;
    } else {
        emit(op, lval, newexpr_constnum(1), lval, 0, line);   /* x = x +/- 1 */
        symbol* t = newtemp(line, act_current_scope);
        expr* tmp = lvalue_expr(t);
        emit(assign_op, lval, NULL, tmp, 0, line);            /* value = new x */
        return tmp;
    }
}

expr* do_postincdec(expr* lval, int isInc, int line) {
    iopcode op = isInc ? add_op : sub_op;
    if (lval->type == tableitem_e) {
        /* match inc_dec.alpha: old-value temp allocated FIRST (lower number),
         * get-temp allocated second, but get quad emitted first. */
        symbol* oldt = newtemp(line, act_current_scope);              /* _t(old) */
        expr* old = lvalue_expr(oldt);
        symbol* gett = newtemp(line, act_current_scope);              /* _t(get) */
        expr* val = lvalue_expr(gett);
        emit(tablegetelem_op, lvalue_expr(lval->sym), lval->index, val, 0, line);
        emit(assign_op, val, NULL, old, 0, line);
        emit(op, val, newexpr_constnum(1), val, 0, line);
        emit(tablesetelem_op, lval->index, val, lvalue_expr(lval->sym), 0, line);
        return old;
    } else {
        symbol* t = newtemp(line, act_current_scope);
        expr* tmp = lvalue_expr(t);
        emit(assign_op, lval, NULL, tmp, 0, line);            /* capture old */
        emit(op, lval, newexpr_constnum(1), lval, 0, line);   /* x = x +/- 1 */
        return tmp;
    }
}

/* ---- object construction ---- */
expr* do_objectdef_elist(expr** elems, int n, int line) {
    symbol* t = newtemp(line, act_current_scope);
    expr* tab = lvalue_expr(t);
    emit(tablecreate_op, NULL, NULL, tab, 0, line);
    for (int i = n - 1; i >= 0; i--) {
        expr* v = to_value(elems[i], line);
        emit(tablesetelem_op, newexpr_constnum(i), v, tab, 0, line);
    }
    return tab;
}

expr* do_objectdef_indexed(expr** keys, expr** vals, int n, int line) {
    symbol* t = newtemp(line, act_current_scope);
    expr* tab = lvalue_expr(t);
    emit(tablecreate_op, NULL, NULL, tab, 0, line);
    for (int i = n - 1; i >= 0; i--) {
        expr* k = to_value(keys[i], line);
        expr* v = to_value(vals[i], line);
        emit(tablesetelem_op, k, v, tab, 0, line);
    }
    return tab;
}

/* ---- calls ---- */
expr* do_call(expr* callee, expr** args, int n, int line) {
    for (int i = n - 1; i >= 0; i--) {
        expr* a = to_value(args[i], line);
        emit(param_op, a, NULL, NULL, 0, line);
    }
    emit(call_op, callee, NULL, NULL, 0, line);
    symbol* t = newtemp(line, act_current_scope);
    expr* ret = lvalue_expr(t);
    emit(getretval_op, NULL, NULL, ret, 0, line);
    return ret;
}

expr* do_methodcall(expr* obj, const char* name, expr** args, int n, int line) {
    obj = emit_iftableitem(obj, line, act_current_scope);
    /* fetch the method as obj["name"] */
    symbol* mt = newtemp(line, act_current_scope);
    expr* method = lvalue_expr(mt);
    emit(tablegetelem_op, obj, newexpr_conststring((char*)name), method, 0, line);
    /* params: explicit args (reverse), then obj last (so obj is pushed last) */
    for (int i = n - 1; i >= 0; i--) {
        expr* a = to_value(args[i], line);
        emit(param_op, a, NULL, NULL, 0, line);
    }
    emit(param_op, obj, NULL, NULL, 0, line);
    emit(call_op, method, NULL, NULL, 0, line);
    symbol* t = newtemp(line, act_current_scope);
    expr* ret = lvalue_expr(t);
    emit(getretval_op, NULL, NULL, ret, 0, line);
    return ret;
}

/* per-function saved temp counter (stack) */
static unsigned func_temp_save[256];
static int      func_temp_save_top = 0;

symbol* do_funcstart(symbol* fsym, int line) {
    fsym->iaddress = nextquadlabel();
    emit(funcstart_op, NULL, NULL, lvalue_expr(fsym), 0, line);
    enterscopespace();
    resetformalargsoffset();
    reset_formalarg_counter();
    if (func_temp_save_top < 256)
        func_temp_save[func_temp_save_top++] = gettempcounter();
    return fsym;
}

void do_funcend(symbol* fsym, int line) {
    fsym->totalLocals = currscopeoffset();
    exitscopespace();
    if (func_temp_save_top > 0)
        settempcounter(func_temp_save[--func_temp_save_top]);
    emit(funcend_op, NULL, NULL, lvalue_expr(fsym), 0, line);
}

void do_return(expr* e, int line) {
    if (e) { e = to_value(e, line); emit(ret_op, NULL, NULL, e, 0, line); }
    else   { emit(ret_op, NULL, NULL, NULL, 0, line); }
}

/* ---- control flow ---- */
cond_info emit_cond_test(expr* cond, int line) {
    cond_info ci;
    expr* v = to_value(cond, line);              /* materialize bool to temp */
    ci.testq = nextquadlabel();
    emit(if_eq_op, v, newexpr_constbool(1), NULL, 0, line);  /* if_eq _t true ? */
    ci.exitq = nextquadlabel();
    emit(jump_op, NULL, NULL, NULL, 0, line);    /* exit jump */
    return ci;
}

/* loop frame stack */
typedef struct lframe { stmt_list_node* brk; stmt_list_node* cont; } lframe;
static lframe lframes[256];
static int    lframes_top = 0;

void loop_enter(void) {
    lframes[lframes_top].brk = NULL;
    lframes[lframes_top].cont = NULL;
    lframes_top++;
}
stmt_list_node* loop_breaklist(void){ return lframes[lframes_top-1].brk; }
stmt_list_node* loop_contlist(void){ return lframes[lframes_top-1].cont; }
void loop_add_break(unsigned q){ if (lframes_top>0) lframes[lframes_top-1].brk = merge_list(lframes[lframes_top-1].brk, makelist(q)); }
void loop_add_continue(unsigned q){ if (lframes_top>0) lframes[lframes_top-1].cont = merge_list(lframes[lframes_top-1].cont, makelist(q)); }
void loop_exit(void){ lframes_top--; }

/* ---- elist-based call/object wrappers ---- */
static int elist_len(expr* e){ int n=0; while(e){n++; e=e->next;} return n; }

expr* do_call_elist(expr* callee, expr* elist, int line) {
    int n = elist_len(elist);
    expr** arr = (expr**) malloc(sizeof(expr*) * (n>0?n:1));
    int i=0; for (expr* e=elist; e; e=e->next) arr[i++]=e;
    expr* r = do_call(callee, arr, n, line);
    free(arr);
    return r;
}

expr* do_methodcall_elist(expr* obj, const char* name, expr* elist, int line) {
    int n = elist_len(elist);
    expr** arr = (expr**) malloc(sizeof(expr*) * (n>0?n:1));
    int i=0; for (expr* e=elist; e; e=e->next) arr[i++]=e;
    expr* r = do_methodcall(obj, name, arr, n, line);
    free(arr);
    return r;
}

expr* do_objectdef_list(expr* elist, int line) {
    int n = elist_len(elist);
    expr** arr = (expr**) malloc(sizeof(expr*) * (n>0?n:1));
    int i=0; for (expr* e=elist; e; e=e->next) arr[i++]=e;
    expr* r = do_objectdef_elist(arr, n, line);
    free(arr);
    return r;
}

/* ---- indexed object accumulator ---- */
#define IDX_MAX 1024
static expr* idx_keys[IDX_MAX];
static expr* idx_vals[IDX_MAX];
static int   idx_n = 0;

void indexed_add(expr* key, expr* val) {
    if (idx_n < IDX_MAX) { idx_keys[idx_n]=key; idx_vals[idx_n]=val; idx_n++; }
}

expr* do_objectdef_indexed_done(int line) {
    expr* r = do_objectdef_indexed(idx_keys, idx_vals, idx_n, line);
    idx_n = 0;
    return r;
}

/* ---- formal-argument offsets (own counter, reset per function) ---- */
static unsigned formalArgCounter = 0;
void     reset_formalarg_counter(void) { formalArgCounter = 0; }
unsigned formalarg_offset_next(void) { return formalArgCounter++; }

/* ---- for marker stack (supports nesting) ---- */
typedef struct forframe { unsigned cond, step, test, body; } forframe;
static forframe forstk[128];
static int forstk_top = 0;
void     for_push_cond(unsigned q){ forstk[forstk_top].cond=q; forstk[forstk_top].step=0;
                                    forstk[forstk_top].test=0; forstk[forstk_top].body=0; forstk_top++; }
unsigned for_get_cond(void){ return forstk[forstk_top-1].cond; }
void     for_push_step(unsigned q){ forstk[forstk_top-1].step=q; }
unsigned for_get_step(void){ return forstk[forstk_top-1].step; }
void     for_set_test(unsigned q){ forstk[forstk_top-1].test=q; }
unsigned for_get_test(void){ return forstk[forstk_top-1].test; }
void     for_set_body(unsigned q){ forstk[forstk_top-1].body=q; }
unsigned for_get_body(void){ return forstk[forstk_top-1].body; }
void     for_pop(void){ if(forstk_top>0) forstk_top--; }

/* ---- while marker stack (supports nesting) ---- */
typedef struct whileframe { unsigned start, exitj; } whileframe;
static whileframe whilestk[128];
static int whilestk_top = 0;
void     while_push_start(unsigned q){ whilestk[whilestk_top].start=q; whilestk[whilestk_top].exitj=0; whilestk_top++; }
unsigned while_get_start(void){ return whilestk[whilestk_top-1].start; }
void     while_set_exit(unsigned q){ whilestk[whilestk_top-1].exitj=q; }
unsigned while_get_exit(void){ return whilestk[whilestk_top-1].exitj; }
void     while_pop(void){ if(whilestk_top>0) whilestk_top--; }
