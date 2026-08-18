%{
/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     aoa.y
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"
#include "error.h"
#include "r1cs.h"

extern int yylex();
extern int yylineno;
extern char *yytext;
extern FILE *yyin;

void yyerror(const char *s);

void handle_constraint_assignment(const char *var_name);
void handle_array_element_assignment(const char *array_name, const char *index_str);
void validate_variable_usage(const char *var_name);
void validate_array_access(const char *array_name, const char *index_str);


/* ----------------------------
        GLOBAL VARIABLES       */

int generate_r1cs = 0;	//R1CS generation mode flag, set from main
int error_count = 0;
int in_constraint_section = 0;

/*
 * Expressions are limited to one operation per line, so tracking the
 * operands and the operator of the current line is enough to build the
 * R1CS constraint when the line is reduced.
 */
#define MAX_OPERANDS 8
static char *expr_opers[MAX_OPERANDS];
static int expr_n_opers = 0;
static char expr_op = 0;		//'+', '-', '*' or 'E' for ==
static char *cur_lhs = NULL;

/*----------------------------*/


static void expr_reset(void) {
	int i;

	for (i = 0; i < expr_n_opers; i++)
		if (expr_opers[i]) {
			free(expr_opers[i]);
			expr_opers[i] = NULL;
		}
	expr_n_opers = 0;
	expr_op = 0;
	if (cur_lhs) {
		free(cur_lhs);
		cur_lhs = NULL;
	}
}

static void expr_add_operand(const char *name) {
	if (generate_r1cs && expr_n_opers < MAX_OPERANDS)
		expr_opers[expr_n_opers++] = strdup(name);
}

static void expr_set_op(char op) {
	if (generate_r1cs)
		expr_op = op;
}

static void expr_set_lhs(const char *name) {
	if (generate_r1cs) {
		if (cur_lhs) free(cur_lhs);
		cur_lhs = strdup(name);
	}
}

static int is_number(const char *p) {
	if (*p == '-') p++;
	if (*p < '0' || *p > '9')
		return 0;
	while (*p >= '0' && *p <= '9') p++;
	return (*p == '\0' || *p == '.');
}

static void generate_constraint(void) {
	int op_idx, res_idx;

	if (!generate_r1cs || !cur_lhs || expr_n_opers < 1)
		return;

	if (expr_op == '*' && expr_n_opers >= 2) {
		r1cs_add_mul_constraint(cur_lhs, expr_opers[0], expr_opers[1]);
	} else if (expr_op == '+' && expr_n_opers >= 2) {
		r1cs_add_add_constraint(cur_lhs, expr_opers[0], expr_opers[1]);
	} else if (expr_op == '-' && expr_n_opers >= 2) {
		r1cs_add_sub_constraint(cur_lhs, expr_opers[0], expr_opers[1]);
	} else if (expr_op == 'E' && expr_n_opers >= 2) {
		r1cs_add_eq_constraint(cur_lhs, expr_opers[0], expr_opers[1]);
	} else if (expr_n_opers == 1) {
		if (is_number(expr_opers[0])) {
			r1cs_add_const_constraint(cur_lhs, expr_opers[0]);
		} else {
			//identity: lhs = operand, as (operand * 1 = lhs)
			r1cs_begin_constraint(cur_lhs, cur_lhs);
			op_idx = r1cs_get_witness_index(expr_opers[0]);
			res_idx = r1cs_get_witness_index(cur_lhs);
			if (op_idx >= 0 && res_idx >= 0) {
				r1cs_add_A(op_idx, "1");
				r1cs_add_B(0, "1");
				r1cs_add_C(res_idx, "1");
			}
			r1cs_end_constraint();
			r1cs_set_gate_expr(cur_lhs, expr_opers[0]);
		}
	}
}

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
      NEWLINE
    | declaration NEWLINE
    | constraint NEWLINE
    | error NEWLINE                  { yyerrok; error_count++; }
    ;

/* ==================== DECLARATIONS ==================== */
/*
 * All declarations must appear before constraints.
 *   Scalars: decl private x, y, z
 *   Arrays:  decl public a[4], b[8]
 *   Mixed:   decl deferred x, data[16], y
 *
 * private is a secret witness, public is a checked public input and
 * deferred is a symbolic/dynamic public input (for GB elimination).
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
      PRIVATE     { $$ = strdup("private"); symbol_set_current_visibility(VISIBILITY_PRIVATE); }
    | PUBLIC      { $$ = strdup("public"); symbol_set_current_visibility(VISIBILITY_PUBLIC); }
    | DEFERRED    { $$ = strdup("deferred"); symbol_set_current_visibility(VISIBILITY_DEFERRED); }
    ;

var_list:
      var_decl
    | var_list COMMA var_decl
    ;

var_decl:
      IDENTIFIER
        {
		if (symbol_lookup($1)) {
			error_report(yylineno, "Variable '%s' already declared", $1);
			error_count++;
		} else {
			symbol_add($1, SYMBOL_SCALAR, 0);
			if (generate_r1cs) {
				symbol_t *sym = symbol_lookup($1);
				if (sym)
					r1cs_register_witness($1, sym->witness_index,
					                      sym->visibility, sym->origin,
					                      sym->type, 0);
			}
		}
		free($1);
        }
    | IDENTIFIER LBRACKET array_size RBRACKET
        {
		if (symbol_lookup($1)) {
			error_report(yylineno, "Variable '%s' already declared", $1);
			error_count++;
		} else {
			symbol_add($1, SYMBOL_ARRAY, $3);
			if (generate_r1cs) {
				symbol_t *sym = symbol_lookup($1);
				if (sym)
					r1cs_register_witness($1, sym->witness_index,
					                      sym->visibility, sym->origin,
					                      sym->type, $3);
			}
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
			$$ = 1;	//default to avoid further errors
		}
		free($1);
        }
    ;

/* ==================== CONSTRAINTS ==================== */
/*
 * Each constraint line creates exactly one gate in the R1CS system.
 *   1. var = expr             - regular gate (add, sub, mul)
 *   2. var = expr1 == expr2   - equality constraint (enforces expr1 - expr2 = 0)
 *   3. expr1 == expr2         - standalone equality, no explicit result variable
 *   4. array[i] = expr        - ERROR: not allowed in AOA
 */

constraint:
      IDENTIFIER ASSIGN expression EQ_OP expression
        {
		handle_constraint_assignment($1);
		expr_set_lhs($1);
		expr_set_op('E');
		generate_constraint();
		expr_reset();
		free($1);
        }
    | IDENTIFIER ASSIGN expression
        {
		handle_constraint_assignment($1);
		expr_set_lhs($1);
		generate_constraint();
		expr_reset();
		free($1);
        }
    | expression EQ_OP expression
        {
		//standalone equality gets an implicit gate variable as its lhs
		in_constraint_section = 1;
		if (generate_r1cs && expr_n_opers >= 2) {
			static int eq_counter = 0;
			char implicit_name[64];
			snprintf(implicit_name, sizeof(implicit_name), "_eq_%d", eq_counter++);
			symbol_add_with_origin(implicit_name, SYMBOL_SCALAR, 0, SYMBOL_GATE);
			symbol_mark_assigned(implicit_name);
			symbol_t *sym = symbol_lookup(implicit_name);
			if (sym)
				r1cs_register_witness(implicit_name, sym->witness_index,
				                      VISIBILITY_PRIVATE, SYMBOL_GATE,
				                      SYMBOL_SCALAR, 0);
			expr_set_lhs(implicit_name);
			expr_set_op('E');
			generate_constraint();
		}
		expr_reset();
        }
    | IDENTIFIER LBRACKET NUMBER RBRACKET ASSIGN expression
        {
		handle_array_element_assignment($1, $3);
		expr_reset();
		free($1);
		free($3);
        }
    ;

/*
 * Expressions carry ONE operation per line for R1CS compatibility.
 * Multi-operation expressions must be split using intermediate variables:
 *   Valid:   temp = a + b; result = temp + c
 *   Invalid: result = a + b + c
 */

expression:
      term
    | expression PLUS term           { expr_set_op('+'); }
    | expression MINUS term          { expr_set_op('-'); }
    | term MULT term                 { expr_set_op('*'); }
    | error
    ;

term:
      IDENTIFIER
        {
		validate_variable_usage($1);
		expr_add_operand($1);
		free($1);
        }
    | IDENTIFIER LBRACKET NUMBER RBRACKET
        {
		validate_array_access($1, $3);
		char buf[256];
		snprintf(buf, sizeof(buf), "%s[%s]", $1, $3);
		expr_add_operand(buf);
		free($1);
		free($3);
        }
    | NUMBER
        {
		expr_add_operand($1);
		free($1);
        }
    ;

%%

//assignments create gate variables on first use and enforce single assignment
void handle_constraint_assignment(const char *var_name) {
	symbol_t *sym;

	in_constraint_section = 1;

	if ((sym = symbol_lookup(var_name)) != NULL) {
		if (symbol_is_assigned(var_name)) {
			error_report(yylineno,
			    "Variable '%s' already assigned (single assignment rule)", var_name);
			error_count++;
		} else
			symbol_mark_assigned(var_name);
	} else {
		symbol_add_with_origin(var_name, SYMBOL_SCALAR, 0, SYMBOL_GATE);
		symbol_mark_assigned(var_name);

		if (generate_r1cs) {
			sym = symbol_lookup(var_name);
			if (sym)
				r1cs_register_witness(var_name, sym->witness_index,
				                      VISIBILITY_PRIVATE, SYMBOL_GATE,
				                      SYMBOL_SCALAR, 0);
		}
	}
}

//arrays can only be declared as inputs, never assigned element by element
void handle_array_element_assignment(const char *array_name, const char *index_str) {
	symbol_t *sym = symbol_lookup(array_name);

	in_constraint_section = 1;

	if (!sym) {
		error_report(yylineno,
		    "Variable '%s' not declared or assigned yet", array_name);
		error_count++;
	} else if (sym->type != SYMBOL_ARRAY) {
		error_report(yylineno,
		    "Scalar variable '%s' cannot be indexed", array_name);
		error_count++;
	} else {
		error_report(yylineno,
		    "Cannot assign to individual array element '%s[%d]' - arrays can only be declared as inputs",
		    array_name, atoi(index_str));
		error_count++;
	}
}

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
		error_report(yylineno,
		    "Gate variable '%s' used before it is assigned a value", var_name);
		error_count++;
	}
}

void validate_array_access(const char *array_name, const char *index_str) {
	symbol_t *sym = symbol_lookup(array_name);
	int index;

	if (!sym) {
		error_report(yylineno,
		    "Variable '%s' not declared or assigned yet", array_name);
		error_count++;
	} else if (sym->type == SYMBOL_SCALAR) {
		error_report(yylineno,
		    "Scalar variable '%s' cannot be indexed", array_name);
		error_count++;
	} else {
		index = atoi(index_str);
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
