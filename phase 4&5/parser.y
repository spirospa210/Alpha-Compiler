%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"
#include "token.h"
#include "targetcode.h"
#include "quads.h"
#include "actions.h"

extern int yylineno;
extern FILE* yyin;
void yyerror(const char* s);

extern struct token * head;
extern int alpha_yylex(void* ylval);
struct token * current_token = NULL;

int current_scope = 0;
int anon_func_counter = 0;
bool next_block_is_func = false;

int func_scope_stack[100];
char* current_func_name_stack[100];
int func_scope_stack_top = 0;

int loop_counter = 0;

/* PHASE 3: count all errors (syntax + semantic). quads.txt is written
 * only when this stays 0 through a successful parse. */
int error_count = 0;
#define SEMERR() (error_count++)

char* new_anon_func_name();
int get_bison_token(struct token* t);
int yylex();

const char* get_symbol_type_string(SymbolType type) {
    switch(type) {
        case GLOBAL_VAR: return "global variable";
        case LOCAL_VAR: return "local variable";
        case FORMAL_ARG: return "formal argument";
        case USER_FUNC: return "user function";
        case LIB_FUNC: return "library function";
        default: return "symbol";
    }
}

/* ---- Phase 3 parser-side helper state ---- */
/* function symbol stack parallel to func_scope_stack */
static symbol* func_sym_stack[100];
static int     func_sym_stack_top = 0;

/* saved loop_counter across function boundaries (spec: reset per function) */
static int loopcount_save[100];
static int loopcount_save_top = 0;

/* convert an lvalue symbol to an expr (variable/func), NULL-safe */
static expr* sym2expr(symbol* s) { return s ? lvalue_expr(s) : NULL; }
%}

%define parse.error verbose

%union {
    char* strVal;
    struct SymbolTableEntry* symNode;
    struct expr* exprNode;
    unsigned    quadLabel;
    struct call_s* callNode;
    int         intVal;
}

%token <strVal> ID
%token IF ELSE WHILE FOR RETURN BREAK CONTINUE FUNCTION LOCAL
%token <strVal> TRUE_TOKEN FALSE_TOKEN NIL NUMBER STRING
%token AND OR NOT GLOBAL_SCOPE PLUS_PLUS MINUS_MINUS EQUAL_EQUAL NOT_EQUAL GREATER_EQUAL LESS_EQUAL DOT_DOT

%type <exprNode> expr term assignexpr primary const objectdef call lvalue member
%type <exprNode> elist expr_list funcdef
%type <callNode> callsuffix normcall methodcall
%type <symNode>  funcname
%type <quadLabel> ifprefix elseprefix
%type <intVal>   indexed indexedelem

/* Precedence and Associativity Rules */
%right '='
%left OR
%left AND
%nonassoc EQUAL_EQUAL NOT_EQUAL
%nonassoc '>' GREATER_EQUAL '<' LESS_EQUAL
%left '+' '-'
%left '*' '/' '%'
%right NOT PLUS_PLUS MINUS_MINUS UMINUS
%left '.' DOT_DOT
%left '[' ']' '(' ')'

%expect 1

%%

program:
      stmt_list
    | /* empty */
    ;

stmt_list:
      stmt_list stmt
    | stmt
    ;

stmt:
      expr ';'       { if ($1) to_value($1, yylineno); }
    | ifstmt
    | whilestmt
    | forstmt
    | returnstmt
    | BREAK ';' {
          if (loop_counter == 0) {
              printf("Error at line %d: Use of 'break' while not in a loop.\n", yylineno);
              error_count++;
          } else {
              unsigned q = nextquadlabel();
              emit(jump_op, NULL, NULL, NULL, 0, yylineno);
              loop_add_break(q);
          }
      }
    | CONTINUE ';' {
          if (loop_counter == 0) {
              printf("Error at line %d: Use of 'continue' while not in a loop.\n", yylineno);
              error_count++;
          } else {
              unsigned q = nextquadlabel();
              emit(jump_op, NULL, NULL, NULL, 0, yylineno);
              loop_add_continue(q);
          }
      }
    | block
    | funcdef
    | ';'
    | error ';' { yyerrok; }
    ;

expr:
      assignexpr            { $$ = $1; }
    | expr '+' expr         { $$ = do_arith(add_op, $1, $3, yylineno); }
    | expr '-' expr         { $$ = do_arith(sub_op, $1, $3, yylineno); }
    | expr '*' expr         { $$ = do_arith(mul_op, $1, $3, yylineno); }
    | expr '/' expr         { $$ = do_arith(div_op, $1, $3, yylineno); }
    | expr '%' expr         { $$ = do_arith(mod_op, $1, $3, yylineno); }
    | expr '>' expr         { $$ = do_relop(if_greater_op,   $1, $3, yylineno); }
    | expr GREATER_EQUAL expr { $$ = do_relop(if_greatereq_op, $1, $3, yylineno); }
    | expr '<' expr         { $$ = do_relop(if_less_op,      $1, $3, yylineno); }
    | expr LESS_EQUAL expr  { $$ = do_relop(if_lesseq_op,    $1, $3, yylineno); }
    | expr EQUAL_EQUAL expr { $$ = do_relop(if_eq_op,        $1, $3, yylineno); }
    | expr NOT_EQUAL expr   { $$ = do_relop(if_noteq_op,     $1, $3, yylineno); }
    | expr AND { $<quadLabel>$ = nextquadlabel(); } expr
                            { $$ = do_and($1, $<quadLabel>3, $4, yylineno); }
    | expr OR  { $<quadLabel>$ = nextquadlabel(); } expr
                            { $$ = do_or($1, $<quadLabel>3, $4, yylineno); }
    | term                  { $$ = $1; }
    ;

term:
      '(' expr ')'          { $$ = $2; }
    | '-' expr %prec UMINUS { $$ = do_uminus($2, yylineno); }
    | NOT expr              { $$ = do_not($2, yylineno); }
    | PLUS_PLUS lvalue {
          if ($2 && ($2->sym) && ($2->sym->type == USER_FUNC || $2->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function '%s'.\n", yylineno, $2->sym->name);
              error_count++;
              $$ = $2;
          } else { $$ = do_preincdec($2, 1, yylineno); }
      }
    | lvalue PLUS_PLUS {
          if ($1 && ($1->sym) && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function '%s'.\n", yylineno, $1->sym->name);
              error_count++;
              $$ = $1;
          } else { $$ = do_postincdec($1, 1, yylineno); }
      }
    | MINUS_MINUS lvalue {
          if ($2 && ($2->sym) && ($2->sym->type == USER_FUNC || $2->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '--' to function '%s'.\n", yylineno, $2->sym->name);
              error_count++;
              $$ = $2;
          } else { $$ = do_preincdec($2, 0, yylineno); }
      }
    | lvalue MINUS_MINUS {
          if ($1 && ($1->sym) && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '--' to function '%s'.\n", yylineno, $1->sym->name);
              error_count++;
              $$ = $1;
          } else { $$ = do_postincdec($1, 0, yylineno); }
      }
    | primary               { $$ = $1; }
    ;

assignexpr:
      lvalue '=' expr {
          if ($1 && $1->sym && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot assign to function '%s'.\n", yylineno, $1->sym->name);
              error_count++;
              $$ = $1;
          } else {
              $$ = do_assign($1, $3, yylineno);
          }
      }
    ;

primary:
      lvalue                { $$ = emit_iftableitem($1, yylineno, current_scope); }
    | call                  { $$ = $1; }
    | objectdef             { $$ = $1; }
    | '(' funcdef ')'       { $$ = $2; }
    | const                 { $$ = $1; }
    ;

lvalue:
      ID {
          SymbolTableEntry* sym = lookup_all($1, current_scope);
          if (sym) {
              if (sym->scope != 0 && sym->type != USER_FUNC && sym->type != LIB_FUNC) {
                  if (func_scope_stack_top > 0) {
                      int closest_func_scope = func_scope_stack[func_scope_stack_top - 1];
                      if (sym->scope < closest_func_scope) {
                          printf("Error at line %d: Cannot access '%s' inside function '%s'.\n",
                                 yylineno, $1, current_func_name_stack[func_scope_stack_top - 1]);
              error_count++;
                      }
                  }
              }
          } else {
              SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
              sym = insert_symbol($1, t, yylineno, current_scope);
              sym->space  = currscopespace();
              sym->offset = currscopeoffset();
              incurrscopeoffset();
          }
          $$ = sym2expr(sym);
      }
    | LOCAL ID {
          SymbolTableEntry* sym = lookup_scope($2, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($2, 0);
          if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: local '%s' collides with a library function.\n", yylineno, $2);
              error_count++;
              $$ = sym2expr(lib_collision);
          } else {
              if (!sym) {
                  SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
                  sym = insert_symbol($2, t, yylineno, current_scope);
                  sym->space  = currscopespace();
                  sym->offset = currscopeoffset();
                  incurrscopeoffset();
              }
              $$ = sym2expr(sym);
          }
      }
    | GLOBAL_SCOPE ID {
          SymbolTableEntry* sym = lookup_scope($2, 0);
          if (!sym) {
              printf("Error at line %d: Global variable '%s' not found.\n", yylineno, $2);
              error_count++;
          }
          $$ = sym2expr(sym);
      }
    | member { $$ = $1; }
    ;

member:
      lvalue '.' ID                 { $$ = member_item($1, $3, yylineno, current_scope); }
    | lvalue '[' expr ']'           {
          expr* base = emit_iftableitem($1, yylineno, current_scope);
          expr* it = newexpr(tableitem_e);
          it->sym = base->sym;
          it->index = to_value($3, yylineno);
          $$ = it;
      }
    | call '.' ID                   { $$ = member_item($1, $3, yylineno, current_scope); }
    | call '[' expr ']'             {
          expr* base = emit_iftableitem($1, yylineno, current_scope);
          expr* it = newexpr(tableitem_e);
          it->sym = base->sym;
          it->index = to_value($3, yylineno);
          $$ = it;
      }
    ;

call:
      call '(' elist ')'            { $$ = do_call_elist($1, $3, yylineno); }
    | lvalue callsuffix             {
          expr* lv = $1;
          if ($2->method) {
              $$ = do_methodcall_elist(lv, $2->name, $2->elist, yylineno);
          } else {
              lv = emit_iftableitem(lv, yylineno, current_scope);
              $$ = do_call_elist(lv, $2->elist, yylineno);
          }
      }
    | '(' funcdef ')' '(' elist ')' { $$ = do_call_elist($2, $5, yylineno); }
    ;

callsuffix:
      normcall                      { $$ = $1; }
    | methodcall                    { $$ = $1; }
    ;

normcall:
      '(' elist ')'                 {
          call_s* c = (call_s*) malloc(sizeof(call_s));
          c->elist = $2; c->method = 0; c->name = NULL;
          $$ = c;
      }
    ;

methodcall:
      DOT_DOT ID '(' elist ')'      {
          call_s* c = (call_s*) malloc(sizeof(call_s));
          c->elist = $4; c->method = 1; c->name = strdup($2);
          $$ = c;
      }
    ;

elist:
      expr_list                     { $$ = $1; }
    | /* empty */                   { $$ = NULL; }
    ;

expr_list:
      expr_list ',' expr            {
          /* append: source order preserved in list; emission reverses later */
          expr* v = to_value($3, yylineno);
          v->next = NULL;
          expr* t = $1;
          while (t->next) t = t->next;
          t->next = v;
          $$ = $1;
      }
    | expr                          {
          expr* v = to_value($1, yylineno);
          v->next = NULL;
          $$ = v;
      }
    ;

objectdef:
      '[' elist ']'                 { $$ = do_objectdef_list($2, yylineno); }
    | '[' indexed ']'               { $$ = do_objectdef_indexed_done(yylineno); }
    ;

indexed:
      indexed ',' indexedelem       { $$ = $1 + $3; }
    | indexedelem                   { $$ = $1; }
    ;

indexedelem:
      '{' expr ':' expr '}'         {
          indexed_add(to_value($2, yylineno), to_value($4, yylineno));
          $$ = 1;
      }
    ;

block_start:
      '{' {
          if (!next_block_is_func) current_scope++;
          next_block_is_func = false;
      }
    ;

block:
      block_start stmt_list '}' {
          hide_scope(current_scope);
          current_scope--;
      }
    | block_start '}' {
          hide_scope(current_scope);
          current_scope--;
      }
    ;

funcname:
      ID {
          SymbolTableEntry* collision = lookup_scope($1, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($1, 0);
          SymbolTableEntry* fsym = NULL;

          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Function name '%s' collides with a library function.\n", yylineno, $1);
              error_count++;
              } else {
                  printf("Error at line %d: Collision with %s named '%s' defined at line %d\n",
                         yylineno, get_symbol_type_string(collision->type), $1, collision->line);
              error_count++;
              }
              fsym = collision;
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Function name '%s' collides with a library function.\n", yylineno, $1);
              error_count++;
              fsym = lib_collision;
          } else {
              fsym = insert_symbol($1, USER_FUNC, yylineno, current_scope);
          }

          next_block_is_func = true;
          current_scope++;

          current_func_name_stack[func_scope_stack_top] = strdup($1);
          func_scope_stack[func_scope_stack_top++] = current_scope;
          func_sym_stack[func_sym_stack_top++] = fsym;

          $$ = fsym;
      }
    | /* empty */ {
          char* name = new_anon_func_name();
          SymbolTableEntry* fsym = insert_symbol(name, USER_FUNC, yylineno, current_scope);

          next_block_is_func = true;
          current_scope++;

          current_func_name_stack[func_scope_stack_top] = strdup(name);
          func_scope_stack[func_scope_stack_top++] = current_scope;
          func_sym_stack[func_sym_stack_top++] = fsym;

          free(name);
          $$ = fsym;
      }
    ;

funcdef:
      FUNCTION funcname {
          /* emit funcstart, enter scope space, save+reset loop counter */
          do_funcstart($2, yylineno);
          loopcount_save[loopcount_save_top++] = loop_counter;
          loop_counter = 0;
      }
      '(' idlist ')' {
          /* record formal-argument count before parsing the (possibly
             nested-function-containing) body resets the formal counter */
          if ($2) $2->totalFormals = formalarg_count();
      }
      block {
          /* restore loop counter, emit funcend */
          loop_counter = loopcount_save[--loopcount_save_top];
          do_funcend($2, yylineno);
          func_scope_stack_top--;
          func_sym_stack_top--;
          free(current_func_name_stack[func_scope_stack_top]);
          $$ = sym2expr($2);
      }
    ;

const:
      NUMBER       { $$ = newexpr_constnum(atof($1)); }
    | STRING       { $$ = newexpr_conststring($1); }
    | NIL          { $$ = newexpr_nil(); }
    | TRUE_TOKEN   { $$ = newexpr_constbool(1); }
    | FALSE_TOKEN  { $$ = newexpr_constbool(0); }
    ;

idlist:
      id_seq
    | /* empty */
    ;

id_seq:
      id_seq ',' ID {
          SymbolTableEntry* collision = lookup_scope($3, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($3, 0);
          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $3);
              error_count++;
              } else {
                  printf("Error at line %d: Formal argument '%s' collides with %s defined at line %d\n",
                         yylineno, $3, get_symbol_type_string(collision->type), collision->line);
              error_count++;
              }
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $3);
              error_count++;
          } else {
              SymbolTableEntry* a = insert_symbol($3, FORMAL_ARG, yylineno, current_scope);
              a->space = FORMAL_ARGUMENT;
              a->offset = formalarg_offset_next();
          }
      }
    | ID {
          SymbolTableEntry* collision = lookup_scope($1, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($1, 0);
          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $1);
              error_count++;
              } else {
                  printf("Error at line %d: Formal argument '%s' collides with %s defined at line %d\n",
                         yylineno, $1, get_symbol_type_string(collision->type), collision->line);
              error_count++;
              }
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $1);
              error_count++;
          } else {
              SymbolTableEntry* a = insert_symbol($1, FORMAL_ARG, yylineno, current_scope);
              a->space = FORMAL_ARGUMENT;
              a->offset = formalarg_offset_next();
          }
      }
    ;

ifprefix:
      IF '(' expr ')' {
          cond_info ci = emit_cond_test($3, yylineno);
          backpatch(makelist(ci.testq), nextquadlabel());
          $$ = ci.exitq;   /* the exit-jump quad to patch later */
      }
    ;

elseprefix:
      ELSE {
          unsigned j = nextquadlabel();
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          $$ = j;
      }
    ;

ifstmt:
      ifprefix stmt elseprefix stmt {
          /* $1 = if-false exit jump quad; $3 = then-branch skip jump quad */
          backpatch(makelist($1), $3 + 1);            /* false -> first quad of else */
          backpatch(makelist($3), nextquadlabel());   /* then-skip -> end */
      }
    | ifprefix stmt {
          backpatch(makelist($1), nextquadlabel());   /* false -> end */
      }
    ;

whilestmt:
      WHILE '('
          { while_push_start(nextquadlabel()); }
      expr ')'
          { loop_counter++;
            loop_enter();
            cond_info ci = emit_cond_test($4, yylineno);
            backpatch(makelist(ci.testq), nextquadlabel());
            while_set_exit(ci.exitq); }
      stmt {
            loop_counter--;
            unsigned wstart = while_get_start();
            unsigned wexit_jump = while_get_exit();
            emit(jump_op, NULL, NULL, NULL, wstart, yylineno);
            unsigned wexit = nextquadlabel();
            backpatch(makelist(wexit_jump), wexit);
            backpatch(loop_breaklist(), wexit);
            backpatch(loop_contlist(), wstart);
            loop_exit();
            while_pop();
      }
    ;

forstmt:
      FOR '(' elist ';'
          { for_push_cond(nextquadlabel()); }             /* remember Qcond */
      expr ';'
          { loop_counter++; loop_enter();
            cond_info ci = emit_cond_test($6, yylineno);
            for_set_test(ci.testq);                        /* testq (+1 = exitq) */
            for_push_step(nextquadlabel()); }              /* remember Qstep */
      elist ')'
          { /* step elist emitted; jump back to condition; body follows */
            emit(jump_op, NULL, NULL, NULL, for_get_cond(), yylineno);
            for_set_body(nextquadlabel()); }               /* body start */
      stmt {
            loop_counter--;
            unsigned testq = for_get_test();
            unsigned exitq = testq + 1;
            unsigned Qstep = for_get_step();
            unsigned Qbody = for_get_body();
            backpatch(makelist(testq), Qbody);             /* test -> body */
            emit(jump_op, NULL, NULL, NULL, Qstep, yylineno); /* body end -> step */
            unsigned Qexit = nextquadlabel();
            backpatch(makelist(exitq), Qexit);
            backpatch(loop_breaklist(), Qexit);
            backpatch(loop_contlist(), Qstep);
            loop_exit();
            for_pop();
      }
    ;

returnstmt:
      RETURN expr ';' {
          if (func_scope_stack_top == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\n", yylineno);
              error_count++;
          } else {
              do_return($2, yylineno);
          }
      }
    | RETURN ';' {
          if (func_scope_stack_top == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\n", yylineno);
              error_count++;
          } else {
              do_return(NULL, yylineno);
          }
      }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Syntax Error at line %d: %s\n", yylineno, s);
    error_count++;
}

char* new_anon_func_name() {
    char buffer[32];
    sprintf(buffer, "_f%d", anon_func_counter++);
    return strdup(buffer);
}

/* 2. Map your custom string tokens to Bison Token IDs */
int get_bison_token(struct token* t) {
    if (strcmp(t->token_type, "KEYWORD") == 0) {
        if (!strcmp(t->content, "if")) return IF;
        if (!strcmp(t->content, "else")) return ELSE;
        if (!strcmp(t->content, "while")) return WHILE;
        if (!strcmp(t->content, "for")) return FOR;
        if (!strcmp(t->content, "function")) return FUNCTION;
        if (!strcmp(t->content, "return")) return RETURN;
        if (!strcmp(t->content, "break")) return BREAK;
        if (!strcmp(t->content, "continue")) return CONTINUE;
        if (!strcmp(t->content, "and")) return AND;
        if (!strcmp(t->content, "not")) return NOT;
        if (!strcmp(t->content, "or")) return OR;
        if (!strcmp(t->content, "local")) return LOCAL;
        if (!strcmp(t->content, "true")) return TRUE_TOKEN;
        if (!strcmp(t->content, "false")) return FALSE_TOKEN;
        if (!strcmp(t->content, "nil")) return NIL;
    }
    else if (strcmp(t->token_type, "OPERATOR") == 0) {
        if (!strcmp(t->content, "=")) return '=';
        if (!strcmp(t->content, "+")) return '+';
        if (!strcmp(t->content, "-")) return '-';
        if (!strcmp(t->content, "*")) return '*';
        if (!strcmp(t->content, "/")) return '/';
        if (!strcmp(t->content, "%")) return '%';
        if (!strcmp(t->content, "==")) return EQUAL_EQUAL;
        if (!strcmp(t->content, "!=")) return NOT_EQUAL;
        if (!strcmp(t->content, "++")) return PLUS_PLUS;
        if (!strcmp(t->content, "--")) return MINUS_MINUS;
        if (!strcmp(t->content, ">")) return '>';
        if (!strcmp(t->content, "<")) return '<';
        if (!strcmp(t->content, ">=")) return GREATER_EQUAL;
        if (!strcmp(t->content, "<=")) return LESS_EQUAL;
    }
    else if (strcmp(t->token_type, "PUNCTUATION") == 0) {
        if (!strcmp(t->content, "{")) return '{';
        if (!strcmp(t->content, "}")) return '}';
        if (!strcmp(t->content, "[")) return '[';
        if (!strcmp(t->content, "]")) return ']';
        if (!strcmp(t->content, "(")) return '(';
        if (!strcmp(t->content, ")")) return ')';
        if (!strcmp(t->content, ";")) return ';';
        if (!strcmp(t->content, ",")) return ',';
        if (!strcmp(t->content, ":")) return ':';
        if (!strcmp(t->content, "::")) return GLOBAL_SCOPE;
        if (!strcmp(t->content, ".")) return '.';
        if (!strcmp(t->content, "..")) return DOT_DOT;
    }
    else if (strcmp(t->token_type, "CONST_INT") == 0 || strcmp(t->token_type, "CONST_REAL") == 0) {
        return NUMBER;
    }
    else if (strcmp(t->token_type, "IDENT") == 0) {
        return ID;
    }
    else if (strcmp(t->token_type, "String") == 0) {
        return STRING;
    }
    return -1; // Unknown token fallback
}

/* 3. Custom yylex() that acts as a bridge between your list and Bison */
int yylex() {
    while (current_token != NULL) {
        // FILTER: If the token is a comment, skip it and move to the next!
        if (strcmp(current_token->token_type, "MULTI_COMMENT") == 0 ||
            strcmp(current_token->token_type, "NESTED_COMMENT") == 0 ||
            strcmp(current_token->token_type, "SINGLE_LINE_COMMENT") == 0) {
            
            current_token = current_token->next;
            continue;
        }

        // Get the Bison equivalent of your custom token
        int token_id = get_bison_token(current_token);

        // Pass values to Bison via yylval for identifiers and literals
        if (token_id == ID || token_id == NUMBER || token_id == STRING) {
            yylval.strVal = strdup(current_token->content);
        }

        // Keep the error reporting line synced with your token's original line
        yylineno = current_token->line;

        // Move the pointer forward for the next call
        current_token = current_token->next;

        if (token_id != -1) {
            return token_id;
        }
    }
    return 0; // Returning 0 tells Bison we reached the End of File
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (!(yyin = fopen(argv[1], "r"))) {
            fprintf(stderr, "Error: Cannot read file: %s\n", argv[1]);
            return 1;
        }
    } else {
        printf("Usage: ./parser <filename>\n");
        return 1;
    }

    /* PHASE 1: Build the token list using your Flex scanner */
    set_source_file(argv[1]);
    alpha_yylex(NULL);

    /* Set the parser pointer to the start of your generated list */
    current_token = head;

    init_symtable();

    /* PHASE 2: Parse using the custom yylex() which skips comments */
    yyparse();
    
    print_symtable();

    /* PHASE 3: write intermediate code ONLY on a fully successful parse
     * (no syntax errors and no semantic errors). */
    if (error_count == 0) {
        write_quads_to_file("quads.txt");
        /* PHASE 4: generate target code and emit the VM binary + text dump. */
        generate_target_code();
        write_targetcode_text("targetcode.txt");
        write_targetcode_binary("out.abc");
    } else {
        fprintf(stderr, "Compilation produced %d error(s); quads.txt not generated.\n", error_count);
    }

    if (yyin) {
        fclose(yyin);
    }
    
    return 0;
}
