/*
 * symtab.c - Tabela de simbolos com escopos encadeados
 *
 * Cada escopo aponta para o pai. Por isso, a busca parte do escopo atual e
 * caminha ate o global, implementando visibilidade e sombreamento.
 *
 * Os escopos fechados nao sao apagados imediatamente: eles permanecem na
 * lista de criacao para que o arquivo .ts possa ser produzido ao final.
 */

#include "symtab.h"

#include <stdlib.h>
#include <string.h>

/* calloc tambem zera ponteiros e contadores dos novos registros. */
static void *allocate(size_t size) {
    void *memory = calloc(1, size);
    if (!memory) {
        perror("Erro ao alocar memoria");
        exit(EXIT_FAILURE);
    }
    return memory;
}

/* Copia texto garantindo terminacao mesmo quando o limite e atingido. */
static void copy_text(char *out, size_t capacity, const char *text) {
    strncpy(out, text, capacity - 1);
    out[capacity - 1] = '\0';
}

void symtab_init(SymbolTable *table) {
    table->current = NULL;
    table->scopes_head = NULL;
    table->scopes_tail = NULL;
}

/* Libera primeiro os simbolos de cada escopo e depois o proprio escopo. */
void symtab_dispose(SymbolTable *table) {
    Scope *scope = table->scopes_head;
    while (scope) {
        Symbol *symbol = scope->symbols_head;
        while (symbol) {
            Symbol *next_symbol = symbol->next;
            free(symbol->parameter_types); /* NULL para variaveis e parametros. */
            free(symbol);
            symbol = next_symbol;
        }
        Scope *next_scope = scope->created_next;
        free(scope);
        scope = next_scope;
    }
}

void symtab_push_scope(SymbolTable *table, const char *name,
                       int opens_activation) {
    Scope *scope = allocate(sizeof(*scope));
    scope->parent = table->current;

    if (scope->parent) {
        /* Forma caminhos como global.fn:soma ou global.proc:main.block#1. */
        copy_text(scope->path, sizeof(scope->path), scope->parent->path);
        strncat(scope->path, ".", sizeof(scope->path) - strlen(scope->path) - 1);
        strncat(scope->path, name, sizeof(scope->path) - strlen(scope->path) - 1);

        /* Somente funcoes e procedimentos elevam o nivel lexico da MEPA. */
        scope->lexical_level = scope->parent->lexical_level + opens_activation;
    } else {
        /* O primeiro escopo criado e sempre o global. */
        copy_text(scope->path, sizeof(scope->path), name);
        scope->lexical_level = 0;
    }

    /* Preserva a ordem de criacao para o dump posterior da tabela. */
    if (table->scopes_tail) table->scopes_tail->created_next = scope;
    else                    table->scopes_head = scope;

    table->scopes_tail = scope;
    table->current = scope;
}

/* Volta ao pai sem apagar o escopo fechado, que ainda sera usado pelo .ts. */
void symtab_pop_scope(SymbolTable *table) {
    table->current = table->current->parent;
}

const char *symtab_current_path(const SymbolTable *table) {
    return table->current->path;
}

int symtab_current_lexical_level(const SymbolTable *table) {
    return table->current->lexical_level;
}

/* Busca local usada para impedir redeclaracoes no mesmo escopo. */
Symbol *symtab_lookup_current(const SymbolTable *table, const char *name) {
    for (Symbol *symbol = table->current->symbols_head;
         symbol;
         symbol = symbol->next) {
        if (strcmp(symbol->name, name) == 0) return symbol;
    }
    return NULL;
}

/* Busca visivel: parte do escopo atual e sobe ate o global. */
Symbol *symtab_lookup(const SymbolTable *table, const char *name) {
    for (Scope *scope = table->current; scope; scope = scope->parent) {
        for (Symbol *symbol = scope->symbols_head;
             symbol;
             symbol = symbol->next) {
            if (strcmp(symbol->name, name) == 0) return symbol;
        }
    }
    return NULL;
}

Symbol *symtab_declare(SymbolTable *table, const char *name,
                       SymbolCategory category, SalType type, int address) {
    Symbol *symbol = allocate(sizeof(*symbol));
    copy_text(symbol->name, sizeof(symbol->name), name);
    symbol->category = category;
    symbol->type = type;
    symbol->lexical_level = table->current->lexical_level;
    symbol->address = address;

    /* Insere ao fim para manter a ordem original das declaracoes no .ts. */
    if (table->current->symbols_tail) table->current->symbols_tail->next = symbol;
    else                              table->current->symbols_head = symbol;

    table->current->symbols_tail = symbol;
    return symbol;
}

/* Copia a assinatura para validar quantidade e tipo dos argumentos futuramente. */
void symbol_set_signature(Symbol *symbol, const SalType *types, size_t count) {
    if (count > 0) {
        symbol->parameter_types = allocate(count * sizeof(*types));
        memcpy(symbol->parameter_types, types, count * sizeof(*types));
    }
    symbol->parameter_count = count;
}

void symbol_set_label(Symbol *symbol, const char *label) {
    copy_text(symbol->label, sizeof(symbol->label), label);
}

const char *symbol_category_name(SymbolCategory category) {
    switch (category) {
        case SYMBOL_VARIABLE:  return "var";
        case SYMBOL_PARAMETER: return "param";
        case SYMBOL_PROCEDURE: return "proc";
        case SYMBOL_FUNCTION:  return "fn";
        default:               return "?";
    }
}

/* Escreve escopos e simbolos na ordem em que apareceram no programa. */
void symtab_dump(const SymbolTable *table, FILE *out) {
    for (Scope *scope = table->scopes_head; scope; scope = scope->created_next) {
        for (Symbol *symbol = scope->symbols_head; symbol; symbol = symbol->next) {
            /* A coluna extra informa quantidade de parametros apenas em rotinas. */
            size_t extra = (symbol->category == SYMBOL_PROCEDURE ||
                            symbol->category == SYMBOL_FUNCTION)
                         ? symbol->parameter_count : 0;
            fprintf(out,
                    "SCOPE=%-32s id=\"%s\" cat=%-5s tipo=%-4s "
                    "extra=%zu nivel=%d addr=%d\n",
                    scope->path,
                    symbol->name,
                    symbol_category_name(symbol->category),
                    sal_type_name(symbol->type),
                    extra,
                    symbol->lexical_level,
                    symbol->address);
        }
    }
}
