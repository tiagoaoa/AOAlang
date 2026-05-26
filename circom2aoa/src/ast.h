/*
 * AST Node Types for Circom Parser
 */

#ifndef AST_H
#define AST_H

#include <stddef.h>

/* Maximum limits */
#define MAX_PARAMS     16
#define MAX_CHILDREN  256
#define MAX_SIGNALS 16384
#define MAX_TEMPLATES  64
#define MAX_NAME_LEN  256

/* Expression node types */
typedef enum {
    EXPR_NUMBER,
    EXPR_IDENT,
    EXPR_ARRAY_ACCESS,
    EXPR_COMPONENT_ACCESS,
    EXPR_BINOP,
    EXPR_UNARYOP,
    EXPR_TERNARY,
    EXPR_CALL
} expr_type_t;

/* Binary operators */
typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_POW, OP_INTDIV,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LEQ, OP_GEQ,
    OP_AND, OP_OR,
    OP_BIT_AND, OP_BIT_OR, OP_BIT_XOR,
    OP_SHL, OP_SHR
} binop_t;

typedef struct expr expr_t;

struct expr {
    expr_type_t type;
    union {
        char numstr[80];                           /* EXPR_NUMBER (string to preserve precision) */
        char name[MAX_NAME_LEN];                   /* EXPR_IDENT */
        struct { char name[MAX_NAME_LEN]; expr_t *index; } array;  /* EXPR_ARRAY_ACCESS */
        struct { char comp[MAX_NAME_LEN]; char field[MAX_NAME_LEN]; } comp_access; /* EXPR_COMPONENT_ACCESS */
        struct { binop_t op; expr_t *left; expr_t *right; } binop; /* EXPR_BINOP */
        struct { char op; expr_t *operand; } unary;                /* EXPR_UNARYOP: '-', '!', '~' */
        struct { expr_t *cond; expr_t *then_e; expr_t *else_e; } ternary; /* EXPR_TERNARY */
        struct { char name[MAX_NAME_LEN]; expr_t *args[MAX_PARAMS]; int nargs; } call; /* EXPR_CALL */
    } u;
};

/* Statement types */
typedef enum {
    STMT_SIGNAL_DECL,
    STMT_VAR_DECL,
    STMT_COMPONENT_DECL,
    STMT_CONSTRAIN_ASSIGN,  /* <== */
    STMT_WITNESS_ASSIGN,    /* <-- */
    STMT_CONSTRAIN_EQ,      /* === */
    STMT_VAR_ASSIGN,        /* = += -= *= etc. */
    STMT_FOR,
    STMT_IF,
    STMT_BLOCK,
    STMT_LOG,
    STMT_ASSERT,
    STMT_RETURN
} stmt_type_t;

/* Signal direction */
typedef enum {
    SIG_NONE,    /* intermediate */
    SIG_INPUT,
    SIG_OUTPUT
} signal_dir_t;

/* Signal visibility (explicit) */
typedef enum {
    SIG_VIS_UNSET,    /* determined by component main {public [...]} */
    SIG_VIS_PRIVATE,
    SIG_VIS_PUBLIC
} signal_vis_t;

/* Assignment operator */
typedef enum {
    ASGN_EQ,        /* = */
    ASGN_PLUS_EQ,   /* += */
    ASGN_MINUS_EQ,  /* -= */
    ASGN_STAR_EQ,   /* *= */
    ASGN_SLASH_EQ,  /* /= */
    ASGN_MOD_EQ,    /* %= */
    ASGN_SHL_EQ,    /* <<= */
    ASGN_SHR_EQ,    /* >>= */
    ASGN_AND_EQ,    /* &= */
    ASGN_OR_EQ,     /* |= */
    ASGN_XOR_EQ,    /* ^= */
    ASGN_POW_EQ     /* **= */
} assign_op_t;

typedef struct stmt stmt_t;

struct stmt {
    stmt_type_t type;
    union {
        struct {
            signal_dir_t dir;
            signal_vis_t vis;
            char names[MAX_PARAMS][MAX_NAME_LEN];
            expr_t *sizes[MAX_PARAMS];  /* NULL for scalar */
            int count;
        } signal_decl;

        struct {
            char names[MAX_PARAMS][MAX_NAME_LEN];
            expr_t *inits[MAX_PARAMS];  /* NULL if no init */
            int count;
        } var_decl;

        struct {
            char name[MAX_NAME_LEN];
            expr_t *size;  /* NULL for non-array */
        } comp_decl;

        struct { expr_t *target; expr_t *value; } constrain_assign;
        struct { expr_t *target; expr_t *value; } witness_assign;
        struct { expr_t *left; expr_t *right; } constrain_eq;

        struct {
            expr_t *target;
            assign_op_t op;
            expr_t *value;
        } var_assign;

        struct {
            stmt_t *init;
            expr_t *cond;
            stmt_t *update;
            stmt_t **body;
            int nbody;
        } for_loop;

        struct {
            expr_t *cond;
            stmt_t **then_body;
            int nthen;
            stmt_t **else_body;
            int nelse;
        } if_else;

        struct {
            stmt_t **stmts;
            int nstmts;
        } block;

        struct { expr_t *args[MAX_PARAMS]; int nargs; } log_stmt;
        struct { expr_t *expr; } assert_stmt;
        struct { expr_t *expr; } return_stmt;
    } u;
};

/* Top-level items */
typedef struct {
    char version[64];
} pragma_t;

typedef struct {
    char path[MAX_NAME_LEN];
} include_t;

typedef struct {
    char name[MAX_NAME_LEN];
    char params[MAX_PARAMS][MAX_NAME_LEN];
    int nparams;
    stmt_t **body;
    int nbody;
} template_t;

typedef struct {
    char public_inputs[MAX_PARAMS][MAX_NAME_LEN];
    int npublic;
    char template_name[MAX_NAME_LEN];
    expr_t *args[MAX_PARAMS];
    int nargs;
} main_component_t;

typedef struct {
    pragma_t pragmas[8];
    int npragmas;
    include_t includes[32];
    int nincludes;
    template_t templates[MAX_TEMPLATES];
    int ntemplates;
    main_component_t *main_comp;
} program_t;


/* Allocators */
expr_t *expr_number(const char *val);
expr_t *expr_ident(const char *name);
expr_t *expr_array_access(const char *name, expr_t *index);
expr_t *expr_component_access(const char *comp, const char *field);
expr_t *expr_binop(binop_t op, expr_t *left, expr_t *right);
expr_t *expr_unary(char op, expr_t *operand);
expr_t *expr_ternary(expr_t *cond, expr_t *then_e, expr_t *else_e);
expr_t *expr_call(const char *name, expr_t **args, int nargs);

stmt_t *stmt_alloc(stmt_type_t type);
void program_init(program_t *prog);
void program_free(program_t *prog);
void expr_free(expr_t *e);
void stmt_free(stmt_t *s);

#endif /* AST_H */
