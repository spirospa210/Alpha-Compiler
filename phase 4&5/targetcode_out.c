#include "targetcode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* vmopcode_str(vmopcode op) {
    switch (op) {
        case assign_v: return "assign";
        case add_v: return "add";   case sub_v: return "sub";
        case mul_v: return "mul";   case div_v: return "div"; case mod_v: return "mod";
        case uminus_v: return "uminus";
        case and_v: return "and";   case or_v: return "or";   case not_v: return "not";
        case jeq_v: return "jeq";   case jne_v: return "jne";
        case jle_v: return "jle";   case jge_v: return "jge";
        case jlt_v: return "jlt";   case jgt_v: return "jgt";
        case call_v: return "call"; case pusharg_v: return "pusharg";
        case funcenter_v: return "funcenter"; case funcexit_v: return "funcexit";
        case newtable_v: return "newtable";
        case tablegetelem_v: return "tablegetelem";
        case tablesetelem_v: return "tablesetelem";
        case nop_v: return "nop";   case jump_v: return "jump";
        case getretval_v: return "getretval";
        default: return "?";
    }
}

static void vmarg_str(vmarg* a, char* buf, size_t n) {
    if (!a) { snprintf(buf, n, ""); return; }
    switch (a->type) {
        case label_a:    snprintf(buf, n, "%u",            a->val); break;
        case global_a:   snprintf(buf, n, "global[%u]",    a->val); break;
        case formal_a:   snprintf(buf, n, "formal[%u]",    a->val); break;
        case local_a:    snprintf(buf, n, "local[%u]",     a->val); break;
        case number_a:   snprintf(buf, n, "%g",  consts_getnumber(a->val)); break;
        case string_a:   snprintf(buf, n, "\"%s\"", consts_getstring(a->val)); break;
        case bool_a:     snprintf(buf, n, "%s", a->val ? "true" : "false"); break;
        case nil_a:      snprintf(buf, n, "nil"); break;
        case userfunc_a: snprintf(buf, n, "userfunc[%u]",  a->val); break;
        case libfunc_a:  snprintf(buf, n, "libfunc[\"%s\"]", libfuncs_getused(a->val)); break;
        case retval_a:   snprintf(buf, n, "retval"); break;
        default:         snprintf(buf, n, "?"); break;
    }
}

void write_targetcode_text(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) { perror(filename); return; }

    /* constant tables */
    fprintf(f, "=== String constants (%u) ===\n", total_strConsts());
    for (unsigned i = 0; i < total_strConsts(); i++)
        fprintf(f, "  %u: \"%s\"\n", i, consts_getstring(i));
    fprintf(f, "=== Number constants (%u) ===\n", total_numConsts());
    for (unsigned i = 0; i < total_numConsts(); i++)
        fprintf(f, "  %u: %g\n", i, consts_getnumber(i));
    fprintf(f, "=== User functions (%u) ===\n", total_userfuncs());
    for (unsigned i = 0; i < total_userfuncs(); i++) {
        userfunc* u = userfuncs_getfunc(i);
        fprintf(f, "  %u: addr=%u localsize=%u id=\"%s\"\n", i, u->address, u->localSize, u->id);
    }
    fprintf(f, "=== Library functions (%u) ===\n", total_libfuncs());
    for (unsigned i = 0; i < total_libfuncs(); i++)
        fprintf(f, "  %u: \"%s\"\n", i, libfuncs_getused(i));

    /* instructions */
    fprintf(f, "=== Target code (%u instructions) ===\n", totalInstructions);
    fprintf(f, "%-6s %-13s %-16s %-16s %-16s %s\n", "addr", "opcode", "result", "arg1", "arg2", "line");
    for (unsigned i = 0; i < totalInstructions; i++) {
        instruction* in = &instructions[i];
        char r[64], a1[64], a2[64];
        /* result column: for jumps/relationals it's a target label */
        vmarg_str(&in->result, r, sizeof r);
        vmarg_str(&in->arg1,   a1, sizeof a1);
        vmarg_str(&in->arg2,   a2, sizeof a2);
        fprintf(f, "%-6u %-13s %-16s %-16s %-16s %u\n",
                i, vmopcode_str(in->opcode), r, a1, a2, in->srcLine);
    }
    fclose(f);
}

/* ============================================================
 *  Binary writer  (magic 340200501 + tables + code)
 * ============================================================ */

static void wr_u(FILE* f, unsigned v)   { fwrite(&v, sizeof(unsigned), 1, f); }
static void wr_d(FILE* f, double v)      { fwrite(&v, sizeof(double), 1, f); }
static void wr_b(FILE* f, unsigned char v){ fwrite(&v, sizeof(unsigned char), 1, f); }
static void wr_str(FILE* f, const char* s) {
    unsigned len = (unsigned)strlen(s) + 1;   /* include null terminator */
    wr_u(f, len);
    fwrite(s, 1, len, f);
}
static void wr_arg(FILE* f, vmarg* a) {
    wr_b(f, (unsigned char)a->type);
    wr_u(f, a->val);
}

void write_targetcode_binary(const char* filename) {
    FILE* f = fopen(filename, "wb");
    if (!f) { perror(filename); return; }

    wr_u(f, MAGICNUMBER);
    wr_u(f, currscopeoffset());   /* total program globals (top init for VM) */

    /* strings */
    wr_u(f, total_strConsts());
    for (unsigned i = 0; i < total_strConsts(); i++) wr_str(f, consts_getstring(i));
    /* numbers */
    wr_u(f, total_numConsts());
    for (unsigned i = 0; i < total_numConsts(); i++) wr_d(f, consts_getnumber(i));
    /* user functions */
    wr_u(f, total_userfuncs());
    for (unsigned i = 0; i < total_userfuncs(); i++) {
        userfunc* u = userfuncs_getfunc(i);
        wr_u(f, u->address);
        wr_u(f, u->localSize);
        wr_str(f, u->id);
    }
    /* library functions */
    wr_u(f, total_libfuncs());
    for (unsigned i = 0; i < total_libfuncs(); i++) wr_str(f, libfuncs_getused(i));

    /* code */
    wr_u(f, totalInstructions);
    for (unsigned i = 0; i < totalInstructions; i++) {
        instruction* in = &instructions[i];
        wr_b(f, (unsigned char)in->opcode);
        wr_arg(f, &in->result);
        wr_arg(f, &in->arg1);
        wr_arg(f, &in->arg2);
        wr_u(f, in->srcLine);
    }
    fclose(f);
}
