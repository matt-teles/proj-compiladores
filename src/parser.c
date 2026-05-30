/*
 * parser.c - ASDR, verificacao semantica e traducao para MEPA
 *
 * Cada funcao parse_* corresponde a uma parte da gramatica SAL. O parser faz
 * uma unica passagem: solicita tokens ao lexer, atualiza a tabela de simbolos,
 * verifica tipos e emite MEPA no momento em que reconhece cada construcao.
 *
 * O processamento termina no primeiro erro.
 */

#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "log.h"

#define SAL_MAX_DECL_NAMES 128
#define SAL_MAX_PARAMS 128
#define SAL_MAX_ARGS 128

typedef enum
{
    DECL_GLOBAL,
    DECL_LOCAL
} DeclarationRegion;

typedef struct
{
    SalType types[SAL_MAX_PARAMS];
    Symbol *symbols[SAL_MAX_PARAMS];
    size_t count;
} ParameterList;

typedef struct
{
    SalType types[SAL_MAX_ARGS];
    size_t count;
} ArgumentList;

/* ------------------------------------------------------------------------- */
/* Infraestrutura interna                                                     */
/* ------------------------------------------------------------------------- */

static void parser_advance(Parser *parser);
static void parser_expect(Parser *parser, TokenKind expected);
static int parser_accept(Parser *parser, TokenKind kind);
static void syntax_error(Parser *parser, const char *expected);
static void semantic_error(int line, const char *fmt, ...);
static void parser_push_scope(Parser *parser, const char *name,
                              int opens_activation);
static void parser_pop_scope(Parser *parser);
static void trace_enter(const char *production);
static void trace_leave(const char *production);
static Symbol *declare_symbol(Parser *parser, const char *name,
                              SymbolCategory category, SalType type,
                              int line, int address);
static Symbol *lookup_symbol(Parser *parser, const char *name, int line);
static void require_variable(const Symbol *symbol, const char *name, int line);
static void require_type(SalType actual, SalType expected,
                         int line, const char *context);
static void set_signature(Symbol *routine, const ParameterList *params);
static void validate_call(const Symbol *routine,
                          const ArgumentList *arguments, int line);
static void assign_parameter_addresses(ParameterList *params);

/* Pequenos atalhos para deixar as acoes de geracao legiveis no parser. */
static void new_label(char out[MEPA_MAX_LABEL]);
static void emit0(const char *label, const char *mnemonic);
static void emit1(const char *label, const char *mnemonic, const char *arg1);
static void emit2(const char *label, const char *mnemonic,
                  const char *arg1, const char *arg2);
static void emit1i(const char *label, const char *mnemonic, int arg1);
static void emit2i(const char *label, const char *mnemonic, int arg1, int arg2);
static void emit_label(const char *label);
static int current_storage_level(const Parser *parser);

/* ------------------------------------------------------------------------- */
/* Producoes                                                                  */
/* ------------------------------------------------------------------------- */

static void parse_globals(Parser *parser);
static void parse_declaration(Parser *parser, DeclarationRegion region);
static SalType parse_type(Parser *parser);
static void parse_function(Parser *parser);
static void parse_procedure_after_keyword(Parser *parser);
static void parse_main_after_keyword(Parser *parser);
static ParameterList parse_parameter_list(Parser *parser);
static void parse_optional_locals(Parser *parser);
static void parse_block(Parser *parser, int creates_scope);
static void parse_command(Parser *parser);
static void parse_output(Parser *parser);
static void parse_input(Parser *parser);
static void parse_if(Parser *parser);
static void parse_match(Parser *parser);
static void parse_when_clause(Parser *parser, int temp_level, int temp_address,
                              const char *end_label);
static void parse_match_item(Parser *parser, int temp_level, int temp_address,
                             const char *body_label);
static int parse_signed_integer(Parser *parser);
static void parse_for(Parser *parser);
static void parse_step_value(Parser *parser);
static void parse_loop(Parser *parser);
static void parse_return(Parser *parser);
static void parse_ident_command(Parser *parser);
static ArgumentList parse_optional_arguments(Parser *parser);
static SalType parse_expression(Parser *parser);
static SalType parse_or(Parser *parser);
static SalType parse_and(Parser *parser);
static SalType parse_relation(Parser *parser);
static SalType parse_addition(Parser *parser);
static SalType parse_multiplication(Parser *parser);
static SalType parse_factor(Parser *parser);
static SalType parse_primary(Parser *parser);

void parser_init(Parser *parser, Lexer *lexer, SymbolTable *symbols)
{
    parser->lexer = lexer;
    parser->symbols = symbols;

    /* Enderecos sao atribuidos sequencialmente conforme surgem declaracoes. */
    parser->block_counter = 0;
    parser->next_global_address = 0;
    parser->next_local_address = 0;

    /* Este contexto e preenchido apenas enquanto uma sub-rotina e analisada. */
    parser->active_routine = NULL;
    parser->active_function_saw_return = 0;
    parser->active_temp_count = 0;
    parser->active_return_label[0] = '\0';
    parser->main_label[0] = '\0';
}

/* Busca o proximo token e registra-o no .tk quando a flag esta ativa. */
static void parser_advance(Parser *parser)
{
    parser->current = lexer_next(parser->lexer);
    log_token(&parser->current);

    if (parser->current.kind == TOK_ERROR)
    {
        diag_error(DIAG_LEXICAL, parser->current.line,
                   "%s", parser->current.lexeme);
    }
}

static void syntax_error(Parser *parser, const char *expected)
{
    diag_error(DIAG_SYNTACTIC, parser->current.line,
               "esperado %s, encontrado %s ('%s')",
               expected,
               token_kind_name(parser->current.kind),
               parser->current.lexeme);
}

/* Concentra a formatacao antes de delegar ao modulo diag. */
static void formatted_error(DiagnosticKind kind, int line,
                            const char *fmt, va_list args)
{
    char message[768];
    vsnprintf(message, sizeof(message), fmt, args);
    diag_error(kind, line, "%s", message);
}

static void semantic_error(int line, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    formatted_error(DIAG_SEMANTIC, line, fmt, args);
    va_end(args);
}

/* Consome obrigatoriamente o token esperado. */
static void parser_expect(Parser *parser, TokenKind expected)
{
    if (parser->current.kind != expected)
    {
        syntax_error(parser, token_kind_name(expected));
    }
    parser_advance(parser);
}

/* Consome tokens opcionais, como else e step. */
static int parser_accept(Parser *parser, TokenKind kind)
{
    if (parser->current.kind != kind)
        return 0;
    parser_advance(parser);
    return 1;
}

static void parser_push_scope(Parser *parser, const char *name,
                              int opens_activation)
{
    symtab_push_scope(parser->symbols, name, opens_activation);
    diag_trace("   + escopo %s", symtab_current_path(parser->symbols));
}

static void parser_pop_scope(Parser *parser)
{
    char path[SAL_MAX_SCOPE_PATH];
    strncpy(path, symtab_current_path(parser->symbols), sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    symtab_pop_scope(parser->symbols);
    diag_trace("   - escopo %s", path);
}

static void trace_enter(const char *production) { diag_trace(">> %s", production); }
static void trace_leave(const char *production) { diag_trace("<< %s", production); }

/* Insere somente depois de confirmar que nao ha duplicata no escopo atual. */
static Symbol *declare_symbol(Parser *parser, const char *name,
                              SymbolCategory category, SalType type,
                              int line, int address)
{
    if (symtab_lookup_current(parser->symbols, name))
    {
        semantic_error(line, "identificador '%s' ja declarado neste escopo", name);
    }
    return symtab_declare(parser->symbols, name, category, type, address);
}

/* Busca respeitando visibilidade; identificadores ausentes sao erro semantico. */
static Symbol *lookup_symbol(Parser *parser, const char *name, int line)
{
    Symbol *symbol = symtab_lookup(parser->symbols, name);
    if (!symbol)
        semantic_error(line, "identificador '%s' nao declarado", name);
    return symbol;
}

static void require_variable(const Symbol *symbol, const char *name, int line)
{
    if (symbol->category != SYMBOL_VARIABLE && symbol->category != SYMBOL_PARAMETER)
    {
        semantic_error(line, "identificador '%s' deve ser uma variavel, mas e %s",
                       name, symbol_category_name(symbol->category));
    }
}

static void require_type(SalType actual, SalType expected,
                         int line, const char *context)
{
    if (actual != expected)
    {
        semantic_error(line, "%s: esperado tipo %s, encontrado %s",
                       context, sal_type_name(expected), sal_type_name(actual));
    }
}

static void set_signature(Symbol *routine, const ParameterList *params)
{
    symbol_set_signature(routine, params->types, params->count);
}

/* Confere primeiro a aridade e depois cada tipo posicional da chamada. */
static void validate_call(const Symbol *routine,
                          const ArgumentList *arguments, int line)
{
    if (routine->parameter_count != arguments->count)
    {
        semantic_error(line,
                       "chamada de '%s': esperado(s) %zu argumento(s), recebido(s) %zu",
                       routine->name, routine->parameter_count, arguments->count);
    }
    for (size_t i = 0; i < arguments->count; i++)
    {
        if (routine->parameter_types[i] != arguments->types[i])
        {
            semantic_error(line,
                           "argumento %zu de '%s': esperado tipo %s, encontrado %s",
                           i + 1, routine->name,
                           sal_type_name(routine->parameter_types[i]),
                           sal_type_name(arguments->types[i]));
        }
    }
}

/*
 * Parametros ficam abaixo da base do registro de ativacao. Por isso recebem
 * deslocamentos negativos, na mesma ordem em que foram declarados.
 */
static void assign_parameter_addresses(ParameterList *params)
{
    for (size_t i = 0; i < params->count; i++)
    {
        params->symbols[i]->address = -(int)(params->count + 4 - i);
    }
}

static void new_label(char out[MEPA_MAX_LABEL]) { gen_new_label(out, MEPA_MAX_LABEL); }
static void emit0(const char *label, const char *mnemonic) { gen_emit0(label, mnemonic); }
static void emit1(const char *label, const char *mnemonic, const char *arg1) { gen_emit1(label, mnemonic, arg1); }
static void emit2(const char *label, const char *mnemonic, const char *arg1, const char *arg2) { gen_emit2(label, mnemonic, arg1, arg2); }
static void emit1i(const char *label, const char *mnemonic, int arg1) { gen_emit1i(label, mnemonic, arg1); }
static void emit2i(const char *label, const char *mnemonic, int arg1, int arg2) { gen_emit2i(label, mnemonic, arg1, arg2); }
static void emit_label(const char *label) { emit0(label, "NADA"); }
static int current_storage_level(const Parser *parser) { return symtab_current_lexical_level(parser->symbols); }

/* ------------------------------------------------------------------------- */
/* Programa, declaracoes e sub-rotinas                                        */
/* ------------------------------------------------------------------------- */

/* Reconhece a estrutura externa: modulo, globais, sub-rotinas e main. */

/* Copia lexemas para buffers locais garantindo terminacao da string. */
static void copy_text(char *out, size_t capacity, const char *text)
{
    strncpy(out, text, capacity - 1);
    out[capacity - 1] = '\0';
}

void parser_parse_program(Parser *parser)
{
    trace_enter("program");
    parser_advance(parser);

    parser_expect(parser, TOK_MODULE);
    parser_expect(parser, TOK_IDENT);
    parser_expect(parser, TOK_SEMICOLON);

    /* O escopo global existe durante toda a compilacao e toda a execucao. */
    parser_push_scope(parser, "global", 0);
    emit0(NULL, "INPP"); /* inicializa a maquina MEPA */

    if (parser->current.kind == TOK_GLOBALS)
        parse_globals(parser);
    /* Globais sao reservadas antes do salto para as sub-rotinas. */
    if (parser->next_global_address > 0)
    {
        emit1i(NULL, "AMEM", parser->next_global_address);
    }

    /* As sub-rotinas ficam antes da main no MEPA, entao saltamos sobre elas. */
    new_label(parser->main_label);
    emit1(NULL, "DSVS", parser->main_label);

    int found_main = 0;
    while (parser->current.kind == TOK_FN || parser->current.kind == TOK_PROC)
    {
        if (parser->current.kind == TOK_FN)
        {
            parse_function(parser);
        }
        else
        {
            parser_expect(parser, TOK_PROC);
            if (parser_accept(parser, TOK_MAIN))
            {
                parse_main_after_keyword(parser);
                found_main = 1;
                break;
            }
            parse_procedure_after_keyword(parser);
        }
    }

    if (!found_main)
        syntax_error(parser, "sPROC main() obrigatorio");
    if (parser->current.kind != TOK_EOF)
        syntax_error(parser, "sEOF");

    /* Libera globais e encerra o codigo executavel antes do marcador textual. */
    if (parser->next_global_address > 0)
    {
        emit1i(NULL, "DMEM", parser->next_global_address);
    }
    emit0(NULL, "PARA");
    gen_emit_marker("FIM"); /* marcador lido pelo interpretador, nao executado */

    parser_pop_scope(parser);
    trace_leave("program");
}

/* globals ::= sGLOBALS declaration+ */
static void parse_globals(Parser *parser)
{
    trace_enter("globals");
    parser_expect(parser, TOK_GLOBALS);
    if (parser->current.kind != TOK_IDENT)
    {
        syntax_error(parser, "ao menos uma declaracao global");
    }
    while (parser->current.kind == TOK_IDENT)
    {
        parse_declaration(parser, DECL_GLOBAL);
    }
    trace_leave("globals");
}

/* declaration ::= id (, id)* : type ; */
static void parse_declaration(Parser *parser, DeclarationRegion region)
{
    trace_enter("declaration");
    char names[SAL_MAX_DECL_NAMES][SAL_MAX_IDENTIFIER];
    int lines[SAL_MAX_DECL_NAMES];
    size_t count = 0;

    do
    {
        if (count == SAL_MAX_DECL_NAMES)
        {
            semantic_error(parser->current.line,
                           "declaracao excede o limite de %d nomes",
                           SAL_MAX_DECL_NAMES);
        }
        if (parser->current.kind != TOK_IDENT)
        {
            syntax_error(parser, "sIDENTIF");
        }
        copy_text(names[count], sizeof(names[count]), parser->current.lexeme);
        lines[count] = parser->current.line;
        count++;
        parser_advance(parser);
    } while (parser_accept(parser, TOK_COMMA));

    parser_expect(parser, TOK_COLON);
    SalType type = parse_type(parser);
    parser_expect(parser, TOK_SEMICOLON);

    /* Cada variavel ocupa uma celula; globais e locais usam contadores distintos. */
    for (size_t i = 0; i < count; i++)
    {
        int address = region == DECL_GLOBAL
                          ? parser->next_global_address++
                          : parser->next_local_address++;
        declare_symbol(parser, names[i], SYMBOL_VARIABLE, type,
                       lines[i], address);
    }
    trace_leave("declaration");
}

/* type ::= sINT | sBOOL | sCHAR */
static SalType parse_type(Parser *parser)
{
    trace_enter("type");
    SalType type;
    if (parser_accept(parser, TOK_INT))
        type = SAL_TYPE_INT;
    else if (parser_accept(parser, TOK_BOOL))
        type = SAL_TYPE_BOOL;
    else if (parser_accept(parser, TOK_CHAR))
        type = SAL_TYPE_CHAR;
    else
    {
        syntax_error(parser, "sINT, sBOOL ou sCHAR");
        type = SAL_TYPE_INVALID; /* apenas satisfaz o compilador C */
    }
    trace_leave("type");
    return type;
}

/* parameter_list ::= id : type (, id : type)* */
static ParameterList parse_parameter_list(Parser *parser)
{
    trace_enter("parameter_list");
    ParameterList params = {0};

    do
    {
        if (params.count == SAL_MAX_PARAMS)
        {
            semantic_error(parser->current.line,
                           "lista excede o limite de %d parametros",
                           SAL_MAX_PARAMS);
        }
        if (parser->current.kind != TOK_IDENT)
        {
            syntax_error(parser, "sIDENTIF (nome do parametro)");
        }

        char name[SAL_MAX_IDENTIFIER];
        copy_text(name, sizeof(name), parser->current.lexeme);
        int line = parser->current.line;
        parser_advance(parser);
        parser_expect(parser, TOK_COLON);
        SalType type = parse_type(parser);

        params.types[params.count] = type;
        params.symbols[params.count] = declare_symbol(
            parser, name, SYMBOL_PARAMETER, type, line, 0);
        params.count++;
    } while (parser_accept(parser, TOK_COMMA));

    assign_parameter_addresses(&params);
    trace_leave("parameter_list");
    return params;
}

static void parse_optional_locals(Parser *parser)
{
    if (!parser_accept(parser, TOK_LOCALS))
        return;

    trace_enter("locals");
    if (parser->current.kind != TOK_IDENT)
    {
        syntax_error(parser, "ao menos uma declaracao local");
    }
    while (parser->current.kind == TOK_IDENT)
    {
        parse_declaration(parser, DECL_LOCAL);
    }
    trace_leave("locals");
}

/* function ::= sFN id ( parameter_list? ) : type locals? block */
static void parse_function(Parser *parser)
{
    trace_enter("function");
    parser_expect(parser, TOK_FN);
    if (parser->current.kind != TOK_IDENT)
    {
        syntax_error(parser, "sIDENTIF (nome da funcao)");
    }

    char name[SAL_MAX_IDENTIFIER];
    copy_text(name, sizeof(name), parser->current.lexeme);
    int line = parser->current.line;
    parser_advance(parser);

    /* Registra antes do corpo para permitir chamadas recursivas. */
    Symbol *function = declare_symbol(parser, name, SYMBOL_FUNCTION,
                                      SAL_TYPE_INVALID, line, 0);
    char label[MEPA_MAX_LABEL];
    new_label(label);
    symbol_set_label(function, label);

    char scope_name[SAL_MAX_SCOPE_PATH];
    snprintf(scope_name, sizeof(scope_name), "fn:%s", name);
    parser_push_scope(parser, scope_name, 1);

    parser->next_local_address = 0;
    parser_expect(parser, TOK_LPAREN);
    ParameterList params = {0};
    if (parser->current.kind != TOK_RPAREN)
    {
        params = parse_parameter_list(parser);
    }
    parser_expect(parser, TOK_RPAREN);
    parser_expect(parser, TOK_COLON);
    function->type = parse_type(parser);
    set_signature(function, &params);

    /* Mantem dados da funcao disponiveis para validar e traduzir comandos ret. */
    parser->active_routine = function;
    parser->active_function_saw_return = 0;
    parser->active_temp_count = 0;

    parse_optional_locals(parser);
    int local_count = parser->next_local_address;
    new_label(parser->active_return_label);

    /* Prologo: rotulo de entrada, novo nivel lexico e memoria para locais. */
    emit_label(function->label);
    emit1i(NULL, "ENPR", 1);
    if (local_count > 0)
        emit1i(NULL, "AMEM", local_count);

    parse_block(parser, 0);
    if (!parser->active_function_saw_return)
    {
        semantic_error(line, "funcao '%s' deve possuir ao menos um comando ret",
                       name);
    }

    /* Caminhos sem ret recebem zero; caminhos com ret saltam para o epilogo. */
    emit1i(NULL, "CRCT", 0);
    emit2i(NULL, "ARMZ", 1, -(int)(params.count + 5));
    emit_label(parser->active_return_label);
    if (local_count > 0)
        emit1i(NULL, "DMEM", local_count);
    emit1i(NULL, "RTPR", (int)params.count);

    parser->active_routine = NULL;
    parser->active_temp_count = 0;
    parser_pop_scope(parser);
    trace_leave("function");
}

/* Procedure: a palavra sPROC ja foi consumida pelo chamador. */
static void parse_procedure_after_keyword(Parser *parser)
{
    trace_enter("procedure");
    if (parser->current.kind != TOK_IDENT)
    {
        syntax_error(parser, "sIDENTIF (nome do procedimento)");
    }

    char name[SAL_MAX_IDENTIFIER];
    copy_text(name, sizeof(name), parser->current.lexeme);
    int line = parser->current.line;
    parser_advance(parser);

    /* Registra antes do corpo para permitir chamadas recursivas. */
    Symbol *procedure = declare_symbol(parser, name, SYMBOL_PROCEDURE,
                                       SAL_TYPE_VOID, line, 0);
    char label[MEPA_MAX_LABEL];
    new_label(label);
    symbol_set_label(procedure, label);

    char scope_name[SAL_MAX_SCOPE_PATH];
    snprintf(scope_name, sizeof(scope_name), "proc:%s", name);
    parser_push_scope(parser, scope_name, 1);

    parser->next_local_address = 0;
    parser_expect(parser, TOK_LPAREN);
    ParameterList params = {0};
    if (parser->current.kind != TOK_RPAREN)
    {
        params = parse_parameter_list(parser);
    }
    parser_expect(parser, TOK_RPAREN);
    set_signature(procedure, &params);

    parser->active_routine = procedure;
    parser->active_temp_count = 0;

    parse_optional_locals(parser);
    int local_count = parser->next_local_address;

    /* Prologo e epilogo seguem a convencao de chamadas da maquina MEPA. */
    emit_label(procedure->label);
    emit1i(NULL, "ENPR", 1);
    if (local_count > 0)
        emit1i(NULL, "AMEM", local_count);
    parse_block(parser, 0);
    if (local_count > 0)
        emit1i(NULL, "DMEM", local_count);
    emit1i(NULL, "RTPR", (int)params.count);

    parser->active_routine = NULL;
    parser->active_temp_count = 0;
    parser_pop_scope(parser);
    trace_leave("procedure");
}

/* Main: a sequencia sPROC sMAIN ja foi consumida pelo chamador. */
static void parse_main_after_keyword(Parser *parser)
{
    trace_enter("main");
    parser_push_scope(parser, "proc:main", 0);
    parser->active_routine = NULL;
    parser->active_temp_count = 0;
    /* A main usa nivel 0; suas locais ficam depois das globais na memoria. */
    parser->next_local_address = parser->next_global_address;

    parser_expect(parser, TOK_LPAREN);
    parser_expect(parser, TOK_RPAREN);
    parse_optional_locals(parser);
    int local_count = parser->next_local_address - parser->next_global_address;

    emit_label(parser->main_label);
    if (local_count > 0)
        emit1i(NULL, "AMEM", local_count);
    parse_block(parser, 0);
    if (local_count > 0)
        emit1i(NULL, "DMEM", local_count);

    parser_pop_scope(parser);
    trace_leave("main");
}

/* block ::= sSTART (command ;)* sEND */
static void parse_block(Parser *parser, int creates_scope)
{
    trace_enter("block");
    if (creates_scope)
    {
        /* Blocos internos criam visibilidade propria, mas nao novo nivel MEPA. */
        char name[32];
        snprintf(name, sizeof(name), "block#%d", ++parser->block_counter);
        parser_push_scope(parser, name, 0);
    }

    parser_expect(parser, TOK_START);
    while (parser->current.kind != TOK_END && parser->current.kind != TOK_EOF)
    {
        parse_command(parser);
        parser_expect(parser, TOK_SEMICOLON);
    }
    parser_expect(parser, TOK_END);

    if (creates_scope)
        parser_pop_scope(parser);
    trace_leave("block");
}

static void parse_command(Parser *parser)
{
    trace_enter("command");
    switch (parser->current.kind)
    {
    case TOK_PRINT:
        parse_output(parser);
        break;
    case TOK_SCAN:
        parse_input(parser);
        break;
    case TOK_IF:
        parse_if(parser);
        break;
    case TOK_MATCH:
        parse_match(parser);
        break;
    case TOK_FOR:
        parse_for(parser);
        break;
    case TOK_LOOP:
        parse_loop(parser);
        break;
    case TOK_RET:
        parse_return(parser);
        break;
    case TOK_IDENT:
        parse_ident_command(parser);
        break;
    case TOK_START:
        parse_block(parser, 1);
        break;
    default:
        syntax_error(parser, "comando valido");
    }
    trace_leave("command");
}

/* output ::= sPRINT ( expression (, expression)* ) */
static void parse_output(Parser *parser)
{
    trace_enter("output");
    parser_expect(parser, TOK_PRINT);
    parser_expect(parser, TOK_LPAREN);

    SalType type = parse_expression(parser);
    /* A MEPA imprime valores, mas nao possui instrucao para strings. */
    if (type != SAL_TYPE_STRING)
        emit0(NULL, "IMPR");
    while (parser_accept(parser, TOK_COMMA))
    {
        type = parse_expression(parser);
        if (type != SAL_TYPE_STRING)
            emit0(NULL, "IMPR");
    }

    parser_expect(parser, TOK_RPAREN);
    trace_leave("output");
}

/* input ::= sSCAN ( id ) */
static void parse_input(Parser *parser)
{
    trace_enter("input");
    parser_expect(parser, TOK_SCAN);
    parser_expect(parser, TOK_LPAREN);
    if (parser->current.kind != TOK_IDENT)
    {
        syntax_error(parser, "sIDENTIF (variavel de entrada)");
    }

    char name[SAL_MAX_IDENTIFIER];
    copy_text(name, sizeof(name), parser->current.lexeme);
    int line = parser->current.line;
    parser_advance(parser);

    Symbol *symbol = lookup_symbol(parser, name, line);
    require_variable(symbol, name, line);
    emit0(NULL, "LEIT");
    emit2i(NULL, "ARMZ", symbol->lexical_level, symbol->address);

    parser_expect(parser, TOK_RPAREN);
    trace_leave("input");
}

/* if ::= sIF ( expression ) command (sELSE command)? */
static void parse_if(Parser *parser)
{
    trace_enter("if");
    int line = parser->current.line;
    parser_expect(parser, TOK_IF);
    parser_expect(parser, TOK_LPAREN);
    SalType condition = parse_expression(parser);
    require_type(condition, SAL_TYPE_BOOL, line, "condicao do if");
    parser_expect(parser, TOK_RPAREN);

    char else_label[MEPA_MAX_LABEL];
    char end_label[MEPA_MAX_LABEL];
    new_label(else_label);
    new_label(end_label);
    /* Condicao falsa salta para else; condicao verdadeira segue em frente. */
    emit1(NULL, "DSVF", else_label);

    parse_command(parser);
    if (parser_accept(parser, TOK_ELSE))
    {
        emit1(NULL, "DSVS", end_label);
        emit_label(else_label);
        parse_command(parser);
        emit_label(end_label);
    }
    else
    {
        emit_label(else_label);
    }
    trace_leave("if");
}

/* match avalia a expressao uma vez e guarda seu valor em uma celula auxiliar. */
static void parse_match(Parser *parser)
{
    trace_enter("match");
    int line = parser->current.line;
    parser_expect(parser, TOK_MATCH);
    parser_expect(parser, TOK_LPAREN);

    /* A expressao e calculada uma vez e guardada para todas as clausulas when. */
    int temp_level = current_storage_level(parser);
    int temp_address = parser->next_local_address++;
    parser->active_temp_count++;
    emit1i(NULL, "AMEM", 1);

    SalType type = parse_expression(parser);
    require_type(type, SAL_TYPE_INT, line, "expressao do match");
    parser_expect(parser, TOK_RPAREN);
    emit2i(NULL, "ARMZ", temp_level, temp_address);

    char end_label[MEPA_MAX_LABEL];
    new_label(end_label);

    if (parser->current.kind != TOK_WHEN)
    {
        syntax_error(parser, "ao menos uma clausula sWHEN");
    }
    while (parser->current.kind == TOK_WHEN)
    {
        parse_when_clause(parser, temp_level, temp_address, end_label);
    }
    if (parser_accept(parser, TOK_OTHERWISE))
    {
        parser_expect(parser, TOK_IMPLIES);
        parse_command(parser);
        parser_expect(parser, TOK_SEMICOLON);
    }
    parser_expect(parser, TOK_END);

    emit_label(end_label);
    emit1i(NULL, "DMEM", 1);
    parser->next_local_address--;
    parser->active_temp_count--;
    trace_leave("match");
}

static void parse_when_clause(Parser *parser, int temp_level, int temp_address,
                              const char *end_label)
{
    trace_enter("when_clause");
    char body_label[MEPA_MAX_LABEL];
    char next_when_label[MEPA_MAX_LABEL];
    new_label(body_label);
    new_label(next_when_label);

    parser_expect(parser, TOK_WHEN);
    /* Cada item bem-sucedido salta para o mesmo corpo associado ao when. */
    parse_match_item(parser, temp_level, temp_address, body_label);
    while (parser_accept(parser, TOK_COMMA))
    {
        parse_match_item(parser, temp_level, temp_address, body_label);
    }
    emit1(NULL, "DSVS", next_when_label);

    parser_expect(parser, TOK_IMPLIES);
    emit_label(body_label);
    parse_command(parser);
    parser_expect(parser, TOK_SEMICOLON);
    emit1(NULL, "DSVS", end_label);
    emit_label(next_when_label);
    trace_leave("when_clause");
}

static void parse_match_item(Parser *parser, int temp_level, int temp_address,
                             const char *body_label)
{
    trace_enter("match_item");
    int first = parse_signed_integer(parser);
    int is_range = parser_accept(parser, TOK_RANGE);
    int second = is_range ? parse_signed_integer(parser) : first;

    char next_label[MEPA_MAX_LABEL];
    new_label(next_label);
    emit2i(NULL, "CRVL", temp_level, temp_address);
    emit1i(NULL, "CRCT", first);

    if (is_range)
    {
        emit0(NULL, "CMAG");
        emit2i(NULL, "CRVL", temp_level, temp_address);
        emit1i(NULL, "CRCT", second);
        emit0(NULL, "CMEG");
        emit0(NULL, "CONJ");
    }
    else
    {
        emit0(NULL, "CMIG");
    }

    /* Falha tenta o proximo item; sucesso entra no corpo da clausula. */
    emit1(NULL, "DSVF", next_label);
    emit1(NULL, "DSVS", body_label);
    emit_label(next_label);
    trace_leave("match_item");
}

static int parse_signed_integer(Parser *parser)
{
    int negative = parser_accept(parser, TOK_MINUS);
    if (parser->current.kind != TOK_INT_LITERAL)
    {
        syntax_error(parser, "sCTEINT");
    }
    int value = (int)strtol(parser->current.lexeme, NULL, 10);
    parser_advance(parser);
    return negative ? -value : value;
}

/* for preserva limite e passo em temporarios durante todas as iteracoes. */
static void parse_for(Parser *parser)
{
    trace_enter("for");
    int line = parser->current.line;
    parser_expect(parser, TOK_FOR);
    if (parser->current.kind != TOK_IDENT)
    {
        syntax_error(parser, "sIDENTIF (variavel de controle)");
    }

    char name[SAL_MAX_IDENTIFIER];
    copy_text(name, sizeof(name), parser->current.lexeme);
    int name_line = parser->current.line;
    parser_advance(parser);

    Symbol *control = lookup_symbol(parser, name, name_line);
    require_variable(control, name, name_line);
    require_type(control->type, SAL_TYPE_INT, name_line,
                 "variavel de controle do for");

    parser_expect(parser, TOK_ASSIGN);
    SalType initial = parse_expression(parser);
    require_type(initial, SAL_TYPE_INT, line, "valor inicial do for");
    emit2i(NULL, "ARMZ", control->lexical_level, control->address);

    parser_expect(parser, TOK_TO);
    /* Limite e passo precisam sobreviver enquanto o corpo e repetido. */
    int temp_level = current_storage_level(parser);
    int limit_address = parser->next_local_address++;
    int step_address = parser->next_local_address++;
    parser->active_temp_count += 2;
    emit1i(NULL, "AMEM", 2);

    SalType limit = parse_expression(parser);
    require_type(limit, SAL_TYPE_INT, line, "limite do for");
    emit2i(NULL, "ARMZ", temp_level, limit_address);

    /* Sem clausula step, o incremento padrao da SAL e +1. */
    if (parser_accept(parser, TOK_STEP))
        parse_step_value(parser);
    else
        emit1i(NULL, "CRCT", 1);
    emit2i(NULL, "ARMZ", temp_level, step_address);
    parser_expect(parser, TOK_DO);

    char test_label[MEPA_MAX_LABEL];
    char nonpositive_label[MEPA_MAX_LABEL];
    char body_label[MEPA_MAX_LABEL];
    char end_label[MEPA_MAX_LABEL];
    new_label(test_label);
    new_label(nonpositive_label);
    new_label(body_label);
    new_label(end_label);

    emit_label(test_label);
    emit2i(NULL, "CRVL", temp_level, step_address);
    emit1i(NULL, "CRCT", 0);
    emit0(NULL, "CMMA");
    emit1(NULL, "DSVF", nonpositive_label);

    /* Passo positivo: continua enquanto controle <= limite. */
    emit2i(NULL, "CRVL", control->lexical_level, control->address);
    emit2i(NULL, "CRVL", temp_level, limit_address);
    emit0(NULL, "CMEG");
    emit1(NULL, "DSVF", end_label);
    emit1(NULL, "DSVS", body_label);

    /* Passo negativo: continua enquanto controle >= limite. */
    emit_label(nonpositive_label);
    emit2i(NULL, "CRVL", temp_level, step_address);
    emit1i(NULL, "CRCT", 0);
    emit0(NULL, "CMME");
    emit1(NULL, "DSVF", end_label); /* passo dinamico igual a zero */
    emit2i(NULL, "CRVL", control->lexical_level, control->address);
    emit2i(NULL, "CRVL", temp_level, limit_address);
    emit0(NULL, "CMAG");
    emit1(NULL, "DSVF", end_label);

    emit_label(body_label);
    parse_command(parser);
    emit2i(NULL, "CRVL", control->lexical_level, control->address);
    emit2i(NULL, "CRVL", temp_level, step_address);
    emit0(NULL, "SOMA");
    emit2i(NULL, "ARMZ", control->lexical_level, control->address);
    emit1(NULL, "DSVS", test_label);

    emit_label(end_label);
    emit1i(NULL, "DMEM", 2);
    parser->next_local_address -= 2;
    parser->active_temp_count -= 2;
    trace_leave("for");
}

/* step aceita constante ou variavel inteira; o sinal negativo e opcional. */
static void parse_step_value(Parser *parser)
{
    int line = parser->current.line;
    int negative = parser_accept(parser, TOK_MINUS);

    if (parser->current.kind == TOK_INT_LITERAL)
    {
        int value = (int)strtol(parser->current.lexeme, NULL, 10);
        if (value == 0)
            semantic_error(line, "passo do for nao pode ser zero");
        parser_advance(parser);
        emit1i(NULL, "CRCT", negative ? -value : value);
        return;
    }

    if (parser->current.kind == TOK_IDENT)
    {
        char name[SAL_MAX_IDENTIFIER];
        copy_text(name, sizeof(name), parser->current.lexeme);
        int name_line = parser->current.line;
        parser_advance(parser);

        Symbol *symbol = lookup_symbol(parser, name, name_line);
        require_variable(symbol, name, name_line);
        require_type(symbol->type, SAL_TYPE_INT, name_line, "passo do for");
        emit2i(NULL, "CRVL", symbol->lexical_level, symbol->address);
        if (negative)
            emit0(NULL, "INVR");
        return;
    }

    syntax_error(parser, "sIDENTIF ou sCTEINT apos sSTEP");
}

static void parse_loop(Parser *parser)
{
    trace_enter("loop");
    int line = parser->current.line;
    parser_expect(parser, TOK_LOOP);

    if (parser_accept(parser, TOK_WHILE))
    {
        /* loop while testa antes de executar o corpo. */
        char start_label[MEPA_MAX_LABEL];
        char end_label[MEPA_MAX_LABEL];
        new_label(start_label);
        new_label(end_label);
        emit_label(start_label);

        parser_expect(parser, TOK_LPAREN);
        SalType condition = parse_expression(parser);
        require_type(condition, SAL_TYPE_BOOL, line, "condicao do loop while");
        parser_expect(parser, TOK_RPAREN);
        emit1(NULL, "DSVF", end_label);
        parse_command(parser);
        emit1(NULL, "DSVS", start_label);
        emit_label(end_label);
        trace_leave("loop");
        return;
    }

    /* Sem while, trata-se de loop ... until: o teste ocorre ao final. */
    char start_label[MEPA_MAX_LABEL];
    new_label(start_label);
    emit_label(start_label);
    while (parser->current.kind != TOK_UNTIL && parser->current.kind != TOK_EOF)
    {
        parse_command(parser);
        parser_expect(parser, TOK_SEMICOLON);
    }
    parser_expect(parser, TOK_UNTIL);
    parser_expect(parser, TOK_LPAREN);
    SalType condition = parse_expression(parser);
    require_type(condition, SAL_TYPE_BOOL, line, "condicao do loop until");
    parser_expect(parser, TOK_RPAREN);
    emit1(NULL, "DSVF", start_label);
    trace_leave("loop");
}

static void parse_return(Parser *parser)
{
    trace_enter("return");
    int line = parser->current.line;
    parser_expect(parser, TOK_RET);
    SalType type = parse_expression(parser);

    if (!parser->active_routine ||
        parser->active_routine->category != SYMBOL_FUNCTION)
    {
        semantic_error(line, "comando ret permitido apenas em funcoes");
    }
    require_type(type, parser->active_routine->type, line, "retorno da funcao");

    parser->active_function_saw_return = 1;
    /* O resultado ocupa a celula reservada pelo chamador abaixo dos parametros. */
    emit2i(NULL, "ARMZ", 1,
           -(int)(parser->active_routine->parameter_count + 5));
    /* Retorno antecipado libera temporarios de for e match ainda ativos. */
    if (parser->active_temp_count > 0)
    {
        emit1i(NULL, "DMEM", parser->active_temp_count);
    }
    emit1(NULL, "DSVS", parser->active_return_label);
    trace_leave("return");
}

/* Apos um identificador, := indica atribuicao e ( indica chamada de proc. */
static void parse_ident_command(Parser *parser)
{
    trace_enter("ident_command");
    char name[SAL_MAX_IDENTIFIER];
    copy_text(name, sizeof(name), parser->current.lexeme);
    int line = parser->current.line;
    parser_expect(parser, TOK_IDENT);

    if (parser_accept(parser, TOK_ASSIGN))
    {
        Symbol *symbol = lookup_symbol(parser, name, line);
        require_variable(symbol, name, line);
        SalType type = parse_expression(parser);
        require_type(type, symbol->type, line, "atribuicao");
        emit2i(NULL, "ARMZ", symbol->lexical_level, symbol->address);
        trace_leave("ident_command");
        return;
    }

    if (parser_accept(parser, TOK_LPAREN))
    {
        Symbol *symbol = lookup_symbol(parser, name, line);
        if (symbol->category != SYMBOL_PROCEDURE)
        {
            semantic_error(line,
                           "chamada como comando exige procedimento; '%s' e %s",
                           name, symbol_category_name(symbol->category));
        }
        ArgumentList args = parse_optional_arguments(parser);
        parser_expect(parser, TOK_RPAREN);
        validate_call(symbol, &args, line);
        emit2(NULL, "CHPR", symbol->label, "1");
        trace_leave("ident_command");
        return;
    }

    syntax_error(parser, "sATRIB ou sABREPAR apos identificador");
}

/* Lista vazia e valida; tipos sao guardados para comparar com a assinatura. */
static ArgumentList parse_optional_arguments(Parser *parser)
{
    ArgumentList args = {0};
    if (parser->current.kind == TOK_RPAREN)
        return args;

    do
    {
        if (args.count == SAL_MAX_ARGS)
        {
            semantic_error(parser->current.line,
                           "chamada excede o limite de %d argumentos",
                           SAL_MAX_ARGS);
        }
        args.types[args.count++] = parse_expression(parser);
    } while (parser_accept(parser, TOK_COMMA));
    return args;
}

/* As camadas abaixo implementam diretamente a precedencia dos operadores. */
static SalType parse_expression(Parser *parser)
{
    trace_enter("expression");
    SalType type = parse_or(parser);
    trace_leave("expression");
    return type;
}

static SalType parse_or(Parser *parser)
{
    SalType left = parse_and(parser);
    while (parser->current.kind == TOK_OR)
    {
        int line = parser->current.line;
        parser_advance(parser);
        SalType right = parse_and(parser);
        require_type(left, SAL_TYPE_BOOL, line, "operando esquerdo de v");
        require_type(right, SAL_TYPE_BOOL, line, "operando direito de v");
        emit0(NULL, "DISJ");
        left = SAL_TYPE_BOOL;
    }
    return left;
}

static SalType parse_and(Parser *parser)
{
    SalType left = parse_relation(parser);
    while (parser->current.kind == TOK_AND)
    {
        int line = parser->current.line;
        parser_advance(parser);
        SalType right = parse_relation(parser);
        require_type(left, SAL_TYPE_BOOL, line, "operando esquerdo de ^");
        require_type(right, SAL_TYPE_BOOL, line, "operando direito de ^");
        emit0(NULL, "CONJ");
        left = SAL_TYPE_BOOL;
    }
    return left;
}

static int is_relational_operator(TokenKind kind)
{
    return kind == TOK_EQ || kind == TOK_NE || kind == TOK_GT ||
           kind == TOK_LT || kind == TOK_GE || kind == TOK_LE;
}

static SalType parse_relation(Parser *parser)
{
    SalType left = parse_addition(parser);
    while (is_relational_operator(parser->current.kind))
    {
        TokenKind op = parser->current.kind;
        int line = parser->current.line;
        parser_advance(parser);
        SalType right = parse_addition(parser);

        if (op == TOK_EQ || op == TOK_NE)
        {
            if (left == SAL_TYPE_STRING || right == SAL_TYPE_STRING ||
                left == SAL_TYPE_VOID || right == SAL_TYPE_VOID ||
                left != right)
            {
                semantic_error(line,
                               "comparacao exige operandos escalares do mesmo tipo; "
                               "encontrados %s e %s",
                               sal_type_name(left), sal_type_name(right));
            }
        }
        else
        {
            require_type(left, SAL_TYPE_INT, line, "operando esquerdo relacional");
            require_type(right, SAL_TYPE_INT, line, "operando direito relacional");
        }

        /* Traduz cada operador SAL para o mnemonico relacional equivalente. */
        switch (op)
        {
        case TOK_EQ:
            emit0(NULL, "CMIG");
            break;
        case TOK_NE:
            emit0(NULL, "CMDG");
            break;
        case TOK_GT:
            emit0(NULL, "CMMA");
            break;
        case TOK_LT:
            emit0(NULL, "CMME");
            break;
        case TOK_GE:
            emit0(NULL, "CMAG");
            break;
        case TOK_LE:
            emit0(NULL, "CMEG");
            break;
        default:
            break;
        }
        left = SAL_TYPE_BOOL;
    }
    return left;
}

static SalType parse_addition(Parser *parser)
{
    SalType left = parse_multiplication(parser);
    while (parser->current.kind == TOK_PLUS || parser->current.kind == TOK_MINUS)
    {
        TokenKind op = parser->current.kind;
        int line = parser->current.line;
        parser_advance(parser);
        SalType right = parse_multiplication(parser);
        require_type(left, SAL_TYPE_INT, line, "operando esquerdo aritmetico");
        require_type(right, SAL_TYPE_INT, line, "operando direito aritmetico");
        emit0(NULL, op == TOK_PLUS ? "SOMA" : "SUBT");
        left = SAL_TYPE_INT;
    }
    return left;
}

static SalType parse_multiplication(Parser *parser)
{
    SalType left = parse_factor(parser);
    while (parser->current.kind == TOK_MULT || parser->current.kind == TOK_DIV)
    {
        TokenKind op = parser->current.kind;
        int line = parser->current.line;
        parser_advance(parser);
        SalType right = parse_factor(parser);
        require_type(left, SAL_TYPE_INT, line, "operando esquerdo aritmetico");
        require_type(right, SAL_TYPE_INT, line, "operando direito aritmetico");
        emit0(NULL, op == TOK_MULT ? "MULT" : "DIVI");
        left = SAL_TYPE_INT;
    }
    return left;
}

/* Operadores unarios chamam factor recursivamente: associatividade a direita. */
static SalType parse_factor(Parser *parser)
{
    if (parser->current.kind == TOK_NOT)
    {
        int line = parser->current.line;
        parser_advance(parser);
        SalType type = parse_factor(parser);
        require_type(type, SAL_TYPE_BOOL, line, "operando de ~");
        emit0(NULL, "NEGA");
        return SAL_TYPE_BOOL;
    }
    if (parser->current.kind == TOK_MINUS)
    {
        int line = parser->current.line;
        parser_advance(parser);
        SalType type = parse_factor(parser);
        require_type(type, SAL_TYPE_INT, line, "operando de - unario");
        emit0(NULL, "INVR");
        return SAL_TYPE_INT;
    }
    if (parser_accept(parser, TOK_LPAREN))
    {
        SalType type = parse_expression(parser);
        parser_expect(parser, TOK_RPAREN);
        return type;
    }
    return parse_primary(parser);
}

static SalType parse_primary(Parser *parser)
{
    switch (parser->current.kind)
    {
    case TOK_INT_LITERAL:
    {
        int value = (int)strtol(parser->current.lexeme, NULL, 10);
        parser_advance(parser);
        emit1i(NULL, "CRCT", value);
        return SAL_TYPE_INT;
    }
    case TOK_CHAR_LITERAL:
    {
        int value = (unsigned char)parser->current.lexeme[1];
        parser_advance(parser);
        emit1i(NULL, "CRCT", value);
        return SAL_TYPE_CHAR;
    }
    case TOK_STRING_LITERAL:
        /* A MEPA nao possui instrucao para imprimir strings. */
        parser_advance(parser);
        return SAL_TYPE_STRING;
    case TOK_TRUE:
        parser_advance(parser);
        emit1i(NULL, "CRCT", 1);
        return SAL_TYPE_BOOL;
    case TOK_FALSE:
        parser_advance(parser);
        emit1i(NULL, "CRCT", 0);
        return SAL_TYPE_BOOL;
    case TOK_IDENT:
    {
        char name[SAL_MAX_IDENTIFIER];
        copy_text(name, sizeof(name), parser->current.lexeme);
        int line = parser->current.line;
        parser_advance(parser);

        Symbol *symbol = lookup_symbol(parser, name, line);
        if (parser_accept(parser, TOK_LPAREN))
        {
            if (symbol->category != SYMBOL_FUNCTION)
            {
                semantic_error(line,
                               "chamada em expressao exige funcao; '%s' e %s",
                               name, symbol_category_name(symbol->category));
            }
            /* O chamador abre uma celula onde a funcao gravara o resultado. */
            emit1i(NULL, "AMEM", 1);
            ArgumentList args = parse_optional_arguments(parser);
            parser_expect(parser, TOK_RPAREN);
            validate_call(symbol, &args, line);
            emit2(NULL, "CHPR", symbol->label, "1");
            return symbol->type;
        }

        require_variable(symbol, name, line);
        emit2i(NULL, "CRVL", symbol->lexical_level, symbol->address);
        return symbol->type;
    }
    default:
        syntax_error(parser, "elemento de expressao valido");
        return SAL_TYPE_INVALID; /* Uso interno */
    }
}
