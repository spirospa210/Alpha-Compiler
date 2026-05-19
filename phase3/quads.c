#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "quads.h"

#define EXPAND_SIZE 1024
#define CURR_SIZE   (total * sizeof(quad))
#define NEW_SIZE    (EXPAND_SIZE * sizeof(quad) + CURR_SIZE)

quad* quads = (quad*) 0;
unsigned total = 0;
unsigned int currQuad = 0;

void expand() {
    assert(total == currQuad);
    quad* p = (quad*) malloc(NEW_SIZE);
    if (quads) {
        memcpy(p, quads, CURR_SIZE);
        free(quads);
    }
    quads = p;
    total += EXPAND_SIZE;
}

void emit(iopcode op, expr* arg1, expr* arg2, expr* result, unsigned label, unsigned line) {
    if (currQuad == total) expand();
    quad* p = quads + currQuad;
    p->op = op;
    p->arg1 = arg1;
    p->arg2 = arg2;
    p->result = result;
    p->label = label;
    p->line = line;
    currQuad++;
}

unsigned nextquad() { return currQuad; }

void patchlabel(unsigned quadNo, unsigned label) {
    assert(quadNo < currQuad);
    quads[quadNo].label = label;
}

expr* newexpr(expr_t t) {
    expr* e = (expr*) malloc(sizeof(expr));
    memset(e, 0, sizeof(expr));
    e->type = t;
    return e;
}

expr* newexpr_constnum(double i) {
    expr* e = newexpr(constnum_e);
    e->numConst = i;
    return e;
}

expr* newexpr_conststring(char* s) {
    expr* e = newexpr(conststring_e);
    e->strConst = strdup(s);
    return e;
}

expr* newexpr_constbool(unsigned char b) {
    expr* e = newexpr(constbool_e);
    e->boolConst = b;
    return e;
}

expr* emit_iftableitem(expr* e, int scope, int line) {
    if (e->type != tableitem_e) return e;
    expr* result = newexpr(var_e);
    result->sym = newtemp(line, scope);
    emit(tablegetelem_op, e, e->index, result, 0, line);
    return result;
}

list_node* makelist(int i) {
    list_node* node = (list_node*)malloc(sizeof(list_node));
    node->quad_no = i;
    node->next = NULL;
    return node;
}

list_node* merge(list_node* l1, list_node* l2) {
    if (!l1) return l2;
    if (!l2) return l1;
    list_node* temp = l1;
    while (temp->next != NULL) temp = temp->next;
    temp->next = l2;
    return l1;
}

void backpatch(list_node* list, int label) {
    list_node* curr = list;
    while (curr != NULL) {
        patchlabel(curr->quad_no, label);
        curr = curr->next;
    }
}

const char* opcode_strings[] = {
    "assign", "add", "sub", "mul", "div", "mod", "uminus",
    "and", "or", "not",
    "if_eq", "if_noteq", "if_lesseq", "if_greatereq", "if_less", "if_greater",
    "call", "param", "ret", "getretval", "funcstart", "funcend",
    "tablecreate", "tablegetelem", "tablesetelem", "jump"
};
// Replace print_expr and print_quads in quads.c with this:

void sprintf_expr(char* buffer, expr* e) {
    if (!e) return;
    switch (e->type) {
        case var_e:
        case tableitem_e:
        case arithexpr_e:
        case boolexpr_e:
        case assignexpr_e:
        case newtable_e:
            if (e->sym) sprintf(buffer, "%s", e->sym->name);
            break;
        case programfunc_e:
            if (e->sym) sprintf(buffer, "$%d", e->sym->taddress); // Or name depending on your preference
            break;
        case libraryfunc_e:
            if (e->sym) sprintf(buffer, "\"%s\"", e->sym->name);
            break;
        case constnum_e: sprintf(buffer, "%g", e->numConst); break;
        case constbool_e: sprintf(buffer, "'%s'", e->boolConst ? "true" : "false"); break;
        case conststring_e: sprintf(buffer, "\"%s\"", e->strConst); break;
        case nil_e: sprintf(buffer, "nil"); break;
        default: break;
    }
}
const char* table_op[] = {
    "ASSIGN", "ADD", "SUB", "MUL", "DIV", "MOD", "UMINUS",
    "AND", "OR", "NOT",
    "IF_EQ", "IF_NOTEQ", "IF_LESSEQ", "IF_GREATEREQ", "IF_LESS", "IF_GREATER",
    "CALL", "PARAM", "RET", "GETRETVAL", "FUNCSTART", "FUNCEND",
    "TABLECREATE", "TABLEGETELEM", "TABLESETELEM", "JUMP"
};

void print_quads() {
    FILE* icode_export = fopen("icode.txt", "w");
    if (!icode_export) icode_export = stderr;
    
    for (unsigned i = 0; i < currQuad; i++) {
        quad* q = quads + i;
        iopcode op = q->op;
        
        // Print 0-based Quad number and uppercase opcode
        fprintf(icode_export, "%3u: %s\t ", i, table_op[op]);
        printf("%3u: %-12s\t", i, table_op[op]); // Console output
        
        char res_str[32] = "", arg1_str[32] = "", arg2_str[32] = "";
        if (q->result) sprintf_expr(res_str, q->result);
        if (q->arg1) sprintf_expr(arg1_str, q->arg1);
        if (q->arg2) sprintf_expr(arg2_str, q->arg2);
        
        if (op == add_op || op == sub_op || op == mul_op || op == div_op || op == mod_op || 
            op == tablegetelem_op || op == tablesetelem_op || op == and_op || op == or_op) {
            fprintf(icode_export, "%s\t %s\t %s\n", arg1_str, arg2_str, res_str);
            printf("%-10s\t%-10s\t%s\n", arg1_str, arg2_str, res_str);
        }
        else if (op == if_greater_op || op == if_greatereq_op || op == if_less_op || 
                 op == if_lesseq_op || op == if_noteq_op || op == if_eq_op) {
            fprintf(icode_export, "%s\t %s\t %u\n", arg1_str, arg2_str, q->label);
            printf("%-10s\t%-10s\t%u\n", arg1_str, arg2_str, q->label);
        }
        else if (op == not_op || op == uminus_op || op == assign_op) {
            fprintf(icode_export, "%s\t %s\n", arg1_str, res_str);
            printf("%-10s\t%s\n", arg1_str, res_str);
        }
        else if (op == jump_op) {
            fprintf(icode_export, "%u\n", q->label);
            printf("%u\n", q->label);
        }
        else if (op == call_op || op == param_op || op == getretval_op || 
                 op == funcstart_op || op == funcend_op || op == tablecreate_op || op == ret_op) {
            fprintf(icode_export, "%s\n", res_str);
            printf("%s\n", res_str);
        }
        else {
            if (q->result) {
                fprintf(icode_export, "%s\n", res_str);
                printf("%s\n", res_str);
            } else {
                fprintf(icode_export, "\n");
                printf("\n");
            }
        }
    }
    if (icode_export != stderr) fclose(icode_export);
}