/* Tipos compartilhados pelos modulos do compilador. */

#ifndef SAL_TYPES_H
#define SAL_TYPES_H

/*
 * Tipos primitivos declaraveis na versao entregue da SAL. SAL_TYPE_STRING
 * e um tipo auxiliar: strings podem aparecer em print, mas nao podem ser
 * armazenadas em variaveis nem usadas em operadores.
 */
typedef enum {
    SAL_TYPE_INT,
    SAL_TYPE_BOOL,
    SAL_TYPE_CHAR,
    SAL_TYPE_VOID,
    SAL_TYPE_STRING,
    SAL_TYPE_INVALID
} SalType;

/* Nome estavel usado em diagnosticos e no arquivo .ts. */
const char *sal_type_name(SalType type);

#endif /* SAL_TYPES_H */
