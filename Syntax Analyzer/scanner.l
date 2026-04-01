%{

/** Phase 1 & 2: Lexical Analyzer
    Authors: Mixalis Metzogiannakis - csd3998
             Spiros Papadomanolakis - csd5108
             Giannis Manassakis     - csd5277
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symtable.h"
#include "parser.tab.h"

int count = 1; //number of current token

char* string_reader();
int comment_reader(int depth);
int single_line_comment_reader();

struct token{
    struct token * next;
    char * token_type;
    char *content;
    int line;
    int token_num;
    char* general_form;
    char* subcategory;
};

int create_token(char * token_type, char * content, int line, int token_num, char * general_form, char * subcategory);
void print_tokens();

struct token * head = NULL;
struct token * tail = NULL;
%}

%option noyywrap
%option yylineno

%%

\"      { 
            char* str = string_reader(); 
            yylval.strVal = str; 
            return STRING; 
        }
"/*"    { comment_reader(0); }
"//"    { single_line_comment_reader(); }
\n      { /* yylineno is incremented automatically */ }

"if"       { create_token("KEYWORD", "if", yylineno, count, "IF", "<-enumerated"); return IF; }
"else"     { create_token("KEYWORD", "else", yylineno, count, "ELSE", "<-enumerated"); return ELSE; }
"while"    { create_token("KEYWORD", "while", yylineno, count, "WHILE", "<-enumerated"); return WHILE; }
"for"      { create_token("KEYWORD", "for", yylineno, count, "FOR", "<-enumerated"); return FOR; }
"function" { create_token("KEYWORD", "function", yylineno, count, "FUNCTION", "<-enumerated"); return FUNCTION; }
"return"   { create_token("KEYWORD", "return", yylineno, count, "RETURN", "<-enumerated"); return RETURN; }
"break"    { create_token("KEYWORD", "break", yylineno, count, "BREAK", "<-enumerated"); return BREAK; }
"continue" { create_token("KEYWORD", "continue", yylineno, count, "CONTINUE", "<-enumerated"); return CONTINUE; }
"and"      { create_token("KEYWORD", "and", yylineno, count, "AND", "<-enumerated"); return AND; }
"not"      { create_token("KEYWORD", "not", yylineno, count, "NOT", "<-enumerated"); return NOT; }
"or"       { create_token("KEYWORD", "or", yylineno, count, "OR", "<-enumerated"); return OR; }
"local"    { create_token("KEYWORD", "local", yylineno, count, "LOCAL", "<-enumerated"); return LOCAL; }
"true"     { create_token("KEYWORD", "true", yylineno, count, "TRUE", "<-enumerated"); return TRUE_TOKEN; }
"false"    { create_token("KEYWORD", "false", yylineno, count, "FALSE", "<-enumerated"); return FALSE_TOKEN; }
"nil"      { create_token("KEYWORD", "nil", yylineno, count, "NIL", "<-enumerated"); return NIL; }

"="     { create_token("OPERATOR", "=", yylineno, count, "OPERATOR ASSIGN", "<-enumerated"); return '='; }
"+"     { create_token("OPERATOR", "+", yylineno, count, "OPERATOR PLUS", "<-enumerated"); return '+'; }
"-"     { create_token("OPERATOR", "-", yylineno, count, "OPERATOR MINUS", "<-enumerated"); return '-'; }
"*"     { create_token("OPERATOR", "*", yylineno, count, "OPERATOR MULTIPLYCATION", "<-enumerated"); return '*'; }
"/"     { create_token("OPERATOR", "/", yylineno, count, "OPERATOR DIV", "<-enumerated"); return '/'; }
"%"     { create_token("OPERATOR", "%", yylineno, count, "OPERATOR MOD", "<-enumerated"); return '%'; }
"=="    { create_token("OPERATOR", "==", yylineno, count, "OPERATOR EQUAL", "<-enumerated"); return EQUAL_EQUAL; }
"!="    { create_token("OPERATOR", "!=", yylineno, count, "OPERATOR NOT EQUAL", "<-enumerated"); return NOT_EQUAL; }
"++"    { create_token("OPERATOR", "++", yylineno, count, "OPERATOR PLUS_PLUS", "<-enumerated"); return PLUS_PLUS; }
"--"    { create_token("OPERATOR", "--", yylineno, count, "OPERATOR MINUS_ONE", "<-enumerated"); return MINUS_MINUS; }
">"     { create_token("OPERATOR", ">", yylineno, count, "OPERATOR BIGGER", "<-enumerated"); return '>'; }
"<"     { create_token("OPERATOR", "<", yylineno, count, "OPERATOR SMALLER", "<-enumerated"); return '<'; }
">="    { create_token("OPERATOR", ">=", yylineno, count, "OPERATOR GREATER_EQUAL", "<-enumerated"); return GREATER_EQUAL; }
"<="    { create_token("OPERATOR", "<=", yylineno, count, "OPERATOR SMALLER_EQUAL", "<-enumerated"); return LESS_EQUAL; }

"{"     { create_token("PUNCTUATION", "{", yylineno, count, "OPEN_CURLY_BRACKET", "<-enumerated"); return '{'; }
"}"     { create_token("PUNCTUATION", "}", yylineno, count, "CLOSE_CURLY_BRACKET", "<-enumerated"); return '}'; }
"["     { create_token("PUNCTUATION", "[", yylineno, count, "OPEN_BRACKET", "<-enumerated"); return '['; }
"]"     { create_token("PUNCTUATION", "]", yylineno, count, "CLOSE_BRACKET", "<-enumerated"); return ']'; }
"("     { create_token("PUNCTUATION", "(", yylineno, count, "OPEN_PARENTHESIS", "<-enumerated"); return '('; }
")"     { create_token("PUNCTUATION", ")", yylineno, count, "CLOSE_PARENTHESIS", "<-enumerated"); return ')'; }
";"     { create_token("PUNCTUATION", ";", yylineno, count, "SEMICOLUMN", "<-enumerated"); return ';'; }
","     { create_token("PUNCTUATION", ",", yylineno, count, "COMA", "<-enumerated"); return ','; }
":"     { create_token("PUNCTUATION", ":", yylineno, count, "COLUMN", "<-enumerated"); return ':'; }
"::"    { create_token("PUNCTUATION", "::", yylineno, count, "DOUBLE_COLUMN", "<-enumerated"); return GLOBAL_SCOPE; }
"."     { create_token("PUNCTUATION", ".", yylineno, count, "PERIOD", "<-enumerated"); return '.'; }
".."    { create_token("PUNCTUATION", "..", yylineno, count, "DOUBLE_PERIOD", "<-enumerated"); return DOT_DOT; }

[0-9]+              { 
                        create_token("CONST_INT", yytext, yylineno, count, yytext, "<-integer");
                        yylval.strVal = strdup(yytext);
                        return NUMBER;
                    }   
[0-9]+\.[0-9]+      { 
                        create_token("CONST_REAL", yytext, yylineno, count, yytext, "<-float"); 
                        yylval.strVal = strdup(yytext);
                        return NUMBER;
                    } 
[a-zA-Z][a-zA-Z0-9_]* { 
                        create_token("IDENT", yytext, yylineno, count, yytext, "<-char *");  
                        yylval.strVal = strdup(yytext);
                        return ID;
                    }

[ \t\r]+  { /*Do nothing*/ }
.      { printf("INVALID CHARACTER: %s, STOPPING EXECUTION\n", yytext); return 0; }
%%

void print_tokens() {
    struct token * temp;
    temp = head;
    int i =1;
    while(temp != NULL){
        printf("%d: #%d \"%s\" %s %s %s\n",temp->line, temp->token_num, temp->content, temp->token_type, temp->general_form, temp->subcategory);
        i++;
        temp = temp->next;
    }
    return;
}

int create_token(char * token_type, char * content, int line, int token_num, char * general_form, char * subcategory){
    struct token * temp;
    temp = (struct token * )malloc(sizeof(struct token));
    temp->token_type=(char * )malloc(sizeof(char)*((strlen(token_type)+1)));
    strcpy(temp->token_type, token_type);
    temp->content=(char * )malloc(sizeof(char)*((strlen(content)+1)));
    strcpy(temp->content, content);
    temp->line=line;
    temp->token_num = token_num;
    temp->general_form=(char * )malloc(sizeof(char)*((strlen(general_form)+1)));
    strcpy(temp->general_form, general_form);
    temp->subcategory=(char * )malloc(sizeof(char)*((strlen(subcategory)+1)));
    strcpy(temp->subcategory, subcategory);
    temp->next = NULL;
    count++;
    if(head == NULL){
        head = temp;
    }else{
        struct token * temp2;
        temp2 = head;
        while(temp2->next){
            temp2 = temp2->next;
        }
        temp2->next = temp;
    }
    return 1;
}

char* string_reader() {
    int buffer_size = 500;
    char *buffer = (char *)malloc(buffer_size * sizeof(char));

    char ch;
    int i = 0;
    int reading_escaped_character = 0;

    while ((ch = input())) {
        if(!reading_escaped_character) {
            if (buffer_size <= i + 1){
                buffer_size += 500;
                char *newbuffer = realloc(buffer, buffer_size);
                if (newbuffer == NULL){
                    free(buffer);
                    return NULL;
                }
                buffer = newbuffer;
            }

            if(ch == '\\') {
                reading_escaped_character = 1;
                continue;
            }

            if (ch == '\"') {
                buffer[i] = '\0';
                create_token("String" , buffer, yylineno , count , buffer, "<-char*");
                return buffer; // Modified to return the string directly for yylval
            }

            buffer[i++] = ch;
        } else {
            switch (ch) {
                case 'n': buffer[i++] = '\n'; break;
                case '\\': buffer[i++] = '\\'; break;
                case 't': buffer[i++] = '\t'; break;
                case '\"': buffer[i++] = '\"'; break;
                case '\'': buffer[i++] = '\''; break;
                default:
                    fprintf(stderr, "Error! Token #%d is a string which contains invalid escape characters\n", count);
                    free(buffer);
                    exit(1);
            }
            reading_escaped_character = 0;
        }
    }

    fprintf(stderr, "Error! Token #%d is unclosed string.\n", count);
    free(buffer);
    exit(1);
}

int comment_reader(int depth) {
    char *buffer;
    char ch;
    int buffer_size = 500;
    buffer = (char *)malloc(buffer_size* sizeof(char));
    buffer[0] = '\0';
    char last_char = input();
    int i = 0;
    int starting_point = yylineno;

    while ((ch = input())) {
        if (last_char == '/' && ch == '*') {
            comment_reader(depth + 1);
            last_char = input();
        } else if (last_char == '*' && ch == '/') {
            if (depth == 0){
                create_token("MULTI_COMMENT", buffer ,starting_point, count, buffer, "<-enumerated");
            } else {
                create_token("NESTED_COMMENT", buffer ,starting_point, count, buffer, "<-enumerated");
            }
            free(buffer);
            return 0;
        } else {
            if (buffer_size <= i + 1){
                buffer_size += 500;
                char *newbuffer = realloc(buffer, buffer_size);
                if (newbuffer == NULL){
                    free(buffer);
                    return 0;
                }
                buffer = newbuffer;
            }
            buffer[i] = last_char;
            i++;
            buffer[i] = '\0';
            last_char = ch;
        }
    }

    printf("Error: Comment not properly closed.\n");
    free(buffer);
    return -1;
}

int single_line_comment_reader(){
    int buffer_size = 500;
    char *buffer;
    buffer = (char *)malloc(buffer_size* sizeof(char));  
    buffer[0]='\0';
    char ch;
    int i = 0;
    
    while ((ch = input())) {
        if(ch == '\n' || ch == EOF){
            break;
        }
        if (buffer_size <= i + 1){
                buffer_size += 500;
                char *newbuffer = realloc(buffer, buffer_size);
                if (newbuffer == NULL){
                    free(buffer);
                    return 0;
                }
                buffer = newbuffer;
            }
        buffer[i++] = ch;
    }
    
    buffer[i] = '\0';
    create_token("SINGLE_LINE_COMMENT" , buffer, yylineno , count , buffer , "<-char*");
    free(buffer);
    return 1;
}
