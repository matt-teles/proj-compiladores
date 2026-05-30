/* Diagnosticos e rastreamento do compilador SAL. */

#ifndef SAL_DIAG_H
#define SAL_DIAG_H

#include <stdio.h>

typedef enum {
    DIAG_LEXICAL,
    DIAG_SYNTACTIC,
    DIAG_SEMANTIC
} DiagnosticKind;

/* O trace e opcional. Quando nao solicitado, trace_file recebe NULL. */
void diag_init(FILE *trace_file);

/* Registra uma linha no arquivo .trc, quando ele estiver habilitado. */
void diag_trace(const char *fmt, ...);

/* Exibe o erro e encerra a compilacao imediatamente. */
void diag_error(DiagnosticKind kind, int line, const char *fmt, ...);

#endif /* SAL_DIAG_H */
