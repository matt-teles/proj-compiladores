/* Tabela de simbolos e pilha de escopos. */

#ifndef SAL_SYMTAB_H
#define SAL_SYMTAB_H

#include <stddef.h>
#include <stdio.h>

#include "types.h"

#define SAL_MAX_IDENTIFIER 256
#define SAL_MAX_SCOPE_PATH 512
#define SAL_MAX_LABEL 32

typedef enum {
    SYMBOL_VARIABLE,
    SYMBOL_PARAMETER,
    SYMBOL_PROCEDURE,
    SYMBOL_FUNCTION
} SymbolCategory;

typedef struct Symbol Symbol;
typedef struct Scope Scope;

struct Symbol {
    char name[SAL_MAX_IDENTIFIER];
    SymbolCategory category;
    SalType type;
    int lexical_level;          /* nivel usado por CRVL e ARMZ */
    int address;                /* deslocamento dentro do nivel */
    size_t parameter_count;     /* usado por proc e fn */
    SalType *parameter_types;   /* tipos formais na ordem declarada */
    char label[SAL_MAX_LABEL];  /* rotulo MEPA de proc ou fn */
    Symbol *next;                /* proximo simbolo declarado no escopo */
};

/*
 * Os escopos fechados continuam na lista created_next para que o arquivo .ts
 * possa ser produzido ao final da compilacao.
 */
struct Scope {
    char path[SAL_MAX_SCOPE_PATH];
    int lexical_level;
    Symbol *symbols_head;
    Symbol *symbols_tail;
    Scope *parent;               /* escopo externo usado durante buscas */
    Scope *created_next;         /* proximo escopo na ordem de criacao */
};

typedef struct {
    Scope *current;              /* topo logico da pilha de escopos */
    Scope *scopes_head;          /* inicio da lista preservada para o .ts */
    Scope *scopes_tail;
} SymbolTable;

void symtab_init(SymbolTable *table);
void symtab_dispose(SymbolTable *table);

/* Blocos internos nao elevam nivel; sub-rotinas elevam. */
void symtab_push_scope(SymbolTable *table, const char *name,
                       int opens_activation);
void symtab_pop_scope(SymbolTable *table);

const char *symtab_current_path(const SymbolTable *table);
int symtab_current_lexical_level(const SymbolTable *table);

Symbol *symtab_lookup_current(const SymbolTable *table, const char *name);
Symbol *symtab_lookup(const SymbolTable *table, const char *name);
Symbol *symtab_declare(SymbolTable *table, const char *name,
                       SymbolCategory category, SalType type, int address);

void symbol_set_signature(Symbol *symbol, const SalType *types, size_t count);
void symbol_set_label(Symbol *symbol, const char *label);

void symtab_dump(const SymbolTable *table, FILE *out);
const char *symbol_category_name(SymbolCategory category);

#endif /* SAL_SYMTAB_H */
