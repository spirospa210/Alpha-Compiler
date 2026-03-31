// symbol_table.h

#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Constants
#define MAX_SYMBOLS 100

// Symbol structure
typedef struct {
    char name[50];
    int type;
    int scope;
} Symbol;

// Symbol Table structure
typedef struct {
    Symbol symbols[MAX_SYMBOLS];
    int count;
} SymbolTable;

// API Functions
void initSymbolTable(SymbolTable *table);
int addSymbol(SymbolTable *table, const char *name, int type, int scope);
int findSymbol(SymbolTable *table, const char *name);
void printSymbolTable(SymbolTable *table);

#endif // SYMBOL_TABLE_H
