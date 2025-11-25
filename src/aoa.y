%{
/*
 * AOA Parser - Parser for Arithmetic Optimization Algebra
 * Uses Yacc (bison) for syntax analysis and semantic validation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"
#include "error.h"

extern int yylex();
extern int yylineno;
extern char *yytext;
extern FILE *yyin;

void yyerror(const char *s);

int error_count = 0;
int in_constraint_section = 0;

%}

%union {
    char *str_val;
    int int_val;
}

%token DECL PRIVATE PUBLIC DEFERRED
%token EQ_OP ASSIGN PLUS MINUS MULT
%token LBRACKET RBRACKET COMMA
%token <str_val> IDENTIFIER NUMBER
%token NEWLINE ERROR

%type <str_val> visibility
%type <int_val> array_size

%%

program:
      /* empty */
    | program line
    ;

line:
      NEWLINE                        { /* Empty line */ }
    | declaration NEWLINE            { /* Valid declaration */ }
    | constraint NEWLINE             { /* Valid constraint */ }
    | error NEWLINE                  { yyerrok; error_count++; }
    ;

/* ==================== DECLARATIONS ==================== */

declaration:
      DECL visibility var_list
        {
            if (in_constraint_section) {
                error_report(yylineno,
                    "Declaration after constraints - all declarations must come first");
                error_count++;
            }
        }
    ;

visibility:
      PRIVATE     { $$ = strdup("private"); }
    | PUBLIC      { $$ = strdup("public"); }
    | DEFERRED    { $$ = strdup("deferred"); }
    ;

var_list:
      var_decl
    | var_list COMMA var_decl
    ;

var_decl:
      IDENTIFIER
        {
            /* Scalar variable */
            if (symbol_lookup($1)) {
                error_report(yylineno, "Variable '%s' already declared", $1);
                error_count++;
            } else {
                symbol_add($1, SYMBOL_SCALAR, 0);
            }
            free($1);
        }
    | IDENTIFIER LBRACKET array_size RBRACKET
        {
            /* Array variable */
            if (symbol_lookup($1)) {
                error_report(yylineno, "Variable '%s' already declared", $1);
                error_count++;
            } else {
                symbol_add($1, SYMBOL_ARRAY, $3);
            }
            free($1);
        }
    ;

array_size:
      NUMBER
        {
            $$ = atoi($1);
            if ($$ <= 0) {
                error_report(yylineno, "Array size must be positive, got %d", $$);
                error_count++;
                $$ = 1; /* Default to avoid further errors */
            }
            free($1);
        }
    ;

/* ==================== CONSTRAINTS ==================== */

constraint:
      IDENTIFIER ASSIGN expression EQ_OP expression
        {
            /* Equality constraint: var = expr1 == expr2 */
            in_constraint_section = 1;

            symbol_t *sym = symbol_lookup($1);
            if (sym) {
                /* Variable exists - check if already assigned */
                if (symbol_is_assigned($1)) {
                    /* Already assigned (violates single assignment rule) */
                    error_report(yylineno,
                        "Variable '%s' already assigned (single assignment rule)", $1);
                    error_count++;
                } else {
                    /* First assignment - mark as assigned */
                    symbol_mark_assigned($1);
                }
            } else {
                /* New gate variable - create and mark as assigned */
                symbol_add_with_origin($1, SYMBOL_SCALAR, 0, SYMBOL_GATE);
                symbol_mark_assigned($1);
            }
            free($1);
        }
    | IDENTIFIER ASSIGN expression
        {
            /* Regular assignment: var = expr */
            in_constraint_section = 1;

            symbol_t *sym = symbol_lookup($1);
            if (sym) {
                /* Variable exists - check if already assigned */
                if (symbol_is_assigned($1)) {
                    /* Already assigned (violates single assignment rule) */
                    error_report(yylineno,
                        "Variable '%s' already assigned (single assignment rule)", $1);
                    error_count++;
                } else {
                    /* First assignment - mark as assigned */
                    symbol_mark_assigned($1);
                }
            } else {
                /* New gate variable - create and mark as assigned */
                symbol_add_with_origin($1, SYMBOL_SCALAR, 0, SYMBOL_GATE);
                symbol_mark_assigned($1);
            }
            free($1);
        }
    | expression EQ_OP expression
        {
            /* Standalone equality constraint (e.g., no_borrow == result) */
            /* This creates an implicit constraint without explicit assignment */
            in_constraint_section = 1;
        }
    | IDENTIFIER LBRACKET NUMBER RBRACKET ASSIGN expression
        {
            /* Assignment to array element - NOT ALLOWED */
            /* Array elements cannot be assigned individually in AOA */
            in_constraint_section = 1;

            symbol_t *sym = symbol_lookup($1);
            if (!sym) {
                error_report(yylineno,
                    "Variable '%s' used before declaration", $1);
                error_count++;
            } else if (sym->type != SYMBOL_ARRAY) {
                error_report(yylineno,
                    "Scalar variable '%s' cannot be indexed", $1);
                error_count++;
            } else {
                /* Array elements cannot be assigned */
                int index = atoi($3);
                error_report(yylineno,
                    "Cannot assign to individual array element '%s[%d]' - arrays can only be declared as inputs",
                    $1, index);
                error_count++;
            }
            free($1);
            free($3);
        }
    ;

expression:
      term
    | expression PLUS term
    | expression MINUS term
    | term MULT term
    | error                          { /* Error recovery for malformed expressions */ }
    ;

term:
      IDENTIFIER
        {
            symbol_t *sym = symbol_lookup($1);
            if (!sym) {
                error_report(yylineno,
                    "Variable '%s' not declared or assigned yet", $1);
                error_count++;
            } else if (sym->type == SYMBOL_ARRAY) {
                error_report(yylineno,
                    "Array '%s' used without index - must use %s[index]",
                    $1, $1);
                error_count++;
            } else if (sym->origin == SYMBOL_GATE && !sym->assigned) {
                /* Gate variable created but not assigned yet */
                error_report(yylineno,
                    "Gate variable '%s' used before it is assigned a value", $1);
                error_count++;
            }
            free($1);
        }
    | IDENTIFIER LBRACKET NUMBER RBRACKET
        {
            symbol_t *sym = symbol_lookup($1);
            if (!sym) {
                error_report(yylineno,
                    "Variable '%s' not declared or assigned yet", $1);
                error_count++;
            } else if (sym->type == SYMBOL_SCALAR) {
                error_report(yylineno,
                    "Scalar variable '%s' cannot be indexed", $1);
                error_count++;
            } else {
                /* Check bounds */
                int index = atoi($3);
                if (index < 0 || index >= sym->size) {
                    error_report(yylineno,
                        "Array index %d out of bounds for '%s' (size %d)",
                        index, $1, sym->size);
                    error_count++;
                }
            }
            free($1);
            free($3);
        }
    | NUMBER
        {
            free($1);
        }
    ;

%%

void yyerror(const char *s) {
    error_report(yylineno, "Syntax error: %s", s);
    error_count++;
}
