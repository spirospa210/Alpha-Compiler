#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quads.h"

/* ============================================================
 *  Dynamic quad array (managed exactly as in the spec)
 * ============================================================ */

quad*    quads    = (quad*) 0;
unsigned total    = 0;     /* allocated capacity (in quads)   */
unsigned currQuad = 0;     /* next free index == count emitted */

#define EXPAND_SIZE 1024
#define CURR_SIZE   (total * sizeof(quad))
#define NEW_SIZE    (EXPAND_SIZE * sizeof(quad) + CURR_SIZE)

static void expand_quads(void) {
    quad* p = (quad*) malloc(NEW_SIZE);
    if (quads) {
        memcpy(p, quads, CURR_SIZE);
        free(quads);
    }
    quads = p;
    total += EXPAND_SIZE;
}

unsigned nextquadlabel(void) {
    return currQuad;   /* 0-based internal index; printed +1 */
}

void emit(iopcode op, expr* arg1, expr* arg2, expr* result,
          unsigned label, unsigned line) {
    if (currQuad == total)
        expand_quads();
    quad* q   = quads + currQuad++;
    q->op     = op;
    q->arg1   = arg1;
    q->arg2   = arg2;
    q->result = result;
    q->label  = label;
    q->line   = line;
}

/* ============================================================
 *  expr constructors
 * ============================================================ */

expr* newexpr(expr_t t) {
    expr* e = (expr*) malloc(sizeof(expr));
    memset(e, 0, sizeof(expr));
    e->type = t;
    return e;
}

expr* newexpr_constnum(double v) {
    expr* e = newexpr(constnum_e);
    e->numConst = v;
    return e;
}

expr* newexpr_conststring(char* s) {
    expr* e = newexpr(conststring_e);
    e->strConst = strdup(s ? s : "");
    return e;
}

expr* newexpr_constbool(unsigned char b) {
    expr* e = newexpr(constbool_e);
    e->boolConst = b ? 1 : 0;
    return e;
}

expr* newexpr_nil(void) {
    return newexpr(nil_e);
}

/* Build an expr that refers to a symbol, choosing the right expr type
 * based on the symbol's kind (variable / user func / library func). */
expr* lvalue_expr(symbol* sym) {
    if (sym == NULL) return NULL;
    expr* e = newexpr(var_e);
    e->sym = sym;
    switch (sym->type) {
        case USER_FUNC: e->type = programfunc_e; break;
        case LIB_FUNC:  e->type = libraryfunc_e; break;
        default:        e->type = var_e;         break;
    }
    return e;
}

/* If e is a table item used as an r-value, emit a tablegetelem into a
 * fresh temp and return that temp; otherwise return e unchanged. */
expr* emit_iftableitem(expr* e, int line, int scope) {
    if (e == NULL || e->type != tableitem_e)
        return e;
    expr* result = newexpr(var_e);
    result->sym = newtemp(line, scope);
    emit(tablegetelem_op, e, e->index, result, 0, line);
    return result;
}

/* member_item for `lvalue . id`. */
expr* member_item(expr* lv, char* name, int line, int scope) {
    lv = emit_iftableitem(lv, line, scope);
    expr* item  = newexpr(tableitem_e);
    item->sym   = lv->sym;
    item->index = newexpr_conststring(name);
    return item;
}

/* ============================================================
 *  Backpatch lists
 * ============================================================ */

stmt_list_node* makelist(unsigned quadNo) {
    stmt_list_node* n = (stmt_list_node*) malloc(sizeof(stmt_list_node));
    n->quadNo = quadNo;
    n->next   = NULL;
    return n;
}

stmt_list_node* merge_list(stmt_list_node* l1, stmt_list_node* l2) {
    if (!l1) return l2;
    if (!l2) return l1;
    stmt_list_node* t = l1;
    while (t->next) t = t->next;
    t->next = l2;
    return l1;
}

void patchlist(stmt_list_node* list, unsigned label) {
    while (list) {
        if (list->quadNo < currQuad)
            quads[list->quadNo].label = label;
        list = list->next;
    }
}

void backpatch(stmt_list_node* list, unsigned label) {
    patchlist(list, label);
}

/* ============================================================
 *  Output: quads.txt
 * ============================================================ */

static const char* opcode_name(iopcode op) {
    switch (op) {
        case assign_op:       return "assign";
        case add_op:          return "add";
        case sub_op:          return "sub";
        case mul_op:          return "mul";
        case div_op:          return "div";
        case mod_op:          return "mod";
        case uminus_op:       return "uminus";
        case and_op:          return "and";
        case or_op:           return "or";
        case not_op:          return "not";
        case if_eq_op:        return "if_eq";
        case if_noteq_op:     return "if_noteq";
        case if_lesseq_op:    return "if_lesseq";
        case if_greatereq_op: return "if_greatereq";
        case if_less_op:      return "if_less";
        case if_greater_op:   return "if_greater";
        case jump_op:         return "jump";
        case call_op:         return "call";
        case param_op:        return "param";
        case ret_op:          return "ret";
        case getretval_op:    return "getretval";
        case funcstart_op:    return "funcstart";
        case funcend_op:      return "funcend";
        case tablecreate_op:  return "tablecreate";
        case tablegetelem_op: return "tablegetelem";
        case tablesetelem_op: return "tablesetelem";
        default:              return "?";
    }
}

/* true for the branch instructions that actually use the `label` field */
static int op_uses_label(iopcode op) {
    switch (op) {
        case jump_op: case if_eq_op: case if_noteq_op:
        case if_lesseq_op: case if_greatereq_op:
        case if_less_op: case if_greater_op:
            return 1;
        default:
            return 0;
    }
}

/* Render an expr operand into buf (numbers, strings, booleans, names). */
static void expr_to_str(expr* e, char* buf, size_t n) {
    if (e == NULL) { buf[0] = '\0'; return; }
    switch (e->type) {
        case constnum_e: {
            double v = e->numConst;
            if (v == (long long) v)
                snprintf(buf, n, "%lld", (long long) v);
            else
                snprintf(buf, n, "%g", v);
            break;
        }
        case conststring_e:
            snprintf(buf, n, "\"%s\"", e->strConst ? e->strConst : "");
            break;
        case constbool_e:
            snprintf(buf, n, "%s", e->boolConst ? "true" : "false");
            break;
        case nil_e:
            snprintf(buf, n, "nil");
            break;
        default:
            if (e->sym && e->sym->name)
                snprintf(buf, n, "%s", e->sym->name);
            else
                buf[0] = '\0';
            break;
    }
}

void write_quads_to_file(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror("quads.txt"); return; }

    /* Header column starts: quad#=0 opcode=8 result=24 arg1=40 arg2=61
     * label=82 line=93  -> field widths 8,16,16,21,21,11,(rest). */
    fprintf(f,
        "quad#   opcode          result          arg1                 arg2                 label      line\n");
    fprintf(f,
        "--------------------------------------------------------------------------------------------------\n\n");

    unsigned prevLine = 0;
    for (unsigned i = 0; i < currQuad; i++) {
        quad* q = &quads[i];

        /* blank line between groups (whenever the source line changes) */
        if (i > 0 && q->line != prevLine)
            fprintf(f, "\n");
        prevLine = q->line;

        char res[128], a1[128], a2[128];
        expr_to_str(q->result, res, sizeof(res));
        expr_to_str(q->arg1,   a1,  sizeof(a1));
        expr_to_str(q->arg2,   a2,  sizeof(a2));

        char qnum[16];
        snprintf(qnum, sizeof(qnum), "%u:", i + 1);

        char lbl[32];
        if (op_uses_label(q->op)) snprintf(lbl, sizeof(lbl), "%u", q->label + 1);
        else                      lbl[0] = '\0';

        /* widths: quad#=8, opcode=16, result=16, arg1=21, arg2=21, label=11, line=5 */
        fprintf(f, "%-8s%-16s%-16s%-21s%-21s%-11s%-5u\n",
                qnum, opcode_name(q->op), res, a1, a2, lbl, q->line);
    }
    fclose(f);
}

void icode_phase_succeeded(void) { /* placeholder hook */ }
