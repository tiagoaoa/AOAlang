/*
 * Flattener: Circom AST → flat AOA operations
 *
 * Handles template instantiation, loop unrolling, compile-time evaluation,
 * and expression flattening to single-assignment form.
 */

#ifndef FLATTENER_H
#define FLATTENER_H

#include "ast.h"

#define MAX_OPS    65536
#define MAX_VARS   16384

/* A single flat AOA operation */
typedef struct {
    char target[MAX_NAME_LEN];
    char op;          /* '+', '-', '*', 'E' (==), 'I' (identity), 'C' (const), '#' (comment) */
    char left[MAX_NAME_LEN];
    char right[MAX_NAME_LEN];
    char comment[MAX_NAME_LEN];
} flat_op_t;

/* Signal metadata */
typedef struct {
    char name[MAX_NAME_LEN];
    int direction;   /* 0=intermediate, 1=input, 2=output */
    int is_public;
    int size;        /* 0 for scalar, >0 for array */
} signal_info_t;

/* Compile-time variable (or runtime alias) */
typedef struct {
    char name[MAX_NAME_LEN];
    long long value;
    char bigvalue[80];             /* string value for large constants that overflow long long */
    int is_big;                    /* 1 if value doesn't fit in long long */
    int is_runtime;                /* 1 if var holds a gate variable name, not a constant */
    char runtime_name[MAX_NAME_LEN]; /* gate variable name when is_runtime=1 */
} ct_var_t;

/* Pending component (deferred body flattening) */
typedef struct {
    char name[MAX_NAME_LEN];
    char prefix[MAX_NAME_LEN];
    int template_idx;       /* index into program_t.templates */
    long long param_values[MAX_PARAMS];
    int nparams;
    int flattened;          /* 1 if body already flattened */
} comp_inst_t;

/* Flattener state */
typedef struct {
    program_t *prog;
    int verbose;

    signal_info_t signals[MAX_SIGNALS];
    int nsignals;

    flat_op_t ops[MAX_OPS];
    int nops;

    ct_var_t vars[MAX_VARS];
    int nvars;

    comp_inst_t comps[256];
    int ncomps;

    int temp_counter;
    int check_counter;

    /* Track assigned gate variables */
    char assigned[MAX_VARS][MAX_NAME_LEN];
    int nassigned;

    int overflowed;
} flattener_t;

void flattener_init(flattener_t *f, program_t *prog, int verbose);
int  flattener_run(flattener_t *f);  /* returns 0 on success */
void flattener_record_overflow(flattener_t *f, const char *kind, int limit);

#endif /* FLATTENER_H */
