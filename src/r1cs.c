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

static int grow_capacity(int current_capacity, int minimum_capacity, int initial_capacity) {
    int new_capacity = current_capacity > 0 ? current_capacity : initial_capacity;
    while (new_capacity < minimum_capacity) {
        new_capacity *= 2;
    }
    return new_capacity;
}

static void *xrealloc_zero_tail(void *ptr, size_t old_count, size_t new_count, size_t elem_size) {
    void *new_ptr = realloc(ptr, new_count * elem_size);
    if (!new_ptr) {
        fprintf(stderr, "Error: Out of memory while growing R1CS storage\n");
        exit(1);
    }

    if (new_count > old_count) {
        size_t old_size = old_count * elem_size;
        size_t tail_size = (new_count - old_count) * elem_size;
        memset((char *)new_ptr + old_size, 0, tail_size);
    }

    return new_ptr;
}

static void ensure_constraint_capacity(int minimum_capacity) {
    if (minimum_capacity <= r1cs.constraints_capacity) return;

    int old_capacity = r1cs.constraints_capacity;
    int new_capacity = grow_capacity(old_capacity, minimum_capacity, INITIAL_CONSTRAINT_CAPACITY);
    r1cs.constraints = xrealloc_zero_tail(
        r1cs.constraints,
        old_capacity,
        new_capacity,
        sizeof(r1cs_constraint_t)
    );
    r1cs.constraints_capacity = new_capacity;
}

static void ensure_witness_capacity(int minimum_capacity) {
    if (minimum_capacity <= r1cs.witnesses_capacity) return;

    int old_capacity = r1cs.witnesses_capacity;
    int new_capacity = grow_capacity(old_capacity, minimum_capacity, INITIAL_WITNESS_CAPACITY);

    r1cs.witnesses = xrealloc_zero_tail(
        r1cs.witnesses,
        old_capacity,
        new_capacity,
        sizeof(witness_entry_t)
    );
    r1cs.constant_indices = xrealloc_zero_tail(
        r1cs.constant_indices,
        old_capacity,
        new_capacity,
        sizeof(int)
    );
    r1cs.private_indices = xrealloc_zero_tail(
        r1cs.private_indices,
        old_capacity,
        new_capacity,
        sizeof(int)
    );
    r1cs.deferred_indices = xrealloc_zero_tail(
        r1cs.deferred_indices,
        old_capacity,
        new_capacity,
        sizeof(int)
    );
    r1cs.gate_indices = xrealloc_zero_tail(
        r1cs.gate_indices,
        old_capacity,
        new_capacity,
        sizeof(int)
    );
    r1cs.public_input_names = xrealloc_zero_tail(
        r1cs.public_input_names,
        old_capacity,
        new_capacity,
        sizeof(char *)
    );
    r1cs.witnesses_capacity = new_capacity;
}

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
    ensure_witness_capacity(1);

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
        ensure_witness_capacity(witness_index + array_size);

        /* Register each array element */
        for (int i = 0; i < array_size; i++) {
            char elem_name[256];
            snprintf(elem_name, sizeof(elem_name), "%s[%d]", name, i);

            int idx = witness_index + i;

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
        ensure_witness_capacity(witness_index + 1);

        /* Register scalar */
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
    ensure_constraint_capacity(r1cs.n_constraints + 1);

    current_constraint = r1cs.n_constraints;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    memset(c, 0, sizeof(r1cs_constraint_t));
    c->lhs_var = lhs_var ? strdup(lhs_var) : NULL;
    c->comment = comment ? strdup(comment) : NULL;
}

void r1cs_add_A(int col, const char *coeff) {
    if (current_constraint < 0 || current_constraint >= r1cs.constraints_capacity) return;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    if (c->A_count >= MAX_ENTRIES_PER_ROW) {
        fprintf(stderr, "Error: Too many entries in A row\n");
        return;
    }

    c->A[c->A_count].col = col;
    strncpy(c->A[c->A_count].coeff, coeff, 79);
    c->A[c->A_count].coeff[79] = '\0';
    c->A_count++;
}

void r1cs_add_B(int col, const char *coeff) {
    if (current_constraint < 0 || current_constraint >= r1cs.constraints_capacity) return;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    if (c->B_count >= MAX_ENTRIES_PER_ROW) {
        fprintf(stderr, "Error: Too many entries in B row\n");
        return;
    }

    c->B[c->B_count].col = col;
    strncpy(c->B[c->B_count].coeff, coeff, 79);
    c->B[c->B_count].coeff[79] = '\0';
    c->B_count++;
}

void r1cs_add_C(int col, const char *coeff) {
    if (current_constraint < 0 || current_constraint >= r1cs.constraints_capacity) return;
    r1cs_constraint_t *c = &r1cs.constraints[current_constraint];

    if (c->C_count >= MAX_ENTRIES_PER_ROW) {
        fprintf(stderr, "Error: Too many entries in C row\n");
        return;
    }

    c->C[c->C_count].col = col;
    strncpy(c->C[c->C_count].coeff, coeff, 79);
    c->C[c->C_count].coeff[79] = '\0';
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
        const char *bracket = strchr(var_name, '[');
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

    /* Check if it's a number (constant) - return index 0 (the constant "1" wire) */
    const char *p = var_name;
    if (*p == '-') p++;
    if (*p >= '0' && *p <= '9') {
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '\0') return 0;
    }

    return -1;
}

/* Helper to check if string is a numeric constant and get its string value */
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

/* Helper to negate a string coefficient */
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

void r1cs_add_mul_constraint(const char *result, const char *left, const char *right) {
    int result_idx = r1cs_get_witness_index(result);
    char left_const_val[80], right_const_val[80];
    int left_is_const = is_constant(left, left_const_val);
    int right_is_const = is_constant(right, right_const_val);

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
        if (left_idx >= 0) r1cs_add_A(left_idx, "1");
    }

    if (right_is_const) {
        r1cs_add_B(0, right_const_val);
    } else {
        int right_idx = r1cs_get_witness_index(right);
        if (right_idx >= 0) r1cs_add_B(right_idx, "1");
    }

    r1cs_add_C(result_idx, "1");
    r1cs_end_constraint();

    /* Set symbolic expression for gate */
    char expr[512];
    snprintf(expr, sizeof(expr), "%s * %s", left, right);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_add_constraint(const char *result, const char *left, const char *right) {
    int result_idx = r1cs_get_witness_index(result);
    char left_const_val[80], right_const_val[80];
    int left_is_const = is_constant(left, left_const_val);
    int right_is_const = is_constant(right, right_const_val);

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
        if (left_idx >= 0) r1cs_add_A(left_idx, "1");
    }

    /* Handle right operand */
    if (right_is_const) {
        r1cs_add_A(0, right_const_val);  /* coefficient * constant_1 */
    } else {
        int right_idx = r1cs_get_witness_index(right);
        if (right_idx >= 0) r1cs_add_A(right_idx, "1");
    }

    r1cs_add_B(0, "1");  /* Constant 1 */
    r1cs_add_C(result_idx, "1");
    r1cs_end_constraint();

    /* Set symbolic expression */
    char expr[512];
    snprintf(expr, sizeof(expr), "%s + %s", left, right);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_sub_constraint(const char *result, const char *left, const char *right) {
    int result_idx = r1cs_get_witness_index(result);
    char left_const_val[80], right_const_val[80];
    int left_is_const = is_constant(left, left_const_val);
    int right_is_const = is_constant(right, right_const_val);

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
        if (left_idx >= 0) r1cs_add_A(left_idx, "1");
    }

    /* Handle right operand (negated for subtraction) */
    if (right_is_const) {
        char neg_val[80];
        negate_coeff(right_const_val, neg_val, sizeof(neg_val));
        r1cs_add_A(0, neg_val);  /* -coefficient * constant_1 */
    } else {
        int right_idx = r1cs_get_witness_index(right);
        if (right_idx >= 0) r1cs_add_A(right_idx, "-1");
    }

    r1cs_add_B(0, "1");  /* Constant 1 */
    r1cs_add_C(result_idx, "1");
    r1cs_end_constraint();

    /* Set symbolic expression */
    char expr[512];
    snprintf(expr, sizeof(expr), "%s - %s", left, right);
    r1cs_set_gate_expr(result, expr);
}

void r1cs_add_const_constraint(const char *result, const char *value) {
    int result_idx = r1cs_get_witness_index(result);

    if (result_idx < 0) {
        fprintf(stderr, "Error: Invalid variable in constant constraint\n");
        return;
    }

    char comment[256];
    snprintf(comment, sizeof(comment), "%s = %s", result, value);

    r1cs_begin_constraint(result, comment);
    r1cs_add_A(result_idx, "1");
    char neg_val[80];
    negate_coeff(value, neg_val, sizeof(neg_val));
    r1cs_add_A(0, neg_val);  /* -value * 1 */
    r1cs_add_B(0, "1");      /* Constant 1 */
    /* C = 0 (empty) */
    r1cs_end_constraint();

    /* Set symbolic expression */
    r1cs_set_gate_expr(result, value);
}

void r1cs_add_eq_constraint(const char *result, const char *left, const char *right) {
    int left_idx = r1cs_get_witness_index(left);
    int right_idx = r1cs_get_witness_index(right);

    char comment[512];
    snprintf(comment, sizeof(comment), "%s: %s == %s", result, left, right);

    r1cs_begin_constraint(result, comment);
    r1cs_add_A(left_idx, "1");
    r1cs_add_A(right_idx, "-1");
    r1cs_add_B(0, "1");  /* Constant 1 */
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
            fprintf(out, "{\"col\": %d, \"val\": \"%s\"}", c->A[j].col, c->A[j].coeff);
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
            fprintf(out, "{\"col\": %d, \"val\": \"%s\"}", c->B[j].col, c->B[j].coeff);
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
            fprintf(out, "{\"col\": %d, \"val\": \"%s\"}", c->C[j].col, c->C[j].coeff);
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

    /* Helper: build dense string row from sparse entries */
    /* Allocate a row of string pointers, default "0" */
    char **row = malloc(n_vars * sizeof(char *));

    /* Print A matrix */
    fprintf(out, "A\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];

        for (int j = 0; j < n_vars; j++) row[j] = "0";
        for (int j = 0; j < c->A_count; j++) {
            row[c->A[j].col] = c->A[j].coeff;
        }

        fprintf(out, "[");
        for (int j = 0; j < n_vars; j++) {
            fprintf(out, "%s", row[j]);
            if (j < n_vars - 1) fprintf(out, ", ");
        }
        fprintf(out, "]\n");
    }

    /* Print B matrix */
    fprintf(out, "B\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];

        for (int j = 0; j < n_vars; j++) row[j] = "0";
        for (int j = 0; j < c->B_count; j++) {
            row[c->B[j].col] = c->B[j].coeff;
        }

        fprintf(out, "[");
        for (int j = 0; j < n_vars; j++) {
            fprintf(out, "%s", row[j]);
            if (j < n_vars - 1) fprintf(out, ", ");
        }
        fprintf(out, "]\n");
    }

    /* Print C matrix */
    fprintf(out, "C\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];

        for (int j = 0; j < n_vars; j++) row[j] = "0";
        for (int j = 0; j < c->C_count; j++) {
            row[c->C[j].col] = c->C[j].coeff;
        }

        fprintf(out, "[");
        for (int j = 0; j < n_vars; j++) {
            fprintf(out, "%s", row[j]);
            if (j < n_vars - 1) fprintf(out, ", ");
        }
        fprintf(out, "]\n");
    }

    free(row);
}

/*
 * Lagrange interpolation for QAP generation
 * Given values y[0..n-1] at points x=1,2,...,n, compute polynomial coefficients
 * Returns coefficients c[0..n-1] where P(x) = c[0] + c[1]*x + c[2]*x^2 + ...
 */
static void lagrange_interpolate(int n, int *y, double *coeffs) {
    double *basis = calloc(n, sizeof(double));
    double *new_basis = calloc(n, sizeof(double));

    if (!basis || !new_basis) {
        fprintf(stderr, "Error: Out of memory during QAP interpolation\n");
        free(basis);
        free(new_basis);
        return;
    }

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

    free(basis);
    free(new_basis);
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

    /* NOTE: QAP polynomial generation uses double/float arithmetic via
       Lagrange interpolation. Coefficients are converted to int via atoi().
       This does NOT support bigint (>254-bit) coefficients. QAP output is
       a debug/educational tool; for production use, the JSON sparse output
       preserves full coefficient precision as strings. */

    /* Build dense matrices (coefficients converted to int for polynomial math) */
    int **A_dense = malloc(n_cons * sizeof(int *));
    int **B_dense = malloc(n_cons * sizeof(int *));
    int **C_dense = malloc(n_cons * sizeof(int *));

    for (int i = 0; i < n_cons; i++) {
        A_dense[i] = calloc(n_vars, sizeof(int));
        B_dense[i] = calloc(n_vars, sizeof(int));
        C_dense[i] = calloc(n_vars, sizeof(int));

        r1cs_constraint_t *c = &r1cs.constraints[i];
        for (int j = 0; j < c->A_count; j++) {
            A_dense[i][c->A[j].col] = atoi(c->A[j].coeff);
        }
        for (int j = 0; j < c->B_count; j++) {
            B_dense[i][c->B[j].col] = atoi(c->B[j].coeff);
        }
        for (int j = 0; j < c->C_count; j++) {
            C_dense[i][c->C[j].col] = atoi(c->C[j].coeff);
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

    /* Compute and store all polynomials for w.A(x), w.B(x), w.C(x) */
    double **A_polys = malloc(n_vars * sizeof(double *));
    double **B_polys = malloc(n_vars * sizeof(double *));
    double **C_polys = malloc(n_vars * sizeof(double *));

    for (int j = 0; j < n_vars; j++) {
        A_polys[j] = malloc(n_cons * sizeof(double));
        B_polys[j] = malloc(n_cons * sizeof(double));
        C_polys[j] = malloc(n_cons * sizeof(double));

        for (int i = 0; i < n_cons; i++) {
            col_vals[i] = A_dense[i][j];
        }
        lagrange_interpolate(n_cons, col_vals, A_polys[j]);

        for (int i = 0; i < n_cons; i++) {
            col_vals[i] = B_dense[i][j];
        }
        lagrange_interpolate(n_cons, col_vals, B_polys[j]);

        for (int i = 0; i < n_cons; i++) {
            col_vals[i] = C_dense[i][j];
        }
        lagrange_interpolate(n_cons, col_vals, C_polys[j]);
    }

    /* Print the combined P(x) expression */
    fprintf(out, "\n# P(x) = (w.A(x)) * (w.B(x)) - (w.C(x))\n\n");

    /* Helper to print a polynomial term with witness variable */
    char buf[64];

    /* Print w.A(x) expanded */
    fprintf(out, "w.A(x) = ");
    int first_term = 1;
    for (int j = 0; j < n_vars; j++) {
        /* Check if polynomial is non-zero */
        int is_zero = 1;
        for (int k = 0; k < n_cons; k++) {
            if (fabs(A_polys[j][k]) > 1e-9) { is_zero = 0; break; }
        }
        if (is_zero) continue;

        if (!first_term) fprintf(out, " + ");
        first_term = 0;
        fprintf(out, "%s*(", r1cs.witnesses[j].name);

        int first_coef = 1;
        for (int k = n_cons - 1; k >= 0; k--) {
            double val = A_polys[j][k];
            double rounded = (val >= 0) ? (int)(val + 0.5) : (int)(val - 0.5);
            if (fabs(val - rounded) < 1e-9) val = rounded;
            if (fabs(val) < 1e-9) continue;

            if (first_coef) {
                format_coeff(buf, sizeof(buf), val);
                fprintf(out, "%s", buf);
                first_coef = 0;
            } else {
                if (val >= 0) {
                    format_coeff(buf, sizeof(buf), val);
                    fprintf(out, " + %s", buf);
                } else {
                    format_coeff(buf, sizeof(buf), -val);
                    fprintf(out, " - %s", buf);
                }
            }
            if (k > 1) fprintf(out, "*x^%d", k);
            else if (k == 1) fprintf(out, "*x");
        }
        if (first_coef) fprintf(out, "0");
        fprintf(out, ")");
    }
    if (first_term) fprintf(out, "0");
    fprintf(out, "\n\n");

    /* Print w.B(x) expanded */
    fprintf(out, "w.B(x) = ");
    first_term = 1;
    for (int j = 0; j < n_vars; j++) {
        int is_zero = 1;
        for (int k = 0; k < n_cons; k++) {
            if (fabs(B_polys[j][k]) > 1e-9) { is_zero = 0; break; }
        }
        if (is_zero) continue;

        if (!first_term) fprintf(out, " + ");
        first_term = 0;
        fprintf(out, "%s*(", r1cs.witnesses[j].name);

        int first_coef = 1;
        for (int k = n_cons - 1; k >= 0; k--) {
            double val = B_polys[j][k];
            double rounded = (val >= 0) ? (int)(val + 0.5) : (int)(val - 0.5);
            if (fabs(val - rounded) < 1e-9) val = rounded;
            if (fabs(val) < 1e-9) continue;

            if (first_coef) {
                format_coeff(buf, sizeof(buf), val);
                fprintf(out, "%s", buf);
                first_coef = 0;
            } else {
                if (val >= 0) {
                    format_coeff(buf, sizeof(buf), val);
                    fprintf(out, " + %s", buf);
                } else {
                    format_coeff(buf, sizeof(buf), -val);
                    fprintf(out, " - %s", buf);
                }
            }
            if (k > 1) fprintf(out, "*x^%d", k);
            else if (k == 1) fprintf(out, "*x");
        }
        if (first_coef) fprintf(out, "0");
        fprintf(out, ")");
    }
    if (first_term) fprintf(out, "0");
    fprintf(out, "\n\n");

    /* Print w.C(x) expanded */
    fprintf(out, "w.C(x) = ");
    first_term = 1;
    for (int j = 0; j < n_vars; j++) {
        int is_zero = 1;
        for (int k = 0; k < n_cons; k++) {
            if (fabs(C_polys[j][k]) > 1e-9) { is_zero = 0; break; }
        }
        if (is_zero) continue;

        if (!first_term) fprintf(out, " + ");
        first_term = 0;
        fprintf(out, "%s*(", r1cs.witnesses[j].name);

        int first_coef = 1;
        for (int k = n_cons - 1; k >= 0; k--) {
            double val = C_polys[j][k];
            double rounded = (val >= 0) ? (int)(val + 0.5) : (int)(val - 0.5);
            if (fabs(val - rounded) < 1e-9) val = rounded;
            if (fabs(val) < 1e-9) continue;

            if (first_coef) {
                format_coeff(buf, sizeof(buf), val);
                fprintf(out, "%s", buf);
                first_coef = 0;
            } else {
                if (val >= 0) {
                    format_coeff(buf, sizeof(buf), val);
                    fprintf(out, " + %s", buf);
                } else {
                    format_coeff(buf, sizeof(buf), -val);
                    fprintf(out, " - %s", buf);
                }
            }
            if (k > 1) fprintf(out, "*x^%d", k);
            else if (k == 1) fprintf(out, "*x");
        }
        if (first_coef) fprintf(out, "0");
        fprintf(out, ")");
    }
    if (first_term) fprintf(out, "0");
    fprintf(out, "\n\n");

    fprintf(out, "P(x) = (w.A(x)) * (w.B(x)) - (w.C(x)) = H(x) * T(x)\n\n");

    /* Compute simplified P(x) with coefficients for each power of x */
    /* P(x) = w.A(x) * w.B(x) - w.C(x)
     * w.A(x) and w.B(x) each have degree n_cons-1, so product has degree 2*(n_cons-1)
     * Each coefficient of x^k is an expression in witness variables
     */
    int p_degree = 2 * (n_cons - 1);

    /* Build coefficient matrix for quadratic terms: coef_quad[i][j] for w_i * w_j
     * and linear terms: coef_lin[i] for w_i (from -w.C(x))
     * For each power of x */
    fprintf(out, "# Simplified P(x) - coefficients are expressions in witness variables\n\n");

    for (int m = p_degree; m >= 0; m--) {
        /* Allocate and zero coefficient accumulators */
        double **coef_quad = malloc(n_vars * sizeof(double *));
        double *coef_lin = calloc(n_vars, sizeof(double));
        for (int i = 0; i < n_vars; i++) {
            coef_quad[i] = calloc(n_vars, sizeof(double));
        }

        /* Accumulate terms from w.A(x) * w.B(x) */
        for (int k = 0; k <= m && k < n_cons; k++) {
            int l = m - k;
            if (l < 0 || l >= n_cons) continue;

            for (int i = 0; i < n_vars; i++) {
                double a_ik = A_polys[i][k];
                if (fabs(a_ik) < 1e-9) continue;

                for (int j = 0; j < n_vars; j++) {
                    double b_jl = B_polys[j][l];
                    if (fabs(b_jl) < 1e-9) continue;

                    /* Combine w_i*w_j and w_j*w_i into w_i*w_j where i <= j */
                    if (i <= j) {
                        coef_quad[i][j] += a_ik * b_jl;
                    } else {
                        coef_quad[j][i] += a_ik * b_jl;
                    }
                }
            }
        }

        /* Subtract terms from w.C(x) */
        if (m < n_cons) {
            for (int i = 0; i < n_vars; i++) {
                coef_lin[i] -= C_polys[i][m];
            }
        }

        /* Print coefficient for x^m */
        fprintf(out, "coef(x^%d) = ", m);
        int first = 1;

        /* Print quadratic terms */
        for (int i = 0; i < n_vars; i++) {
            for (int j = i; j < n_vars; j++) {
                double c = coef_quad[i][j];
                double c_r = (c >= 0) ? (int)(c + 0.5) : (int)(c - 0.5);
                if (fabs(c - c_r) < 1e-9) c = c_r;
                if (fabs(c) < 1e-9) continue;

                if (!first) fprintf(out, c >= 0 ? " + " : " - ");
                else if (c < 0) fprintf(out, "-");
                first = 0;

                format_coeff(buf, sizeof(buf), fabs(c));

                /* Special handling for constant "1" */
                int i_is_one = (strcmp(r1cs.witnesses[i].name, "1") == 0);
                int j_is_one = (strcmp(r1cs.witnesses[j].name, "1") == 0);

                if (i_is_one && j_is_one) {
                    fprintf(out, "%s", buf);  /* Just the coefficient */
                } else if (i_is_one) {
                    fprintf(out, "%s*%s", buf, r1cs.witnesses[j].name);
                } else if (j_is_one) {
                    fprintf(out, "%s*%s", buf, r1cs.witnesses[i].name);
                } else if (i == j) {
                    fprintf(out, "%s*%s^2", buf, r1cs.witnesses[i].name);
                } else {
                    fprintf(out, "%s*%s*%s", buf, r1cs.witnesses[i].name, r1cs.witnesses[j].name);
                }
            }
        }

        /* Print linear terms (from -w.C) */
        for (int i = 0; i < n_vars; i++) {
            double c = coef_lin[i];
            double c_r = (c >= 0) ? (int)(c + 0.5) : (int)(c - 0.5);
            if (fabs(c - c_r) < 1e-9) c = c_r;
            if (fabs(c) < 1e-9) continue;

            /* Skip if witness is "1" - already handled in quadratic */
            if (strcmp(r1cs.witnesses[i].name, "1") == 0) {
                if (!first) fprintf(out, c >= 0 ? " + " : " - ");
                else if (c < 0) fprintf(out, "-");
                first = 0;
                format_coeff(buf, sizeof(buf), fabs(c));
                fprintf(out, "%s", buf);
                continue;
            }

            if (!first) fprintf(out, c >= 0 ? " + " : " - ");
            else if (c < 0) fprintf(out, "-");
            first = 0;

            format_coeff(buf, sizeof(buf), fabs(c));
            fprintf(out, "%s*%s", buf, r1cs.witnesses[i].name);
        }

        if (first) fprintf(out, "0");
        fprintf(out, "\n");

        /* Free */
        for (int i = 0; i < n_vars; i++) free(coef_quad[i]);
        free(coef_quad);
        free(coef_lin);
    }

    /* Print final P(x) formula */
    fprintf(out, "\nP(x) = ");
    for (int m = p_degree; m >= 0; m--) {
        if (m < p_degree) fprintf(out, " + ");
        fprintf(out, "coef(x^%d)", m);
        if (m > 1) fprintf(out, "*x^%d", m);
        else if (m == 1) fprintf(out, "*x");
    }
    fprintf(out, "\n");

    /* Compute Z(x) = (x-1)(x-2)...(x-n) coefficients */
    fprintf(out, "\n# Vanishing polynomial Z(x) = (x-1)(x-2)...(x-%d)\n", n_cons);

    /* Z(x) has degree n_cons, so we need n_cons+1 coefficients */
    double *Z_coeffs = calloc(n_cons + 1, sizeof(double));
    Z_coeffs[0] = 1.0;  /* Start with polynomial = 1 */
    int Z_degree = 0;

    /* Multiply by (x - k) for k = 1 to n_cons */
    for (int k = 1; k <= n_cons; k++) {
        /* Multiply current polynomial by (x - k) */
        /* If P(x) = sum(Z_coeffs[i] * x^i), then P(x) * (x - k) =
           sum(Z_coeffs[i] * x^(i+1)) - k * sum(Z_coeffs[i] * x^i) */
        double *new_Z = calloc(n_cons + 1, sizeof(double));
        for (int i = 0; i <= Z_degree; i++) {
            new_Z[i + 1] += Z_coeffs[i];      /* x^(i+1) term */
            new_Z[i] -= k * Z_coeffs[i];       /* -k * x^i term */
        }
        Z_degree++;
        for (int i = 0; i <= Z_degree; i++) {
            Z_coeffs[i] = new_Z[i];
        }
        free(new_Z);
    }

    /* Print Z(x) coefficients and polynomial */
    fprintf(out, "Z(x) = ");
    print_poly_coeffs(out, Z_coeffs, n_cons + 1);
    fprintf(out, "\n");

    /* H(x) = P(x) / Z(x) for valid witnesses */
    /* H(x) has degree 2*(n_cons-1) - n_cons = n_cons - 2 */
    int h_degree = n_cons - 2;
    if (h_degree >= 0) {
        fprintf(out, "\n# Quotient polynomial H(x) = P(x) / Z(x)\n");
        fprintf(out, "# For a valid witness, P(x) is divisible by Z(x)\n");
        fprintf(out, "# H(x) has degree %d\n", h_degree);
        fprintf(out, "\n");

        /* Print H(x) formula - coefficients are expressions in witness variables */
        fprintf(out, "H(x) = ");
        for (int m = h_degree; m >= 0; m--) {
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

    /* Free polynomial storage */
    for (int j = 0; j < n_vars; j++) {
        free(A_polys[j]);
        free(B_polys[j]);
        free(C_polys[j]);
    }
    free(A_polys);
    free(B_polys);
    free(C_polys);

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

void r1cs_generate_c_checker(FILE *out, const char *circuit_name) {
    r1cs_build_partition();

    int n_vars = r1cs.n_witnesses;
    int n_cons = r1cs.n_constraints;

    /* Header */
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

    /* Constants */
    fprintf(out, "#define N_WITNESSES %d\n", n_vars);
    fprintf(out, "#define N_CONSTRAINTS %d\n\n", n_cons);

    /* Witness names */
    fprintf(out, "/* Witness variable names */\n");
    fprintf(out, "static const char *witness_names[N_WITNESSES] = {\n");
    for (int i = 0; i < n_vars; i++) {
        fprintf(out, "    \"%s\"", r1cs.witnesses[i].name);
        if (i < n_vars - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    /* Build dense matrices - coefficients converted via atoll() (64-bit precision limit) */
    fprintf(out, "/* A matrix (sparse entries only) */\n");
    fprintf(out, "/* Format: {row, col, val} */\n");
    fprintf(out, "static const long long A_entries[][3] = {\n");
    int a_count = 0;
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        for (int j = 0; j < c->A_count; j++) {
            fprintf(out, "    {%d, %d, %sLL}", i, c->A[j].col, c->A[j].coeff);
            a_count++;
            if (i < n_cons - 1 || j < c->A_count - 1) fprintf(out, ",");
            fprintf(out, "\n");
        }
    }
    if (a_count == 0) fprintf(out, "    {-1, -1, 0}  /* placeholder */\n");
    fprintf(out, "};\n");
    fprintf(out, "#define A_ENTRIES %d\n\n", a_count > 0 ? a_count : 1);

    fprintf(out, "/* B matrix (sparse entries only) */\n");
    fprintf(out, "static const long long B_entries[][3] = {\n");
    int b_count = 0;
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        for (int j = 0; j < c->B_count; j++) {
            fprintf(out, "    {%d, %d, %sLL}", i, c->B[j].col, c->B[j].coeff);
            b_count++;
            if (i < n_cons - 1 || j < c->B_count - 1) fprintf(out, ",");
            fprintf(out, "\n");
        }
    }
    if (b_count == 0) fprintf(out, "    {-1, -1, 0}  /* placeholder */\n");
    fprintf(out, "};\n");
    fprintf(out, "#define B_ENTRIES %d\n\n", b_count > 0 ? b_count : 1);

    fprintf(out, "/* C matrix (sparse entries only) */\n");
    fprintf(out, "static const long long C_entries[][3] = {\n");
    int c_count = 0;
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        for (int j = 0; j < c->C_count; j++) {
            fprintf(out, "    {%d, %d, %sLL}", i, c->C[j].col, c->C[j].coeff);
            c_count++;
            if (i < n_cons - 1 || j < c->C_count - 1) fprintf(out, ",");
            fprintf(out, "\n");
        }
    }
    if (c_count == 0) fprintf(out, "    {-1, -1, 0}  /* placeholder */\n");
    fprintf(out, "};\n");
    fprintf(out, "#define C_ENTRIES %d\n\n", c_count > 0 ? c_count : 1);

    /* Constraint comments */
    fprintf(out, "/* Constraint descriptions */\n");
    fprintf(out, "static const char *constraint_desc[N_CONSTRAINTS] = {\n");
    for (int i = 0; i < n_cons; i++) {
        r1cs_constraint_t *c = &r1cs.constraints[i];
        fprintf(out, "    \"%s\"", c->comment ? c->comment : "");
        if (i < n_cons - 1) fprintf(out, ",");
        fprintf(out, "\n");
    }
    fprintf(out, "};\n\n");

    /* Sparse matrix-vector multiplication */
    fprintf(out, "/* Compute sparse matrix-vector product: result[row] += coeff * w[col] */\n");
    fprintf(out, "static void sparse_mv(long long *result, const long long entries[][3], int n_entries, const long long *w) {\n");
    fprintf(out, "    for (int i = 0; i < n_entries; i++) {\n");
    fprintf(out, "        long long row = entries[i][0];\n");
    fprintf(out, "        long long col = entries[i][1];\n");
    fprintf(out, "        long long val = entries[i][2];\n");
    fprintf(out, "        if (row >= 0) result[row] += val * w[col];\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    /* Verification function */
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

    /* Print witness function */
    fprintf(out, "/* Print witness values */\n");
    fprintf(out, "void print_witness(const long long *witness) {\n");
    fprintf(out, "    printf(\"Witness values:\\n\");\n");
    fprintf(out, "    for (int i = 0; i < N_WITNESSES; i++) {\n");
    fprintf(out, "        printf(\"  w[%%d] = %%lld  (%%s)\\n\", i, witness[i], witness_names[i]);\n");
    fprintf(out, "    }\n");
    fprintf(out, "}\n\n");

    /* Main function with example witness */
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

    /* Generate example witness values based on circuit type */
    fprintf(out, "    /* TODO: Set your witness values here */\n");
    fprintf(out, "    /* Example for vitalik_minimal (u^3 + u + 5 = 35, where u=3): */\n");

    /* Try to generate example values for common cases */
    for (int i = 1; i < n_vars && i < 10; i++) {
        fprintf(out, "    /* witness[%d] = ???;  // %s */\n", i, r1cs.witnesses[i].name);
    }
    if (n_vars > 10) {
        fprintf(out, "    /* ... (and %d more witnesses) */\n", n_vars - 10);
    }
    fprintf(out, "\n");

    /* Parse command line for witness values */
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
            printf("(%d:%s)", c->A[j].col, c->A[j].coeff);
            if (j < c->A_count - 1) printf(", ");
        }
        printf("]\n");

        printf("  B = [");
        for (int j = 0; j < c->B_count; j++) {
            printf("(%d:%s)", c->B[j].col, c->B[j].coeff);
            if (j < c->B_count - 1) printf(", ");
        }
        printf("]\n");

        printf("  C = [");
        for (int j = 0; j < c->C_count; j++) {
            printf("(%d:%s)", c->C[j].col, c->C[j].coeff);
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

expr_node_t *expr_create_const(const char *value) {
    expr_node_t *node = (expr_node_t *)malloc(sizeof(expr_node_t));
    node->type = EXPR_CONST;
    node->data.const_val = strdup(value);
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
        case EXPR_CONST:
            free(node->data.const_val);
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
            return strdup(node->data.const_val);

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
