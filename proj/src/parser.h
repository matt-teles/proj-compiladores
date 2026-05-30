/* Interface publica do analisador descendente recursivo. */

#ifndef SAL_PARSER_H
#define SAL_PARSER_H

#include "generator.h"
#include "lex.h"
#include "symtab.h"

/*
 * O parser guarda apenas o estado necessario para uma compilacao. Erros sao
 * fatais e encerram o processo pelo modulo diag.
 */
typedef struct
{
    Lexer *lexer;
    SymbolTable *symbols;
    Token current;           /* token de lookahead */
    int block_counter;       /* nomeia block#1, block#2... */
    int next_global_address; /* proxima celula reservada para globais */
    int next_local_address;  /* proxima celula local ou temporaria */

    /* Contexto da sub-rotina atualmente analisada. */
    Symbol *active_routine;                   /* NULL no global e na main */
    int active_function_saw_return;           /* exige ao menos um ret em cada funcao */
    int active_temp_count;                    /* temporarios ativos de for e match */
    char active_return_label[MEPA_MAX_LABEL]; /* epilogo comum dos retornos */
    char main_label[MEPA_MAX_LABEL];          /* destino do salto inicial */
} Parser;

void parser_init(Parser *parser, Lexer *lexer, SymbolTable *symbols);
void parser_parse_program(Parser *parser);

#endif /* SAL_PARSER_H */
