%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"

extern int yylineno;
extern char* yytext;
extern FILE* yyin; 
int yylex();
void yyerror(const char* s);

int current_scope = 0;
int anon_func_counter = 0;
bool next_block_is_func = false;

char* new_anon_func_name() {
    char buffer[32];
    sprintf(buffer, "$%d", anon_func_counter++);
    return strdup(buffer);
}

%}

%union {
    char* strVal;
    struct SymbolTableEntry* symNode;
}

%token <strVal> ID
%token IF ELSE WHILE FOR RETURN BREAK CONTINUE FUNCTION LOCAL
%token TRUE_TOKEN FALSE_TOKEN NIL NUMBER STRING
%token AND OR NOT GLOBAL_SCOPE PLUS_PLUS MINUS_MINUS EQUAL_EQUAL NOT_EQUAL GREATER_EQUAL LESS_EQUAL DOT_DOT

%type <symNode> lvalue

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
      stmt_list 
    | /* empty */ 
    ;

stmt_list:
      stmt_list stmt
    | stmt
    ;

stmt:
      expr ';' 
    | ifstmt 
    | whilestmt 
    | forstmt 
    | returnstmt 
    | BREAK ';' 
    | CONTINUE ';' 
    | block 
    | funcdef 
    | ';' 
    | error ';' { yyerrok; /* Safely skip to the next semicolon on error */ }
    ;

expr:
      assignexpr 
    | expr '+' expr 
    | expr '-' expr 
    | expr '*' expr 
    | expr '/' expr 
    | expr '%' expr 
    | expr '>' expr 
    | expr GREATER_EQUAL expr 
    | expr '<' expr 
    | expr LESS_EQUAL expr 
    | expr EQUAL_EQUAL expr 
    | expr NOT_EQUAL expr 
    | expr AND expr 
    | expr OR expr 
    | term 
    ;

term:
      '(' expr ')' 
    | '-' expr %prec UMINUS 
    | NOT expr 
    | PLUS_PLUS lvalue 
    | lvalue PLUS_PLUS 
    | MINUS_MINUS lvalue 
    | lvalue MINUS_MINUS 
    | primary 
    ;

assignexpr:
      lvalue '=' expr 
    ;

primary:
      lvalue 
    | call 
    | objectdef 
    | '(' funcdef ')' 
    | const 
    ;

lvalue:
      ID { 
          SymbolTableEntry* sym = lookup_all($1, current_scope);
          if (!sym) {
              SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
              sym = insert_symbol($1, t, yylineno, current_scope);
          }
          $$ = sym;
      }
    | LOCAL ID { 
          SymbolTableEntry* sym = lookup_scope($2, current_scope);
          if (!sym) {
              SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
              sym = insert_symbol($2, t, yylineno, current_scope);
          }
          $$ = sym;
      }
    | GLOBAL_SCOPE ID { 
          SymbolTableEntry* sym = lookup_scope($2, 0);
          if (!sym) {
              printf("Error at line %d: Global variable '%s' not found.\n", yylineno, $2);
          }
          $$ = sym;
      }
    | member { $$ = NULL; }
    ;

member:
      lvalue '.' ID 
    | lvalue '[' expr ']' 
    | call '.' ID 
    | call '[' expr ']' 
    ;

call:
      call '(' elist ')' 
    | lvalue callsuffix 
    | '(' funcdef ')' '(' elist ')' 
    ;

callsuffix:
      normcall 
    | methodcall 
    ;

normcall:
      '(' elist ')' 
    ;

methodcall:
      DOT_DOT ID '(' elist ')' 
    ;

elist:
      expr_list 
    | /* empty */ 
    ;

expr_list:
      expr_list ',' expr
    | expr
    ;

objectdef:
      '[' elist ']' 
    | '[' indexed ']' 
    ;

indexed:
      indexed ',' indexedelem 
    | indexedelem 
    ;

indexedelem:
      '{' expr ':' expr '}' 
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
          insert_symbol($1, USER_FUNC, yylineno, current_scope);
          next_block_is_func = true;
          current_scope++;
      }
    | /* empty */ {
          char* name = new_anon_func_name();
          insert_symbol(name, USER_FUNC, yylineno, current_scope);
          free(name);
          next_block_is_func = true;
          current_scope++;
      }
    ;

funcdef:
      FUNCTION funcname '(' idlist ')' block 
    ;

const:
      NUMBER 
    | STRING 
    | NIL 
    | TRUE_TOKEN 
    | FALSE_TOKEN 
    ;

idlist:
      id_seq 
    | /* empty */ 
    ;

id_seq:
      id_seq ',' ID { insert_symbol($3, FORMAL_ARG, yylineno, current_scope); }
    | ID { insert_symbol($1, FORMAL_ARG, yylineno, current_scope); }
    ;

ifstmt:
      IF '(' expr ')' stmt ELSE stmt 
    | IF '(' expr ')' stmt 
    ;

whilestmt:
      WHILE '(' expr ')' stmt 
    ;

forstmt:
      FOR '(' elist ';' expr ';' elist ')' stmt 
    ;

returnstmt:
      RETURN expr ';' 
    | RETURN ';' 
    ;

%%

void yyerror(const char* s) {
    fprintf(stderr, "Syntax Error at line %d: %s\n", yylineno, s);
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

    init_symtable();
    
    yyparse();
    
    print_symtable();
    
    if (yyin) {
        fclose(yyin);
    }
    
    return 0;
}
