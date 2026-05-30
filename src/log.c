/*
 * log.c - Gravacao opcional de .tk, .ts e .trc
 */

#include "log.h"

#include <string.h>

#include "config.h"

static FILE *g_tokens = NULL;
static FILE *g_symbols = NULL;
static FILE *g_trace = NULL;

/* Retorna apenas o nome do arquivo, sem o caminho de origem. */
static const char *file_name_only(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Monta resultados/nome.ext a partir de qualquer caminho de entrada. */
static void make_path(char *out, size_t capacity,
                      const char *source_path, const char *extension)
{
    char base[512];
    strncpy(base, file_name_only(source_path), sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';

    /* Remove somente a ultima extensao do nome do fonte. */
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';

    snprintf(out, capacity, "%s/%s.%s", SAL_RESULTS_DIR, base, extension);
}

static FILE *open_log(const char *source_path, const char *extension)
{
    char path[1024];
    make_path(path, sizeof(path), source_path, extension);
    return fopen(path, "w");
}

int log_init(const char *source_path, int tokens, int symtab, int trace)
{
    /* Cada bloco so abre o arquivo correspondente a uma flag ativa. */
    if (tokens)
    {
        g_tokens = open_log(source_path, "tk");
        if (!g_tokens)
            goto error;
    }
    if (symtab)
    {
        g_symbols = open_log(source_path, "ts");
        if (!g_symbols)
            goto error;
    }
    if (trace)
    {
        g_trace = open_log(source_path, "trc");
        if (!g_trace)
            goto error;
    }
    return 0;

error:
    /* Fecha arquivos eventualmente abertos antes da primeira falha. */
    log_close();
    return -1;
}

void log_token(const Token *token)
{
    if (!g_tokens)
        return;
    fprintf(g_tokens, "%5d  %-15s  \"%s\"\n",
            token->line, token_kind_name(token->kind), token->lexeme);
}

void log_symtab(const SymbolTable *table)
{
    if (g_symbols)
        symtab_dump(table, g_symbols);
}

FILE *log_trace_file(void)
{
    return g_trace;
}

void log_close(void)
{
    if (g_tokens)
        fclose(g_tokens);
    if (g_symbols)
        fclose(g_symbols);
    if (g_trace)
        fclose(g_trace);

    /* Zera os ponteiros para manter o estado consistente apos o fechamento. */
    g_tokens = NULL;
    g_symbols = NULL;
    g_trace = NULL;
}
