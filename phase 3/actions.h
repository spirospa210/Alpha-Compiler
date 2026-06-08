#ifndef ACTIONS_H
#define ACTIONS_H
#include "quads.h"
#include "symtable.h"

/* These are the exact semantic-action helpers the parser will call.
 * They are kept in a separate TU so they can be unit-validated offline
 * with direct-call harnesses, then invoked verbatim from parser.y. */

/* current scope is owned by the parser; harness/parser set it. */
extern int act_current_scope;

/* coerce: if e is a boolexpr, materialize it to a temp holding true/false
 * (backpatching its lists); else return e unchanged. */
expr* to_value(expr* e, int line);

/* arithmetic with constant folding + div0 warnings (returns folded const
 * or emits an op into a fresh temp). */
expr* do_arith(iopcode op, expr* a, expr* b, int line);

/* relational: build a boolexpr with fresh true/false lists + 2 quads. */
expr* do_relop(iopcode ifop, expr* a, expr* b, int line);

/* true-test a non-boolean operand into a boolexpr (for use inside and/or/not). */
expr* to_bool(expr* e, int line);

/* logical ops (short-circuit). M = nextquadlabel captured between operands. */
expr* do_and(expr* e1, unsigned M, expr* e2, int line);
expr* do_or (expr* e1, unsigned M, expr* e2, int line);
expr* do_not(expr* e1, int line);

/* assignment: lvalue = rhs, with table-item awareness + trailing temp. */
expr* do_assign(expr* lval, expr* rhs, int line);

/* unary minus */
expr* do_uminus(expr* e, int line);

/* prefix/postfix inc/dec on a plain-or-table lvalue */
expr* do_preincdec(expr* lval, int isInc, int line);
expr* do_postincdec(expr* lval, int isInc, int line);

#endif

/* ---- object/table construction ---- */
/* elems is a NULL-terminated array of value exprs (source order);
   builds tablecreate + reverse-order tablesetelem with numeric indices. */
expr* do_objectdef_elist(expr** elems, int n, int line);
/* pairs: parallel arrays of key/value exprs (source order). */
expr* do_objectdef_indexed(expr** keys, expr** vals, int n, int line);

/* ---- calls & functions ---- */
/* emit params (reverse source order) for a NULL-term arg array, then call+getretval */
expr* do_call(expr* callee, expr** args, int n, int line);
/* method call: obj..name(args)  ==  (tablegetelem obj "name") ( args..., obj ) */
expr* do_methodcall(expr* obj, const char* name, expr** args, int n, int line);
/* function definition bracketing */
symbol* do_funcstart(symbol* fsym, int line);   /* emits funcstart, sets iaddress */
void    do_funcend(symbol* fsym, int line);     /* emits funcend, records totalLocals */
/* return */
void do_return(expr* e, int line);

/* ---- control flow ---- */
/* condition -> value+test. Returns the index of the IF_EQ test quad whose
   label must be backpatched to the body; also emits the exit jump.
   Fills *exitjump with the index of the exit JUMP quad. */
typedef struct cond_info { unsigned testq; unsigned exitq; } cond_info;
cond_info emit_cond_test(expr* cond, int line);

/* loop frame stack for break/continue */
void loop_enter(void);
stmt_list_node* loop_breaklist(void);
stmt_list_node* loop_contlist(void);
void loop_add_break(unsigned q);
void loop_add_continue(unsigned q);
void loop_exit(void);

/* ---- elist-based wrappers (expr linked-list of args, source order) ---- */
expr* do_call_elist(expr* callee, expr* elist, int line);
expr* do_methodcall_elist(expr* obj, const char* name, expr* elist, int line);
expr* do_objectdef_list(expr* elist, int line);

/* indexed-object accumulator (for [ {k:v}, ... ]) */
void  indexed_add(expr* key, expr* val);
expr* do_objectdef_indexed_done(int line);

/* formal-argument offset allocator */
unsigned formalarg_offset_next(void);
void     reset_formalarg_counter(void);

/* ---- for/while marker stacks (avoid fragile mid-rule $N indexing) ---- */
void     for_push_cond(unsigned q);
unsigned for_get_cond(void);
void     for_push_step(unsigned q);
unsigned for_get_step(void);
void     for_set_test(unsigned q);
unsigned for_get_test(void);
void     for_set_body(unsigned q);
unsigned for_get_body(void);
void     for_pop(void);

void     while_push_start(unsigned q);
unsigned while_get_start(void);
void     while_set_exit(unsigned q);
unsigned while_get_exit(void);
void     while_pop(void);

/* source filename for compiler-warning lines (set by main) */
void set_source_file(const char* p);
