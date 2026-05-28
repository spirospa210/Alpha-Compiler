#ifndef QUADS_H
#define QUADS_H

#include "symtable.h"
#include <stdbool.h>

typedef enum {
    assign_op, add_op, sub_op, mul_op, div_op, mod_op,
    uminus_op, and_op, or_op, not_op, if_eq_op, if_noteq_op,
    if_lesseq_op, if_greatereq_op, if_less_op, if_greater_op,
    call_op, param_op, ret_op, getretval_op, funcstart_op,
    funcend_op, tablecreate_op, tablegetelem_op, tablesetelem_op, jump_op
} iopcode;

typedef enum {
    var_e, tableitem_e, programfunc_e, libraryfunc_e,
    arithexpr_e, boolexpr_e, assignexpr_e, newtable_e,
    constnum_e, constbool_e, conststring_e, nil_e
} expr_t;

struct list_node {
    int quad_index;
    struct list_node* next;
};

struct expr {
    expr_t type;
    SymbolTableEntry* sym;
    struct expr* index;
    double numConst;
    char* strConst;
    unsigned char boolConst;
    struct expr* next; 
    
    struct list_node* truelist;
    struct list_node* falselist;
    unsigned quad_jump;
};

struct quad {
    iopcode op;
    struct expr* result;
    struct expr* arg1;
    struct expr* arg2;
    unsigned label;
    unsigned line;
};

struct stmt_t {
    struct list_node* breaklist;
    struct list_node* continuelist;
};

struct callsuffix_t {
    struct expr* elist;
    bool method;
    char* name;
};

void emit(iopcode op, struct expr* res, struct expr* arg1, struct expr* arg2, unsigned label, unsigned line);
unsigned nextquadlabel();
void patchlabel(unsigned quadNo, unsigned label);
void print_quads(const char* filename);

struct expr* newexpr(expr_t t);
struct expr* newexpr_conststring(char* s);
struct expr* newexpr_constnum(double i);
struct expr* newexpr_constbool(unsigned char b);

SymbolTableEntry* newtemp(int scope, int line);
void resettemp();

struct expr* emit_iftableitem(struct expr* e, int line);
void make_booltest(struct expr* e, int line);
void emit_params(struct expr* elist, int line);

struct list_node* makelist(int i);
struct list_node* merge(struct list_node* l1, struct list_node* l2);
void backpatch(struct list_node* l, int label);
struct stmt_t* makestmt();

#endif
