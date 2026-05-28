#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quads.h"

struct quad* quads = (struct quad*) 0;
unsigned total = 0;
unsigned int currQuad = 1; /* Match Alpha's 1-based quad index requirement */
unsigned int temp_counter = 0;

void expand() {
    if (currQuad == total || total == 0) {
        unsigned int new_size = (total == 0) ? 1024 : total * 2;
        quads = (struct quad*) realloc(quads, new_size * sizeof(struct quad));
        total = new_size;
    }
}

void emit(iopcode op, struct expr* res, struct expr* arg1, struct expr* arg2, unsigned label, unsigned line) {
    expand();
    quads[currQuad].op = op;
    quads[currQuad].result = res;
    quads[currQuad].arg1 = arg1;
    quads[currQuad].arg2 = arg2;
    quads[currQuad].label = label;
    quads[currQuad].line = line;
    currQuad++;
}

unsigned nextquadlabel() { return currQuad; }
void patchlabel(unsigned quadNo, unsigned label) { quads[quadNo].label = label; }

/* Temporaries */
char* newtempname() {
    char* buf = malloc(16);
    sprintf(buf, "_t%d", temp_counter++);
    return buf;
}
void resettemp() { temp_counter = 0; }

SymbolTableEntry* newtemp(int scope, int line) {
    char* name = newtempname();
    SymbolTableEntry* sym = lookup_scope(name, scope);
    if (!sym) sym = insert_symbol(name, LOCAL_VAR, line, scope); 
    return sym;
}

/* Expressions */
struct expr* newexpr(expr_t t) {
    struct expr* e = (struct expr*)malloc(sizeof(struct expr));
    memset(e, 0, sizeof(struct expr));
    e->type = t;
    return e;
}
struct expr* newexpr_constnum(double i) { struct expr* e = newexpr(constnum_e); e->numConst = i; return e; }
struct expr* newexpr_conststring(char* s) { struct expr* e = newexpr(conststring_e); e->strConst = strdup(s); return e; }
struct expr* newexpr_constbool(unsigned char b) { struct expr* e = newexpr(constbool_e); e->boolConst = b; return e; }

struct expr* emit_iftableitem(struct expr* e, int line) {
    if (e->type != tableitem_e) return e;
    struct expr* res = newexpr(var_e);
    res->sym = newtemp(0, line); 
    emit(tablegetelem_op, res, e, e->index, 0, line);
    return res;
}

/* Boolean Materialization */
void make_booltest(struct expr* e, int line) {
    if (e->type == boolexpr_e) return;
    e->truelist = makelist(nextquadlabel());
    e->falselist = makelist(nextquadlabel() + 1);
    emit(if_eq_op, NULL, e, newexpr_constbool(1), 0, line);
    emit(jump_op, NULL, NULL, NULL, 0, line);
}

/* Statement Struct Creation */
struct stmt_t* makestmt() {
    struct stmt_t* s = malloc(sizeof(struct stmt_t));
    s->breaklist = NULL;
    s->continuelist = NULL;
    return s;
}

/* Backpatching */
struct list_node* makelist(int i) {
    struct list_node* node = malloc(sizeof(struct list_node));
    node->quad_index = i;
    node->next = NULL;
    return node;
}
struct list_node* merge(struct list_node* l1, struct list_node* l2) {
    if (!l1) return l2;
    if (!l2) return l1;
    struct list_node* temp = l1;
    while (temp->next) temp = temp->next;
    temp->next = l2;
    return l1;
}
void backpatch(struct list_node* l, int label) {
    while (l) {
        patchlabel(l->quad_index, label);
        l = l->next;
    }
}

/* Parameters (Recursive Reverse Order) */
void emit_params(struct expr* elist, int line) {
    if (!elist) return;
    emit_params(elist->next, line); 
    emit(param_op, NULL, elist, NULL, 0, line);
}

/* Print Formatting */
const char* get_op_string(iopcode op) {
    switch (op) {
        case assign_op: return "assign"; case add_op: return "add"; case sub_op: return "sub";
        case mul_op: return "mul"; case div_op: return "div"; case mod_op: return "mod";
        case uminus_op: return "uminus"; case if_eq_op: return "if_eq"; case if_noteq_op: return "if_noteq";
        case if_lesseq_op: return "if_lesseq"; case if_greatereq_op: return "if_greatereq";
        case if_less_op: return "if_less"; case if_greater_op: return "if_greater";
        case jump_op: return "jump"; case call_op: return "call"; case param_op: return "param";
        case ret_op: return "return"; case getretval_op: return "getretval"; case funcstart_op: return "funcstart";
        case funcend_op: return "funcend"; case tablecreate_op: return "tablecreate";
        case tablegetelem_op: return "tablegetelem"; case tablesetelem_op: return "tablesetelem";
        default: return "unknown";
    }
}

void print_expr(FILE* f, struct expr* e) {
    if (!e) { fprintf(f, "          "); return; }
    if (e->type == constnum_e) fprintf(f, "%-10g", e->numConst);
    else if (e->type == conststring_e) {
        char buf[64];
        snprintf(buf, sizeof(buf), "\"%s\"", e->strConst);
        fprintf(f, "%-10s", buf);
    }
    else if (e->type == constbool_e) {
        fprintf(f, "'%-8s'", e->boolConst ? "true" : "false");
    }
    else if (e->type == nil_e) fprintf(f, "%-10s", "nil");
    else if (e->sym) fprintf(f, "%-10s", e->sym->name);
    else fprintf(f, "%-10s", " ");
}

void print_quads(const char* filename) {
    FILE* f = fopen(filename, "w");
    fprintf(f, "| quad# | opcode       | result     | arg1       | arg2       | label   |\n");
    fprintf(f, "|-------|--------------|------------|------------|------------|---------|\n");
    for(unsigned i = 1; i < currQuad; i++) {
        char num_buf[16];
        sprintf(num_buf, "%d:", i);
        fprintf(f, "| %-5s | %-12s | ", num_buf, get_op_string(quads[i].op));
        print_expr(f, quads[i].result); fprintf(f, "| ");
        print_expr(f, quads[i].arg1); fprintf(f, "| ");
        print_expr(f, quads[i].arg2); fprintf(f, "| ");
        if (quads[i].label != 0) fprintf(f, "%-7d |", quads[i].label);
        else fprintf(f, "        |");
        fprintf(f, "\n");
    }
    fclose(f);
}
