#ifndef TOKEN_H
#define TOKEN_H

struct token {
    struct token * next;
    char * token_type;
    char *content;
    int line;
    int token_num;
    char* general_form;
    char* subcategory;
};

#endif
