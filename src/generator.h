/* Emissao textual das instrucoes MEPA. */

#ifndef SAL_GENERATOR_H
#define SAL_GENERATOR_H

#include <stddef.h>

#define MEPA_MAX_PATH 1024
#define MEPA_MAX_LABEL 32

/* Abre resultados/nome.mepa para escrita. */
int gen_init(const char *source_path);

/* Fecha o arquivo MEPA ao final da compilacao. */
int gen_close(void);

/* Caminho do arquivo .mepa, usado pela mensagem final do main. */
const char *gen_output_path(void);

/* Gera rotulos L1, L2, ... */
void gen_new_label(char *out, size_t capacity);

/*
 * Emissao por quantidade de argumentos. O rotulo pode ser NULL.
 * dois argumentos usam virgula.
 */
void gen_emit0(const char *label, const char *mnemonic);
void gen_emit1(const char *label, const char *mnemonic, const char *arg1);
void gen_emit2(const char *label, const char *mnemonic,
               const char *arg1, const char *arg2);
void gen_emit_marker(const char *marker);

/* Variantes convenientes para argumentos numericos. */
void gen_emit1i(const char *label, const char *mnemonic, int arg1);
void gen_emit2i(const char *label, const char *mnemonic, int arg1, int arg2);

#endif /* SAL_GENERATOR_H */
