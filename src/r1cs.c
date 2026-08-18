/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     r1cs.c
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "r1cs.h"
#include "symbol_table.h"

static void *xgrow(void *p, int oldn, int newn, size_t size);
static void ensure_constraints(int n);
static void ensure_witnesses(int n);
static void put_witness(const char *name, int idx, visibility_t vis, symbol_origin_t origin);
static void row_add(r1cs_entry_t *row, int *count, int col, const char *coeff, const char *which);

static int is_constant(const char *str, char *value_out);
static void negate_coeff(const char *in, char *out, int out_size);
static void put_A_operand(const char *opnd, int negate);

static void print_json_int_array(FILE *out, int *arr, int count);
static void print_json_string(FILE *out, const char *str);
static void print_partition_json(FILE *out, const char *label, int *indices, int n, const char *tail);
static void print_json_matrix(FILE *out, char which);
static void print_dense_matrix(FILE *out, char which, char **row);

static void lagrange_interpolate(int n, int *y, double *coeffs);
static double snap(double v);
static void format_coeff(char *buf, size_t bufsize, double val);
static void print_sign(FILE *out, double c, int *first);
static void print_term_coeff(FILE *out, double val, int *first);
static void print_poly_coeffs(FILE *out, double *coeffs, int n);
static void print_var_polys(FILE *out, char which, int **dense, int *col_vals, double *coeffs);
static void print_wpoly(FILE *out, char which, double **polys);
static void print_quad_term(FILE *out, const char *coeff, int i, int j);
static void print_pcoef(FILE *out, int m, double **A_polys, double **B_polys, double **C_polys);
static void compute_vanishing(int n, double *Z);

static void print_checker_entries(FILE *out, char which);
static void print_row(const char *label, r1cs_entry_t *row, int count);


/* ----------------------------
        GLOBAL VARIABLES       */

r1cs_system_t r1cs;

static int cur_cons = -1;	//constraint currently being built, -1 if none

/*----------------------------*/


static void *xgrow(void *p, int oldn, int newn, size_t size) {
	if ((p = realloc(p, newn * size)) == NULL) {
		fprintf(stderr, "Error growing R1CS storage\n");
		exit(1);
	}
	memset((char *)p + oldn * size, 0, (newn - oldn) * size);	//new slots must start zeroed
	return p;
}

static void ensure_constraints(int n) {
	int cap = r1cs.constraints_capacity, newcap;

	if (n <= cap)
		return;
	newcap = cap > 0 ? cap : INITIAL_CONSTRAINT_CAPACITY;
	while (newcap < n)
		newcap *= 2;
	r1cs.constraints = xgrow(r1cs.constraints, cap, newcap, sizeof(r1cs_constraint_t));
	r1cs.constraints_capacity = newcap;
}

static void ensure_witnesses(int n) {
	int cap = r1cs.witnesses_capacity, newcap;

	if (n <= cap)
		return;
	newcap = cap > 0 ? cap : INITIAL_WITNESS_CAPACITY;
	while (newcap < n)
		newcap *= 2;

	r1cs.witnesses = xgrow(r1cs.witnesses, cap, newcap, sizeof(witness_entry_t));
	r1cs.constant_indices = xgrow(r1cs.constant_indices, cap, newcap, sizeof(int));
	r1cs.private_indices = xgrow(r1cs.private_indices, cap, newcap, sizeof(int));
	r1cs.deferred_indices = xgrow(r1cs.deferred_indices, cap, newcap, sizeof(int));
	r1cs.gate_indices = xgrow(r1cs.gate_indices, cap, newcap, sizeof(int));
	r1cs.public_input_names = xgrow(r1cs.public_input_names, cap, newcap, sizeof(char *));
	r1cs.witnesses_capacity = newcap;
}

void r1cs_init(void) {
	memset(&r1cs, 0, sizeof(r1cs_system_t));
	cur_cons = -1;
}

void r1cs_free(void) {
	int i;

	for (i = 0; i < r1cs.n_constraints; i++) {
		free(r1cs.constraints[i].comment);
		free(r1cs.constraints[i].lhs_var);
	}
	for (i = 0; i < r1cs.n_witnesses; i++) {
		free(r1cs.witnesses[i].name);
		free(r1cs.witnesses[i].symbolic_expr);
	}
	for (i = 0; i < r1cs.n_public_inputs; i++)
		free(r1cs.public_input_names[i]);

	free(r1cs.constraints);
	free(r1cs.witnesses);
	free(r1cs.constant_indices);
	free(r1cs.private_indices);
	free(r1cs.deferred_indices);
	free(r1cs.gate_indices);
	free(r1cs.public_input_names);
	memset(&r1cs, 0, sizeof(r1cs_system_t));
}

void r1cs_add_constant_one(void) {
	ensure_witnesses(1);

	r1cs.witnesses[0].index = 0;
	r1cs.witnesses[0].name = strdup("1");
	r1cs.witnesses[0].visibility = VISIBILITY_PUBLIC;
	r1cs.witnesses[0].origin = SYMBOL_DECLARED;
	r1cs.witnesses[0].symbolic_expr = strdup("1");
	r1cs.n_witnesses = 1;

	r1cs.public_input_names[0] = strdup("1");
	r1cs.n_public_inputs = 1;

	r1cs.constant_indices[0] = 0;
	r1cs.n_constants = 1;
}

static void put_witness(const char *name, int idx, visibility_t vis, symbol_origin_t origin) {
	if (idx >= r1cs.n_witnesses)
		r1cs.n_witnesses = idx + 1;

	r1cs.witnesses[idx].index = idx;
	r1cs.witnesses[idx].name = strdup(name);
	r1cs.witnesses[idx].visibility = vis;
	r1cs.witnesses[idx].origin = origin;
	r1cs.witnesses[idx].symbolic_expr = strdup(name);

	if (vis == VISIBILITY_DEFERRED || vis == VISIBILITY_PUBLIC)
		r1cs.public_input_names[r1cs.n_public_inputs++] = strdup(name);
}

void r1cs_register_witness(const char *name, int witness_index,
                           visibility_t vis, symbol_origin_t origin,
                           symbol_type_t type, int array_size) {
	char elem[256];
	int i;

	if (type == SYMBOL_ARRAY) {
		ensure_witnesses(witness_index + array_size);
		for (i = 0; i < array_size; i++) {	//one witness slot per element
			snprintf(elem, sizeof(elem), "%s[%d]", name, i);
			put_witness(elem, witness_index + i, vis, origin);
		}
	} else {
		ensure_witnesses(witness_index + 1);
		put_witness(name, witness_index, vis, origin);
	}
}

void r1cs_begin_constraint(const char *lhs_var, const char *comment) {
	r1cs_constraint_t *c;

	ensure_constraints(r1cs.n_constraints + 1);

	cur_cons = r1cs.n_constraints;
	c = &r1cs.constraints[cur_cons];

	memset(c, 0, sizeof(r1cs_constraint_t));
	c->lhs_var = lhs_var ? strdup(lhs_var) : NULL;
	c->comment = comment ? strdup(comment) : NULL;
}

static void row_add(r1cs_entry_t *row, int *count, int col, const char *coeff, const char *which) {
	if (*count >= MAX_ENTRIES_PER_ROW) {
		fprintf(stderr, "Error: Too many entries in %s row\n", which);
		return;
	}
	row[*count].col = col;
	strncpy(row[*count].coeff, coeff, 79);
	row[*count].coeff[79] = '\0';
	(*count)++;
}

void r1cs_add_A(int col, const char *coeff) {
	if (cur_cons < 0 || cur_cons >= r1cs.constraints_capacity)
		return;
	row_add(r1cs.constraints[cur_cons].A, &r1cs.constraints[cur_cons].A_count, col, coeff, "A");
}

void r1cs_add_B(int col, const char *coeff) {
	if (cur_cons < 0 || cur_cons >= r1cs.constraints_capacity)
		return;
	row_add(r1cs.constraints[cur_cons].B, &r1cs.constraints[cur_cons].B_count, col, coeff, "B");
}

void r1cs_add_C(int col, const char *coeff) {
	if (cur_cons < 0 || cur_cons >= r1cs.constraints_capacity)
		return;
	row_add(r1cs.constraints[cur_cons].C, &r1cs.constraints[cur_cons].C_count, col, coeff, "C");
}

void r1cs_end_constraint(void) {
	if (cur_cons >= 0) {
		r1cs.n_constraints++;
		cur_cons = -1;
	}
}

int r1cs_get_witness_index(const char *var_name) {
	char name_buf[256];
	const char *bracket, *p;
	size_t name_len;
	symbol_t *sym;
	int idx;

	if ((bracket = strchr(var_name, '[')) != NULL) {
		name_len = bracket - var_name;
		strncpy(name_buf, var_name, name_len);
		name_buf[name_len] = '\0';
		idx = atoi(bracket + 1);

		sym = symbol_lookup(name_buf);
		if (sym && sym->type == SYMBOL_ARRAY)
			return sym->witness_index + idx;
	} else {
		sym = symbol_lookup(var_name);
		if (sym)
			return sym->witness_index;
	}

	//a bare number is a constant and rides on the w[0] = 1 wire
	p = var_name;
	if (*p == '-') p++;
	if (*p >= '0' && *p <= '9') {
		while (*p >= '0' && *p <= '9') p++;
		if (*p == '\0') return 0;
	}

	return -1;
}

static int is_constant(const char *str, char *value_out) {
	const char *p = str;

	if (*p == '-') p++;
	if (*p >= '0' && *p <= '9') {
		while (*p >= '0' && *p <= '9') p++;
		if (*p == '\0' || *p == '.') {
			strncpy(value_out, str, 79);
			value_out[79] = '\0';
			return 1;
		}
	}
	return 0;
}

static void negate_coeff(const char *in, char *out, int out_size) {
	if (in[0] == '-') {
		strncpy(out, in + 1, out_size - 1);
	} else if (strcmp(in, "0") == 0) {
		strncpy(out, "0", out_size - 1);
	} else {
		out[0] = '-';
		strncpy(out + 1, in, out_size - 2);
	}
	out[out_size - 1] = '\0';
}

//adds an operand to the A row of the current constraint,
//using the w[0] = 1 wire when the operand is a constant
static void put_A_operand(const char *opnd, int negate) {
	char cval[80], neg[80];
	int idx;

	if (is_constant(opnd, cval)) {
		if (negate) {
			negate_coeff(cval, neg, sizeof(neg));
			r1cs_add_A(0, neg);
		} else
			r1cs_add_A(0, cval);
	} else if ((idx = r1cs_get_witness_index(opnd)) >= 0)
		r1cs_add_A(idx, negate ? "-1" : "1");
}

void r1cs_add_mul_constraint(const char *result, const char *left, const char *right) {
	char comment[512], expr[512], cval[80];
	int residx = r1cs_get_witness_index(result), idx;

	if (residx < 0) {
		fprintf(stderr, "Error: Invalid variable in multiplication constraint\n");
		return;
	}

	snprintf(comment, sizeof(comment), "%s = %s * %s", result, left, right);
	r1cs_begin_constraint(result, comment);

	//result = left * right is a single native R1CS row: left in A, right in B
	if (is_constant(left, cval))
		r1cs_add_A(0, cval);
	else if ((idx = r1cs_get_witness_index(left)) >= 0)
		r1cs_add_A(idx, "1");

	if (is_constant(right, cval))
		r1cs_add_B(0, cval);
	else if ((idx = r1cs_get_witness_index(right)) >= 0)
		r1cs_add_B(idx, "1");

	r1cs_add_C(residx, "1");
	r1cs_end_constraint();

	snprintf(expr, sizeof(expr), "%s * %s", left, right);
	r1cs_set_gate_expr(result, expr);
}

void r1cs_add_add_constraint(const char *result, const char *left, const char *right) {
	char comment[512], expr[512];
	int residx = r1cs_get_witness_index(result);

	if (residx < 0) {
		fprintf(stderr, "Error: Invalid variable in addition constraint\n");
		return;
	}

	snprintf(comment, sizeof(comment), "%s = %s + %s", result, left, right);
	r1cs_begin_constraint(result, comment);

	put_A_operand(left, 0);
	put_A_operand(right, 0);
	r1cs_add_B(0, "1");	//(left + right) * 1 = result
	r1cs_add_C(residx, "1");
	r1cs_end_constraint();

	snprintf(expr, sizeof(expr), "%s + %s", left, right);
	r1cs_set_gate_expr(result, expr);
}

void r1cs_add_sub_constraint(const char *result, const char *left, const char *right) {
	char comment[512], expr[512];
	int residx = r1cs_get_witness_index(result);

	if (residx < 0) {
		fprintf(stderr, "Error: Invalid variable in subtraction constraint\n");
		return;
	}

	snprintf(comment, sizeof(comment), "%s = %s - %s", result, left, right);
	r1cs_begin_constraint(result, comment);

	put_A_operand(left, 0);
	put_A_operand(right, 1);	//negated for subtraction
	r1cs_add_B(0, "1");
	r1cs_add_C(residx, "1");
	r1cs_end_constraint();

	snprintf(expr, sizeof(expr), "%s - %s", left, right);
	r1cs_set_gate_expr(result, expr);
}

void r1cs_add_const_constraint(const char *result, const char *value) {
	char comment[256], neg[80];
	int residx = r1cs_get_witness_index(result);

	if (residx < 0) {
		fprintf(stderr, "Error: Invalid variable in constant constraint\n");
		return;
	}

	snprintf(comment, sizeof(comment), "%s = %s", result, value);
	r1cs_begin_constraint(result, comment);

	//(result - value) * 1 = 0
	r1cs_add_A(residx, "1");
	negate_coeff(value, neg, sizeof(neg));
	r1cs_add_A(0, neg);
	r1cs_add_B(0, "1");
	r1cs_end_constraint();

	r1cs_set_gate_expr(result, value);
}

void r1cs_add_eq_constraint(const char *result, const char *left, const char *right) {
	char comment[512];
	int lidx = r1cs_get_witness_index(left);
	int ridx = r1cs_get_witness_index(right);

	snprintf(comment, sizeof(comment), "%s: %s == %s", result, left, right);
	r1cs_begin_constraint(result, comment);

	//(left - right) * 1 = 0
	r1cs_add_A(lidx, "1");
	r1cs_add_A(ridx, "-1");
	r1cs_add_B(0, "1");
	r1cs_end_constraint();

	r1cs_set_gate_expr(result, "0");
}

void r1cs_set_gate_expr(const char *gate_name, const char *expr) {
	int idx = r1cs_get_witness_index(gate_name);

	if (idx >= 0 && idx < r1cs.n_witnesses) {
		free(r1cs.witnesses[idx].symbolic_expr);
		r1cs.witnesses[idx].symbolic_expr = strdup(expr);
	}
}

void r1cs_build_partition(void) {
	witness_entry_t *w;
	int i;

	r1cs.n_constants = 0;
	r1cs.n_private = 0;
	r1cs.n_deferred = 0;
	r1cs.n_gates = 0;

	for (i = 0; i < r1cs.n_witnesses; i++) {
		w = &r1cs.witnesses[i];

		if (i == 0)	//the constant 1
			r1cs.constant_indices[r1cs.n_constants++] = i;
		else if (w->origin == SYMBOL_GATE)
			r1cs.gate_indices[r1cs.n_gates++] = i;
		else if (w->visibility == VISIBILITY_PRIVATE)
			r1cs.private_indices[r1cs.n_private++] = i;
		else
			r1cs.deferred_indices[r1cs.n_deferred++] = i;
	}
}



/* ----------------------------
        JSON OUTPUT            */

static void print_json_int_array(FILE *out, int *arr, int count) {
	int i;

	fprintf(out, "[");
	for (i = 0; i < count; i++) {
		fprintf(out, "%d", arr[i]);
		if (i < count - 1) fprintf(out, ", ");
	}
	fprintf(out, "]");
}

static void print_json_string(FILE *out, const char *str) {
	const char *p;

	fprintf(out, "\"");
	if (str)
		for (p = str; *p; p++)
			switch (*p) {
				case '"':  fprintf(out, "\\\""); break;
				case '\\': fprintf(out, "\\\\"); break;
				case '\n': fprintf(out, "\\n"); break;
				case '\r': fprintf(out, "\\r"); break;
				case '\t': fprintf(out, "\\t"); break;
				default:   fputc(*p, out);
			}
	fprintf(out, "\"");
}

static void print_partition_json(FILE *out, const char *label, int *indices, int n, const char *tail) {
	int i;

	fprintf(out, "      \"%s\": {\"indices\": ", label);
	print_json_int_array(out, indices, n);
	fprintf(out, ", \"names\": [");
	for (i = 0; i < n; i++) {
		print_json_string(out, r1cs.witnesses[indices[i]].name);
		if (i < n - 1) fprintf(out, ", ");
	}
	fprintf(out, "]}%s\n", tail);
}

static void print_json_matrix(FILE *out, char which) {
	r1cs_constraint_t *c;
	r1cs_entry_t *row;
	int i, j, count;

	for (i = 0; i < r1cs.n_constraints; i++) {
		c = &r1cs.constraints[i];
		row = (which == 'A') ? c->A : (which == 'B') ? c->B : c->C;
		count = (which == 'A') ? c->A_count : (which == 'B') ? c->B_count : c->C_count;

		fprintf(out, "      {\"row\": %d, \"entries\": [", i);
		for (j = 0; j < count; j++) {
			fprintf(out, "{\"col\": %d, \"val\": \"%s\"}", row[j].col, row[j].coeff);
			if (j < count - 1) fprintf(out, ", ");
		}
		fprintf(out, "]");
		if (which == 'A' && c->comment) {	//comments only make sense once per constraint
			fprintf(out, ", \"comment\": ");
			print_json_string(out, c->comment);
		}
		fprintf(out, "}");
		if (i < r1cs.n_constraints - 1) fprintf(out, ",");
		fprintf(out, "\n");
	}
}

void r1cs_generate_json(FILE *out, const char *circuit_name) {
	witness_entry_t *w;
	int i, idx;

	r1cs_build_partition();

	fprintf(out, "{\n");

	fprintf(out, "  \"circuit\": ");
	print_json_string(out, circuit_name);
	fprintf(out, ",\n");
	fprintf(out, "  \"version\": \"1.0\",\n");
	fprintf(out, "  \"field\": \"bn254\",\n");
	fprintf(out, "\n");

	fprintf(out, "  \"witness\": {\n");
	fprintf(out, "    \"total\": %d,\n", r1cs.n_witnesses);

	fprintf(out, "    \"partition\": {\n");
	print_partition_json(out, "constant", r1cs.constant_indices, r1cs.n_constants, ",");
	print_partition_json(out, "private", r1cs.private_indices, r1cs.n_private, ",");
	print_partition_json(out, "deferred", r1cs.deferred_indices, r1cs.n_deferred, ",");
	print_partition_json(out, "gates", r1cs.gate_indices, r1cs.n_gates, "");
	fprintf(out, "    },\n");

	fprintf(out, "    \"entries\": [\n");
	for (i = 0; i < r1cs.n_witnesses; i++) {
		w = &r1cs.witnesses[i];
		fprintf(out, "      {\"index\": %d, \"name\": ", w->index);
		print_json_string(out, w->name);
		fprintf(out, ", \"visibility\": ");
		print_json_string(out, visibility_to_string(w->visibility));
		fprintf(out, ", \"origin\": ");
		print_json_string(out, w->origin == SYMBOL_GATE ? "gate" : "declared");
		if (w->symbolic_expr) {
			fprintf(out, ", \"symbolic\": ");
			print_json_string(out, w->symbolic_expr);
		}
		fprintf(out, "}");
		if (i < r1cs.n_witnesses - 1) fprintf(out, ",");
		fprintf(out, "\n");
	}
	fprintf(out, "    ]\n");
	fprintf(out, "  },\n\n");

	fprintf(out, "  \"r1cs\": {\n");
	fprintf(out, "    \"n_constraints\": %d,\n", r1cs.n_constraints);
	fprintf(out, "    \"n_variables\": %d,\n", r1cs.n_witnesses);
	fprintf(out, "\n");

	fprintf(out, "    \"A\": [\n");
	print_json_matrix(out, 'A');
	fprintf(out, "    ],\n\n");

	fprintf(out, "    \"B\": [\n");
	print_json_matrix(out, 'B');
	fprintf(out, "    ],\n\n");

	fprintf(out, "    \"C\": [\n");
	print_json_matrix(out, 'C');
	fprintf(out, "    ]\n");
	fprintf(out, "  },\n\n");

	fprintf(out, "  \"public_inputs\": [\n");
	for (i = 0; i < r1cs.n_public_inputs; i++) {
		fprintf(out, "    ");
		print_json_string(out, r1cs.public_input_names[i]);
		if (i < r1cs.n_public_inputs - 1) fprintf(out, ",");
		fprintf(out, "\n");
	}
	fprintf(out, "  ],\n\n");

	fprintf(out, "  \"symbolic_propagation\": {\n");
	for (i = 0; i < r1cs.n_gates; i++) {
		idx = r1cs.gate_indices[i];
		w = &r1cs.witnesses[idx];
		fprintf(out, "    ");
		print_json_string(out, w->name);
		fprintf(out, ": ");
		print_json_string(out, w->symbolic_expr ? w->symbolic_expr : w->name);
		if (i < r1cs.n_gates - 1) fprintf(out, ",");
		fprintf(out, "\n");
	}
	fprintf(out, "  }\n");

	fprintf(out, "}\n");
}



/* ----------------------------
        DENSE OUTPUT           */

static void print_dense_matrix(FILE *out, char which, char **row) {
	r1cs_constraint_t *c;
	r1cs_entry_t *entries;
	int i, j, count;
	int n_vars = r1cs.n_witnesses;

	fprintf(out, "%c\n", which);
	for (i = 0; i < r1cs.n_constraints; i++) {
		c = &r1cs.constraints[i];
		entries = (which == 'A') ? c->A : (which == 'B') ? c->B : c->C;
		count = (which == 'A') ? c->A_count : (which == 'B') ? c->B_count : c->C_count;

		for (j = 0; j < n_vars; j++) row[j] = "0";
		for (j = 0; j < count; j++)
			row[entries[j].col] = entries[j].coeff;

		fprintf(out, "[");
		for (j = 0; j < n_vars; j++) {
			fprintf(out, "%s", row[j]);
			if (j < n_vars - 1) fprintf(out, ", ");
		}
		fprintf(out, "]\n");
	}
}

void r1cs_generate_dense(FILE *out) {
	char **row;
	int i, n_vars;

	r1cs_build_partition();
	n_vars = r1cs.n_witnesses;

	fprintf(out, "w = [");
	for (i = 0; i < n_vars; i++) {
		fprintf(out, "%s", r1cs.witnesses[i].name);
		if (i < n_vars - 1) fprintf(out, ", ");
	}
	fprintf(out, "]\n");

	row = (char **)malloc(n_vars * sizeof(char *));	//scratch row of coefficient strings
	print_dense_matrix(out, 'A', row);
	print_dense_matrix(out, 'B', row);
	print_dense_matrix(out, 'C', row);
	free(row);
}



/* ----------------------------
        QAP OUTPUT             */

/*
 * Given values y[0..n-1] at points x = 1, 2, ..., n, computes the coefficients
 * c[0..n-1] of the interpolating polynomial P(x) = c[0] + c[1]*x + c[2]*x^2 + ...
 */
static void lagrange_interpolate(int n, int *y, double *coeffs) {
	double *basis, *new_basis, denom;
	int i, j, k, xi, xj;

	if ((basis = (double *)malloc(n * sizeof(double))) == NULL ||
	    (new_basis = (double *)malloc(n * sizeof(double))) == NULL) {
		fprintf(stderr, "Error allocating memory for QAP interpolation\n");
		exit(1);
	}

	for (i = 0; i < n; i++)
		coeffs[i] = 0.0;

	for (i = 0; i < n; i++) {
		if (y[i] == 0) continue;

		xi = i + 1;	//interpolation points are 1-indexed

		for (k = 0; k < n; k++) basis[k] = 0.0;
		basis[0] = (double)y[i];

		//multiply by (x - xj) / (xi - xj) for each j != i
		for (j = 0; j < n; j++) {
			if (j == i) continue;
			xj = j + 1;
			denom = (double)(xi - xj);

			for (k = 0; k < n; k++) new_basis[k] = 0.0;
			for (k = 0; k < n; k++) {
				if (basis[k] != 0.0) {
					//x^k * (x - xj) = x^(k+1) - xj * x^k
					if (k + 1 < n)
						new_basis[k + 1] += basis[k] / denom;
					new_basis[k] -= (xj * basis[k]) / denom;
				}
			}
			for (k = 0; k < n; k++) basis[k] = new_basis[k];
		}

		for (k = 0; k < n; k++)
			coeffs[k] += basis[k];
	}

	free(basis);
	free(new_basis);
}

//rounds v to the nearest integer when it is within 1e-9 of one
static double snap(double v) {
	double r = (v >= 0) ? (int)(v + 0.5) : (int)(v - 0.5);

	return (fabs(v - r) < 1e-9) ? r : v;
}

static void format_coeff(char *buf, size_t bufsize, double val) {
	double rounded = (val >= 0) ? (int)(val + 0.5) : (int)(val - 0.5);

	if (fabs(val - rounded) < 1e-9)
		snprintf(buf, bufsize, "%.0f", rounded);
	else
		snprintf(buf, bufsize, "%.6g", val);
}

//prints " + " or " - " between terms, or a bare "-" for a leading negative term
static void print_sign(FILE *out, double c, int *first) {
	if (!*first)
		fprintf(out, c >= 0 ? " + " : " - ");
	else if (c < 0)
		fprintf(out, "-");
	*first = 0;
}

//prints a polynomial term coefficient with its separating sign;
//the leading term keeps the sign inside the number itself
static void print_term_coeff(FILE *out, double val, int *first) {
	char buf[64];

	if (*first) {
		format_coeff(buf, sizeof(buf), val);
	} else {
		fprintf(out, val >= 0 ? " + " : " - ");
		format_coeff(buf, sizeof(buf), val >= 0 ? val : -val);
	}
	*first = 0;
	fprintf(out, "%s", buf);
}

static void print_poly_coeffs(FILE *out, double *coeffs, int n) {
	char buf[64];
	double val;
	int i, first;

	fprintf(out, "[");
	for (i = 0; i < n; i++) {
		format_coeff(buf, sizeof(buf), coeffs[i]);
		fprintf(out, "%s", buf);
		if (i < n - 1) fprintf(out, ", ");
	}
	fprintf(out, "]");

	fprintf(out, "  =  ");
	first = 1;
	for (i = n - 1; i >= 0; i--) {
		val = snap(coeffs[i]);
		if (fabs(val) < 1e-9) continue;

		print_term_coeff(out, val, &first);

		if (i > 1)
			fprintf(out, "*x^%d", i);
		else if (i == 1)
			fprintf(out, "*x");
	}
	if (first)
		fprintf(out, "0");
}

//interpolates and prints one polynomial per witness column of the given matrix
static void print_var_polys(FILE *out, char which, int **dense, int *col_vals, double *coeffs) {
	int i, j;
	int n_vars = r1cs.n_witnesses, n_cons = r1cs.n_constraints;

	fprintf(out, "%c(x) polynomials:\n", which);
	for (j = 0; j < n_vars; j++) {
		for (i = 0; i < n_cons; i++)
			col_vals[i] = dense[i][j];
		lagrange_interpolate(n_cons, col_vals, coeffs);
		fprintf(out, "  %c_%s(x) = ", which, r1cs.witnesses[j].name);
		print_poly_coeffs(out, coeffs, n_cons);
		fprintf(out, "\n");
	}
}

//prints the inner product w.M(x) expanded as a sum of witness * polynomial terms
static void print_wpoly(FILE *out, char which, double **polys) {
	double val;
	int j, k, is_zero, first_term, first_coef;
	int n_vars = r1cs.n_witnesses, n_cons = r1cs.n_constraints;

	fprintf(out, "w.%c(x) = ", which);
	first_term = 1;
	for (j = 0; j < n_vars; j++) {
		is_zero = 1;
		for (k = 0; k < n_cons; k++)
			if (fabs(polys[j][k]) > 1e-9) { is_zero = 0; break; }
		if (is_zero) continue;

		if (!first_term) fprintf(out, " + ");
		first_term = 0;
		fprintf(out, "%s*(", r1cs.witnesses[j].name);

		first_coef = 1;
		for (k = n_cons - 1; k >= 0; k--) {
			val = snap(polys[j][k]);
			if (fabs(val) < 1e-9) continue;

			print_term_coeff(out, val, &first_coef);

			if (k > 1)
				fprintf(out, "*x^%d", k);
			else if (k == 1)
				fprintf(out, "*x");
		}
		if (first_coef) fprintf(out, "0");
		fprintf(out, ")");
	}
	if (first_term) fprintf(out, "0");
	fprintf(out, "\n\n");
}

static void print_quad_term(FILE *out, const char *coeff, int i, int j) {
	int i_one = (strcmp(r1cs.witnesses[i].name, "1") == 0);
	int j_one = (strcmp(r1cs.witnesses[j].name, "1") == 0);

	if (i_one && j_one)
		fprintf(out, "%s", coeff);	//the constant wire contributes no name
	else if (i_one)
		fprintf(out, "%s*%s", coeff, r1cs.witnesses[j].name);
	else if (j_one)
		fprintf(out, "%s*%s", coeff, r1cs.witnesses[i].name);
	else if (i == j)
		fprintf(out, "%s*%s^2", coeff, r1cs.witnesses[i].name);
	else
		fprintf(out, "%s*%s*%s", coeff, r1cs.witnesses[i].name, r1cs.witnesses[j].name);
}

//prints the coefficient of x^m in P(x) = w.A(x) * w.B(x) - w.C(x)
//as an expression in the witness variables
static void print_pcoef(FILE *out, int m, double **A_polys, double **B_polys, double **C_polys) {
	char buf[64];
	double c, **coef_quad, *coef_lin;
	int i, j, k, l, first;
	int n_vars = r1cs.n_witnesses, n_cons = r1cs.n_constraints;

	coef_quad = (double **)malloc(n_vars * sizeof(double *));
	coef_lin = (double *)calloc(n_vars, sizeof(double));
	for (i = 0; i < n_vars; i++)
		coef_quad[i] = (double *)calloc(n_vars, sizeof(double));

	//terms of w.A(x) * w.B(x) that land on x^m
	for (k = 0; k <= m && k < n_cons; k++) {
		l = m - k;
		if (l < 0 || l >= n_cons) continue;

		for (i = 0; i < n_vars; i++) {
			if (fabs(A_polys[i][k]) < 1e-9) continue;
			for (j = 0; j < n_vars; j++) {
				if (fabs(B_polys[j][l]) < 1e-9) continue;
				if (i <= j)	//w_i*w_j and w_j*w_i fold into a single term
					coef_quad[i][j] += A_polys[i][k] * B_polys[j][l];
				else
					coef_quad[j][i] += A_polys[i][k] * B_polys[j][l];
			}
		}
	}

	if (m < n_cons)
		for (i = 0; i < n_vars; i++)
			coef_lin[i] -= C_polys[i][m];

	fprintf(out, "coef(x^%d) = ", m);
	first = 1;

	for (i = 0; i < n_vars; i++)
		for (j = i; j < n_vars; j++) {
			c = snap(coef_quad[i][j]);
			if (fabs(c) < 1e-9) continue;

			print_sign(out, c, &first);
			format_coeff(buf, sizeof(buf), fabs(c));
			print_quad_term(out, buf, i, j);
		}

	for (i = 0; i < n_vars; i++) {
		c = snap(coef_lin[i]);
		if (fabs(c) < 1e-9) continue;

		print_sign(out, c, &first);
		format_coeff(buf, sizeof(buf), fabs(c));
		if (strcmp(r1cs.witnesses[i].name, "1") == 0)
			fprintf(out, "%s", buf);
		else
			fprintf(out, "%s*%s", buf, r1cs.witnesses[i].name);
	}

	if (first) fprintf(out, "0");
	fprintf(out, "\n");

	for (i = 0; i < n_vars; i++) free(coef_quad[i]);
	free(coef_quad);
	free(coef_lin);
}

//computes Z(x) = (x-1)(x-2)...(x-n); Z must hold n+1 coefficients
static void compute_vanishing(int n, double *Z) {
	double *tmp;
	int i, k, deg = 0;

	Z[0] = 1.0;
	for (k = 1; k <= n; k++) {
		tmp = (double *)calloc(n + 1, sizeof(double));
		for (i = 0; i <= deg; i++) {
			tmp[i + 1] += Z[i];	//x^(i+1) term of (x - k) * x^i
			tmp[i] -= k * Z[i];
		}
		deg++;
		for (i = 0; i <= deg; i++)
			Z[i] = tmp[i];
		free(tmp);
	}
}

void r1cs_generate_qap(FILE *out) {
	r1cs_constraint_t *c;
	int **A_dense, **B_dense, **C_dense;
	double **A_polys, **B_polys, **C_polys, *coeffs, *Z_coeffs;
	int *col_vals;
	int i, j, m, n_vars, n_cons, p_degree, h_degree;

	r1cs_build_partition();

	n_vars = r1cs.n_witnesses;
	n_cons = r1cs.n_constraints;

	if (n_cons == 0) {
		fprintf(out, "# No constraints\n");
		return;
	}

	//NOTE: this output path does the polynomial math in doubles, with coefficients
	//converted through atoi(), so it cannot represent full 254-bit field elements.
	//It is a debug/educational view; the JSON output keeps full precision as strings.

	A_dense = (int **)malloc(n_cons * sizeof(int *));
	B_dense = (int **)malloc(n_cons * sizeof(int *));
	C_dense = (int **)malloc(n_cons * sizeof(int *));

	for (i = 0; i < n_cons; i++) {
		A_dense[i] = (int *)calloc(n_vars, sizeof(int));
		B_dense[i] = (int *)calloc(n_vars, sizeof(int));
		C_dense[i] = (int *)calloc(n_vars, sizeof(int));

		c = &r1cs.constraints[i];
		for (j = 0; j < c->A_count; j++)
			A_dense[i][c->A[j].col] = atoi(c->A[j].coeff);
		for (j = 0; j < c->B_count; j++)
			B_dense[i][c->B[j].col] = atoi(c->B[j].coeff);
		for (j = 0; j < c->C_count; j++)
			C_dense[i][c->C[j].col] = atoi(c->C[j].coeff);
	}

	fprintf(out, "# QAP (Quadratic Arithmetic Program)\n");
	fprintf(out, "# Polynomials interpolated at x = 1, 2, ..., %d\n", n_cons);
	fprintf(out, "# Coefficients: [const, x, x^2, ...]\n");
	fprintf(out, "# P(x) = (w.A(x)) * (w.B(x)) - (w.C(x)) = H(x) * T(x)\n");
	fprintf(out, "# T(x) = (x-1)(x-2)...(x-%d)\n\n", n_cons);

	fprintf(out, "w = [");
	for (i = 0; i < n_vars; i++) {
		fprintf(out, "%s", r1cs.witnesses[i].name);
		if (i < n_vars - 1) fprintf(out, ", ");
	}
	fprintf(out, "]\n\n");

	col_vals = (int *)malloc(n_cons * sizeof(int));
	coeffs = (double *)malloc(n_cons * sizeof(double));

	print_var_polys(out, 'A', A_dense, col_vals, coeffs);
	fprintf(out, "\n");
	print_var_polys(out, 'B', B_dense, col_vals, coeffs);
	fprintf(out, "\n");
	print_var_polys(out, 'C', C_dense, col_vals, coeffs);

	fprintf(out, "\nT(x) = ");
	for (i = 1; i <= n_cons; i++)
		fprintf(out, "(x-%d)", i);
	fprintf(out, "\n");

	A_polys = (double **)malloc(n_vars * sizeof(double *));
	B_polys = (double **)malloc(n_vars * sizeof(double *));
	C_polys = (double **)malloc(n_vars * sizeof(double *));

	for (j = 0; j < n_vars; j++) {
		A_polys[j] = (double *)malloc(n_cons * sizeof(double));
		B_polys[j] = (double *)malloc(n_cons * sizeof(double));
		C_polys[j] = (double *)malloc(n_cons * sizeof(double));

		for (i = 0; i < n_cons; i++)
			col_vals[i] = A_dense[i][j];
		lagrange_interpolate(n_cons, col_vals, A_polys[j]);

		for (i = 0; i < n_cons; i++)
			col_vals[i] = B_dense[i][j];
		lagrange_interpolate(n_cons, col_vals, B_polys[j]);

		for (i = 0; i < n_cons; i++)
			col_vals[i] = C_dense[i][j];
		lagrange_interpolate(n_cons, col_vals, C_polys[j]);
	}

	fprintf(out, "\n# P(x) = (w.A(x)) * (w.B(x)) - (w.C(x))\n\n");

	print_wpoly(out, 'A', A_polys);
	print_wpoly(out, 'B', B_polys);
	print_wpoly(out, 'C', C_polys);

	fprintf(out, "P(x) = (w.A(x)) * (w.B(x)) - (w.C(x)) = H(x) * T(x)\n\n");

	//w.A(x) and w.B(x) each have degree n_cons-1, so their product has degree 2*(n_cons-1)
	p_degree = 2 * (n_cons - 1);

	fprintf(out, "# Simplified P(x) - coefficients are expressions in witness variables\n\n");
	for (m = p_degree; m >= 0; m--)
		print_pcoef(out, m, A_polys, B_polys, C_polys);

	fprintf(out, "\nP(x) = ");
	for (m = p_degree; m >= 0; m--) {
		if (m < p_degree) fprintf(out, " + ");
		fprintf(out, "coef(x^%d)", m);
		if (m > 1) fprintf(out, "*x^%d", m);
		else if (m == 1) fprintf(out, "*x");
	}
	fprintf(out, "\n");

	fprintf(out, "\n# Vanishing polynomial Z(x) = (x-1)(x-2)...(x-%d)\n", n_cons);
	Z_coeffs = (double *)calloc(n_cons + 1, sizeof(double));
	compute_vanishing(n_cons, Z_coeffs);
	fprintf(out, "Z(x) = ");
	print_poly_coeffs(out, Z_coeffs, n_cons + 1);
	fprintf(out, "\n");

	//H(x) = P(x) / Z(x) has degree 2*(n_cons-1) - n_cons = n_cons - 2
	h_degree = n_cons - 2;
	if (h_degree >= 0) {
		fprintf(out, "\n# Quotient polynomial H(x) = P(x) / Z(x)\n");
		fprintf(out, "# For a valid witness, P(x) is divisible by Z(x)\n");
		fprintf(out, "# H(x) has degree %d\n", h_degree);
		fprintf(out, "\n");

		fprintf(out, "H(x) = ");
		for (m = h_degree; m >= 0; m--) {
			if (m < h_degree) fprintf(out, " + ");
			fprintf(out, "h_%d", m);
			if (m > 1) fprintf(out, "*x^%d", m);
			else if (m == 1) fprintf(out, "*x");
		}
		fprintf(out, "\n\n");

		fprintf(out, "# where h_i coefficients are determined by polynomial division\n");
		fprintf(out, "# such that P(x) = H(x) * Z(x)\n");
	}

	free(Z_coeffs);
	for (j = 0; j < n_vars; j++) {
		free(A_polys[j]);
		free(B_polys[j]);
		free(C_polys[j]);
	}
	free(A_polys);
	free(B_polys);
	free(C_polys);
	free(col_vals);
	free(coeffs);
	for (i = 0; i < n_cons; i++) {
		free(A_dense[i]);
		free(B_dense[i]);
		free(C_dense[i]);
	}
	free(A_dense);
	free(B_dense);
	free(C_dense);
}



/* ----------------------------
        C CHECKER OUTPUT       */

static void print_checker_entries(FILE *out, char which) {
	r1cs_constraint_t *c;
	r1cs_entry_t *row;
	int i, j, rowcount, count = 0;
	int n_cons = r1cs.n_constraints;

	fprintf(out, "static const long long %c_entries[][3] = {\n", which);
	for (i = 0; i < n_cons; i++) {
		c = &r1cs.constraints[i];
		row = (which == 'A') ? c->A : (which == 'B') ? c->B : c->C;
		rowcount = (which == 'A') ? c->A_count : (which == 'B') ? c->B_count : c->C_count;

		for (j = 0; j < rowcount; j++) {
			fprintf(out, "    {%d, %d, %sLL}", i, row[j].col, row[j].coeff);
			count++;
			if (i < n_cons - 1 || j < rowcount - 1) fprintf(out, ",");
			fprintf(out, "\n");
		}
	}
	if (count == 0) fprintf(out, "    {-1, -1, 0}  /* placeholder */\n");
	fprintf(out, "};\n");
	fprintf(out, "#define %c_ENTRIES %d\n\n", which, count > 0 ? count : 1);
}

void r1cs_generate_c_checker(FILE *out, const char *circuit_name) {
	r1cs_constraint_t *c;
	int i, n_vars, n_cons;

	r1cs_build_partition();

	n_vars = r1cs.n_witnesses;
	n_cons = r1cs.n_constraints;

	fprintf(out, "/*\n");
	fprintf(out, " * R1CS Sanity Checker for: %s\n", circuit_name);
	fprintf(out, " * Auto-generated by AOAlang compiler\n");
	fprintf(out, " *\n");
	fprintf(out, " * Verifies: A*w . B*w = C*w for each constraint\n");
	fprintf(out, " *\n");
	fprintf(out, " * NOTE: This checker uses long long (64-bit) arithmetic via atoll().\n");
	fprintf(out, " * It does NOT handle full 254-bit field element coefficients.\n");
	fprintf(out, " * Use the JSON output for production verification.\n");
	fprintf(out, " */\n\n");

	fprintf(out, "#include <stdio.h>\n");
	fprintf(out, "#include <stdlib.h>\n");
	fprintf(out, "#include <string.h>\n\n");

	fprintf(out, "#define N_WITNESSES %d\n", n_vars);
	fprintf(out, "#define N_CONSTRAINTS %d\n\n", n_cons);

	fprintf(out, "/* Witness variable names */\n");
	fprintf(out, "static const char *witness_names[N_WITNESSES] = {\n");
	for (i = 0; i < n_vars; i++) {
		fprintf(out, "    \"%s\"", r1cs.witnesses[i].name);
		if (i < n_vars - 1) fprintf(out, ",");
		fprintf(out, "\n");
	}
	fprintf(out, "};\n\n");

	fprintf(out, "/* A matrix (sparse entries only) */\n");
	fprintf(out, "/* Format: {row, col, val} */\n");
	print_checker_entries(out, 'A');
	fprintf(out, "/* B matrix (sparse entries only) */\n");
	print_checker_entries(out, 'B');
	fprintf(out, "/* C matrix (sparse entries only) */\n");
	print_checker_entries(out, 'C');

	fprintf(out, "/* Constraint descriptions */\n");
	fprintf(out, "static const char *constraint_desc[N_CONSTRAINTS] = {\n");
	for (i = 0; i < n_cons; i++) {
		c = &r1cs.constraints[i];
		fprintf(out, "    \"%s\"", c->comment ? c->comment : "");
		if (i < n_cons - 1) fprintf(out, ",");
		fprintf(out, "\n");
	}
	fprintf(out, "};\n\n");

	fprintf(out, "/* Compute sparse matrix-vector product: result[row] += coeff * w[col] */\n");
	fprintf(out, "static void sparse_mv(long long *result, const long long entries[][3], int n_entries, const long long *w) {\n");
	fprintf(out, "    for (int i = 0; i < n_entries; i++) {\n");
	fprintf(out, "        long long row = entries[i][0];\n");
	fprintf(out, "        long long col = entries[i][1];\n");
	fprintf(out, "        long long val = entries[i][2];\n");
	fprintf(out, "        if (row >= 0) result[row] += val * w[col];\n");
	fprintf(out, "    }\n");
	fprintf(out, "}\n\n");

	fprintf(out, "/*\n");
	fprintf(out, " * Verify R1CS constraints: (A*w) . (B*w) = C*w for each row\n");
	fprintf(out, " * Returns 0 if all constraints satisfied, -1 otherwise\n");
	fprintf(out, " */\n");
	fprintf(out, "int verify_r1cs(const long long *witness) {\n");
	fprintf(out, "    long long Aw[N_CONSTRAINTS] = {0};\n");
	fprintf(out, "    long long Bw[N_CONSTRAINTS] = {0};\n");
	fprintf(out, "    long long Cw[N_CONSTRAINTS] = {0};\n\n");

	fprintf(out, "    /* Compute A*w, B*w, C*w */\n");
	fprintf(out, "    sparse_mv(Aw, A_entries, A_ENTRIES, witness);\n");
	fprintf(out, "    sparse_mv(Bw, B_entries, B_ENTRIES, witness);\n");
	fprintf(out, "    sparse_mv(Cw, C_entries, C_ENTRIES, witness);\n\n");

	fprintf(out, "    /* Check each constraint: Aw[i] * Bw[i] == Cw[i] */\n");
	fprintf(out, "    int errors = 0;\n");
	fprintf(out, "    for (int i = 0; i < N_CONSTRAINTS; i++) {\n");
	fprintf(out, "        long long lhs = Aw[i] * Bw[i];\n");
	fprintf(out, "        long long rhs = Cw[i];\n");
	fprintf(out, "        if (lhs != rhs) {\n");
	fprintf(out, "            printf(\"Constraint %%d FAILED: %%s\\n\", i, constraint_desc[i]);\n");
	fprintf(out, "            printf(\"  (A*w)[%%d] = %%lld\\n\", i, Aw[i]);\n");
	fprintf(out, "            printf(\"  (B*w)[%%d] = %%lld\\n\", i, Bw[i]);\n");
	fprintf(out, "            printf(\"  (A*w)*(B*w) = %%lld\\n\", lhs);\n");
	fprintf(out, "            printf(\"  (C*w)[%%d] = %%lld (expected)\\n\", i, rhs);\n");
	fprintf(out, "            errors++;\n");
	fprintf(out, "        }\n");
	fprintf(out, "    }\n\n");

	fprintf(out, "    return errors == 0 ? 0 : -1;\n");
	fprintf(out, "}\n\n");

	fprintf(out, "/* Print witness values */\n");
	fprintf(out, "void print_witness(const long long *witness) {\n");
	fprintf(out, "    printf(\"Witness values:\\n\");\n");
	fprintf(out, "    for (int i = 0; i < N_WITNESSES; i++) {\n");
	fprintf(out, "        printf(\"  w[%%d] = %%lld  (%%s)\\n\", i, witness[i], witness_names[i]);\n");
	fprintf(out, "    }\n");
	fprintf(out, "}\n\n");

	fprintf(out, "/*\n");
	fprintf(out, " * Example main function\n");
	fprintf(out, " * Fill in witness values and run to verify\n");
	fprintf(out, " */\n");
	fprintf(out, "int main(int argc, char **argv) {\n");
	fprintf(out, "    printf(\"R1CS Sanity Checker: %s\\n\");\n", circuit_name);
	fprintf(out, "    printf(\"Witnesses: %%d, Constraints: %%d\\n\\n\", N_WITNESSES, N_CONSTRAINTS);\n\n");

	fprintf(out, "    /* Initialize witness vector */\n");
	fprintf(out, "    /* w[0] = 1 (constant) */\n");
	fprintf(out, "    long long witness[N_WITNESSES] = {0};\n");
	fprintf(out, "    witness[0] = 1;  /* Constant 1 */\n\n");

	fprintf(out, "    /* TODO: Set your witness values here */\n");
	fprintf(out, "    /* Example for vitalik_minimal (u^3 + u + 5 = 35, where u=3): */\n");
	for (i = 1; i < n_vars && i < 10; i++)
		fprintf(out, "    /* witness[%d] = ???;  // %s */\n", i, r1cs.witnesses[i].name);
	if (n_vars > 10)
		fprintf(out, "    /* ... (and %d more witnesses) */\n", n_vars - 10);
	fprintf(out, "\n");

	fprintf(out, "    /* Parse command line arguments: checker w1 w2 w3 ... */\n");
	fprintf(out, "    if (argc > 1) {\n");
	fprintf(out, "        for (int i = 1; i < argc && i < N_WITNESSES; i++) {\n");
	fprintf(out, "            witness[i] = atoll(argv[i]);\n");
	fprintf(out, "        }\n");
	fprintf(out, "    }\n\n");

	fprintf(out, "    print_witness(witness);\n");
	fprintf(out, "    printf(\"\\n\");\n\n");

	fprintf(out, "    /* Verify constraints */\n");
	fprintf(out, "    if (verify_r1cs(witness) == 0) {\n");
	fprintf(out, "        printf(\"\\nAll %%d constraints PASSED!\\n\", N_CONSTRAINTS);\n");
	fprintf(out, "        return 0;\n");
	fprintf(out, "    } else {\n");
	fprintf(out, "        printf(\"\\nSome constraints FAILED!\\n\");\n");
	fprintf(out, "        return 1;\n");
	fprintf(out, "    }\n");
	fprintf(out, "}\n");
}



/* ----------------------------
        DEBUG PRINT            */

static void print_row(const char *label, r1cs_entry_t *row, int count) {
	int j;

	printf("  %s = [", label);
	for (j = 0; j < count; j++) {
		printf("(%d:%s)", row[j].col, row[j].coeff);
		if (j < count - 1) printf(", ");
	}
	printf("]\n");
}

void r1cs_print(void) {
	r1cs_constraint_t *c;
	int i;

	printf("\n=== R1CS System ===\n");
	printf("Constraints: %d\n", r1cs.n_constraints);
	printf("Witnesses: %d\n", r1cs.n_witnesses);
	printf("\n");

	printf("Witness partition:\n");
	printf("  Constants: %d\n", r1cs.n_constants);
	printf("  Private: %d\n", r1cs.n_private);
	printf("  Deferred: %d\n", r1cs.n_deferred);
	printf("  Gates: %d\n", r1cs.n_gates);
	printf("\n");

	for (i = 0; i < r1cs.n_constraints; i++) {
		c = &r1cs.constraints[i];
		printf("Constraint %d: %s\n", i, c->comment ? c->comment : "(no comment)");
		print_row("A", c->A, c->A_count);
		print_row("B", c->B, c->B_count);
		print_row("C", c->C, c->C_count);
		printf("\n");
	}
	printf("==================\n\n");
}
