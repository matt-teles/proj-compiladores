/* Interface publica do analisador lexico sequencial. */

#ifndef SAL_LEX_H
#define SAL_LEX_H

#include <stdio.h>

#define SAL_MAX_LEXEME 256

typedef enum {
    /* Identificadores e literais */
    TOK_IDENT,
    TOK_INT_LITERAL,
    TOK_CHAR_LITERAL,
    TOK_STRING_LITERAL,

    /* Palavras reservadas */
    TOK_MODULE,
    TOK_GLOBALS,
    TOK_LOCALS,
    TOK_PROC,
    TOK_FN,
    TOK_MAIN,
    TOK_START,
    TOK_END,
    TOK_IF,
    TOK_ELSE,
    TOK_MATCH,
    TOK_WHEN,
    TOK_OTHERWISE,
    TOK_FOR,
    TOK_TO,
    TOK_STEP,
    TOK_DO,
    TOK_LOOP,
    TOK_WHILE,
    TOK_UNTIL,
    TOK_PRINT,
    TOK_SCAN,
    TOK_RET,
    TOK_INT,
    TOK_BOOL,
    TOK_CHAR,
    TOK_TRUE,
    TOK_FALSE,

    /* Operadores */
    TOK_ASSIGN,      /* := */
    TOK_PLUS,        /* +  */
    TOK_MINUS,       /* -  */
    TOK_MULT,        /* *  */
    TOK_DIV,         /* /  */
    TOK_EQ,          /* =  */
    TOK_NE,          /* <> */
    TOK_GT,          /* >  */
    TOK_LT,          /* <  */
    TOK_GE,          /* >= */
    TOK_LE,          /* <= */
    TOK_AND,         /* ^  */
    TOK_OR,          /* v  */
    TOK_NOT,         /* ~  */
    TOK_IMPLIES,     /* => */
    TOK_RANGE,       /* .. */

    /* Separadores */
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_COLON,
    TOK_LPAREN,
    TOK_RPAREN,

    TOK_EOF,
    TOK_ERROR
} TokenKind;

/* Unidade devolvida pelo lexer para o parser. */
typedef struct {
    TokenKind kind;                    /* categoria sintatica               */
    char lexeme[SAL_MAX_LEXEME];       /* texto original lido do fonte       */
    int line;                          /* linha de inicio do token            */
} Token;

/* Estado necessario para percorrer um unico arquivo SAL. */
typedef struct {
    FILE *source;                      /* arquivo atualmente aberto          */
    int line;                          /* linha corrente                     */
    int buffered_char;                 /* caractere devolvido pelo lookahead */
    int has_buffered_char;             /* indica se o buffer esta ocupado    */
} Lexer;

/* Abre o arquivo fonte e inicializa o estado do lexer. */
int lexer_open(Lexer *lexer, const char *source_path);

/* Fecha o arquivo associado, se houver. */
void lexer_close(Lexer *lexer);

/* Consome e devolve o proximo token da entrada. */
Token lexer_next(Lexer *lexer);

/* Nome estavel da categoria, usado em logs e diagnosticos. */
const char *token_kind_name(TokenKind kind);

#endif /* SAL_LEX_H */
