%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int line_count = 1;
%}

%%

\n              { line_count++; }
\t              ;
"([^"]*)"     { printf("STRING: %s\n", yytext); }
[0-9]+         { printf("NUMBER: %s\n", yytext); }
[a-zA-Z_][a-zA-Z0-9_]* { printf("IDENTIFIER: %s\n", yytext); }
\+            { printf("OPERATOR: +\n"); }
-              { printf("OPERATOR: -\n"); }
\*            { printf("OPERATOR: *\n"); }
/              { printf("OPERATOR: /\n"); }
=              { printf("OPERATOR: =\n"); }
==             { printf("OPERATOR: ==\n"); }
!=             { printf("OPERATOR: !=\n"); }
<=             { printf("OPERATOR: <=\n"); }
>=             { printf("OPERATOR: >=\n"); }
;;
\ COMMENT    //.*                ;
%%

int yywrap() {
    return 1;
}

int main() {
    yylex();
    return 0;
}