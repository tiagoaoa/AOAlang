/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     r1cs.h
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#ifndef R1CS_H
#define R1CS_H

#include <stdio.h>
#include "symbol_table.h"

#ifndef INITIAL_CONSTRAINT_CAPACITY
#define INITIAL_CONSTRAINT_CAPACITY 1024
#endif

#ifndef INITIAL_WITNESS_CAPACITY
#define INITIAL_WITNESS_CAPACITY 1024
#endif

#ifndef MAX_ENTRIES_PER_ROW
#define MAX_ENTRIES_PER_ROW 64
#endif

typedef struct {
	int col;		//witness index (column)
	char coeff[80];		//coefficient kept as a string to preserve precision
} r1cs_entry_t;

typedef struct {
	r1cs_entry_t A[MAX_ENTRIES_PER_ROW];
	r1cs_entry_t B[MAX_ENTRIES_PER_ROW];
	r1cs_entry_t C[MAX_ENTRIES_PER_ROW];
	int A_count;
	int B_count;
	int C_count;
	char *comment;		//original expression, for debugging
	char *lhs_var;		//left-hand side variable name
} r1cs_constraint_t;

typedef struct {
	int index;
	char *name;
	visibility_t visibility;
	symbol_origin_t origin;
	char *symbolic_expr;	//for gates: the expression that computes the witness
} witness_entry_t;

typedef struct {
	r1cs_constraint_t *constraints;
	int n_constraints;
	int constraints_capacity;

	witness_entry_t *witnesses;
	int n_witnesses;
	int witnesses_capacity;

	int *constant_indices;	//witness partition, filled by r1cs_build_partition()
	int n_constants;
	int *private_indices;
	int n_private;
	int *deferred_indices;
	int n_deferred;
	int *gate_indices;
	int n_gates;

	char **public_input_names;	//for discriminants
	int n_public_inputs;
} r1cs_system_t;

extern r1cs_system_t r1cs;



void r1cs_init(void);
void r1cs_free(void);

void r1cs_add_constant_one(void);	//adds the constant 1 as witness 0
void r1cs_register_witness(const char *name, int witness_index,
                           visibility_t vis, symbol_origin_t origin,
                           symbol_type_t type, int array_size);
int r1cs_get_witness_index(const char *var_name);	//handles array[index] notation

void r1cs_begin_constraint(const char *lhs_var, const char *comment);
void r1cs_add_A(int col, const char *coeff);
void r1cs_add_B(int col, const char *coeff);
void r1cs_add_C(int col, const char *coeff);
void r1cs_end_constraint(void);

void r1cs_add_mul_constraint(const char *result, const char *left, const char *right);
void r1cs_add_add_constraint(const char *result, const char *left, const char *right);
void r1cs_add_sub_constraint(const char *result, const char *left, const char *right);
void r1cs_add_const_constraint(const char *result, const char *value);
void r1cs_add_eq_constraint(const char *result, const char *left, const char *right);

void r1cs_set_gate_expr(const char *gate_name, const char *expr);
void r1cs_build_partition(void);

void r1cs_generate_json(FILE *out, const char *circuit_name);
void r1cs_generate_dense(FILE *out);
void r1cs_generate_qap(FILE *out);
void r1cs_generate_c_checker(FILE *out, const char *circuit_name);
void r1cs_print(void);

#endif
