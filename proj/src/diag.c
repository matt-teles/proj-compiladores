/*
 * diag.c - Diagnosticos centralizados
 */

#include "diag.h"

#include <stdarg.h>
#include <stdlib.h>

/* O trace e opcional; permanece NULL quando --trace nao foi informado. */
static FILE *g_trace = NULL;

void diag_init(FILE *trace_file)
{
    g_trace = trace_file;
}

/* Converte a categoria interna no prefixo exibido ao usuario. */
static const char *kind_name(DiagnosticKind kind)
{
    switch (kind)
    {
    case DIAG_LEXICAL:
        return "ERRO LEXICO";
    case DIAG_SYNTACTIC:
        return "ERRO SINTATICO";
    case DIAG_SEMANTIC:
        return "ERRO SEMANTICO";
    default:
        return "ERRO";
    }
}

/* Escrita silenciosa quando o rastreamento nao foi solicitado. */
void diag_trace(const char *fmt, ...)
{
    if (!g_trace)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(g_trace, fmt, args);
    va_end(args);
    fputc('\n', g_trace);
}

/* Exibe uma unica mensagem e termina a compilacao imediatamente. */
void diag_error(DiagnosticKind kind, int line, const char *fmt, ...)
{
    fprintf(stderr, "[%s]", kind_name(kind));
    if (line > 0)
        fprintf(stderr, " linha %d", line);
    fputs(": ", stderr);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);

    exit(EXIT_FAILURE);
}
