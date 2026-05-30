/*
 * types.c - Conversao dos tipos internos para nomes legiveis
 *
 * A representacao comum de tipos evita que lexer, parser, tabela de simbolos
 * e diagnosticos usem convencoes diferentes para int, bool, char e void.
 */

#include "types.h"

/* Mantem uma representacao textual unica para logs e diagnosticos. */
const char *sal_type_name(SalType type) {
    switch (type) {
        case SAL_TYPE_INT:     return "int";
        case SAL_TYPE_BOOL:    return "bool";
        case SAL_TYPE_CHAR:    return "char";
        case SAL_TYPE_VOID:    return "-";
        case SAL_TYPE_STRING:  return "string";
        case SAL_TYPE_INVALID: return "invalido";
        default:               return "invalido";
    }
}
