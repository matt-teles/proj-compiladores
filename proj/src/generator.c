/*
 * generator.c - Escrita textual do codigo MEPA
 *
 * O parser chama este modulo conforme reconhece cada construcao da SAL.
 * O gerador nao interpreta as instrucoes: apenas grava uma linha por vez no
 * arquivo resultados/nome.mepa e cria rotulos sequenciais L1, L2, ...
 */

#include "generator.h"

#include <stdio.h>
#include <string.h>

#include "config.h"

static FILE *g_output = NULL;
static char g_output_path[MEPA_MAX_PATH];
static unsigned g_next_label = 1;

/* Descarta o diretorio original para concentrar as saidas em resultados/. */
static const char *file_name_only(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Monta resultados/nome.mepa removendo caminho e extensao do fonte. */
static void make_output_path(const char *source_path)
{
    char base[512];
    strncpy(base, file_name_only(source_path), sizeof(base) - 1);
    base[sizeof(base) - 1] = '\0';

    /* A ultima extensao e removida: exemplo.sal -> exemplo. */
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';

    snprintf(g_output_path, sizeof(g_output_path),
             "%s/%s.mepa", SAL_RESULTS_DIR, base);
}

/* Abre a saida definitiva e reinicia a numeracao de rotulos. */
int gen_init(const char *source_path)
{
    make_output_path(source_path);
    g_output = fopen(g_output_path, "w");
    g_next_label = 1;
    return g_output ? 0 : -1;
}

int gen_close(void)
{
    if (fclose(g_output) != 0)
        return -1;
    g_output = NULL;
    return 0;
}

const char *gen_output_path(void)
{
    return g_output_path;
}

void gen_new_label(char *out, size_t capacity)
{
    snprintf(out, capacity, "L%u", g_next_label++);
}

/*
 * Rotulos aparecem antes da instrucao. Linhas sem rotulo comecam diretamente
 */
static void emit_label_prefix(const char *label)
{
    if (label)
        fprintf(g_output, "%s: ", label);
}

/* Instrucao sem argumentos: INPP, SOMA, PARA... */
void gen_emit0(const char *label, const char *mnemonic)
{
    emit_label_prefix(label);
    fprintf(g_output, "%s\n", mnemonic);
}

/* Instrucao com um argumento: CRCT 7, DSVS L1... */
void gen_emit1(const char *label, const char *mnemonic, const char *arg1)
{
    emit_label_prefix(label);
    fprintf(g_output, "%s %s\n", mnemonic, arg1);
}

/* Dois argumentos sao separados por virgula sem espaco: ARMZ 0,1. */
void gen_emit2(const char *label, const char *mnemonic,
               const char *arg1, const char *arg2)
{
    emit_label_prefix(label);
    fprintf(g_output, "%s %s,%s\n", mnemonic, arg1, arg2);
}

/* Marcadores como FIM nao sao instrucoes executaveis, mas delimitam o arquivo. */
void gen_emit_marker(const char *marker)
{
    fprintf(g_output, "%s\n", marker);
}

/* Variantes numericas evitam conversoes repetidas dentro do parser. */
void gen_emit1i(const char *label, const char *mnemonic, int arg1)
{
    char text[32];
    snprintf(text, sizeof(text), "%d", arg1);
    gen_emit1(label, mnemonic, text);
}

void gen_emit2i(const char *label, const char *mnemonic, int arg1, int arg2)
{
    char first[32];
    char second[32];
    snprintf(first, sizeof(first), "%d", arg1);
    snprintf(second, sizeof(second), "%d", arg2);
    gen_emit2(label, mnemonic, first, second);
}
