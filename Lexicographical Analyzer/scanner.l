
%{

/** Phase 1: Lexicographical Analyzer

    Authors: Mixalis Metzogiannakis - csd3998
             Spiros Papadomanolakis - csd5108
             Giannis Manassakis     - csd5277
  */


#define YY_DECL int alpha_yylex (void* ylval)
int count = 1; //number of current token
int max_string_size = 700;
FILE *file;  // Declare a file pointer to handle input files

int string_reader();

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



%option yylineno
%%

\"      { string_reader(); }
"/*"   { comment_reader(0); }
"//"    {single_line_comment_reader();}
\n     { /* yylineno is incremented automatically */ }


"if"   { create_token("KEYWORD", "if", yylineno, count, "IF", "<-enumerated"); }
"else"   { create_token("KEYWORD", "else", yylineno, count, "ELSE", "<-enumerated"); }
"while"   { create_token("KEYWORD", "while", yylineno, count, "WHILE", "<-enumerated"); }
"for"   { create_token("KEYWORD", "for", yylineno, count, "FOR", "<-enumerated"); }
"function"   { create_token("KEYWORD", "function", yylineno, count, "FUNCTION", "<-enumerated"); }
"return"   { create_token("KEYWORD", "return", yylineno, count, "RETURN", "<-enumerated"); }
"break"   { create_token("KEYWORD", "break", yylineno, count, "BREAK", "<-enumerated"); }
"continue"   { create_token("KEYWORD", "continue", yylineno, count, "CONTINUE", "<-enumerated"); }
"and"   { create_token("KEYWORD", "and", yylineno, count, "AND", "<-enumerated"); }
"not"   { create_token("KEYWORD", "not", yylineno, count, "NOT", "<-enumerated"); }
"or"   { create_token("KEYWORD", "or", yylineno, count, "OR", "<-enumerated"); }
"local"   { create_token("KEYWORD", "local", yylineno, count, "LOCAL", "<-enumerated"); }
"true"   { create_token("KEYWORD", "true", yylineno, count, "TRUE", "<-enumerated"); }
"false"   { create_token("KEYWORD", "false", yylineno, count, "FALSE", "<-enumerated"); }
"nil"   { create_token("KEYWORD", "nil", yylineno, count, "NIL", "<-enumerated"); }


"="   { create_token("OPERATOR", "=", yylineno, count, "OPERATOR ASSIGN", "<-enumerated"); }
"+"   { create_token("OPERATOR", "+", yylineno, count, "OPERATOR PLUS", "<-enumerated"); }
"-"   { create_token("OPERATOR", "-", yylineno, count, "OPERATOR MINUS", "<-enumerated"); }
"*"   { create_token("OPERATOR", "*", yylineno, count, "OPERATOR MULTIPLYCATION", "<-enumerated"); }
"/"   { create_token("OPERATOR", "/", yylineno, count, "OPERATOR DIV", "<-enumerated"); }
"%"   { create_token("OPERATOR", "%", yylineno, count, "OPERATOR MOD", "<-enumerated"); }
"=="   { create_token("OPERATOR", "==", yylineno, count, "OPERATOR EQUAL", "<-enumerated"); }
"!="   { create_token("OPERATOR", "!=", yylineno, count, "OPERATOR NOT EQUAL", "<-enumerated"); }
"++"   { create_token("OPERATOR", "++", yylineno, count, "OPERATOR PLUS_PLUS", "<-enumerated"); }
"--"   { create_token("OPERATOR", "--", yylineno, count, "OPERATOR MINUS_ONE", "<-enumerated"); }
">"   { create_token("OPERATOR", ">", yylineno, count, "OPERATOR BIGGER", "<-enumerated"); }
"<"   { create_token("OPERATOR", "<", yylineno, count, "OPERATOR SMALLER", "<-enumerated"); }
">="   { create_token("OPERATOR", ">=", yylineno, count, "OPERATOR GREATER_EQUAL", "<-enumerated"); }
"<="   { create_token("OPERATOR", "<=", yylineno, count, "OPERATOR SMALLER_EQUAL", "<-enumerated"); }


"{"   { create_token("PUNCTUATION", "{", yylineno, count, "OPEN_CURLY_BRACKET", "<-enumerated"); }
"}"   { create_token("PUNCTUATION", "}", yylineno, count, "CLOSE_CURLY_BRACKET", "<-enumerated"); }
"["   { create_token("PUNCTUATION", "[", yylineno, count, "OPEN_BRACKET", "<-enumerated"); }
"]"   { create_token("PUNCTUATION", "]", yylineno, count, "CLOSE_BRACKET", "<-enumerated"); }
"("   { create_token("PUNCTUATION", "(", yylineno, count, "OPEN_PARENTHESIS", "<-enumerated"); }
")"   { create_token("PUNCTUATION", ")", yylineno, count, "CLOSE_PARENTHESIS", "<-enumerated"); }
";"   { create_token("PUNCTUATION", ";", yylineno, count, "SEMICOLUMN", "<-enumerated"); }
","   { create_token("PUNCTUATION", ",", yylineno, count, "COMA", "<-enumerated"); }
":"   { create_token("PUNCTUATION", ":", yylineno, count, "COLUMN", "<-enumerated"); }
"::"   { create_token("PUNCTUATION", "::", yylineno, count, "DOUBLE_COLUMN", "<-enumerated"); }
"."   { create_token("PUNCTUATION", ".", yylineno, count, "PERIOD", "<-enumerated"); }
".."   { create_token("PUNCTUATION", "..", yylineno, count, "DOUBLE_PERIOD", "<-enumerated"); }

[0-9]+              { create_token("CONST_INT", yytext ,yylineno, count, yytext, "<-integer");}   /* Match an integer */
[0-9]+\.[0-9]+      { create_token("CONST_REAL", yytext ,yylineno, count, yytext, "<-float"); }  /* Match a real number */
[a-zA-Z][a-zA-Z0-9_]*   { create_token("IDENT", yytext ,yylineno, count, yytext, "<-char *");  }
" "   { /*Do nothing*/ }
.      {printf("INVALID CHARACTERS, STOPPING EXECUTION\n"); return 0; /* do nothing */ }
%%


int yywrap(){}
void print_tokens() {
    struct token * temp;
    temp = head;
    int i =1;
    while(temp != NULL){
        //printf("Token %d: Type:%s, Content:%s\n",i,temp->token_type,temp->content);
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

int string_reader() {
    char *buffer;
    buffer = (char *)malloc(max_string_size* sizeof(char));  // Allocate memory for buffer

    char ch;
    int i = 0;
    int reading_escaped_character = 0;


    while ((ch = input())) {

        if(!reading_escaped_character) {
            if(ch == '\\') {
                reading_escaped_character = 1;
                continue;
            }

            if (ch == '\"') {  // End of string found
                buffer[i] = '\0';  // Null-terminate the string

                create_token("String" , buffer, yylineno , count , buffer , "<-char*");
                free(buffer);  // Free memory of buffer
                return 1; // Return 1 as successful string reading
            }

            buffer[i++] = ch;
        } else {
            switch (ch) {
                case 'n':
                    buffer[i++] = '\n';  
                break;

                case '\\':
                    buffer[i++] = '\\'; 
                break;

                case 't':
                    buffer[i++] = '\t'; 
                break;

                case '\"':
                    buffer[i++] = '\"'; 
                break;

                case '\'':
                    buffer[i++] = '\''; 
                break;

                default:
                    perror("Error! This string contains invalid escape characters\n");
                    free(buffer);
                    return -1;
                break;

        }

        /* End of reading escaped character */
        reading_escaped_character = 0;

        }

    }

    // In case we reach EOF without finding another quote
    fprintf(stderr, "Error! Token #%d is unclosed string.\n", count);
    free(buffer);
    exit(0);
}

int comment_reader(int depth) {
    char *buffer;
    char ch;
    buffer = (char *)malloc(max_string_size* sizeof(char)); // Allocate space for buffer
    buffer[0] = '\0'; // Null-terminate the buffer initially
    char last_char = input();
    int i = 0;
    int starting_point = yylineno;

    // Read the file character by character
    while ((ch = input())) {
        if (last_char == '/' && ch == '*') {
            // Found the start of a nested comment
            comment_reader(depth + 1); // Recursively call for nested comment
            last_char = input(); // Continue after nested comment
        } else if (last_char == '*' && ch == '/') {
            // End of the comment found
            
            if (depth == 0){
                create_token("MULTI_COMMENT", buffer ,starting_point, count, buffer, "<-enumerated");

            }else{
                create_token("NESTED_COMMENT", buffer ,starting_point, count, buffer, "<-enumerated");
            }
            free(buffer); // Free allocated memory
            return 0; // End the comment reader
        } else {
            // Append the previous character to the buffer
            buffer[i] = last_char;
            i++;
            buffer[i] = '\0'; // Null-terminate buffer
            last_char = ch; // Move to the next character
        }
    }

    printf("Error: Comment not properly closed.\n");
    free(buffer); // Free memory before exiting
    return -1; // Return error if EOF reached without closing comment
}
int single_line_comment_reader(){
    char *buffer;
    buffer = (char *)malloc(max_string_size* sizeof(char));  
    buffer[0]='\0';
    char ch;
    int i = 0;
    
    while ((ch = input())) {
        if(ch=='\n'){
            buffer[i]='\0';
            create_token("SINGLE_LINE_COMMENT" , buffer,yylineno , count , buffer , "<-char*");
            free(buffer);
            return 1;
            
        }else{
            buffer[i]=ch;
            i++;
        }
        
         
    }
    
    create_token("SINGLE_LINE_COMMENT" , buffer,yylineno , count , buffer , "<-char*");
    free(buffer);
    return 1;
}

int main(int argc, char *argv[]) {

    int k = 0;
    
    if (argc > 1) {
        file = fopen(argv[1], "r");
        if (!file) {
            printf("Error: Could not open file %s\n", argv[1]);
            return 1;
        }

        yyin = file;  // Set Flex input to the opened file
    }
    
    void* ylval = NULL;  
    while (alpha_yylex(ylval) > 0);
    
    print_tokens();
    
    // Close the file
    if (file != NULL) {
        fclose(file);
    }   
    

    return 0;
}
