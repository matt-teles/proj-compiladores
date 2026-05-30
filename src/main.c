/*
 * main.c - Ponto de entrada do compilador SAL
 *
 * Uso:
 *   ./build/SAL arquivo.sal [--tokens] [--symtab] [--trace]
 *
 * O main apenas interpreta a linha de comando, inicializa os modulos e chama
 * o parser. Toda a analise ocorre em uma unica passagem pelo arquivo-fonte.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "config.h"
#include "diag.h"
#include "generator.h"
#include "lex.h"
#include "log.h"
#include "parser.h"
#include "symtab.h"

static void print_usage(const char *program) {
    fprintf(stderr,
            "Uso: %s <arquivo.sal> [--tokens] [--symtab] [--trace]\n",
            program);
}

/*
 * Cria a pasta usada por todos os artefatos produzidos. EEXIST nao e erro:
 * normalmente a pasta permanece disponivel entre compilacoes sucessivas.
 */
static int ensure_results_directory(void) {
    if (mkdir(SAL_RESULTS_DIR, 0777) == 0 || errno == EEXIST) return 0;
    perror("Erro ao criar a pasta de resultados");
    return -1;
}

int main(int argc, char *argv[]) {
    /* O primeiro argumento obrigatorio e sempre o caminho do fonte SAL. */
    if (argc < 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *source_path = argv[1];
    int emit_tokens = 0;
    int emit_symtab = 0;
    int emit_trace = 0;

    /* As flags apenas habilitam relatorios opcionais; o .mepa e sempre gerado. */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0) emit_tokens = 1;
        else if (strcmp(argv[i], "--symtab") == 0) emit_symtab = 1;
        else if (strcmp(argv[i], "--trace") == 0) emit_trace = 1;
        else {
            fprintf(stderr, "Erro: opcao desconhecida '%s'.\n", argv[i]);
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    if (ensure_results_directory() != 0) return EXIT_FAILURE;

    /* O lexer e aberto antes dos artefatos para detectar fonte inexistente cedo. */
    Lexer lexer;
    if (lexer_open(&lexer, source_path) != 0) {
        fprintf(stderr, "Erro: nao foi possivel abrir '%s'.\n", source_path);
        return EXIT_FAILURE;
    }

    /* Abre apenas os logs solicitados na linha de comando. */
    if (log_init(source_path, emit_tokens, emit_symtab, emit_trace) != 0) {
        fputs("Erro: nao foi possivel criar os arquivos de log.\n", stderr);
        lexer_close(&lexer);
        return EXIT_FAILURE;
    }
    diag_init(log_trace_file()); /* NULL quando --trace nao foi informado. */

    SymbolTable symbols;
    symtab_init(&symbols);

    /* O gerador escreve resultados/nome.mepa durante a propria analise. */
    if (gen_init(source_path) != 0) {
        fputs("Erro: nao foi possivel criar o arquivo MEPA.\n", stderr);
        symtab_dispose(&symbols);
        log_close();
        lexer_close(&lexer);
        return EXIT_FAILURE;
    }

    Parser parser;
    parser_init(&parser, &lexer, &symbols);
    parser_parse_program(&parser); /* Erros encerram a execucao via diag_error(). */

    /* A tabela consolidada so e escrita depois que todos os escopos foram lidos. */
    log_symtab(&symbols);
    if (gen_close() != 0) {
        fputs("Erro: nao foi possivel concluir o arquivo MEPA.\n", stderr);
        symtab_dispose(&symbols);
        log_close();
        lexer_close(&lexer);
        return EXIT_FAILURE;
    }

    printf("Compilacao concluida com sucesso.\nArquivo MEPA: %s\n",
           gen_output_path());

    /* Libera memoria e fecha os arquivos ainda abertos antes de sair. */
    symtab_dispose(&symbols);
    log_close();
    lexer_close(&lexer);
    return EXIT_SUCCESS;
}
