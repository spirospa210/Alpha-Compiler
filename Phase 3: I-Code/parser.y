%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"
#include "token.h"
#include "quads.h"

extern int yylineno;
extern FILE* yyin; 
void yyerror(const char* s);

/* External variables from the scanner.l */
extern struct token * head;
extern int alpha_yylex(void* ylval);

/* Pointer to traverse the linked list during parsing */
struct token * current_token = NULL;

int current_scope = 0;
int anon_func_counter = 0;
bool next_block_is_func = false;

/* Stacks to keep track of function boundaries to prevent illegal outer access */
int* func_scope_stack = NULL;
char** current_func_name_stack = NULL; 
int func_scope_stack_top = 0;
int func_scope_stack_capacity = 0;

/* Add this to track if we are inside a loop */
int loop_counter = 0;

/* Function prototypes */
char* new_anon_func_name();
int get_bison_token(struct token* t);
int yylex();
void push_func_stack(const char* name, int scope);

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

/* Helper to dynamically push to the function stack */
void push_func_stack(const char* name, int scope) {
    if (func_scope_stack_top >= func_scope_stack_capacity) {
        func_scope_stack_capacity = (func_scope_stack_capacity == 0) ? 10 : func_scope_stack_capacity * 2;
        func_scope_stack = realloc(func_scope_stack, func_scope_stack_capacity * sizeof(int));
        current_func_name_stack = realloc(current_func_name_stack, func_scope_stack_capacity * sizeof(char*));
        
        if (!func_scope_stack || !current_func_name_stack) {
            fprintf(stderr, "Fatal Error: Memory reallocation failed for function stack.\n");
            exit(1);
        }
    }
    current_func_name_stack[func_scope_stack_top] = strdup(name);
    func_scope_stack[func_scope_stack_top++] = scope;
}
%}

/* Enable Bison's detailed reduction error reporting */
%define parse.error verbose

%union {
    char* strVal;
    struct SymbolTableEntry* symNode;
    struct expr* exprNode;
    struct stmt_t* stmtNode;
    struct callsuffix_t* callSuffixNode;
    unsigned quadLabel;
}

%token <strVal> ID NUMBER STRING
%token IF ELSE WHILE FOR RETURN BREAK CONTINUE FUNCTION LOCAL
%token TRUE_TOKEN FALSE_TOKEN NIL
%token AND OR NOT GLOBAL_SCOPE PLUS_PLUS MINUS_MINUS EQUAL_EQUAL NOT_EQUAL GREATER_EQUAL LESS_EQUAL DOT_DOT

%type <exprNode> lvalue expr term assignexpr primary call const elist expr_list objectdef indexed indexedelem member
%type <stmtNode> stmt stmt_list ifstmt whilestmt forstmt returnstmt block funcdef break_stmt continue_stmt loopstmt
%type <callSuffixNode> callsuffix normcall methodcall
%type <exprNode> ifprefix whileexpr forprefix funcdeclare funcparams
%type <quadLabel> M N elseprefix loopenter whilestart

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

/* Expect 1 shift/reduce conflict for dangling else */
%expect 1

%%

program:
      stmt_list { printf("LINE: %d program <- stmt_list\n", yylineno); }
    | /* empty */ { printf("LINE: %d program <- empty\n", yylineno); }
    ;

stmt_list:
      stmt_list stmt { 
          printf("LINE: %d statement list <- statement statement list\n", yylineno); 
          $$ = makestmt();
          $$->breaklist = merge($1 ? $1->breaklist : NULL, $2 ? $2->breaklist : NULL);
          $$->continuelist = merge($1 ? $1->continuelist : NULL, $2 ? $2->continuelist : NULL);
      }
    | stmt { 
          printf("LINE: %d statement list <- statement\n", yylineno); 
          $$ = $1;
      }
    ;

stmt:
      expr ';' { 
          printf("LINE: %d statement <- expression ;\n", yylineno); 
          $$ = makestmt(); 
          resettemp(); 
      }
    | ifstmt { printf("LINE: %d statement <- if\n", yylineno); $$ = $1; }
    | whilestmt { printf("LINE: %d statement <- while\n", yylineno); $$ = $1; }
    | forstmt { printf("LINE: %d statement <- for\n", yylineno); $$ = $1; }
    | returnstmt { printf("LINE: %d statement <- return\n", yylineno); $$ = $1; }
    | break_stmt { printf("LINE: %d statement <- break ;\n", yylineno); $$ = $1; }
    | continue_stmt { printf("LINE: %d statement <- continue ;\n", yylineno); $$ = $1; }
    | block { printf("LINE: %d statement <- block\n", yylineno); $$ = $1; }
    | funcdef { printf("LINE: %d statement <- function definition\n", yylineno); $$ = $1; }
    | ';' { printf("LINE: %d statement <- ;\n", yylineno); $$ = makestmt(); }
    /* Error recovery */
    | error ';' { yyerrok; $$ = NULL; }
    ;

/* Invisible Markers */
M: /* empty */ { $$ = nextquadlabel(); };
N: /* empty */ { $$ = nextquadlabel(); emit(jump_op, NULL, NULL, NULL, 0, yylineno); };

break_stmt:
      BREAK ';' {
          printf("LINE: %d break <- BREAK ;\n", yylineno);
          $$ = makestmt();
          $$->breaklist = makelist(nextquadlabel());
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          if (loop_counter == 0) {
              printf("Error at line %d: Use of 'break' while not in a loop.\n", yylineno);
          }
      }
    ;

continue_stmt:
      CONTINUE ';' {
          printf("LINE: %d continue <- CONTINUE ;\n", yylineno);
          $$ = makestmt();
          $$->continuelist = makelist(nextquadlabel());
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          if (loop_counter == 0) {
              printf("Error at line %d: Use of 'continue' while not in a loop.\n", yylineno);
          }
      }
    ;

expr:
      assignexpr { printf("LINE: %d expression <- assignexpr\n", yylineno); $$ = $1; }
    | expr '+' expr { printf("LINE: %d expression <- expression + expression\n", yylineno); $$ = newexpr(arithexpr_e); $$->sym = newtemp(current_scope, yylineno); emit(add_op, $$, $1, $3, 0, yylineno); }
    | expr '-' expr { printf("LINE: %d expression <- expression - expression\n", yylineno); $$ = newexpr(arithexpr_e); $$->sym = newtemp(current_scope, yylineno); emit(sub_op, $$, $1, $3, 0, yylineno); }
    | expr '*' expr { printf("LINE: %d expression <- expression * expression\n", yylineno); $$ = newexpr(arithexpr_e); $$->sym = newtemp(current_scope, yylineno); emit(mul_op, $$, $1, $3, 0, yylineno); }
    | expr '/' expr { printf("LINE: %d expression <- expression / expression\n", yylineno); $$ = newexpr(arithexpr_e); $$->sym = newtemp(current_scope, yylineno); emit(div_op, $$, $1, $3, 0, yylineno); }
    | expr '%' expr { printf("LINE: %d expression <- expression %% expression\n", yylineno); $$ = newexpr(arithexpr_e); $$->sym = newtemp(current_scope, yylineno); emit(mod_op, $$, $1, $3, 0, yylineno); }
    | expr '>' expr { printf("LINE: %d expression <- expression > expression\n", yylineno); $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); $$->truelist = makelist(nextquadlabel()); $$->falselist = makelist(nextquadlabel() + 1); emit(if_greater_op, NULL, $1, $3, 0, yylineno); emit(jump_op, NULL, NULL, NULL, 0, yylineno); }
    | expr GREATER_EQUAL expr { printf("LINE: %d expression <- expression >= expression\n", yylineno); $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); $$->truelist = makelist(nextquadlabel()); $$->falselist = makelist(nextquadlabel() + 1); emit(if_greatereq_op, NULL, $1, $3, 0, yylineno); emit(jump_op, NULL, NULL, NULL, 0, yylineno); }
    | expr '<' expr { printf("LINE: %d expression <- expression < expression\n", yylineno); $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); $$->truelist = makelist(nextquadlabel()); $$->falselist = makelist(nextquadlabel() + 1); emit(if_less_op, NULL, $1, $3, 0, yylineno); emit(jump_op, NULL, NULL, NULL, 0, yylineno); }
    | expr LESS_EQUAL expr { printf("LINE: %d expression <- expression <= expression\n", yylineno); $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); $$->truelist = makelist(nextquadlabel()); $$->falselist = makelist(nextquadlabel() + 1); emit(if_lesseq_op, NULL, $1, $3, 0, yylineno); emit(jump_op, NULL, NULL, NULL, 0, yylineno); }
    | expr EQUAL_EQUAL expr { printf("LINE: %d expression <- expression == expression\n", yylineno); $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); $$->truelist = makelist(nextquadlabel()); $$->falselist = makelist(nextquadlabel() + 1); emit(if_eq_op, NULL, $1, $3, 0, yylineno); emit(jump_op, NULL, NULL, NULL, 0, yylineno); }
    | expr NOT_EQUAL expr { printf("LINE: %d expression <- expression != expression\n", yylineno); $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); $$->truelist = makelist(nextquadlabel()); $$->falselist = makelist(nextquadlabel() + 1); emit(if_noteq_op, NULL, $1, $3, 0, yylineno); emit(jump_op, NULL, NULL, NULL, 0, yylineno); }
    | expr AND M expr { 
          printf("LINE: %d expression <- expression AND expression\n", yylineno); 
          make_booltest($1, yylineno); make_booltest($4, yylineno);
          $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno);
          backpatch($1->truelist, $3);
          $$->truelist = $4->truelist;
          $$->falselist = merge($1->falselist, $4->falselist);
      }
    | expr OR M expr { 
          printf("LINE: %d expression <- expression OR expression\n", yylineno); 
          make_booltest($1, yylineno); make_booltest($4, yylineno);
          $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno);
          backpatch($1->falselist, $3);
          $$->truelist = merge($1->truelist, $4->truelist);
          $$->falselist = $4->falselist;
      }
    | term { printf("LINE: %d expression <- term\n", yylineno); $$ = $1; }
    ;

term:
      '(' expr ')' { printf("LINE: %d term <- ( expr )\n", yylineno); $$ = $2; }
    | '-' expr %prec UMINUS { printf("LINE: %d term <- - expression\n", yylineno); $$ = newexpr(arithexpr_e); $$->sym = newtemp(current_scope, yylineno); emit(uminus_op, $$, $2, NULL, 0, yylineno); }
    | NOT expr { 
          printf("LINE: %d term <- not expression\n", yylineno); 
          make_booltest($2, yylineno); 
          $$ = newexpr(boolexpr_e); $$->sym = newtemp(current_scope, yylineno); 
          $$->truelist = $2->falselist; $$->falselist = $2->truelist; 
      }
    | PLUS_PLUS lvalue {
          printf("LINE: %d term <- ++lvalue\n", yylineno);
          if ($2 != NULL && ($2->sym) && ($2->sym->type == USER_FUNC || $2->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function '%s'.\n", yylineno, $2->sym->name);
          }
          emit(add_op, $2, $2, newexpr_constnum(1), 0, yylineno);
          $$ = newexpr(assignexpr_e); $$->sym = newtemp(current_scope, yylineno);
          emit(assign_op, $$, $2, NULL, 0, yylineno); 
      }
    | lvalue PLUS_PLUS {
          printf("LINE: %d term <- lvalue++\n", yylineno);
          if ($1 != NULL && ($1->sym) && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function '%s'.\n", yylineno, $1->sym->name);
          }
          $$ = newexpr(assignexpr_e); $$->sym = newtemp(current_scope, yylineno);
          emit(assign_op, $$, $1, NULL, 0, yylineno); 
          emit(add_op, $1, $1, newexpr_constnum(1), 0, yylineno);
      }
    | MINUS_MINUS lvalue {
          printf("LINE: %d term <- --lvalue\n", yylineno);
          if ($2 != NULL && ($2->sym) && ($2->sym->type == USER_FUNC || $2->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '--' to function '%s'.\n", yylineno, $2->sym->name);
          }
          emit(sub_op, $2, $2, newexpr_constnum(1), 0, yylineno);
          $$ = newexpr(assignexpr_e); $$->sym = newtemp(current_scope, yylineno);
          emit(assign_op, $$, $2, NULL, 0, yylineno); 
      }
    | lvalue MINUS_MINUS {
          printf("LINE: %d term <- lvalue--\n", yylineno);
          if ($1 != NULL && ($1->sym) && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '--' to function '%s'.\n", yylineno, $1->sym->name);
          }
          $$ = newexpr(assignexpr_e); $$->sym = newtemp(current_scope, yylineno);
          emit(assign_op, $$, $1, NULL, 0, yylineno); 
          emit(sub_op, $1, $1, newexpr_constnum(1), 0, yylineno);
      }
    | primary { printf("LINE: %d term <- primary\n", yylineno); $$ = $1; }
    ;

assignexpr:
      lvalue '=' expr {
          printf("LINE: %d assignexpr <- lvalue = expression\n", yylineno);
          if ($1 != NULL && ($1->sym) && ($1->sym->type == USER_FUNC || $1->sym->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot assign to function '%s'.\n", yylineno, $1->sym->name);
          }
          if ($1->type == tableitem_e) {
              emit(tablesetelem_op, $1, $1->index, $3, 0, yylineno);
              $$ = emit_iftableitem($1, yylineno);
              $$->type = assignexpr_e;
          } else {
              emit(assign_op, $1, $3, NULL, 0, yylineno);
              $$ = newexpr(assignexpr_e);
              $$->sym = newtemp(current_scope, yylineno);
              emit(assign_op, $$, $1, NULL, 0, yylineno); 
          }
      }
    ;

primary:
      lvalue { printf("LINE: %d primary <- lvalue\n", yylineno); $$ = emit_iftableitem($1, yylineno); }
    | call { printf("LINE: %d primary <- call\n", yylineno); $$ = $1; }
    | objectdef { printf("LINE: %d primary <- objectdef\n", yylineno); $$ = $1; }
    | '(' funcdef ')' { printf("LINE: %d primary <- ( funcdef )\n", yylineno); $$ = newexpr(programfunc_e); }
    | const { printf("LINE: %d primary <- const\n", yylineno); $$ = $1; }
    ;

lvalue:
      ID { 
          printf("LINE: %d lvalue <- ID\n", yylineno);
          SymbolTableEntry* sym = lookup_all($1, current_scope);
          if (sym) {
              if (sym->scope != 0 && sym->type != USER_FUNC && sym->type != LIB_FUNC) {
                  if (func_scope_stack_top > 0) {
                      int closest_func_scope = func_scope_stack[func_scope_stack_top - 1];
                      if (sym->scope < closest_func_scope) {
                          printf("Error at line %d: Cannot access '%s' inside function '%s'.\n", 
                                 yylineno, $1, current_func_name_stack[func_scope_stack_top - 1]);
                      }
                  }
              }
          } else {
              SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
              sym = insert_symbol($1, t, yylineno, current_scope);
          }
          $$ = newexpr(var_e); $$->sym = sym;
      }
    | LOCAL ID { 
          printf("LINE: %d lvalue <- LOCAL ID\n", yylineno);
          SymbolTableEntry* sym = lookup_scope($2, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($2, 0);
          
          if (current_scope != 0 && lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: local '%s' collides with a library function.\n", yylineno, $2);
              $$ = newexpr(libraryfunc_e); $$->sym = lib_collision;
          } else {
              if (!sym) {
                  SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
                  sym = insert_symbol($2, t, yylineno, current_scope);
              }
              $$ = newexpr(var_e); $$->sym = sym;
          }
      }
    | GLOBAL_SCOPE ID { 
          printf("LINE: %d lvalue <- :: ID\n", yylineno);
          SymbolTableEntry* sym = lookup_scope($2, 0);
          if (!sym) {
              printf("Error at line %d: Global variable '%s' not found.\n", yylineno, $2);
          }
          $$ = newexpr(var_e); $$->sym = sym;
      }
    | member { 
          printf("LINE: %d lvalue <- member\n", yylineno);
          $$ = $1; 
      }
    ;

member:
      lvalue '.' ID { printf("LINE: %d member <- lvalue . ID\n", yylineno); $$ = newexpr(tableitem_e); $$->sym = $1->sym; $$->index = newexpr_conststring($3); }
    | lvalue '[' expr ']' { printf("LINE: %d member <- lvalue [ expression ]\n", yylineno); $$ = newexpr(tableitem_e); $$->sym = $1->sym; $$->index = $3; }
    | call '.' ID { printf("LINE: %d member <- call . ID\n", yylineno); $$ = newexpr(tableitem_e); $$->sym = $1->sym; $$->index = newexpr_conststring($3); }
    | call '[' expr ']' { printf("LINE: %d member <- call [ expression ]\n", yylineno); $$ = newexpr(tableitem_e); $$->sym = $1->sym; $$->index = $3; }
    ;

call:
      call '(' elist ')' { 
          printf("LINE: %d call <- call ( elist )\n", yylineno); 
          emit_params($3, yylineno);
          emit(call_op, $1, NULL, NULL, 0, yylineno);
          $$ = newexpr(var_e); $$->sym = newtemp(current_scope, yylineno);
          emit(getretval_op, $$, NULL, NULL, 0, yylineno);
      }
    | lvalue callsuffix { 
          printf("LINE: %d call <- lvalue callsuffix\n", yylineno); 
          if ($2->method) {
              struct expr* self = $1;
              $1 = emit_iftableitem(newexpr(tableitem_e), yylineno);
              $1->sym = self->sym; $1->index = newexpr_conststring($2->name);
              $1 = emit_iftableitem($1, yylineno);
              struct expr* self_param = emit_iftableitem(self, yylineno);
              self_param->next = $2->elist; 
              $2->elist = self_param;
          }
          emit_params($2->elist, yylineno);
          emit(call_op, $1, NULL, NULL, 0, yylineno);
          $$ = newexpr(var_e); $$->sym = newtemp(current_scope, yylineno);
          emit(getretval_op, $$, NULL, NULL, 0, yylineno);
      }
    | '(' funcdef ')' '(' elist ')' { 
          printf("LINE: %d call <- ( funcdef ) ( elist )\n", yylineno); 
          struct expr* func = newexpr(programfunc_e);
          emit_params($5, yylineno);
          emit(call_op, func, NULL, NULL, 0, yylineno);
          $$ = newexpr(var_e); $$->sym = newtemp(current_scope, yylineno);
          emit(getretval_op, $$, NULL, NULL, 0, yylineno);
      }
    ;

callsuffix:
      normcall { printf("LINE: %d callsuffix <- normcall\n", yylineno); $$ = $1; }
    | methodcall { printf("LINE: %d callsuffix <- methodcall\n", yylineno); $$ = $1; }
    ;

normcall:
      '(' elist ')' { 
          printf("LINE: %d normcall <- ( elist )\n", yylineno); 
          $$ = (struct callsuffix_t*)malloc(sizeof(struct callsuffix_t));
          $$->elist = $2; $$->method = false; $$->name = NULL;
      }
    ;

methodcall:
      DOT_DOT ID '(' elist ')' { 
          printf("LINE: %d methodcall <- .. ID ( elist )\n", yylineno); 
          $$ = (struct callsuffix_t*)malloc(sizeof(struct callsuffix_t));
          $$->elist = $4; $$->method = true; $$->name = strdup($2);
      }
    ;

elist:
      expr_list { printf("LINE: %d elist <- expr_list\n", yylineno); $$ = $1; }
    | /* empty */ { printf("LINE: %d elist <- empty\n", yylineno); $$ = NULL; }
    ;

expr_list:
      expr_list ',' expr { printf("LINE: %d expr_list <- expr_list , expression\n", yylineno); struct expr* temp = $1; while(temp->next) temp = temp->next; temp->next = $3; $$ = $1; }
    | expr { printf("LINE: %d expr_list <- expression\n", yylineno); $$ = $1; }
    ;

objectdef:
      '[' elist ']' { 
          printf("LINE: %d objectdef <- [ elist ]\n", yylineno); 
          $$ = newexpr(newtable_e); $$->sym = newtemp(current_scope, yylineno);
          emit(tablecreate_op, $$, NULL, NULL, 0, yylineno);
          int i = 0; struct expr* temp = $2;
          while (temp) {
              emit(tablesetelem_op, $$, newexpr_constnum(i++), temp, 0, yylineno);
              temp = temp->next;
          }
      }
    | '[' indexed ']' { 
          printf("LINE: %d objectdef <- [ indexed ]\n", yylineno); 
          $$ = newexpr(newtable_e); $$->sym = newtemp(current_scope, yylineno);
          emit(tablecreate_op, $$, NULL, NULL, 0, yylineno);
          struct expr* temp = $2;
          while (temp) {
              emit(tablesetelem_op, $$, temp->index, temp, 0, yylineno);
              temp = temp->next;
          }
      }
    ;

indexed:
      indexed ',' indexedelem { printf("LINE: %d indexed <- indexed , indexedelem\n", yylineno); struct expr* temp = $1; while(temp->next) temp = temp->next; temp->next = $3; $$ = $1; }
    | indexedelem { printf("LINE: %d indexed <- indexedelem\n", yylineno); $$ = $1; }
    ;

indexedelem:
      '{' expr ':' expr '}' { printf("LINE: %d indexedelem <- { expression : expression }\n", yylineno); $$ = $4; $$->index = $2; }
    ;

block:
      '{' {
          if (!next_block_is_func) current_scope++;
          next_block_is_func = false;
      } stmt_list '}' { 
          printf("LINE: %d block <- { stmt_list }\n", yylineno);
          hide_scope(current_scope);
          current_scope--; 
          $$ = $3;
      }
    | '{' {
          if (!next_block_is_func) current_scope++;
          next_block_is_func = false;
      } '}' { 
          printf("LINE: %d block <- { }\n", yylineno);
          hide_scope(current_scope);
          current_scope--; 
          $$ = makestmt();
      }
    ;

funcdeclare:
      FUNCTION ID {
          SymbolTableEntry* collision = lookup_scope($2, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($2, 0);
          
          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Function name '%s' collides with a library function.\n", yylineno, $2);
              } else {
                  printf("Error at line %d: Collision with %s named '%s' defined at line %d\n", 
                         yylineno, get_symbol_type_string(collision->type), $2, collision->line);
              }
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Function name '%s' collides with a library function.\n", yylineno, $2);
          } else {
              insert_symbol($2, USER_FUNC, yylineno, current_scope);
          }
          
          next_block_is_func = true;
          current_scope++;
          
          push_func_stack($2, current_scope);
          printf("LINE: %d funcdeclare <- FUNCTION ID\n", yylineno);

          $$ = newexpr(programfunc_e);
          $$->sym = lookup_scope($2, current_scope-1);
          $$->quad_jump = nextquadlabel();
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          emit(funcstart_op, $$, NULL, NULL, 0, yylineno);
      }
    | FUNCTION {
          char* name = new_anon_func_name();
          insert_symbol(name, USER_FUNC, yylineno, current_scope);
          
          next_block_is_func = true;
          current_scope++;
          
          push_func_stack(name, current_scope);
          printf("LINE: %d funcdeclare <- FUNCTION\n", yylineno);

          $$ = newexpr(programfunc_e);
          $$->sym = lookup_scope(name, current_scope-1);
          free(name);
          $$->quad_jump = nextquadlabel();
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
          emit(funcstart_op, $$, NULL, NULL, 0, yylineno);
      }
    ;

funcparams:
      '(' idlist ')' { printf("LINE: %d funcparams <- ( idlist )\n", yylineno); }
    ;

funcdef:
      funcdeclare funcparams block {
          printf("LINE: %d funcdef <- FUNCTION ID ( idlist ) block\n", yylineno);
          func_scope_stack_top--;
          free(current_func_name_stack[func_scope_stack_top]);

          emit(funcend_op, $1, NULL, NULL, 0, yylineno);
          patchlabel($1->quad_jump, nextquadlabel());

          $$ = makestmt();
      }
    ;

const:
      NUMBER { printf("LINE: %d const <- NUMBER\n", yylineno); $$ = newexpr_constnum(atof($1)); }
    | STRING { printf("LINE: %d const <- STRING\n", yylineno); $$ = newexpr_conststring($1); }
    | NIL { printf("LINE: %d const <- NIL\n", yylineno); $$ = newexpr(nil_e); }
    | TRUE_TOKEN { printf("LINE: %d const <- TRUE\n", yylineno); $$ = newexpr_constbool(1); }
    | FALSE_TOKEN { printf("LINE: %d const <- FALSE\n", yylineno); $$ = newexpr_constbool(0); }
    ;

idlist:
      id_seq { printf("LINE: %d idlist <- id_seq\n", yylineno); }
    | /* empty */ { printf("LINE: %d idlist <- \n", yylineno); }
    ;

id_seq:
      id_seq ',' ID { 
          printf("LINE: %d id_seq <- id_seq , ID\n", yylineno);
          SymbolTableEntry* collision = lookup_scope($3, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($3, 0);
          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $3);
              } else {
                  printf("Error at line %d: Formal argument '%s' collides with %s defined at line %d\n", 
                         yylineno, $3, get_symbol_type_string(collision->type), collision->line);
              }
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $3);
          } else {
              insert_symbol($3, FORMAL_ARG, yylineno, current_scope);
          }
      }
    | ID { 
          printf("LINE: %d id_seq <- ID\n", yylineno);
          SymbolTableEntry* collision = lookup_scope($1, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($1, 0);
          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $1);
              } else {
                  printf("Error at line %d: Formal argument '%s' collides with %s defined at line %d\n", 
                         yylineno, $1, get_symbol_type_string(collision->type), collision->line);
              }
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Formal argument '%s' collides with a library function.\n", yylineno, $1);
          } else {
              insert_symbol($1, FORMAL_ARG, yylineno, current_scope);
          }
      }
    ;

ifprefix:
      IF '(' expr ')' { 
          printf("LINE: %d ifprefix <- IF ( expr )\n", yylineno); 
          make_booltest($3, yylineno);
          $$ = $3;
      }
    ;

elseprefix:
      ELSE { 
          printf("LINE: %d elseprefix <- ELSE\n", yylineno); 
          $$ = nextquadlabel();
          emit(jump_op, NULL, NULL, NULL, 0, yylineno);
      }
    ;

ifstmt:
      ifprefix M stmt { 
          printf("LINE: %d ifstmt <- ifprefix stmt\n", yylineno); 
          backpatch($1->truelist, $2);
          $$ = makestmt();
          $$->breaklist = $3 ? $3->breaklist : NULL;
          $$->continuelist = $3 ? $3->continuelist : NULL;
          backpatch($1->falselist, nextquadlabel());
      }
    | ifprefix M stmt elseprefix M stmt { 
          printf("LINE: %d ifstmt <- ifprefix stmt elseprefix stmt\n", yylineno); 
          backpatch($1->truelist, $2);
          backpatch($1->falselist, $5);
          patchlabel($4, nextquadlabel());
          $$ = makestmt();
          $$->breaklist = merge($3 ? $3->breaklist : NULL, $6 ? $6->breaklist : NULL);
          $$->continuelist = merge($3 ? $3->continuelist : NULL, $6 ? $6->continuelist : NULL);
      }
    ;

loopenter:
      /* empty */ { loop_counter++; $$ = nextquadlabel(); }
    ;

loopexit:
      /* empty */ { loop_counter--; }
    ;

loopstmt:
      loopenter stmt loopexit { 
          printf("LINE: %d loopstmt <- loopenter stmt loopexit\n", yylineno); 
          $$ = $2;
      }
    ;

whilestart:
      WHILE { printf("LINE: %d whilestart <- WHILE\n", yylineno); $$ = nextquadlabel(); }
    ;

whileexpr:
      '(' expr ')' { 
          printf("LINE: %d whileexpr <- ( expr )\n", yylineno); 
          make_booltest($2, yylineno);
          $$ = $2;
      }
    ;

whilestmt:
      whilestart whileexpr M loopstmt { 
          printf("LINE: %d whilestmt <- while stmt\n", yylineno); 
          backpatch($2->truelist, $3);
          emit(jump_op, NULL, NULL, NULL, $1, yylineno);
          backpatch($2->falselist, nextquadlabel());
          $$ = makestmt();
          if ($4) {
              backpatch($4->continuelist, $1);
              $$->breaklist = $4->breaklist;
          }
          $$->breaklist = merge($$->breaklist, $2->falselist);
      }
    ;

forprefix:
      FOR '(' elist ';' M expr ';' { 
          printf("LINE: %d forprefix <- FOR ( elist ; expr ;\n", yylineno); 
          make_booltest($6, yylineno);
          $$ = $6;
          $$->quad_jump = $5;
      }
    ;

forstmt:
      forprefix M elist N ')' M loopstmt { 
          printf("LINE: %d forstmt <- for ( elist ; expr ; elist ) stmt\n", yylineno); 
          backpatch($1->truelist, $6); 
          patchlabel($4, $1->quad_jump); 
          emit(jump_op, NULL, NULL, NULL, $2, yylineno); 
          $$ = makestmt();
          $$->breaklist = $7 ? $7->breaklist : NULL;
          $$->continuelist = $7 ? $7->continuelist : NULL;
          if ($$) {
               backpatch($$->continuelist, $2);
               $$->breaklist = merge($$->breaklist, $1->falselist);
          }
      }
    ;

returnstmt:
      RETURN expr ';' {
          printf("LINE: %d returnstmt <- RETURN expression ;\n", yylineno);
          if (func_scope_stack_top == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\n", yylineno);
          }
          emit(ret_op, $2, NULL, NULL, 0, yylineno);
          $$ = makestmt();
      }
    | RETURN ';' {
          printf("LINE: %d returnstmt <- RETURN ;\n", yylineno);
          if (func_scope_stack_top == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\n", yylineno);
          }
          emit(ret_op, NULL, NULL, NULL, 0, yylineno);
          $$ = makestmt();
      }
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Syntax Error at line %d: %s\n", yylineno, s);
}

char* new_anon_func_name() {
    char buffer[32];
    sprintf(buffer, "$%d", anon_func_counter++);
    return strdup(buffer);
}

/* Map custom string tokens to Bison Token IDs */
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

/* Custom yylex() that acts as a bridge between the list and Bison */
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
            fprintf(stderr, "Error: Cannot read file: %s\n", argv[1]);
            return 1;
        }
    } else {
        printf("Usage: ./parser <filename>\n");
        return 1;
    }

    alpha_yylex(NULL);
    current_token = head;
    init_symtable();
    
    yyparse();
    
    /* Output generation for Phase 3 and Phase 2 */
    print_quads("quads.txt");
    print_symtable();

    if (yyin) {
        fclose(yyin);
    }
    
    if (func_scope_stack) {
        free(func_scope_stack);
    }
    if (current_func_name_stack) {
        free(current_func_name_stack);
    }
    
    return 0;
}
