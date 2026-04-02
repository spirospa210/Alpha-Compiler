%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"
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

/* Stack to keep track of function boundaries to prevent illegal outer access */
int func_scope_stack[100];
char* current_func_name_stack[100]; /* Stack to track current function name */
int func_scope_stack_top = 0;

/* Add this to track if we are inside a loop */
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
    | BREAK ';' {
          if (loop_counter == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'break' while not in a loop.\n", yylineno);
          }
      }
    | CONTINUE ';' {
          if (loop_counter == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'continue' while not in a loop.\n", yylineno);
          }
      }
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
    | PLUS_PLUS lvalue {
          if ($2 != NULL && ($2->type == USER_FUNC || $2->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function '%s'.\n", yylineno, $2->name);
          }
      }
    | lvalue PLUS_PLUS {
          if ($1 != NULL && ($1->type == USER_FUNC || $1->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '++' to function '%s'.\n", yylineno, $1->name);
          }
      }
    | MINUS_MINUS lvalue {
          if ($2 != NULL && ($2->type == USER_FUNC || $2->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '--' to function '%s'.\n", yylineno, $2->name);
          }
      }
    | lvalue MINUS_MINUS {
          if ($1 != NULL && ($1->type == USER_FUNC || $1->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot apply '--' to function '%s'.\n", yylineno, $1->name);
          }
      }
    | primary 
    ;

assignexpr:
      lvalue '=' expr {
          if ($1 != NULL && ($1->type == USER_FUNC || $1->type == LIB_FUNC)) {
              printf("Error at line %d: Cannot assign to function '%s'.\n", yylineno, $1->name);
          }
      }
    | call '=' expr {
          /* Added check for assigning to a function call */
          printf("Error at line %d: Function call is not an lvalue.\n", yylineno);
      }
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
          if (sym) {
              /* Check Accessibility Rule: Cannot access outer local variables */
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
          $$ = sym;
      }
    | LOCAL ID { 
          SymbolTableEntry* sym = lookup_scope($2, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($2, 0);
          
          if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: local '%s' collides with a library function.\n", yylineno, $2);
              $$ = lib_collision;
          } else {
              if (!sym) {
                  SymbolType t = (current_scope == 0) ? GLOBAL_VAR : LOCAL_VAR;
                  sym = insert_symbol($2, t, yylineno, current_scope);
              }
              $$ = sym;
          }
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
          SymbolTableEntry* collision = lookup_scope($1, current_scope);
          SymbolTableEntry* lib_collision = lookup_scope($1, 0);
          
          if (collision) {
              if (collision->type == LIB_FUNC) {
                  printf("Error at line %d: Function name '%s' collides with a library function.\n", yylineno, $1);
              } else {
                  printf("Error at line %d: Collision with %s named '%s' defined at line %d\n", 
                         yylineno, get_symbol_type_string(collision->type), $1, collision->line);
              }
          } else if (lib_collision && lib_collision->type == LIB_FUNC) {
              printf("Error at line %d: Function name '%s' collides with a library function.\n", yylineno, $1);
          } else {
              insert_symbol($1, USER_FUNC, yylineno, current_scope);
          }
          
          next_block_is_func = true;
          current_scope++;
          
          /* Push the function name and scope to the respective stacks */
          current_func_name_stack[func_scope_stack_top] = strdup($1);
          func_scope_stack[func_scope_stack_top++] = current_scope;
      }
    | /* empty */ {
          char* name = new_anon_func_name();
          insert_symbol(name, USER_FUNC, yylineno, current_scope);
          
          next_block_is_func = true;
          current_scope++;
          
          /* Push the anonymous function name and scope to the respective stacks */
          current_func_name_stack[func_scope_stack_top] = strdup(name);
          func_scope_stack[func_scope_stack_top++] = current_scope;
          
          free(name);
      }
    ;

funcdef:
      FUNCTION funcname '(' idlist ')' block {
          /* Pop the function's boundary scope and name off the stacks when exiting */
          func_scope_stack_top--;
          free(current_func_name_stack[func_scope_stack_top]);
      }
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
      id_seq ',' ID { 
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

ifstmt:
      IF '(' expr ')' stmt ELSE stmt 
    | IF '(' expr ')' stmt 
    ;

whilestmt:
      WHILE '(' expr ')' { loop_counter++; } stmt { loop_counter--; }
    ;

forstmt:
      FOR '(' elist ';' expr ';' elist ')' { loop_counter++; } stmt { loop_counter--; }
    ;

returnstmt:
      RETURN expr ';' {
          if (func_scope_stack_top == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\n", yylineno);
          }
      }
    | RETURN ';' {
          if (func_scope_stack_top == 0 && current_scope == 0) {
              printf("Error at line %d: Use of 'return' while not in a function.\n", yylineno);
          }
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
    alpha_yylex(NULL);

    /* Set the parser pointer to the start of your generated list */
    current_token = head;

    init_symtable();

    /* PHASE 2: Parse using the custom yylex() which skips comments */
    yyparse();
    
    print_symtable();

    if (yyin) {
        fclose(yyin);
    }
    
    return 0;
}
