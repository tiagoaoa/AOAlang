/*
 * R1CS - Rank-1 Constraint System implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "r1cs.h"
#include "symbol_table.h"

/* Global R1CS system instance */
r1cs_system_t r1cs;

/* Current constraint being built */
static int current_constraint = -1;

void r1cs_init(void) {
    memset(&r1cs, 0, sizeof(r1cs_system_t));
    current_constraint = -1;
}

void r1cs_free(void) {
    for (int i = 0; i < r1cs.n_constraints; i++) {
        if (r1cs.constraints[i].comment) {
            free(r1cs.constraints[i].comment);
        }
        if (r1cs.constraints[i].lhs_var) {
            free(r1cs.constraints[i].lhs_var);
        }
    }
    for (int i = 0; i < r1cs.n_witnesses; i++) {
        if (r1cs.witnesses[i].name) {
            free(r1cs.witnesses[i].name);
        }
        if (r1cs.witnesses[i].symbolic_expr) {
            free(r1cs.witnesses[i].symbolic_expr);
        }
    }
    for (int i = 0; i < r1cs.n_public_inputs; i++) {
        if (r1cs.public_input_names[i]) {
            free(r1cs.public_input_names[i]);
        }
    }
    memset(&r1cs, 0, sizeof(r1cs_system_t));
}

void r1cs_add_constant_one(void) {
    /* Add constant "1" at index 0 */
    r1cs.witnesses[0].index = 0;
    r1cs.witnesses[0].name = strdup("1");
    r1cs.witnesses[0].visibility = VISIBILITY_PUBLIC;
    r1cs.witnesses[0].origin = SYMBOL_DECLARED;
    r1cs.witnesses[0].symbolic_expr = strdup("1");
    r1cs.n_witnesses = 1;

    /* Add to public inputs */
    r1cs.public_input_names[0] = strdup("1");
    r1cs.n_public_inputs = 1;

    /* Add to constant partition */
    r1cs.constant_indices[0] = 0;
    r1cs.n_constants = 1;
}

void r1cs_register_witness(const char *name, int witness_index,
                           visibility_t vis, symbol_origin_t origin,
                           symbol_type_t type, int array_size) {
    if (type == SYMBOL_ARRAY) {
        /* Register each array element */
        for (int i = 0; i < array_size; i++) {
            char elem_name[256];
            snprintf(elem_name, sizeof(elem_name), "%s[%d]", name, i);

            int idx = witness_index + i;
            if (idx >= MAX_VARIABLES) {
                fprintf(stderr, "Error: Too many witness variables\n");
                return;
            }

            /* Ensure we have space */
            if (idx >= r1cs.n_witnesses) {
                r1cs.n_witnesses = idx + 1;
            }

            r1cs.witnesses[idx].index = idx;
            r1cs.witnesses[idx].name = strdup(elem_name);
            r1cs.witnesses[idx].visibility = vis;
            r1cs.witnesses[idx].origin = origin;
            r1cs.witnesses[idx].symbolic_expr = strdup(elem_name);

            /* Add to public inputs if deferred or public */
            if (vis == VISIBILITY_DEFERRED || vis == VISIBILITY_PUBLIC) {
                r1cs.public_input_names[r1cs.n_public_inputs++] = strdup(elem_name);
            }
        }
    } else {
        /* Register scalar */
        if (witness_index >= MAX_VARIABLES) {
            fprintf(stderr, "Error: Too many witness variables\n");
            return;
        }

        if (witness_index >= r1cs.n_witnesses) {
            r1cs.n_witnesses = witness_index + 1;
        }

        r1cs.witnesses[witness_index].index = witness_index;
        r1cs.witnesses[witness_index].name = strdup(name);
        r1cs.witnesses[witness_index].visibility = vis;
        r1cs.witnesses[witness_index].origin = origin;
        r1cs.witnesses[witness_index].symbolic_expr = strdup(name);

        /* Add to public inputs if deferred or public */
        if (vis == VISIBILITY_DEFERRED || vis == VISIBILITY_PUBLIC) {
            r1cs.public_input_names[r1cs.n_public_inputs++] = strdup(name);
        }
    }
}

void r1cs_begin_constraint(const char *lhs_var, const char *comment) {
    if (r1cs.n_constraints >= MAX_CONSTRAINTS) {
        fprintf(stderr, "Error: Too many constraints\n");
        return;
    }

    current_constraint = r1cs.n_constraints;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    memset(c, 0, sizeof(r1cs_constraint_t));
    c->lhs_var = lhs_var ? strdup(lhs_var) : NULL;
    c->comment = comment ? strdup(comment) : NULL;
}

void r1cs_add_A(int col, int coeff) {
    if (current_constraint < 0 || current_constraint >= MAX_CONSTRAINTS) return;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    if (c->A_count >= MAX_ENTRIES_PER_ROW) {
        fprintf(stderr, "Error: Too many entries in A row\n");
        return;
    }

    c->A[c->A_count].col = col;
    c->A[c->A_count].coeff = coeff;
    c->A_count++;
}

void r1cs_add_B(int col, int coeff) {
    if (current_constraint < 0 || current_constraint >= MAX_CONSTRAINTS) return;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    if (c->B_count >= MAX_ENTRIES_PER_ROW) {
        fprintf(stderr, "Error: Too many entries in B row\n");
        return;
    }

    c->B[c->B_count].col = col;
    c->B[c->B_count].coeff = coeff;
    c->B_count++;
}

void r1cs_add_C(int col, int coeff) {
    if (current_constraint < 0 || current_constraint >= MAX_CONSTRAINTS) return;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    if (c->C_count >= MAX_ENTRIES_PER_ROW) {
        fprintf(stderr, "Error: Too many entries in C row\n");
        return;
    }

    c->C[c->C_count].col = col;
    c->C[c->C_count].coeff = coeff;
    c->C_count++;
}

void r1cs_end_constraint(void) {
    if (current_constraint >= 0) {
        r1cs.n_constraints++;
        current_constraint = -1;
    }
}

int r1cs_get_witness_index(const char *var_name) {
    /* Check for array access notation: name[index] */
    char name_buf[256];
    int idx = -1;

    if (strchr(var_name, '[') != NULL) {
        /* Parse array access */
        char *bracket = strchr(var_name, '[');
        size_t name_len = bracket - var_name;
        strncpy(name_buf, var_name, name_len);
        name_buf[name_len] = '\0';
        idx = atoi(bracket + 1);

        /* Look up base array */
        symbol_t *sym = symbol_lookup(name_buf);
        if (sym && sym->type == SYMBOL_ARRAY) {
            return sym->witness_index + idx;
        }
    } else {
        /* Scalar lookup */
        symbol_t *sym = symbol_lookup(var_name);
        if (sym) {
            return sym->witness_index;
        }
    }

    /* Check if it's a number (constant) */
    char *endptr;
    long val = strtol(var_name, &endptr, 10);
    if (*endptr == '\0') {
        /* It's a constant - return index 0 (constant "1") */
        return 0;
    }

    return -1;
}

/* Helper to check if string is a numeric constant and get its value */
static int is_constant(const char *str, int *value) {
    char *endptr;
    long val = strtol(str, &endptr, 10);
    if (*endptr == '\0') {
        *value = (int)val;
        return 1;
    }
    return 0;
}

void r1cs_add_mul_constraint(const char *result, const char *left, const char *right) {
    int result_idx = r1cs_get_witness_index(result);
    int left_const_val, right_const_val;
    int left_is_const = is_constant(left, &left_const_val);
    int right_is_const = is_constant(right, &right_const_val);

    if (result_idx < 0) {
        fprintf(stderr, "Error: Invalid variable in multiplication constraint\n");
        return;
    }

    char comment[512];
    snprintf(comment, sizeof(comment), "%s = %s * %s", result, left, right);

    r1cs_begin_constraint(result, comment);

    /* Handle multiplication with constants:
     * const * var: A[0]=const, B[var]=1, C[result]=1
     * var * const: A[var]=1, B[0]=const, C[result]=1
     * var * var: A[var1]=1, B[var2]=1, C[result]=1
     * const * const: should be folded, but handle anyway
     */
    if (left_is_const) {
        r1cs_add_A(0, left_const_val);
    } else {
        int left_idx = r1cs_get_witness_index(left);
        if (left_idx >= 0) r1cs_add_A(left_idx, 1);
    }

    if (right_is_const) {
        r1cs_add_B(0, right_const_val);
    } else {
        int right_idx = r1cs_get_witness_index(right);
        if (right_idx >= 0) r1cs_add_B(right_idx, 1);
    }

    r1cs_add_C(result_idx, 1);
    r1cs_end_constraint();

    /* Set symbolic expression for gate */
    char expr[512];
    snprintf(expr, sizeof(expr), "%s * %s", left, right);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_add_constraint(const char *result, const char *left, const char *right) {
    int result_idx = r1cs_get_witness_index(result);
    int left_const_val, right_const_val;
    int left_is_const = is_constant(left, &left_const_val);
    int right_is_const = is_constant(right, &right_const_val);

    if (result_idx < 0) {
        fprintf(stderr, "Error: Invalid variable in addition constraint\n");
        return;
    }

    char comment[512];
    snprintf(comment, sizeof(comment), "%s = %s + %s", result, left, right);

    r1cs_begin_constraint(result, comment);

    /* Handle left operand - use constant value as coefficient if it's a constant */
    if (left_is_const) {
        r1cs_add_A(0, left_const_val);  /* coefficient * constant_1 */
    } else {
        int left_idx = r1cs_get_witness_index(left);
        if (left_idx >= 0) r1cs_add_A(left_idx, 1);
    }

    /* Handle right operand */
    if (right_is_const) {
        r1cs_add_A(0, right_const_val);  /* coefficient * constant_1 */
    } else {
        int right_idx = r1cs_get_witness_index(right);
        if (right_idx >= 0) r1cs_add_A(right_idx, 1);
    }

    r1cs_add_B(0, 1);  /* Constant 1 */
    r1cs_add_C(result_idx, 1);
    r1cs_end_constraint();

    /* Set symbolic expression */
    char expr[512];
    snprintf(expr, sizeof(expr), "%s + %s", left, right);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_sub_constraint(const char *result, const char *left, const char *right) {
    int result_idx = r1cs_get_witness_index(result);
    int left_const_val, right_const_val;
    int left_is_const = is_constant(left, &left_const_val);
    int right_is_const = is_constant(right, &right_const_val);

    if (result_idx < 0) {
        fprintf(stderr, "Error: Invalid variable in subtraction constraint\n");
        return;
    }

    char comment[512];
    snprintf(comment, sizeof(comment), "%s = %s - %s", result, left, right);

    r1cs_begin_constraint(result, comment);

    /* Handle left operand */
    if (left_is_const) {
        r1cs_add_A(0, left_const_val);  /* coefficient * constant_1 */
    } else {
        int left_idx = r1cs_get_witness_index(left);
        if (left_idx >= 0) r1cs_add_A(left_idx, 1);
    }

    /* Handle right operand (negated for subtraction) */
    if (right_is_const) {
        r1cs_add_A(0, -right_const_val);  /* -coefficient * constant_1 */
    } else {
        int right_idx = r1cs_get_witness_index(right);
        if (right_idx >= 0) r1cs_add_A(right_idx, -1);
    }

    r1cs_add_B(0, 1);  /* Constant 1 */
    r1cs_add_C(result_idx, 1);
    r1cs_end_constraint();

    /* Set symbolic expression */
    char expr[512];
    snprintf(expr, sizeof(expr), "%s - %s", left, right);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_const_constraint(const char *result, int value) {
    int result_idx = r1cs_get_witness_index(result);

    if (result_idx < 0) {
        fprintf(stderr, "Error: Invalid variable in constant constraint\n");
        return;
    }

    char comment[256];
    snprintf(comment, sizeof(comment), "%s = %d", result, value);

    r1cs_begin_constraint(result, comment);
    r1cs_add_A(result_idx, 1);
    r1cs_add_A(0, -value);  /* -value * 1 */
    r1cs_add_B(0, 1);       /* Constant 1 */
    /* C = 0 (empty) */
    r1cs_end_constraint();

    /* Set symbolic expression */
    char expr[64];
    snprintf(expr, sizeof(expr), "%d", value);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_eq_constraint(const char *result, const char *left, const char *right) {
    int left_idx = r1cs_get_witness_index(left);
    int right_idx = r1cs_get_witness_index(right);

    char comment[512];
    snprintf(comment, sizeof(comment), "%s: %s == %s", result, left, right);

    r1cs_begin_constraint(result, comment);
    r1cs_add_A(left_idx, 1);
    r1cs_add_A(right_idx, -1);
    r1cs_add_B(0, 1);  /* Constant 1 */
    /* C = 0: (left - right) * 1 = 0 */
    r1cs_end_constraint();

    /* Set symbolic expression */
    r1cs_set_gate_expr(result, "0");
}

void r1cs_set_gate_expr(const char *gate_name, const char *expr) {
    int idx = r1cs_get_witness_index(gate_name);
    if (idx >= 0 && idx < r1cs.n_witnesses) {
        if (r1cs.witnesses[idx].symbolic_expr) {
            free(r1cs.witnesses[idx].symbolic_expr);
        }
        r1cs.witnesses[idx].symbolic_expr = strdup(expr);
    }
}

void r1cs_build_partition(void) {
    r1cs.n_constants = 0;
    r1cs.n_private = 0;
    r1cs.n_deferred = 0;
    r1cs.n_gates = 0;

    for (int i = 0; i < r1cs.n_witnesses; i++) {
        witness_entry_t *w = &r1cs.witnesses[i];

        if (i == 0) {
            /* Constant "1" */
            r1cs.constant_indices[r1cs.n_constants++] = i;
        } else if (w->origin == SYMBOL_GATE) {
            r1cs.gate_indices[r1cs.n_gates++] = i;
        } else if (w->visibility == VISIBILITY_PRIVATE) {
            r1cs.private_indices[r1cs.n_private++] = i;
        } else {
            r1cs.deferred_indices[r1cs.n_deferred++] = i;
        }
    }
}

/* Helper function to print JSON array of integers */
static void print_json_int_array(FILE *out, int *arr, int count) {
    fprintf(out, "[");
    for (int i = 0; i < count; i++) {
        fprintf(out, "%d", arr[i]);
        if (i < count - 1) fprintf(out, ", ");
    }
    fprintf(out, "]");
}

/* Helper function to escape string for JSON */
static void print_json_string(FILE *out, const char *str) {
    fprintf(out, "\"");
    if (str) {
        for (const char *p = str; *p; p++) {
            switch (*p) {
                case '"':  fprintf(out, "\\\""); break;
                case '\\': fprintf(out, "\\\\"); break;
                case '\n': fprintf(out, "\\n"); break;
                case '\r': fprintf(out, "\\r"); break;
                case '\t': fprintf(out, "\\t"); break;
                default:   fputc(*p, out);
            }
        }
    }
    fprintf(out, "\"");
}

void r1cs_generate_json(FILE *out, const char *circuit_name) {
    r1cs_build_partition();

    fprintf(out, "{\n");

    /* Header */
    fprintf(out, "  \"circuit\": ");
    print_json_string(out, circuit_name);
    fprintf(out, ",\n");
    fprintf(out, "  \"version\": \"1.0\",\n");
    fprintf(out, "  \"field\": \"bn254\",\n");
    fprintf(out, "\n");

    /* Witness section */
    fprintf(out, "  \"witness\": {\n");
    fprintf(out, "    \"total\": %d,\n", r1cs.n_witnesses);

    /* Partition */
    fprintf(out, "    \"partition\": {\n");

    fprintf(out, "      \"constant\": {\"indices\": ");
    print_json_int_array(out, r1cs.constant_indices, r1cs.n_constants);
    fprintf(out, ", \"names\": [\"1\"]},\n");

    fprintf(out, "      \"private\": {\"indices\": ");
    print_json_int_array(out, r1cs.private_indices, r1cs.n_private);
    fprintf(out, ", \"names\": [");
    for (int i = 0; i < r1cs.n_private; i++) {
        print_json_string(out, r1cs.witnesses[r1cs.private_indices[i]].name);
        if (i < r1cs.n_private - 1) fprintf(out, ", ");
    }
    fprintf(out, "]},\n");

    fprintf(out, "      \"deferred\": {\"indices\": ");
    print_json_int_array(out, r1cs.deferred_indices, r1cs.n_deferred);
    fprintf(out, ", \"names\": [");
    for (int i = 0; i < r1cs.n_deferred; i++) {
        print_json_string(out, r1cs.witnesses[r1cs.deferred_indices[i]].name);
        if (i < r1cs.n_deferred - 1) fprintf(out, ", ");
    }
    fprintf(out, "]},\n");

    fprintf(out, "      \"gates\": {\"indices\": ");
    print_json_int_array(out, r1cs.gate_indices, r1cs.n_gates);
    fprintf(out, ", \"names\": [");
    for (int i = 0; i < r1cs.n_gates; i++) {
        print_json_string(out, r1cs.witnesses[r1cs.gate_indices[i]].name);
        if (i < r1cs.n_gates - 1) fprintf(out, ", ");
    }
    fprintf(out, "]}\n");

    fprintf(out, "    },\n");

    /* Full witness list */
    fprintf(out, "    \"entries\": [\n");
    for (int i = 0; i < r1cs.n_witnesses; i++) {
        witness_entry_t *w = &r1cs.witnesses[i];
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

    /* R1CS matrices */
    fprintf(out, "  \"r1cs\": {\n");
    fprintf(out, "    \"n_constraints\": %d,\n", r1cs.n_constraints);
    fprintf(out, "    \"n_variables\": %d,\n", r1cs.n_witnesses);
    fprintf(out, "\n");

    /* A matrix */
    fprintf(out, "    \"A\": [\n");
    for (int i = 0; i < r1cs.n_constraints; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        fprintf(out, "      {\"row\": %d, \"entries\": [", i);
        for (int j = 0; j < c->A_count; j++) {
            fprintf(out, "{\"col\": %d, \"val\": %d}", c->A[j].col, c->A[j].coeff);
            if (j < c->A_count - 1) fprintf(out, ", ");
        }
        fprintf(out, "]");
        if (c->comment) {
            fprintf(out, ", \"comment\": ");
            print_json_string(out, c->comment);
        }
        fprintf(out, "}");
        if (i < r1cs.n_constraints - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "    ],\n\n");

    /* B matrix */
    fprintf(out, "    \"B\": [\n");
    for (int i = 0; i < r1cs.n_constraints; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        fprintf(out, "      {\"row\": %d, \"entries\": [", i);
        for (int j = 0; j < c->B_count; j++) {
            fprintf(out, "{\"col\": %d, \"val\": %d}", c->B[j].col, c->B[j].coeff);
            if (j < c->B_count - 1) fprintf(out, ", ");
        }
        fprintf(out, "]}");
        if (i < r1cs.n_constraints - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "    ],\n\n");

    /* C matrix */
    fprintf(out, "    \"C\": [\n");
    for (int i = 0; i < r1cs.n_constraints; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        fprintf(out, "      {\"row\": %d, \"entries\": [", i);
        for (int j = 0; j < c->C_count; j++) {
            fprintf(out, "{\"col\": %d, \"val\": %d}", c->C[j].col, c->C[j].coeff);
            if (j < c->C_count - 1) fprintf(out, ", ");
        }
        fprintf(out, "]}");
        if (i < r1cs.n_constraints - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "    ]\n");
    fprintf(out, "  },\n\n");

    /* Public input names for discriminants */
    fprintf(out, "  \"public_inputs\": [\n");
    for (int i = 0; i < r1cs.n_public_inputs; i++) {
        fprintf(out, "    ");
        print_json_string(out, r1cs.public_input_names[i]);
        if (i < r1cs.n_public_inputs - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "  ],\n\n");

    /* Symbolic propagation (gate expressions) */
    fprintf(out, "  \"symbolic_propagation\": {\n");
    for (int i = 0; i < r1cs.n_gates; i++) {
        int idx = r1cs.gate_indices[i];
        witness_entry_t *w = &r1cs.witnesses[idx];
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

void r1cs_generate_dense(FILE *out) {
    r1cs_build_partition();

    int n_vars = r1cs.n_witnesses;
    int n_cons = r1cs.n_constraints;

    /* Print witness vector */
    fprintf(out, "w = [");
    for (int i = 0; i < n_vars; i++) {
        fprintf(out, "%s", r1cs.witnesses[i].name);
        if (i < n_vars - 1) fprintf(out, ", ");
    }
    fprintf(out, "]\n");

    /* Print A matrix */
    fprintf(out, "A\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];

        /* Build dense row */
        int *row = calloc(n_vars, sizeof(int));
        for (int j = 0; j < c->A_count; j++) {
            row[c->A[j].col] = c->A[j].coeff;
        }

        /* Print row */
        fprintf(out, "[");
        for (int j = 0; j < n_vars; j++) {
            fprintf(out, "%d", row[j]);
            if (j < n_vars - 1) fprintf(out, ", ");
        }
        fprintf(out, "]\n");
        free(row);
    }

    /* Print B matrix */
    fprintf(out, "B\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];

        /* Build dense row */
        int *row = calloc(n_vars, sizeof(int));
        for (int j = 0; j < c->B_count; j++) {
            row[c->B[j].col] = c->B[j].coeff;
        }

        /* Print row */
        fprintf(out, "[");
        for (int j = 0; j < n_vars; j++) {
            fprintf(out, "%d", row[j]);
            if (j < n_vars - 1) fprintf(out, ", ");
        }
        fprintf(out, "]\n");
        free(row);
    }

    /* Print C matrix */
    fprintf(out, "C\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];

        /* Build dense row */
        int *row = calloc(n_vars, sizeof(int));
        for (int j = 0; j < c->C_count; j++) {
            row[c->C[j].col] = c->C[j].coeff;
        }

        /* Print row */
        fprintf(out, "[");
        for (int j = 0; j < n_vars; j++) {
            fprintf(out, "%d", row[j]);
            if (j < n_vars - 1) fprintf(out, ", ");
        }
        fprintf(out, "]\n");
        free(row);
    }
}

/*
 * Lagrange interpolation for QAP generation
 * Given values y[0..n-1] at points x=1,2,...,n, compute polynomial coefficients
 * Returns coefficients c[0..n-1] where P(x) = c[0] + c[1]*x + c[2]*x^2 + ...
 */
static void lagrange_interpolate(int n, int *y, double *coeffs) {
    /* Initialize coefficients to zero */
    for (int i = 0; i < n; i++) {
        coeffs[i] = 0.0;
    }

    /* For each point, add its Lagrange basis polynomial scaled by y[i] */
    for (int i = 0; i < n; i++) {
        if (y[i] == 0) continue;  /* Skip if y value is zero */

        /* Compute L_i(x) = product of (x - j) / (i+1 - j) for j != i+1 */
        /* We work with x-values 1, 2, ..., n (not 0-indexed) */
        int xi = i + 1;  /* x-value for this point */

        /* Start with polynomial = y[i] (constant) */
        double basis[MAX_CONSTRAINTS];
        for (int k = 0; k < n; k++) basis[k] = 0.0;
        basis[0] = (double)y[i];

        /* Multiply by (x - xj) / (xi - xj) for each j != i */
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            int xj = j + 1;
            double denom = (double)(xi - xj);

            /* Multiply current polynomial by (x - xj) / denom */
            /* If P(x) = sum(basis[k] * x^k), then P(x) * (x - xj) =
               sum(basis[k] * x^(k+1)) - xj * sum(basis[k] * x^k) */
            double new_basis[MAX_CONSTRAINTS];
            for (int k = 0; k < n; k++) new_basis[k] = 0.0;

            for (int k = 0; k < n; k++) {
                if (basis[k] != 0.0) {
                    /* x^k * (x - xj) = x^(k+1) - xj * x^k */
                    if (k + 1 < n) {
                        new_basis[k + 1] += basis[k] / denom;
                    }
                    new_basis[k] -= (xj * basis[k]) / denom;
                }
            }

            for (int k = 0; k < n; k++) basis[k] = new_basis[k];
        }

        /* Add this basis polynomial to the result */
        for (int k = 0; k < n; k++) {
            coeffs[k] += basis[k];
        }
    }
}

/* Helper to format a coefficient value, rounding near-integers */
static void format_coeff(char *buf, size_t bufsize, double val) {
    double rounded = (val >= 0) ? (int)(val + 0.5) : (int)(val - 0.5);
    if (fabs(val - rounded) < 1e-9) {
        snprintf(buf, bufsize, "%.0f", rounded);
    } else {
        snprintf(buf, bufsize, "%.6g", val);
    }
}

/* Helper to print polynomial coefficients, rounding near-integers */
static void print_poly_coeffs(FILE *out, double *coeffs, int n) {
    char buf[64];

    /* Print coefficient array */
    fprintf(out, "[");
    for (int i = 0; i < n; i++) {
        format_coeff(buf, sizeof(buf), coeffs[i]);
        fprintf(out, "%s", buf);
        if (i < n - 1) fprintf(out, ", ");
    }
    fprintf(out, "]");

    /* Print polynomial expression */
    fprintf(out, "  =  ");
    int first = 1;
    for (int i = n - 1; i >= 0; i--) {
        double val = coeffs[i];
        double rounded = (val >= 0) ? (int)(val + 0.5) : (int)(val - 0.5);
        if (fabs(val - rounded) < 1e-9) val = rounded;

        if (fabs(val) < 1e-9) continue;  /* Skip zero terms */

        /* Print sign and coefficient */
        if (first) {
            format_coeff(buf, sizeof(buf), val);
            fprintf(out, "%s", buf);
            first = 0;
        } else {
            if (val >= 0) {
                format_coeff(buf, sizeof(buf), val);
                fprintf(out, " + %s", buf);
            } else {
                format_coeff(buf, sizeof(buf), -val);
                fprintf(out, " - %s", buf);
            }
        }

        /* Print x^power */
        if (i > 1) {
            fprintf(out, "*x^%d", i);
        } else if (i == 1) {
            fprintf(out, "*x");
        }
        /* i == 0: constant term, no x */
    }

    /* Handle zero polynomial */
    if (first) {
        fprintf(out, "0");
    }
}

void r1cs_generate_qap(FILE *out) {
    r1cs_build_partition();

    int n_vars = r1cs.n_witnesses;
    int n_cons = r1cs.n_constraints;

    if (n_cons == 0) {
        fprintf(out, "# No constraints\n");
        return;
    }

    /* Build dense matrices */
    int **A_dense = malloc(n_cons * sizeof(int *));
    int **B_dense = malloc(n_cons * sizeof(int *));
    int **C_dense = malloc(n_cons * sizeof(int *));

    for (int i = 0; i < n_cons; i++) {
        A_dense[i] = calloc(n_vars, sizeof(int));
        B_dense[i] = calloc(n_vars, sizeof(int));
        C_dense[i] = calloc(n_vars, sizeof(int));

        r1cs_constraint_t *c = &r1cs.constraints[i];
        for (int j = 0; j < c->A_count; j++) {
            A_dense[i][c->A[j].col] = c->A[j].coeff;
        }
        for (int j = 0; j < c->B_count; j++) {
            B_dense[i][c->B[j].col] = c->B[j].coeff;
        }
        for (int j = 0; j < c->C_count; j++) {
            C_dense[i][c->C[j].col] = c->C[j].coeff;
        }
    }

    /* Print header */
    fprintf(out, "# QAP (Quadratic Arithmetic Program)\n");
    fprintf(out, "# Polynomials interpolated at x = 1, 2, ..., %d\n", n_cons);
    fprintf(out, "# Coefficients: [const, x, x^2, ...]\n");
    fprintf(out, "# P(x) = (w.A(x)) * (w.B(x)) - (w.C(x)) = H(x) * T(x)\n");
    fprintf(out, "# T(x) = (x-1)(x-2)...(x-%d)\n\n", n_cons);

    /* Print witness vector */
    fprintf(out, "w = [");
    for (int i = 0; i < n_vars; i++) {
        fprintf(out, "%s", r1cs.witnesses[i].name);
        if (i < n_vars - 1) fprintf(out, ", ");
    }
    fprintf(out, "]\n\n");

    /* Allocate space for column values and coefficients */
    int *col_vals = malloc(n_cons * sizeof(int));
    double *coeffs = malloc(n_cons * sizeof(double));

    /* Print A polynomials */
    fprintf(out, "A(x) polynomials:\n");
    for (int j = 0; j < n_vars; j++) {
        /* Extract column j from A */
        for (int i = 0; i < n_cons; i++) {
            col_vals[i] = A_dense[i][j];
        }
        lagrange_interpolate(n_cons, col_vals, coeffs);
        fprintf(out, "  A_%s(x) = ", r1cs.witnesses[j].name);
        print_poly_coeffs(out, coeffs, n_cons);
        fprintf(out, "\n");
    }

    /* Print B polynomials */
    fprintf(out, "\nB(x) polynomials:\n");
    for (int j = 0; j < n_vars; j++) {
        /* Extract column j from B */
        for (int i = 0; i < n_cons; i++) {
            col_vals[i] = B_dense[i][j];
        }
        lagrange_interpolate(n_cons, col_vals, coeffs);
        fprintf(out, "  B_%s(x) = ", r1cs.witnesses[j].name);
        print_poly_coeffs(out, coeffs, n_cons);
        fprintf(out, "\n");
    }

    /* Print C polynomials */
    fprintf(out, "\nC(x) polynomials:\n");
    for (int j = 0; j < n_vars; j++) {
        /* Extract column j from C */
        for (int i = 0; i < n_cons; i++) {
            col_vals[i] = C_dense[i][j];
        }
        lagrange_interpolate(n_cons, col_vals, coeffs);
        fprintf(out, "  C_%s(x) = ", r1cs.witnesses[j].name);
        print_poly_coeffs(out, coeffs, n_cons);
        fprintf(out, "\n");
    }

    /* Print target polynomial T(x) = (x-1)(x-2)...(x-n) */
    fprintf(out, "\nT(x) = ");
    for (int i = 1; i <= n_cons; i++) {
        fprintf(out, "(x-%d)", i);
    }
    fprintf(out, "\n");

    /* Print the combined P(x) expression */
    fprintf(out, "\n# P(x) = (w.A(x)) * (w.B(x)) - (w.C(x))\n\n");

    /* Print w.A(x) */
    fprintf(out, "w.A(x) = ");
    for (int j = 0; j < n_vars; j++) {
        if (j > 0) fprintf(out, " + ");
        fprintf(out, "%s*A_%s(x)", r1cs.witnesses[j].name, r1cs.witnesses[j].name);
    }
    fprintf(out, "\n");

    /* Print w.B(x) */
    fprintf(out, "w.B(x) = ");
    for (int j = 0; j < n_vars; j++) {
        if (j > 0) fprintf(out, " + ");
        fprintf(out, "%s*B_%s(x)", r1cs.witnesses[j].name, r1cs.witnesses[j].name);
    }
    fprintf(out, "\n");

    /* Print w.C(x) */
    fprintf(out, "w.C(x) = ");
    for (int j = 0; j < n_vars; j++) {
        if (j > 0) fprintf(out, " + ");
        fprintf(out, "%s*C_%s(x)", r1cs.witnesses[j].name, r1cs.witnesses[j].name);
    }
    fprintf(out, "\n");

    fprintf(out, "\nP(x) = (w.A(x)) * (w.B(x)) - (w.C(x)) = H(x) * T(x)\n");

    /* Cleanup */
    free(col_vals);
    free(coeffs);
    for (int i = 0; i < n_cons; i++) {
        free(A_dense[i]);
        free(B_dense[i]);
        free(C_dense[i]);
    }
    free(A_dense);
    free(B_dense);
    free(C_dense);
}

void r1cs_print(void) {
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

    for (int i = 0; i < r1cs.n_constraints; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        printf("Constraint %d: %s\n", i, c->comment ? c->comment : "(no comment)");

        printf("  A = [");
        for (int j = 0; j < c->A_count; j++) {
            printf("(%d:%d)", c->A[j].col, c->A[j].coeff);
            if (j < c->A_count - 1) printf(", ");
        }
        printf("]\n");

        printf("  B = [");
        for (int j = 0; j < c->B_count; j++) {
            printf("(%d:%d)", c->B[j].col, c->B[j].coeff);
            if (j < c->B_count - 1) printf(", ");
        }
        printf("]\n");

        printf("  C = [");
        for (int j = 0; j < c->C_count; j++) {
            printf("(%d:%d)", c->C[j].col, c->C[j].coeff);
            if (j < c->C_count - 1) printf(", ");
        }
        printf("]\n\n");
    }
    printf("==================\n\n");
}

/* Expression node helpers */
expr_node_t *expr_create_var(const char *name) {
    expr_node_t *node = (expr_node_t *)malloc(sizeof(expr_node_t));
    node->type = EXPR_VAR;
    node->data.var_name = strdup(name);
    return node;
}

expr_node_t *expr_create_array(const char *name, int index) {
    expr_node_t *node = (expr_node_t *)malloc(sizeof(expr_node_t));
    node->type = EXPR_ARRAY_ACCESS;
    node->data.array_access.array_name = strdup(name);
    node->data.array_access.index = index;
    return node;
}

expr_node_t *expr_create_const(int value) {
    expr_node_t *node = (expr_node_t *)malloc(sizeof(expr_node_t));
    node->type = EXPR_CONST;
    node->data.const_val = value;
    return node;
}

expr_node_t *expr_create_binary(expr_type_t type, expr_node_t *left, expr_node_t *right) {
    expr_node_t *node = (expr_node_t *)malloc(sizeof(expr_node_t));
    node->type = type;
    node->data.binary.left = left;
    node->data.binary.right = right;
    return node;
}

void expr_free(expr_node_t *node) {
    if (!node) return;

    switch (node->type) {
        case EXPR_VAR:
            free(node->data.var_name);
            break;
        case EXPR_ARRAY_ACCESS:
            free(node->data.array_access.array_name);
            break;
        case EXPR_ADD:
        case EXPR_SUB:
        case EXPR_MUL:
        case EXPR_EQ:
            expr_free(node->data.binary.left);
            expr_free(node->data.binary.right);
            break;
        default:
            break;
    }
    free(node);
}

char *expr_to_string(expr_node_t *node) {
    if (!node) return strdup("");

    char buf[1024];

    switch (node->type) {
        case EXPR_VAR:
            return strdup(node->data.var_name);

        case EXPR_ARRAY_ACCESS:
            snprintf(buf, sizeof(buf), "%s[%d]",
                     node->data.array_access.array_name,
                     node->data.array_access.index);
            return strdup(buf);

        case EXPR_CONST:
            snprintf(buf, sizeof(buf), "%d", node->data.const_val);
            return strdup(buf);

        case EXPR_ADD:
        case EXPR_SUB:
        case EXPR_MUL: {
            char *left_str = expr_to_string(node->data.binary.left);
            char *right_str = expr_to_string(node->data.binary.right);
            char op = (node->type == EXPR_ADD) ? '+' :
                      (node->type == EXPR_SUB) ? '-' : '*';
            snprintf(buf, sizeof(buf), "(%s %c %s)", left_str, op, right_str);
            free(left_str);
            free(right_str);
            return strdup(buf);
        }

        case EXPR_EQ: {
            char *left_str = expr_to_string(node->data.binary.left);
            char *right_str = expr_to_string(node->data.binary.right);
            snprintf(buf, sizeof(buf), "(%s == %s)", left_str, right_str);
            free(left_str);
            free(right_str);
            return strdup(buf);
        }

        default:
            return strdup("");
    }
}
