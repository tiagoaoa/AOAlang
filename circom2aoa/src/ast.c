/*
 * AST Node Allocators and Helpers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

expr_t *expr_number(const char *val) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_NUMBER;
    strncpy(e->u.numstr, val, 79);
    e->u.numstr[79] = '\0';
    return e;
}

expr_t *expr_ident(const char *name) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_IDENT;
    strncpy(e->u.name, name, MAX_NAME_LEN - 1);
    return e;
}

expr_t *expr_array_access(const char *name, expr_t *index) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_ARRAY_ACCESS;
    strncpy(e->u.array.name, name, MAX_NAME_LEN - 1);
    e->u.array.index = index;
    return e;
}

expr_t *expr_component_access(const char *comp, const char *field) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_COMPONENT_ACCESS;
    strncpy(e->u.comp_access.comp, comp, MAX_NAME_LEN - 1);
    strncpy(e->u.comp_access.field, field, MAX_NAME_LEN - 1);
    return e;
}

expr_t *expr_binop(binop_t op, expr_t *left, expr_t *right) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_BINOP;
    e->u.binop.op = op;
    e->u.binop.left = left;
    e->u.binop.right = right;
    return e;
}

expr_t *expr_unary(char op, expr_t *operand) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_UNARYOP;
    e->u.unary.op = op;
    e->u.unary.operand = operand;
    return e;
}

expr_t *expr_ternary(expr_t *cond, expr_t *then_e, expr_t *else_e) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_TERNARY;
    e->u.ternary.cond = cond;
    e->u.ternary.then_e = then_e;
    e->u.ternary.else_e = else_e;
    return e;
}

expr_t *expr_call(const char *name, expr_t **args, int nargs) {
    expr_t *e = calloc(1, sizeof(expr_t));
    e->type = EXPR_CALL;
    strncpy(e->u.call.name, name, MAX_NAME_LEN - 1);
    e->u.call.nargs = nargs;
    for (int i = 0; i < nargs && i < MAX_PARAMS; i++)
        e->u.call.args[i] = args[i];
    return e;
}

stmt_t *stmt_alloc(stmt_type_t type) {
    stmt_t *s = calloc(1, sizeof(stmt_t));
    s->type = type;
    return s;
}

void expr_free(expr_t *e) {
    if (!e) return;
    switch (e->type) {
    case EXPR_ARRAY_ACCESS:
        expr_free(e->u.array.index);
        break;
    case EXPR_BINOP:
        expr_free(e->u.binop.left);
        expr_free(e->u.binop.right);
        break;
    case EXPR_UNARYOP:
        expr_free(e->u.unary.operand);
        break;
    case EXPR_TERNARY:
        expr_free(e->u.ternary.cond);
        expr_free(e->u.ternary.then_e);
        expr_free(e->u.ternary.else_e);
        break;
    case EXPR_CALL:
        for (int i = 0; i < e->u.call.nargs; i++)
            expr_free(e->u.call.args[i]);
        break;
    default:
        break;
    }
    free(e);
}

void stmt_free(stmt_t *s) {
    if (!s) return;
    switch (s->type) {
    case STMT_SIGNAL_DECL:
        for (int i = 0; i < s->u.signal_decl.count; i++)
            expr_free(s->u.signal_decl.sizes[i]);
        break;
    case STMT_VAR_DECL:
        for (int i = 0; i < s->u.var_decl.count; i++)
            expr_free(s->u.var_decl.inits[i]);
        break;
    case STMT_COMPONENT_DECL:
        expr_free(s->u.comp_decl.size);
        break;
    case STMT_CONSTRAIN_ASSIGN:
        expr_free(s->u.constrain_assign.target);
        expr_free(s->u.constrain_assign.value);
        break;
    case STMT_WITNESS_ASSIGN:
        expr_free(s->u.witness_assign.target);
        expr_free(s->u.witness_assign.value);
        break;
    case STMT_CONSTRAIN_EQ:
        expr_free(s->u.constrain_eq.left);
        expr_free(s->u.constrain_eq.right);
        break;
    case STMT_VAR_ASSIGN:
        expr_free(s->u.var_assign.target);
        expr_free(s->u.var_assign.value);
        break;
    case STMT_FOR:
        stmt_free(s->u.for_loop.init);
        expr_free(s->u.for_loop.cond);
        stmt_free(s->u.for_loop.update);
        for (int i = 0; i < s->u.for_loop.nbody; i++)
            stmt_free(s->u.for_loop.body[i]);
        free(s->u.for_loop.body);
        break;
    case STMT_IF:
        expr_free(s->u.if_else.cond);
        for (int i = 0; i < s->u.if_else.nthen; i++)
            stmt_free(s->u.if_else.then_body[i]);
        free(s->u.if_else.then_body);
        for (int i = 0; i < s->u.if_else.nelse; i++)
            stmt_free(s->u.if_else.else_body[i]);
        free(s->u.if_else.else_body);
        break;
    case STMT_BLOCK:
        for (int i = 0; i < s->u.block.nstmts; i++)
            stmt_free(s->u.block.stmts[i]);
        free(s->u.block.stmts);
        break;
    default:
        break;
    }
    free(s);
}

void program_init(program_t *prog) {
    memset(prog, 0, sizeof(program_t));
}

void program_free(program_t *prog) {
    for (int i = 0; i < prog->ntemplates; i++) {
        for (int j = 0; j < prog->templates[i].nbody; j++)
            stmt_free(prog->templates[i].body[j]);
        free(prog->templates[i].body);
    }
    if (prog->main_comp) {
        for (int i = 0; i < prog->main_comp->nargs; i++)
            expr_free(prog->main_comp->args[i]);
        free(prog->main_comp);
    }
}
