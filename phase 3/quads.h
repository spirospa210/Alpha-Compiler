#ifndef QUADS_H
#define QUADS_H

#include "symtable.h"

/* ============================================================
 *  Intermediate-code (quad) generation - Phase 3
 * ============================================================ */

/* Opcode set exactly per the specification enum (note the spec's
 * spelling: `ret` for return, `if_geatereq` kept verbatim but we
 * also provide the readable alias used in print). */
typedef enum iopcode {
    assign_op, add_op, sub_op,
    mul_op, div_op, mod_op,
    uminus_op, and_op, or_op,
    not_op, if_eq_op, if_noteq_op,
    if_lesseq_op, if_greatereq_op, if_less_op,
    if_greater_op, jump_op, call_op, param_op,
    ret_op, getretval_op, funcstart_op,
    funcend_op, tablecreate_op,
    tablegetelem_op, tablesetelem_op
} iopcode;

typedef enum expr_t {
    var_e,
    tableitem_e,

    programfunc_e,
    libraryfunc_e,

    arithexpr_e,
    boolexpr_e,
    assignexpr_e,
    newtable_e,

    constnum_e,
    constbool_e,
    conststring_e,

    nil_e
} expr_t;

typedef struct expr expr;
struct stmt_list_node;  /* forward declaration for boolean backpatch lists */

typedef struct expr {
    expr_t          type;
    symbol*         sym;
    struct expr*    index;
    double          numConst;
    char*           strConst;
    unsigned char   boolConst;
    struct expr*    next;
    /* short-circuit boolean backpatch lists (NULL for non-boolean exprs) */
    struct stmt_list_node* truelist;
    struct stmt_list_node* falselist;
} expr;

typedef struct quad {
    iopcode         op;
    expr*           result;
    expr*           arg1;
    expr*           arg2;
    unsigned        label;
    unsigned        line;
} quad;

/* dedicated struct for call-suffix handling (methodcall/normcall),
 * per FAQ #27 - not representable by plain expr. */
typedef struct call_s {
    expr*           elist;       /* argument list (in source order)     */
    unsigned char   method;      /* 1 if `..name(...)` method call      */
    char*           name;        /* method name when method == 1        */
} call_s;

/* --- backpatch lists --- */
typedef struct stmt_list_node {
    unsigned                quadNo;
    struct stmt_list_node*  next;
} stmt_list_node;

/* for-loop prefix bookkeeping (test/jump quad indices) */
typedef struct for_prefix {
    unsigned test;   /* index of first condition quad   */
    unsigned enter;  /* index of the IF_EQ TRUE ... quad */
} for_prefix;

/* ---- global quad array (per spec) ---- */
extern quad*        quads;
extern unsigned     total;
extern unsigned     currQuad;

/* ---- core emission ---- */
unsigned nextquadlabel(void);
void     emit(iopcode op, expr* arg1, expr* arg2, expr* result,
              unsigned label, unsigned line);

/* ---- expr constructors ---- */
expr* newexpr(expr_t t);
expr* newexpr_constnum(double v);
expr* newexpr_conststring(char* s);
expr* newexpr_constbool(unsigned char b);
expr* newexpr_nil(void);
expr* lvalue_expr(symbol* sym);
expr* emit_iftableitem(expr* e, int line, int scope);
expr* member_item(expr* lv, char* name, int line, int scope);

/* ---- backpatch list ops ---- */
stmt_list_node* makelist(unsigned quadNo);
stmt_list_node* merge_list(stmt_list_node* l1, stmt_list_node* l2);
void            patchlist(stmt_list_node* list, unsigned label);
void            backpatch(stmt_list_node* list, unsigned label);

/* ---- output ---- */
void write_quads_to_file(const char* filename);

/* ---- compiler warnings (printed to stderr, non-fatal) ---- */
void icode_phase_succeeded(void);

#endif
