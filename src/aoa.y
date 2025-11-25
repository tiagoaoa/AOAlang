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

/* Semantic action helper functions */
void handle_constraint_assignment(const char *var_name);
void handle_array_element_assignment(const char *array_name, const char *index_str);
void validate_variable_usage(const char *var_name);
void validate_array_access(const char *array_name, const char *index_str);

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
/*
 * All declarations must appear before constraints.
 * Variables can be declared as:
 *   - Scalars: decl private x, y, z
 *   - Arrays:  decl public a[4], b[8]
 *   - Mixed:   decl deferred x, data[16], y
 *
 * Visibility types:
 *   - private:  Secret witness values
 *   - public:   Public inputs (checked)
 *   - deferred: Symbolic/dynamic public inputs (for GB elimination)
 */

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
            /* Scalar variable: decl private x */
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
            /* Array variable: decl private a[4] */
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
/*
 * Constraints represent gates in the R1CS system.
 * Each constraint creates exactly one gate constraint.
 *
 * Forms:
 *   1. var = expr                    - Regular gate (addition, subtraction, multiplication)
 *   2. var = expr1 == expr2          - Equality constraint (enforces expr1 - expr2 = 0)
 *   3. expr1 == expr2                - Standalone equality (no explicit result variable)
 *   4. array[i] = expr               - ERROR: Not allowed in AOA
 */

constraint:
      IDENTIFIER ASSIGN expression EQ_OP expression
        {
            /* Equality constraint: var = expr1 == expr2 */
            handle_constraint_assignment($1);
            free($1);
        }
    | IDENTIFIER ASSIGN expression
        {
            /* Regular assignment: var = expr */
            handle_constraint_assignment($1);
            free($1);
        }
    | expression EQ_OP expression
        {
            /* Standalone equality constraint (e.g., no_borrow == result) */
            in_constraint_section = 1;
        }
    | IDENTIFIER LBRACKET NUMBER RBRACKET ASSIGN expression
        {
            /* Assignment to array element - NOT ALLOWED in AOA */
            handle_array_element_assignment($1, $3);
            free($1);
            free($3);
        }
    ;

/*
 * Expressions - ONE operation per line for R1CS compatibility
 * Multi-operation expressions must be split using intermediate variables.
 *
 * Valid:   temp = a + b; result = temp + c
 * Invalid: result = a + b + c
 */

expression:
      term
    | expression PLUS term           { /* Addition gate */ }
    | expression MINUS term          { /* Subtraction gate */ }
    | term MULT term                 { /* Multiplication gate */ }
    | error                          { /* Error recovery for malformed expressions */ }
    ;

/*
 * Terms - atomic operands in expressions
 * Can be: scalar variable, array element, or constant
 */

term:
      IDENTIFIER
        {
            /* Scalar variable reference */
            validate_variable_usage($1);
            free($1);
        }
    | IDENTIFIER LBRACKET NUMBER RBRACKET
        {
            /* Array element access */
            validate_array_access($1, $3);
            free($1);
            free($3);
        }
    | NUMBER
        {
            /* Numeric constant */
            free($1);
        }
    ;

%%

/*
 * Semantic Action Helper Functions
 * These functions encapsulate the semantic validation logic
 * to keep the grammar rules clean and readable.
 */

/**
 * handle_constraint_assignment - Process assignment to a variable in a constraint
 * @var_name: Name of the variable being assigned
 *
 * Handles both first-time assignments to declared variables and creation of
 * new gate variables. Enforces single assignment rule.
 */
void handle_constraint_assignment(const char *var_name) {
    in_constraint_section = 1;

    symbol_t *sym = symbol_lookup(var_name);
    if (sym) {
        /* Variable exists - check if already assigned */
        if (symbol_is_assigned(var_name)) {
            /* Already assigned (violates single assignment rule) */
            error_report(yylineno,
                "Variable '%s' already assigned (single assignment rule)", var_name);
            error_count++;
        } else {
            /* First assignment - mark as assigned */
            symbol_mark_assigned(var_name);
        }
    } else {
        /* New gate variable - create and mark as assigned */
        symbol_add_with_origin(var_name, SYMBOL_SCALAR, 0, SYMBOL_GATE);
        symbol_mark_assigned(var_name);
    }
}

/**
 * handle_array_element_assignment - Process assignment to array element
 * @array_name: Name of the array
 * @index_str: String representation of the index
 *
 * Array element assignment is not allowed in AOA - arrays can only be
 * declared as inputs. This function reports appropriate errors.
 */
void handle_array_element_assignment(const char *array_name, const char *index_str) {
    in_constraint_section = 1;

    symbol_t *sym = symbol_lookup(array_name);
    if (!sym) {
        error_report(yylineno,
            "Variable '%s' not declared or assigned yet", array_name);
        error_count++;
    } else if (sym->type != SYMBOL_ARRAY) {
        error_report(yylineno,
            "Scalar variable '%s' cannot be indexed", array_name);
        error_count++;
    } else {
        /* Array elements cannot be assigned */
        int index = atoi(index_str);
        error_report(yylineno,
            "Cannot assign to individual array element '%s[%d]' - arrays can only be declared as inputs",
            array_name, index);
        error_count++;
    }
}

/**
 * validate_variable_usage - Validate use of a scalar variable in expression
 * @var_name: Name of the variable being used
 *
 * Checks that:
 * - Variable has been declared or assigned
 * - Variable is a scalar (not an array)
 * - If it's a gate variable, it has been assigned
 */
void validate_variable_usage(const char *var_name) {
    symbol_t *sym = symbol_lookup(var_name);

    if (!sym) {
        error_report(yylineno,
            "Variable '%s' not declared or assigned yet", var_name);
        error_count++;
    } else if (sym->type == SYMBOL_ARRAY) {
        error_report(yylineno,
            "Array '%s' used without index - must use %s[index]",
            var_name, var_name);
        error_count++;
    } else if (sym->origin == SYMBOL_GATE && !sym->assigned) {
        /* Gate variable created but not assigned yet */
        error_report(yylineno,
            "Gate variable '%s' used before it is assigned a value", var_name);
        error_count++;
    }
}

/**
 * validate_array_access - Validate array element access
 * @array_name: Name of the array
 * @index_str: String representation of the index
 *
 * Checks that:
 * - Array has been declared
 * - Variable is actually an array (not a scalar)
 * - Index is within bounds
 */
void validate_array_access(const char *array_name, const char *index_str) {
    symbol_t *sym = symbol_lookup(array_name);

    if (!sym) {
        error_report(yylineno,
            "Variable '%s' not declared or assigned yet", array_name);
        error_count++;
    } else if (sym->type == SYMBOL_SCALAR) {
        error_report(yylineno,
            "Scalar variable '%s' cannot be indexed", array_name);
        error_count++;
    } else {
        /* Check bounds */
        int index = atoi(index_str);
        if (index < 0 || index >= sym->size) {
            error_report(yylineno,
                "Array index %d out of bounds for '%s' (size %d)",
                index, array_name, sym->size);
            error_count++;
        }
    }
}

void yyerror(const char *s) {
    error_report(yylineno, "Syntax error: %s", s);
    error_count++;
}
