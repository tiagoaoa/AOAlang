/*
 * R1CS - Rank-1 Constraint System representation and JSON generation
 */

#ifndef R1CS_H
#define R1CS_H

#include "symbol_table.h"

/* Maximum constraints and variables */
#define MAX_CONSTRAINTS 4096
#define MAX_VARIABLES 4096
#define MAX_ENTRIES_PER_ROW 64

/* Sparse matrix entry */
typedef struct {
    int col;        /* Witness index (column) */
    int coeff;      /* Coefficient value */
} r1cs_entry_t;

/* Single R1CS constraint row */
typedef struct {
    r1cs_entry_t A[MAX_ENTRIES_PER_ROW];
    r1cs_entry_t B[MAX_ENTRIES_PER_ROW];
    r1cs_entry_t C[MAX_ENTRIES_PER_ROW];
    int A_count;
    int B_count;
    int C_count;
    char *comment;          /* Original expression for debugging */
    char *lhs_var;          /* Left-hand side variable name */
} r1cs_constraint_t;

/* Expression types for symbolic tracking */
typedef enum {
    EXPR_NONE,
    EXPR_VAR,           /* Variable reference */
    EXPR_ARRAY_ACCESS,  /* Array element access */
    EXPR_CONST,         /* Numeric constant */
    EXPR_ADD,           /* Addition */
    EXPR_SUB,           /* Subtraction */
    EXPR_MUL,           /* Multiplication */
    EXPR_EQ             /* Equality constraint */
} expr_type_t;

/* Expression node for AST */
typedef struct expr_node {
    expr_type_t type;
    union {
        char *var_name;              /* EXPR_VAR */
        struct {
            char *array_name;
            int index;
        } array_access;              /* EXPR_ARRAY_ACCESS */
        int const_val;               /* EXPR_CONST */
        struct {
            struct expr_node *left;
            struct expr_node *right;
        } binary;                    /* EXPR_ADD, EXPR_SUB, EXPR_MUL, EXPR_EQ */
    } data;
} expr_node_t;

/* Witness entry for JSON output */
typedef struct {
    int index;
    char *name;
    visibility_t visibility;
    symbol_origin_t origin;
    char *symbolic_expr;    /* For gates: symbolic expression */
} witness_entry_t;

/* R1CS system */
typedef struct {
    r1cs_constraint_t constraints[MAX_CONSTRAINTS];
    int n_constraints;

    witness_entry_t witnesses[MAX_VARIABLES];
    int n_witnesses;

    /* Partition info */
    int constant_indices[MAX_VARIABLES];
    int n_constants;
    int private_indices[MAX_VARIABLES];
    int n_private;
    int deferred_indices[MAX_VARIABLES];
    int n_deferred;
    int gate_indices[MAX_VARIABLES];
    int n_gates;

    /* Public input names for discriminants */
    char *public_input_names[MAX_VARIABLES];
    int n_public_inputs;
} r1cs_system_t;

/* Global R1CS system instance */
extern r1cs_system_t r1cs;

/* Initialize R1CS system */
void r1cs_init(void);

/* Free R1CS system resources */
void r1cs_free(void);

/* Add constant "1" as first witness */
void r1cs_add_constant_one(void);

/* Register a witness from symbol table */
void r1cs_register_witness(const char *name, int witness_index,
                           visibility_t vis, symbol_origin_t origin,
                           symbol_type_t type, int array_size);

/* Start a new constraint */
void r1cs_begin_constraint(const char *lhs_var, const char *comment);

/* Add entry to current constraint's A vector */
void r1cs_add_A(int col, int coeff);

/* Add entry to current constraint's B vector */
void r1cs_add_B(int col, int coeff);

/* Add entry to current constraint's C vector */
void r1cs_add_C(int col, int coeff);

/* Finalize current constraint */
void r1cs_end_constraint(void);

/* Build constraint for: result = left OP right */
void r1cs_add_mul_constraint(const char *result, const char *left, const char *right);
void r1cs_add_add_constraint(const char *result, const char *left, const char *right);
void r1cs_add_sub_constraint(const char *result, const char *left, const char *right);
void r1cs_add_const_constraint(const char *result, int value);
void r1cs_add_eq_constraint(const char *result, const char *left, const char *right);

/* Set symbolic expression for a gate */
void r1cs_set_gate_expr(const char *gate_name, const char *expr);

/* Get witness index for variable (handles array[index] notation) */
int r1cs_get_witness_index(const char *var_name);

/* Build witness partition from symbol table */
void r1cs_build_partition(void);

/* Generate JSON output */
void r1cs_generate_json(FILE *out, const char *circuit_name);

/* Print R1CS system for debugging */
void r1cs_print(void);

/* Generate dense matrix output (.r1cs format) */
void r1cs_generate_dense(FILE *out);

/* Generate QAP polynomial output (.qap format) */
void r1cs_generate_qap(FILE *out);

/* Expression node helpers */
expr_node_t *expr_create_var(const char *name);
expr_node_t *expr_create_array(const char *name, int index);
expr_node_t *expr_create_const(int value);
expr_node_t *expr_create_binary(expr_type_t type, expr_node_t *left, expr_node_t *right);
void expr_free(expr_node_t *node);
char *expr_to_string(expr_node_t *node);

#endif /* R1CS_H */
