/*
 * lex.c - Analisador lexico sequencial da linguagem SAL
 *
 * O lexer le um caractere por vez e devolve um Token por chamada. Ele ignora
 * espacos e comentarios, preserva o numero da linha e diferencia palavras
 * reservadas, identificadores, literais, operadores e separadores.
 *
 * Como vetores foram retirados da entregam, '[' e ']'
 * produzem erro lexico quando aparecem fora de strings e comentarios.
 */

#include "lex.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    const char *text;
    TokenKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"bool", TOK_BOOL},
    {"char", TOK_CHAR},
    {"do", TOK_DO},
    {"else", TOK_ELSE},
    {"end", TOK_END},
    {"false", TOK_FALSE},
    {"fn", TOK_FN},
    {"for", TOK_FOR},
    {"globals", TOK_GLOBALS},
    {"if", TOK_IF},
    {"int", TOK_INT},
    {"locals", TOK_LOCALS},
    {"loop", TOK_LOOP},
    {"main", TOK_MAIN},
    {"match", TOK_MATCH},
    {"module", TOK_MODULE},
    {"otherwise", TOK_OTHERWISE},
    {"print", TOK_PRINT},
    {"proc", TOK_PROC},
    {"ret", TOK_RET},
    {"scan", TOK_SCAN},
    {"start", TOK_START},
    {"step", TOK_STEP},
    {"to", TOK_TO},
    {"true", TOK_TRUE},
    {"until", TOK_UNTIL},
    {"when", TOK_WHEN},
    {"while", TOK_WHILE},
};

/* Monta um token comum e limita o tamanho copiado do lexema. */
static Token make_token(TokenKind kind, int line, const char *lexeme)
{
    Token token;
    token.kind = kind;
    token.line = line;
    strncpy(token.lexeme, lexeme, SAL_MAX_LEXEME - 1);
    token.lexeme[SAL_MAX_LEXEME - 1] = '\0';
    return token;
}

/* Erros lexicos percorrem o mesmo canal de tokens ate chegarem ao parser. */
static Token make_error(int line, const char *message)
{
    return make_token(TOK_ERROR, line, message);
}

/*
 * Obtem o proximo caractere. O lexer possui um unico slot de lookahead:
 * quando ha caractere devolvido por lexer_unget_char(), ele e consumido antes
 * de uma nova leitura do arquivo.
 */
static int lexer_get_char(Lexer *lexer)
{
    int ch;
    if (lexer->has_buffered_char)
    {
        lexer->has_buffered_char = 0;
        ch = lexer->buffered_char;
    }
    else
    {
        ch = fgetc(lexer->source);
    }
    if (ch == '\n')
        lexer->line++;
    return ch;
}

/* Devolve um unico caractere e desfaz a contagem de linha quando necessario. */
static void lexer_unget_char(Lexer *lexer, int ch)
{
    if (ch == EOF)
        return;
    if (ch == '\n')
        lexer->line--;
    lexer->buffered_char = ch;
    lexer->has_buffered_char = 1;
}

/* Espia sem consumir: le e devolve imediatamente ao buffer de lookahead. */
static int lexer_peek_char(Lexer *lexer)
{
    int ch = lexer_get_char(lexer);
    lexer_unget_char(lexer, ch);
    return ch;
}

/*
 * Descarta espacos e comentarios. Retorna 0 em sucesso e -1 quando
 * encontra um comentario de bloco sem fechamento; nesse caso, preenche
 * error_token com o diagnostico correspondente.
 */
static int skip_ignored(Lexer *lexer, Token *error_token)
{
    for (;;)
    {
        int ch = lexer_get_char(lexer);

        if (ch == EOF)
            return 0;
        if (isspace((unsigned char)ch))
            continue;

        if (ch != '@')
        {
            lexer_unget_char(lexer, ch);
            return 0;
        }

        if (lexer_peek_char(lexer) != '{')
        {
            /* Comentario de linha: @ ... fim da linha */
            while ((ch = lexer_get_char(lexer)) != EOF && ch != '\n')
            {
                /* apenas descarta */
            }
            continue;
        }

        /* Comentario de bloco: @{ ... }@ */
        const int start_line = lexer->line;
        lexer_get_char(lexer); /* consome '{' */
        for (;;)
        {
            ch = lexer_get_char(lexer);
            if (ch == EOF)
            {
                *error_token = make_error(start_line,
                                          "comentario de bloco nao fechado");
                return -1;
            }
            if (ch == '}' && lexer_peek_char(lexer) == '@')
            {
                lexer_get_char(lexer); /* consome '@' */
                break;
            }
        }
    }
}

/* Decide se um identificador lido corresponde a palavra reservada. */
static TokenKind keyword_kind(const char *lexeme)
{
    size_t count = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);
    for (size_t i = 0; i < count; i++)
    {
        if (strcmp(lexeme, KEYWORDS[i].text) == 0)
            return KEYWORDS[i].kind;
    }
    return TOK_IDENT;
}

/* Le letras, digitos e sublinhados; depois distingue palavra reservada. */
static Token read_identifier(Lexer *lexer, int first, int line)
{
    char lexeme[SAL_MAX_LEXEME];
    size_t len = 0;
    int ch = first;

    do
    {
        if (len >= SAL_MAX_LEXEME - 1)
        {
            return make_error(line, "identificador excede o limite de tamanho");
        }
        lexeme[len++] = (char)ch;
        ch = lexer_get_char(lexer);
    } while (isalnum((unsigned char)ch) || ch == '_');

    lexer_unget_char(lexer, ch);
    lexeme[len] = '\0';

    /* A disjuncao e representada pela palavra isolada v. */
    if (strcmp(lexeme, "v") == 0)
        return make_token(TOK_OR, line, lexeme);
    return make_token(keyword_kind(lexeme), line, lexeme);
}

/* Acumula somente digitos; sinais sao tratados pelo parser como operadores. */
static Token read_integer(Lexer *lexer, int first, int line)
{
    char lexeme[SAL_MAX_LEXEME];
    size_t len = 0;
    int ch = first;

    do
    {
        if (len >= SAL_MAX_LEXEME - 1)
        {
            return make_error(line, "constante inteira excede o limite de tamanho");
        }
        lexeme[len++] = (char)ch;
        ch = lexer_get_char(lexer);
    } while (isdigit((unsigned char)ch));

    lexer_unget_char(lexer, ch);
    lexeme[len] = '\0';
    return make_token(TOK_INT_LITERAL, line, lexeme);
}

/* Strings devem terminar na mesma linha em que foram iniciadas. */
static Token read_string(Lexer *lexer, int line)
{
    char lexeme[SAL_MAX_LEXEME];
    size_t len = 0;
    lexeme[len++] = '"';

    for (;;)
    {
        int ch = lexer_get_char(lexer);
        if (ch == EOF || ch == '\n')
        {
            return make_error(line, "string nao fechada");
        }
        if (len >= SAL_MAX_LEXEME - 1)
        {
            return make_error(line, "string excede o limite de tamanho");
        }
        lexeme[len++] = (char)ch;
        if (ch == '"')
            break;
    }

    lexeme[len] = '\0';
    return make_token(TOK_STRING_LITERAL, line, lexeme);
}

/* Um literal char possui exatamente um caractere entre aspas simples. */
static Token read_char_literal(Lexer *lexer, int line)
{
    char lexeme[4];
    int value = lexer_get_char(lexer);
    int closing = lexer_get_char(lexer);

    if (value == EOF || value == '\n')
    {
        return make_error(line, "caractere nao fechado");
    }
    if (closing != '\'')
    {
        return make_error(line, "constante char invalida");
    }

    lexeme[0] = '\'';
    lexeme[1] = (char)value;
    lexeme[2] = '\'';
    lexeme[3] = '\0';
    return make_token(TOK_CHAR_LITERAL, line, lexeme);
}

/* Abre uma nova entrada e zera o slot usado pelo lookahead. */
int lexer_open(Lexer *lexer, const char *source_path)
{
    lexer->source = fopen(source_path, "r");
    lexer->line = 1;
    lexer->buffered_char = 0;
    lexer->has_buffered_char = 0;
    return lexer->source ? 0 : -1;
}

/* Fecha apenas recursos pertencentes ao lexer. */
void lexer_close(Lexer *lexer)
{
    fclose(lexer->source);
    lexer->source = NULL;
    lexer->has_buffered_char = 0;
}

/*
 * Funcao publica principal do lexer. Depois de descartar elementos ignorados,
 * examina o primeiro caractere significativo e delega a leitura completa do
 * token para o tratamento apropriado.
 */
Token lexer_next(Lexer *lexer)
{
    Token ignored_error;
    if (skip_ignored(lexer, &ignored_error) != 0)
        return ignored_error;

    const int ch = lexer_get_char(lexer);
    const int line = lexer->line;

    if (ch == EOF)
        return make_token(TOK_EOF, line, "EOF");
    if (isalpha((unsigned char)ch) || ch == '_')
    {
        return read_identifier(lexer, ch, line);
    }
    if (isdigit((unsigned char)ch))
        return read_integer(lexer, ch, line);

    switch (ch)
    {
    case '"':
        return read_string(lexer, line);
    case '\'':
        return read_char_literal(lexer, line);
    case '+':
        return make_token(TOK_PLUS, line, "+");
    case '-':
        return make_token(TOK_MINUS, line, "-");
    case '*':
        return make_token(TOK_MULT, line, "*");
    case '/':
        return make_token(TOK_DIV, line, "/");
    case '^':
        return make_token(TOK_AND, line, "^");
    case '~':
        return make_token(TOK_NOT, line, "~");
    case ';':
        return make_token(TOK_SEMICOLON, line, ";");
    case ',':
        return make_token(TOK_COMMA, line, ",");
    case '(':
        return make_token(TOK_LPAREN, line, "(");
    case ')':
        return make_token(TOK_RPAREN, line, ")");

    case '[':
    case ']':
        return make_error(line,
                          "vetores nao sao suportados nesta versao da SAL");

    case ':':
        if (lexer_peek_char(lexer) == '=')
        {
            lexer_get_char(lexer);
            return make_token(TOK_ASSIGN, line, ":=");
        }
        return make_token(TOK_COLON, line, ":");

    case '=':
        if (lexer_peek_char(lexer) == '>')
        {
            lexer_get_char(lexer);
            return make_token(TOK_IMPLIES, line, "=>");
        }
        return make_token(TOK_EQ, line, "=");

    case '<':
    {
        int next = lexer_peek_char(lexer);
        if (next == '=')
        {
            lexer_get_char(lexer);
            return make_token(TOK_LE, line, "<=");
        }
        if (next == '>')
        {
            lexer_get_char(lexer);
            return make_token(TOK_NE, line, "<>");
        }
        return make_token(TOK_LT, line, "<");
    }

    case '>':
        if (lexer_peek_char(lexer) == '=')
        {
            lexer_get_char(lexer);
            return make_token(TOK_GE, line, ">=");
        }
        return make_token(TOK_GT, line, ">");

    case '.':
        if (lexer_peek_char(lexer) == '.')
        {
            lexer_get_char(lexer);
            return make_token(TOK_RANGE, line, "..");
        }
        return make_error(line, "ponto isolado invalido");

    default:
    {
        char message[96];
        if (isprint((unsigned char)ch))
        {
            snprintf(message, sizeof(message),
                     "caractere invalido '%c'", ch);
        }
        else
        {
            snprintf(message, sizeof(message),
                     "caractere invalido 0x%02X", (unsigned char)ch);
        }
        return make_error(line, message);
    }
    }
}

/* Os nomes s... reproduzem as categorias usadas na especificacao da SAL. */
const char *token_kind_name(TokenKind kind)
{
    switch (kind)
    {
    case TOK_IDENT:
        return "sIDENTIF";
    case TOK_INT_LITERAL:
        return "sCTEINT";
    case TOK_CHAR_LITERAL:
        return "sCTECHAR";
    case TOK_STRING_LITERAL:
        return "sSTRING";
    case TOK_MODULE:
        return "sMODULE";
    case TOK_GLOBALS:
        return "sGLOBALS";
    case TOK_LOCALS:
        return "sLOCALS";
    case TOK_PROC:
        return "sPROC";
    case TOK_FN:
        return "sFN";
    case TOK_MAIN:
        return "sMAIN";
    case TOK_START:
        return "sSTART";
    case TOK_END:
        return "sEND";
    case TOK_IF:
        return "sIF";
    case TOK_ELSE:
        return "sELSE";
    case TOK_MATCH:
        return "sMATCH";
    case TOK_WHEN:
        return "sWHEN";
    case TOK_OTHERWISE:
        return "sOTHERWISE";
    case TOK_FOR:
        return "sFOR";
    case TOK_TO:
        return "sTO";
    case TOK_STEP:
        return "sSTEP";
    case TOK_DO:
        return "sDO";
    case TOK_LOOP:
        return "sLOOP";
    case TOK_WHILE:
        return "sWHILE";
    case TOK_UNTIL:
        return "sUNTIL";
    case TOK_PRINT:
        return "sPRINT";
    case TOK_SCAN:
        return "sSCAN";
    case TOK_RET:
        return "sRET";
    case TOK_INT:
        return "sINT";
    case TOK_BOOL:
        return "sBOOL";
    case TOK_CHAR:
        return "sCHAR";
    case TOK_TRUE:
        return "sTRUE";
    case TOK_FALSE:
        return "sFALSE";
    case TOK_ASSIGN:
        return "sATRIB";
    case TOK_PLUS:
        return "sSOMA";
    case TOK_MINUS:
        return "sSUBRAT";
    case TOK_MULT:
        return "sMULT";
    case TOK_DIV:
        return "sDIV";
    case TOK_EQ:
        return "sIGUAL";
    case TOK_NE:
        return "sDIFERENTE";
    case TOK_GT:
        return "sMAIOR";
    case TOK_LT:
        return "sMENOR";
    case TOK_GE:
        return "sMAIORIG";
    case TOK_LE:
        return "sMENORIG";
    case TOK_AND:
        return "sAND";
    case TOK_OR:
        return "sOR";
    case TOK_NOT:
        return "sNEG";
    case TOK_IMPLIES:
        return "sIMPLIC";
    case TOK_RANGE:
        return "sPTOPTO";
    case TOK_SEMICOLON:
        return "sPONTVIRG";
    case TOK_COMMA:
        return "sVIRGULA";
    case TOK_COLON:
        return "sDOIS_PONT";
    case TOK_LPAREN:
        return "sABREPAR";
    case TOK_RPAREN:
        return "sFECHAPAR";
    case TOK_EOF:
        return "sEOF";
    case TOK_ERROR:
        return "sERRO";
    default:
        return "sINVALIDO";
    }
}
