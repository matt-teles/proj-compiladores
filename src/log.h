/* Logs opcionais produzidos pelo compilador SAL. */

#ifndef SAL_LOG_H
#define SAL_LOG_H

#include <stdio.h>

#include "lex.h"
#include "symtab.h"

/* Abre somente os arquivos solicitados pelas flags da linha de comando. */
int log_init(const char *source_path, int tokens, int symtab, int trace);

/* Escritas silenciosas quando o respectivo arquivo nao foi solicitado. */
void log_token(const Token *token);
void log_symtab(const SymbolTable *table);

/* O modulo diag reutiliza o FILE* do trace. */
FILE *log_trace_file(void);

void log_close(void);

#endif /* SAL_LOG_H */
