%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"
#include "quads.h"
#include "token.h"

extern int yylineno;
extern FILE* yyin; 
void yyerror(const char* s);

/* External variables from your scanner.l */
extern struct token * head;
extern int alpha_yylex(void* ylval);

/* Pointer to traverse your linked list during parsing */
struct token * current_token = NULL;

int current_scope = 0;
int anon_func_counter = 0;
bool next_block_is_func = false;

/* Stacks to keep track of function boundaries */
int func_scope_stack[100];
char* current_func_name_stack[100]; 
int func_scope_stack_top = 0;

/* Offset saving stack for nested functions */
unsigned local_offset_stack[100];

/* Stack to track return jumps to patch at funcend */
list_node* return_list_stack[100]; 

int loop_counter = 0;

/* Function prototypes */
char* new_anon_func_name();
int get_bison_token(struct token* t);
int yylex();

/* Helper to convert SymbolType enum to a string for error messages */
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

/* Helper to generate function calls */
expr* make_call(expr* lv, expr* reversed_elist, int line, int scope) {
    expr* func = emit_iftableitem(lv, scope, line);
    
    expr* tmp = reversed_elist;
    while(tmp != NULL) {
        emit(param_op, NULL, NULL, tmp, 0, line);
        tmp = tmp->next;
    }
    
    emit(call_op, NULL, NULL, func, 0, line);
    expr* result = newexpr(var_e);
    result->sym = newtemp(line, scope);
    emit(getretval_op, NULL, NULL, result, 0, line);
    return result;
}

/* Helper for logical short-circuiting check */
void make_bool(expr* e) {
    if (e->truelist == NULL && e->falselist == NULL) {
        e->truelist = makelist(nextquad());
        emit(if_eq_op, e, newexpr_constbool(1), NULL, 0, yylineno);
        e->falselist = makelist(nextquad());
        emit(jump_op, NULL, NULL, NULL, 0, yylineno);
    }
}

/* Helper to materialize short-circuit booleans into a temporary variable */
expr* backpatch_boolexpr(expr* e) {
    if (e->truelist != NULL || e->falselist != NULL) {
        expr* result = newexpr(var_e);
        result->sym = newtemp(yylineno, current_scope);
        
        backpatch(e->truelist, nextquad());
        emit(assign_op, newexpr_constbool(1), NULL, result, 0, yylineno);
        int jump_out = nextquad();
        emit(jump_op, NULL, NULL, NULL, 0, yylineno);
        
        backpatch(e->falselist, nextquad());
        emit(assign_op, newexpr_constbool(0), NULL, result, 0, yylineno);
        
        patchlabel(jump_out, nextquad());
        
        e->truelist = NULL;
        e->falselist = NULL;
        return result;
    }
    return e;
}


%}


%define parse.error verbose

%code requires {
    #include "quads.h"
    typedef struct stmt_t {
        list_node* breaklist;
        list_node* contlist;
    } stmt_t;
}

%union {
    char* strVal;
    struct SymbolTableEntry* symNode;
    struct expr* exprNode;
    struct stmt_t stmtNode;
    int label;
}

%token <strVal> ID NUMBER STRING
%token IF ELSE WHILE FOR RETURN BREAK CONTINUE FUNCTION LOCAL
%token TRUE_TOKEN FALSE_TOKEN NIL 
%token AND OR NOT GLOBAL_SCOPE PLUS_PLUS MINUS_MINUS EQUAL_EQUAL NOT_EQUAL GREATER_EQUAL LESS_EQUAL DOT_DOT

%type <exprNode> lvalue expr assignexpr term primary call objectdef const elist expr_list indexed indexedelem member callsuffix normcall methodcall funcname funcdef funcprefix if_prefix
%type <stmtNode> stmt stmt_list block ifstmt whilestmt forstmt returnstmt
%type <label> M N

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

/* Let Bison know we expect 1 shift/reduce conflict purely for the dangling else */
%expect 1 

%%

program:
      stmt_list 
    | /* empty */ 
    ;

stmt_list:
      stmt_list stmt {
          $$.breaklist = merge($1.breaklist, $2.breaklist);
          $$.contlist = merge($1.contlist, $2.contlist);
      }
    | stmt {
          $$.breaklist = $1.breaklist;
          $$.contlist = $1.contlist;
      }
    ;

stmt:
      expr ';' { $$.breaklist = NULL; $$.contlist = NULL; resettemp(); }
    | ifstmt { $$ = $1; resettemp(); }
    | whilestmt { $$ = $1; resettemp(); }
    | forstmt { $$ = $1; resettemp(); }
    | returnstmt { $$ = $1; resettemp(); }
    | BREAK ';' {
          if (loop_counter == 0) {
              printf("Error at line %d: Use of 'break' while not in a loop.\n", yylineno);
          }
          $$.breaklist = makelist(nextquad());
          $$.contlist = NULL;
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
      }
    | CONTINUE ';' {
          if (loop_counter == 0) {
              printf("Error at line %d: Use of 'continue' while not in a loop.\n", yylineno);
          }
          $$.breaklist = NULL;
          $$.contlist = makelist(nextquad());
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
      }
    | block { $$ = $1; }
    | funcdef { $$.breaklist = NULL; $$.contlist = NULL; }
    | ';' { $$.breaklist = NULL; $$.contlist = NULL; }
    | error ';' { yyerrok; $$.breaklist = NULL; $$.contlist = NULL; }
    ;

/* --- Dedicated Rule for the IF condition to prevent mid-rule conflicts --- */
if_prefix:
      IF '(' expr ')' {
          make_bool($3);
          $$ = $3;
      }
    ;

/* --- Marker Non-Terminals for Backpatching targets --- */
M: /* empty */ { $$ = nextquad(); } ;
N: /* empty */ { $$ = nextquad(); emit(jump_op, NULL, NULL, NULL, 0, yylineno); } ;

expr:
      assignexpr { $$ = $1; }
    | expr '+' expr {
          $$ = newexpr(arithexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(add_op, $1, $3, $$, 0, yylineno);
      }
    | expr '-' expr {
          $$ = newexpr(arithexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(sub_op, $1, $3, $$, 0, yylineno);
      }
    | expr '*' expr {
          $$ = newexpr(arithexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(mul_op, $1, $3, $$, 0, yylineno);
      }
    | expr '/' expr {
          $$ = newexpr(arithexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(div_op, $1, $3, $$, 0, yylineno);
      }
    | expr '%' expr {
          $$ = newexpr(arithexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(mod_op, $1, $3, $$, 0, yylineno);
      }
    | expr '>' expr {
          $$ = newexpr(boolexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(if_greater_op, $1, $3, NULL, nextquad() + 3, yylineno);
          emit(assign_op, newexpr_constbool(0), NULL, $$, 0, yylineno);
          emit(jump_op, NULL, NULL, NULL, nextquad() + 2, yylineno);
          emit(assign_op, newexpr_constbool(1), NULL, $$, 0, yylineno);
      }
    | expr GREATER_EQUAL expr {
          $$ = newexpr(boolexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(if_greatereq_op, $1, $3, NULL, nextquad() + 3, yylineno);
          emit(assign_op, newexpr_constbool(0), NULL, $$, 0, yylineno);
          emit(jump_op, NULL, NULL, NULL, nextquad() + 2, yylineno);
          emit(assign_op, newexpr_constbool(1), NULL, $$, 0, yylineno);
      }
    | expr '<' expr {
          $$ = newexpr(boolexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(if_less_op, $1, $3, NULL, nextquad() + 3, yylineno);
          emit(assign_op, newexpr_constbool(0), NULL, $$, 0, yylineno);
          emit(jump_op, NULL, NULL, NULL, nextquad() + 2, yylineno);
          emit(assign_op, newexpr_constbool(1), NULL, $$, 0, yylineno);
      }
    | expr LESS_EQUAL expr {
          $$ = newexpr(boolexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(if_lesseq_op, $1, $3, NULL, nextquad() + 3, yylineno);
          emit(assign_op, newexpr_constbool(0), NULL, $$, 0, yylineno);
          emit(jump_op, NULL, NULL, NULL, nextquad() + 2, yylineno);
          emit(assign_op, newexpr_constbool(1), NULL, $$, 0, yylineno);
      }
    | expr EQUAL_EQUAL expr {
          $$ = newexpr(boolexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(if_eq_op, $1, $3, NULL, nextquad() + 3, yylineno);
          emit(assign_op, newexpr_constbool(0), NULL, $$, 0, yylineno);
          emit(jump_op, NULL, NULL, NULL, nextquad() + 2, yylineno);
          emit(assign_op, newexpr_constbool(1), NULL, $$, 0, yylineno);
      }
    | expr NOT_EQUAL expr {
          $$ = newexpr(boolexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(if_noteq_op, $1, $3, NULL, nextquad() + 3, yylineno);
          emit(assign_op, newexpr_constbool(0), NULL, $$, 0, yylineno);
          emit(jump_op, NULL, NULL, NULL, nextquad() + 2, yylineno);
          emit(assign_op, newexpr_constbool(1), NULL, $$, 0, yylineno);
      }
    | expr AND { make_bool($1); $<label>$ = nextquad(); } expr {
          make_bool($4);
          $$ = newexpr(boolexpr_e);
          backpatch($1->truelist, $<label>3);
          $$->truelist = $4->truelist;
          $$->falselist = merge($1->falselist, $4->falselist);
      }
    | expr OR { make_bool($1); $<label>$ = nextquad(); } expr {
          make_bool($4);
          $$ = newexpr(boolexpr_e);
          backpatch($1->falselist, $<label>3);
          $$->truelist = merge($1->truelist, $4->truelist);
          $$->falselist = $4->falselist;
      }
    | term { $$ = $1; }
    ;

term:
      '(' expr ')' { $$ = $2; }
    | '-' expr %prec UMINUS {
          $$ = newexpr(arithexpr_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(uminus_op, $2, NULL, $$, 0, yylineno);
      }
    | NOT expr {
          make_bool($2);
          $$ = newexpr(boolexpr_e);
          $$->truelist = $2->falselist;
          $$->falselist = $2->truelist;
      }
    | PLUS_PLUS lvalue {
          if ($2->sym && ($2->sym->type == USER_FUNC || $2->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function.\\n", yylineno);
          }
          if ($2->type == tableitem_e) {
              $$ = emit_iftableitem($2, current_scope, yylineno);
              emit(add_op, $$, newexpr_constnum(1), $$, 0, yylineno);
              emit(tablesetelem_op, $2, $2->index, $$, 0, yylineno);
          } else {
              emit(add_op, $2, newexpr_constnum(1), $2, 0, yylineno);
              $$ = newexpr(var_e);
              $$->sym = newtemp(yylineno, current_scope);
              emit(assign_op, $2, NULL, $$, 0, yylineno);
          }
      }
    | lvalue PLUS_PLUS {
          $$ = newexpr(var_e);
          $$->sym = newtemp(yylineno, current_scope);
          if ($1->type == tableitem_e) {
              expr* val = emit_iftableitem($1, current_scope, yylineno);
              emit(assign_op, val, NULL, $$, 0, yylineno);
              emit(add_op, val, newexpr_constnum(1), val, 0, yylineno);
              emit(tablesetelem_op, $1, $1->index, val, 0, yylineno);
          } else {
              emit(assign_op, $1, NULL, $$, 0, yylineno);
              emit(add_op, $1, newexpr_constnum(1), $1, 0, yylineno);
          }
      }
    | MINUS_MINUS lvalue {
          $$ = newexpr(var_e); $$->sym = newtemp(yylineno, current_scope);
      }
    | lvalue MINUS_MINUS {
          $$ = newexpr(var_e); $$->sym = newtemp(yylineno, current_scope);
      }
    | primary { $$ = $1; }
    ;

assignexpr:
      lvalue '=' expr {
          // Resolve any dangling boolean jumps before assigning!
          $3 = backpatch_boolexpr($3);
          
          if ($1 != NULL && $1->sym != NULL && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot assign to function '%s'.\n", yylineno, $1->sym->name);
          }
          
          if ($1->type == tableitem_e) {
              emit(tablesetelem_op, $1, $1->index, $3, 0, yylineno);
              $$ = emit_iftableitem($1, current_scope, yylineno);
              emit(assign_op, $3, NULL, $$, 0, yylineno);
          } else {
              emit(assign_op, $3, NULL, $1, 0, yylineno);
              $$ = newexpr(assignexpr_e);
              $$->sym = newtemp(yylineno, current_scope);
              emit(assign_op, $1, NULL, $$, 0, yylineno);
          }
      }
    ;

primary:
      lvalue { $$ = emit_iftableitem($1, current_scope, yylineno); }
    | call { $$ = $1; }
    | objectdef { $$ = $1; }
    | '(' funcdef ')' { $$ = newexpr(programfunc_e); $$->sym = $2->sym; }
    | const { $$ = $1; }
    ;

lvalue:
      ID { 
          SymbolTableEntry* sym = lookup_all($1, current_scope);
          if (sym) {
              if (sym->scope != 0 && sym->type != USER_FUNC && sym->type != LIB_FUNC) {
                  if (func_scope_stack_top > 0) {
                      int closest_func_scope = func_scope_stack[func_scope_stack_top - 1];
                      if (sym->scope < closest_func_scope) {
                          printf("Error at line %d: Cannot access '%s' inside function.\\n", yylineno, $1);
                      }
                  }
              }
          } else {
              SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
              sym = insert_symbol($1, t, yylineno, current_scope);
          }
          $$ = newexpr(var_e);
          $$->sym = sym;
      }
    | LOCAL ID { 
          SymbolTableEntry* sym = lookup_scope($2, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($2, 0);
          
          if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: local '%s' collides with a library function.\\n", yylineno, $2);
              $$ = newexpr(var_e); $$->sym = lib_collision;
          } else {
              if (!sym) {
                  SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
                  sym = insert_symbol($2, t, yylineno, current_scope);
              }
              $$ = newexpr(var_e);
              $$->sym = sym;
          }
      }
    | GLOBAL_SCOPE ID { 
          SymbolTableEntry* sym = lookup_scope($2, 0);
          if (!sym) {
              printf("Error at line %d: Global variable '%s' not found.\\n", yylineno, $2);
          }
          $$ = newexpr(var_e);
          $$->sym = sym;
      }
    | member { $$ = $1; }
    ;

member:
      lvalue '.' ID {
          $$ = newexpr(tableitem_e);
          $$->sym = $1->sym;
          $$->index = newexpr_conststring($3);
      }
    | lvalue '[' expr ']' {
          $$ = newexpr(tableitem_e);
          $$->sym = $1->sym;
          $$->index = $3;
      }
    | call '.' ID {
          $$ = newexpr(tableitem_e);
          $$->sym = $1->sym;
          $$->index = newexpr_conststring($3);
      }
    | call '[' expr ']' {
          $$ = newexpr(tableitem_e);
          $$->sym = $1->sym;
          $$->index = $3;
      }
    ;

call:
      call '(' elist ')' { $$ = make_call($1, $3, yylineno, current_scope); }
    | lvalue callsuffix {
          $$ = make_call($1, $2, yylineno, current_scope);
      }
    | '(' funcdef ')' '(' elist ')' {
          expr* func = newexpr(programfunc_e);
          func->sym = $2->sym;
          $$ = make_call(func, $5, yylineno, current_scope);
      }
    ;

callsuffix:
      normcall { $$ = $1; }
    | methodcall { $$ = $1; }
    ;

normcall:
      '(' elist ')' { $$ = $2; }
    ;

methodcall:
      DOT_DOT ID '(' elist ')' { $$ = $4; }
    ;

elist:
      expr_list { $$ = $1; }
    | /* empty */ { $$ = NULL; }
    ;

expr_list:
      expr_list ',' expr {
          expr* tmp = $1;
          while(tmp->next) tmp = tmp->next;
          tmp->next = $3;
          $$ = $1;
      }
    | expr { $$ = $1; }
    ;

objectdef:
      '[' elist ']' {
          $$ = newexpr(newtable_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(tablecreate_op, NULL, NULL, $$, 0, yylineno);
          
          int i = 0;
          expr* tmp = $2;
          while (tmp) {
              emit(tablesetelem_op, $$, newexpr_constnum(i++), tmp, 0, yylineno);
              tmp = tmp->next;
          }
      }
    | '[' indexed ']' {
          $$ = newexpr(newtable_e);
          $$->sym = newtemp(yylineno, current_scope);
          emit(tablecreate_op, NULL, NULL, $$, 0, yylineno);
      }
    ;

indexed:
      indexed ',' indexedelem { $$ = $1; }
    | indexedelem { $$ = $1; }
    ;

indexedelem:
      '{' expr ':' expr '}' { $$ = $2; }
    ;

block_start:
      '{' {
          if (!next_block_is_func) current_scope++;
          next_block_is_func = false;
      }
    ;

block:
      block_start stmt_list '}' { 
          $$ = $2; 
          hide_scope(current_scope);
          current_scope--; 
      }
    | block_start '}' { 
          $$.breaklist = NULL; $$.contlist = NULL;
          hide_scope(current_scope);
          current_scope--; 
      }
    ;

funcprefix:
      FUNCTION funcname {
          $$ = $2;
          
          $$->truelist = makelist(nextquad()); 
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          
          emit(funcstart_op, NULL, NULL, newexpr_conststring($$->sym->name), 0, yylineno);
          
          local_offset_stack[func_scope_stack_top] = currscopeoffset();
          entering_function();
          resetformalargsoffset();
          resetfunctionlocalsoffset();
          
          next_block_is_func = true;
          current_scope++;
          
          current_func_name_stack[func_scope_stack_top] = strdup($$->sym->name);
          return_list_stack[func_scope_stack_top] = NULL; 
          func_scope_stack[func_scope_stack_top++] = current_scope;
      }
    ;

funcname:
      ID {
          SymbolTableEntry* sym = insert_symbol($1, USER_FUNC, yylineno, current_scope);
          sym->taddress = nextquad(); 
          $$ = newexpr(programfunc_e);
          $$->sym = sym;
      }
    | /* empty */ {
          char* name = new_anon_func_name();
          SymbolTableEntry* sym = insert_symbol(name, USER_FUNC, yylineno, current_scope);
          sym->taddress = nextquad();
          $$ = newexpr(programfunc_e);
          $$->sym = sym;
          free(name);
      }
    ;

funcdef:
      funcprefix '(' idlist ')' block {
          func_scope_stack_top--;
          free(current_func_name_stack[func_scope_stack_top]);
          
          $1->sym->totalLocals = currscopeoffset(); 
          exiting_function();
          
          backpatch(return_list_stack[func_scope_stack_top], nextquad());
          
          emit(funcend_op, NULL, NULL, newexpr_conststring($1->sym->name), 0, yylineno);
          
          backpatch($1->truelist, nextquad());
          
          $$ = $1;
      }
    ;

const:
      NUMBER { $$ = newexpr_constnum(atof($1)); }
    | STRING { $$ = newexpr_conststring($1); }
    | NIL { $$ = newexpr(nil_e); }
    | TRUE_TOKEN { $$ = newexpr_constbool(1); }
    | FALSE_TOKEN { $$ = newexpr_constbool(0); }
    ;

idlist:
      id_seq 
    | /* empty */ 
    ;

id_seq:
      id_seq ',' ID { insert_symbol($3, FORMAL_ARG, yylineno, current_scope); }
    | ID { insert_symbol($1, FORMAL_ARG, yylineno, current_scope); }
    ;

/* --- IF Statements refactored using common if_prefix and M/N markers --- */
ifstmt:
      if_prefix M stmt ELSE N M stmt {
          backpatch($1->truelist, $2);
          backpatch($1->falselist, $6);
          $$.breaklist = merge($3.breaklist, $7.breaklist);
          $$.contlist = merge($3.contlist, $7.contlist);
          patchlabel($5, nextquad());
      }
    | if_prefix M stmt {
          backpatch($1->truelist, $2);
          $$.breaklist = $3.breaklist;
          $$.contlist = $3.contlist;
          backpatch($1->falselist, nextquad());
      }
    ;

whilestmt:
      WHILE '(' { loop_counter++; $<label>$ = nextquad(); } expr ')' { make_bool($4); $<label>$ = nextquad(); } stmt {
          backpatch($4->truelist, $<label>6);
          backpatch($7.contlist, $<label>3);
          emit(jump_op, NULL, NULL, NULL, $<label>3, yylineno);
          backpatch($4->falselist, nextquad());
          backpatch($7.breaklist, nextquad());
          loop_counter--;
      }
    ;
forstmt:
      FOR '(' elist ';' { $<label>$ = nextquad(); } expr ';' { $<label>$ = nextquad(); } elist { $<label>$ = nextquad(); emit(jump_op, NULL, NULL, NULL, 0, yylineno); } ')' { $<label>$ = nextquad(); } stmt {
          loop_counter--;
      }
    ;

returnstmt:
      RETURN expr ';' {
          if (func_scope_stack_top == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\\n", yylineno);
          }
          emit(ret_op, NULL, NULL, $2, 0, yylineno);
          
          return_list_stack[func_scope_stack_top - 1] = merge(
              return_list_stack[func_scope_stack_top - 1], 
              makelist(nextquad())
          );
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          
          $$.breaklist = NULL; $$.contlist = NULL;
      }
    | RETURN ';' {
          if (func_scope_stack_top == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\\n", yylineno);
          }
          emit(ret_op, NULL, NULL, NULL, 0, yylineno);
          
          return_list_stack[func_scope_stack_top - 1] = merge(
              return_list_stack[func_scope_stack_top - 1], 
              makelist(nextquad())
          );
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          
          $$.breaklist = NULL; $$.contlist = NULL;
      }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Syntax Error at line %d: %s\\n", yylineno, s);
}

char* new_anon_func_name() {
    char buffer[32];
    sprintf(buffer, "$%d", anon_func_counter++);
    return strdup(buffer);
}

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
    return -1; 
}

int yylex() {
    while (current_token != NULL) {
        if (strcmp(current_token->token_type, "MULTI_COMMENT") == 0 ||
            strcmp(current_token->token_type, "NESTED_COMMENT") == 0 ||
            strcmp(current_token->token_type, "SINGLE_LINE_COMMENT") == 0) {
            current_token = current_token->next;
            continue;
        }

        int token_id = get_bison_token(current_token);

        if (token_id == ID || token_id == NUMBER || token_id == STRING) {
            yylval.strVal = strdup(current_token->content);
        }

        yylineno = current_token->line;
        current_token = current_token->next;

        if (token_id != -1) {
            return token_id;
        }
    }
    return 0; 
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (!(yyin = fopen(argv[1], "r"))) {
            fprintf(stderr, "Error: Cannot read file: %s\\n", argv[1]);
            return 1;
        }
    } else {
        printf("Usage: ./parser <filename>\\n");
        return 1;
    }

    alpha_yylex(NULL);
    current_token = head;

    init_symtable();
    
    if (yyparse() == 0) {
        print_quads();
    }
    
    if (yyin) {
        fclose(yyin);
    }
    
    return 0;
}