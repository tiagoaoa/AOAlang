/*
 * Recursive Descent Parser for Circom
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

/* --- Helpers --- */

static token_t *cur(parser_t *p) {
    return &p->tokens[p->pos];
}

static token_type_t peek(parser_t *p) {
    return cur(p)->type;
}

static token_t *advance(parser_t *p) {
    token_t *t = cur(p);
    if (t->type != TOK_EOF) p->pos++;
    return t;
}

static int match(parser_t *p, token_type_t type) {
    if (peek(p) == type) { advance(p); return 1; }
    return 0;
}

static token_t *expect(parser_t *p, token_type_t type) {
    token_t *t = cur(p);
    if (t->type != type) {
        fprintf(stderr, "Parse error at line %d, col %d: expected %s, got %s '%s'\n",
                t->line, t->col, token_type_name(type), token_type_name(t->type), t->value);
        return NULL;
    }
    return advance(p);
}

static int at(parser_t *p, token_type_t type) {
    return peek(p) == type;
}

/* Forward declarations */
static expr_t *parse_expression(parser_t *p);
static stmt_t *parse_statement(parser_t *p);
static stmt_t **parse_statement_list(parser_t *p, int *count);

/* --- Expression parsing (precedence climbing) --- */

static expr_t *parse_primary(parser_t *p) {
    if (at(p, TOK_NUMBER)) {
        token_t *t = advance(p);
        return expr_number(atoll(t->value));
    }

    if (at(p, TOK_IDENT)) {
        token_t *t = advance(p);
        /* Function call? */
        if (at(p, TOK_LPAREN)) {
            advance(p);
            expr_t *args[MAX_PARAMS];
            int nargs = 0;
            if (!at(p, TOK_RPAREN)) {
                args[nargs++] = parse_expression(p);
                while (match(p, TOK_COMMA)) {
                    if (nargs >= MAX_PARAMS) break;
                    args[nargs++] = parse_expression(p);
                }
            }
            if (!expect(p, TOK_RPAREN)) return NULL;
            return expr_call(t->value, args, nargs);
        }
        return expr_ident(t->value);
    }

    if (match(p, TOK_LPAREN)) {
        expr_t *e = parse_expression(p);
        if (!expect(p, TOK_RPAREN)) return NULL;
        return e;
    }

    fprintf(stderr, "Parse error at line %d, col %d: expected expression, got %s '%s'\n",
            cur(p)->line, cur(p)->col, token_type_name(peek(p)), cur(p)->value);
    return NULL;
}

static expr_t *parse_postfix(parser_t *p) {
    expr_t *e = parse_primary(p);
    if (!e) return NULL;

    while (1) {
        if (match(p, TOK_LBRACKET)) {
            expr_t *idx = parse_expression(p);
            if (!expect(p, TOK_RBRACKET)) return NULL;
            if (e->type == EXPR_IDENT) {
                char name[MAX_NAME_LEN];
                strncpy(name, e->u.name, MAX_NAME_LEN);
                free(e);
                e = expr_array_access(name, idx);
            } else if (e->type == EXPR_COMPONENT_ACCESS) {
                /* comp.field[idx] → array access with dotted name "comp.field" */
                char composite[MAX_NAME_LEN];
                snprintf(composite, MAX_NAME_LEN, "%s.%s",
                         e->u.comp_access.comp, e->u.comp_access.field);
                free(e);
                e = expr_array_access(composite, idx);
            } else {
                /* Nested array access on non-ident */
                expr_free(idx);
            }
        } else if (match(p, TOK_DOT)) {
            token_t *field = expect(p, TOK_IDENT);
            if (!field) return NULL;
            if (e->type == EXPR_IDENT) {
                char comp[MAX_NAME_LEN];
                strncpy(comp, e->u.name, MAX_NAME_LEN);
                free(e);
                e = expr_component_access(comp, field->value);
            } else if (e->type == EXPR_ARRAY_ACCESS) {
                /* comp[i].field — treat as component access with indexed name */
                char comp[MAX_NAME_LEN];
                strncpy(comp, e->u.array.name, MAX_NAME_LEN);
                /* For now, skip index for array-of-components support */
                free(e);
                e = expr_component_access(comp, field->value);
            } else {
                fprintf(stderr, "Parse error at line %d: invalid component access\n", cur(p)->line);
                return NULL;
            }
        } else {
            break;
        }
    }
    return e;
}

static expr_t *parse_unary(parser_t *p) {
    if (at(p, TOK_MINUS)) {
        advance(p);
        expr_t *operand = parse_unary(p);
        if (!operand) return NULL;
        return expr_unary('-', operand);
    }
    if (at(p, TOK_NOT)) {
        advance(p);
        expr_t *operand = parse_unary(p);
        if (!operand) return NULL;
        return expr_unary('!', operand);
    }
    if (at(p, TOK_BIT_NOT)) {
        advance(p);
        expr_t *operand = parse_unary(p);
        if (!operand) return NULL;
        return expr_unary('~', operand);
    }
    return parse_postfix(p);
}

static expr_t *parse_power(parser_t *p) {
    expr_t *base = parse_unary(p);
    if (!base) return NULL;
    if (match(p, TOK_POWER)) {
        expr_t *exp = parse_power(p); /* right-associative */
        if (!exp) return NULL;
        return expr_binop(OP_POW, base, exp);
    }
    return base;
}

static expr_t *parse_multiplicative(parser_t *p) {
    expr_t *left = parse_power(p);
    if (!left) return NULL;
    while (at(p, TOK_STAR) || at(p, TOK_SLASH) || at(p, TOK_PERCENT) || at(p, TOK_BACKSLASH)) {
        binop_t op;
        if (at(p, TOK_STAR)) op = OP_MUL;
        else if (at(p, TOK_SLASH)) op = OP_DIV;
        else if (at(p, TOK_PERCENT)) op = OP_MOD;
        else op = OP_INTDIV;
        advance(p);
        expr_t *right = parse_power(p);
        if (!right) return NULL;
        left = expr_binop(op, left, right);
    }
    return left;
}

static expr_t *parse_additive(parser_t *p) {
    expr_t *left = parse_multiplicative(p);
    if (!left) return NULL;
    while (at(p, TOK_PLUS) || at(p, TOK_MINUS)) {
        binop_t op = at(p, TOK_PLUS) ? OP_ADD : OP_SUB;
        advance(p);
        expr_t *right = parse_multiplicative(p);
        if (!right) return NULL;
        left = expr_binop(op, left, right);
    }
    return left;
}

static expr_t *parse_shift(parser_t *p) {
    expr_t *left = parse_additive(p);
    if (!left) return NULL;
    while (at(p, TOK_SHL) || at(p, TOK_SHR)) {
        binop_t op = at(p, TOK_SHL) ? OP_SHL : OP_SHR;
        advance(p);
        expr_t *right = parse_additive(p);
        if (!right) return NULL;
        left = expr_binop(op, left, right);
    }
    return left;
}

static expr_t *parse_comparison(parser_t *p) {
    expr_t *left = parse_shift(p);
    if (!left) return NULL;
    while (at(p, TOK_LT) || at(p, TOK_GT) || at(p, TOK_LEQ) || at(p, TOK_GEQ)) {
        binop_t op;
        if (at(p, TOK_LT)) op = OP_LT;
        else if (at(p, TOK_GT)) op = OP_GT;
        else if (at(p, TOK_LEQ)) op = OP_LEQ;
        else op = OP_GEQ;
        advance(p);
        expr_t *right = parse_shift(p);
        if (!right) return NULL;
        left = expr_binop(op, left, right);
    }
    return left;
}

static expr_t *parse_equality(parser_t *p) {
    expr_t *left = parse_comparison(p);
    if (!left) return NULL;
    while (at(p, TOK_EQ) || at(p, TOK_NEQ)) {
        binop_t op = at(p, TOK_EQ) ? OP_EQ : OP_NEQ;
        advance(p);
        expr_t *right = parse_comparison(p);
        if (!right) return NULL;
        left = expr_binop(op, left, right);
    }
    return left;
}

static expr_t *parse_bit_and(parser_t *p) {
    expr_t *left = parse_equality(p);
    if (!left) return NULL;
    while (match(p, TOK_BIT_AND)) {
        expr_t *right = parse_equality(p);
        if (!right) return NULL;
        left = expr_binop(OP_BIT_AND, left, right);
    }
    return left;
}

static expr_t *parse_bit_xor(parser_t *p) {
    expr_t *left = parse_bit_and(p);
    if (!left) return NULL;
    while (match(p, TOK_BIT_XOR)) {
        expr_t *right = parse_bit_and(p);
        if (!right) return NULL;
        left = expr_binop(OP_BIT_XOR, left, right);
    }
    return left;
}

static expr_t *parse_bit_or(parser_t *p) {
    expr_t *left = parse_bit_xor(p);
    if (!left) return NULL;
    while (match(p, TOK_BIT_OR)) {
        expr_t *right = parse_bit_xor(p);
        if (!right) return NULL;
        left = expr_binop(OP_BIT_OR, left, right);
    }
    return left;
}

static expr_t *parse_logical_and(parser_t *p) {
    expr_t *left = parse_bit_or(p);
    if (!left) return NULL;
    while (match(p, TOK_AND)) {
        expr_t *right = parse_bit_or(p);
        if (!right) return NULL;
        left = expr_binop(OP_AND, left, right);
    }
    return left;
}

static expr_t *parse_logical_or(parser_t *p) {
    expr_t *left = parse_logical_and(p);
    if (!left) return NULL;
    while (match(p, TOK_OR)) {
        expr_t *right = parse_logical_and(p);
        if (!right) return NULL;
        left = expr_binop(OP_OR, left, right);
    }
    return left;
}

static expr_t *parse_ternary(parser_t *p) {
    expr_t *cond = parse_logical_or(p);
    if (!cond) return NULL;
    if (match(p, TOK_QUESTION)) {
        expr_t *then_e = parse_expression(p);
        if (!expect(p, TOK_COLON)) return NULL;
        expr_t *else_e = parse_expression(p);
        if (!then_e || !else_e) return NULL;
        return expr_ternary(cond, then_e, else_e);
    }
    return cond;
}

static expr_t *parse_expression(parser_t *p) {
    return parse_ternary(p);
}

/* --- Statement parsing --- */

static stmt_t *parse_signal_decl(parser_t *p) {
    expect(p, TOK_SIGNAL);
    signal_dir_t dir = SIG_NONE;
    signal_vis_t vis = SIG_VIS_UNSET;
    if (at(p, TOK_PRIVATE)) { vis = SIG_VIS_PRIVATE; advance(p); }
    else if (at(p, TOK_PUBLIC)) { vis = SIG_VIS_PUBLIC; advance(p); }
    if (at(p, TOK_INPUT)) { dir = SIG_INPUT; advance(p); }
    else if (at(p, TOK_OUTPUT)) { dir = SIG_OUTPUT; advance(p); }

    stmt_t *s = stmt_alloc(STMT_SIGNAL_DECL);
    s->u.signal_decl.dir = dir;
    s->u.signal_decl.vis = vis;
    s->u.signal_decl.count = 0;

    do {
        int idx = s->u.signal_decl.count;
        if (idx >= MAX_PARAMS) break;
        token_t *name = expect(p, TOK_IDENT);
        if (!name) { stmt_free(s); return NULL; }
        strncpy(s->u.signal_decl.names[idx], name->value, MAX_NAME_LEN - 1);
        s->u.signal_decl.sizes[idx] = NULL;
        if (match(p, TOK_LBRACKET)) {
            s->u.signal_decl.sizes[idx] = parse_expression(p);
            if (!expect(p, TOK_RBRACKET)) { stmt_free(s); return NULL; }
        }
        s->u.signal_decl.count++;
    } while (match(p, TOK_COMMA));

    expect(p, TOK_SEMICOLON);
    return s;
}

static stmt_t *parse_var_decl(parser_t *p) {
    expect(p, TOK_VAR);
    stmt_t *s = stmt_alloc(STMT_VAR_DECL);
    s->u.var_decl.count = 0;

    do {
        int idx = s->u.var_decl.count;
        if (idx >= MAX_PARAMS) break;
        token_t *name = expect(p, TOK_IDENT);
        if (!name) { stmt_free(s); return NULL; }
        strncpy(s->u.var_decl.names[idx], name->value, MAX_NAME_LEN - 1);
        s->u.var_decl.inits[idx] = NULL;
        if (match(p, TOK_ASSIGN)) {
            s->u.var_decl.inits[idx] = parse_expression(p);
        }
        s->u.var_decl.count++;
    } while (match(p, TOK_COMMA));

    expect(p, TOK_SEMICOLON);
    return s;
}

static stmt_t *parse_component_stmt(parser_t *p) {
    expect(p, TOK_COMPONENT);
    token_t *name = expect(p, TOK_IDENT);
    if (!name) return NULL;

    expr_t *size = NULL;
    if (match(p, TOK_LBRACKET)) {
        size = parse_expression(p);
        if (!expect(p, TOK_RBRACKET)) return NULL;
    }

    if (match(p, TOK_ASSIGN)) {
        /* component c = Template(args); */
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        /* Emit both a decl and assignment in a block */
        stmt_t *decl = stmt_alloc(STMT_COMPONENT_DECL);
        strncpy(decl->u.comp_decl.name, name->value, MAX_NAME_LEN - 1);
        decl->u.comp_decl.size = size;

        stmt_t *asgn = stmt_alloc(STMT_VAR_ASSIGN);
        asgn->u.var_assign.target = expr_ident(name->value);
        asgn->u.var_assign.op = ASGN_EQ;
        asgn->u.var_assign.value = val;

        stmt_t *blk = stmt_alloc(STMT_BLOCK);
        blk->u.block.stmts = malloc(2 * sizeof(stmt_t *));
        blk->u.block.stmts[0] = decl;
        blk->u.block.stmts[1] = asgn;
        blk->u.block.nstmts = 2;
        return blk;
    }

    expect(p, TOK_SEMICOLON);
    stmt_t *s = stmt_alloc(STMT_COMPONENT_DECL);
    strncpy(s->u.comp_decl.name, name->value, MAX_NAME_LEN - 1);
    s->u.comp_decl.size = size;
    return s;
}

static stmt_t *parse_for_init(parser_t *p) {
    if (at(p, TOK_VAR)) return parse_var_decl(p);
    /* Simple expression statement */
    expr_t *e = parse_expression(p);
    if (!e) return NULL;
    if (at(p, TOK_ASSIGN) || at(p, TOK_PLUS_ASSIGN) || at(p, TOK_MINUS_ASSIGN) ||
        at(p, TOK_STAR_ASSIGN)) {
        assign_op_t op = ASGN_EQ;
        if (at(p, TOK_PLUS_ASSIGN)) op = ASGN_PLUS_EQ;
        else if (at(p, TOK_MINUS_ASSIGN)) op = ASGN_MINUS_EQ;
        else if (at(p, TOK_STAR_ASSIGN)) op = ASGN_STAR_EQ;
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
        s->u.var_assign.target = e;
        s->u.var_assign.op = op;
        s->u.var_assign.value = val;
        return s;
    }
    expect(p, TOK_SEMICOLON);
    stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
    s->u.var_assign.target = e;
    s->u.var_assign.op = ASGN_EQ;
    s->u.var_assign.value = expr_number(0);
    return s;
}

static stmt_t *parse_for_update(parser_t *p) {
    expr_t *e = parse_expression(p);
    if (!e) return NULL;

    /* Handle i++ and i-- */
    if (at(p, TOK_INCREMENT)) {
        advance(p);
        stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
        s->u.var_assign.target = e;
        s->u.var_assign.op = ASGN_PLUS_EQ;
        s->u.var_assign.value = expr_number(1);
        return s;
    }
    if (at(p, TOK_DECREMENT)) {
        advance(p);
        stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
        s->u.var_assign.target = e;
        s->u.var_assign.op = ASGN_MINUS_EQ;
        s->u.var_assign.value = expr_number(1);
        return s;
    }

    assign_op_t op = ASGN_EQ;
    if (at(p, TOK_ASSIGN)) { op = ASGN_EQ; advance(p); }
    else if (at(p, TOK_PLUS_ASSIGN)) { op = ASGN_PLUS_EQ; advance(p); }
    else if (at(p, TOK_MINUS_ASSIGN)) { op = ASGN_MINUS_EQ; advance(p); }
    else if (at(p, TOK_STAR_ASSIGN)) { op = ASGN_STAR_EQ; advance(p); }
    else {
        /* No assignment op - just wrap expr */
        stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
        s->u.var_assign.target = e;
        s->u.var_assign.op = ASGN_EQ;
        s->u.var_assign.value = expr_number(0);
        return s;
    }
    expr_t *val = parse_expression(p);
    stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
    s->u.var_assign.target = e;
    s->u.var_assign.op = op;
    s->u.var_assign.value = val;
    return s;
}

static stmt_t *parse_for(parser_t *p) {
    expect(p, TOK_FOR);
    expect(p, TOK_LPAREN);
    stmt_t *init = parse_for_init(p);
    expr_t *cond = parse_expression(p);
    expect(p, TOK_SEMICOLON);
    stmt_t *update = parse_for_update(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_LBRACE);
    int nbody = 0;
    stmt_t **body = parse_statement_list(p, &nbody);
    expect(p, TOK_RBRACE);

    stmt_t *s = stmt_alloc(STMT_FOR);
    s->u.for_loop.init = init;
    s->u.for_loop.cond = cond;
    s->u.for_loop.update = update;
    s->u.for_loop.body = body;
    s->u.for_loop.nbody = nbody;
    return s;
}

static stmt_t *parse_if(parser_t *p) {
    expect(p, TOK_IF);
    expect(p, TOK_LPAREN);
    expr_t *cond = parse_expression(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_LBRACE);
    int nthen = 0;
    stmt_t **then_body = parse_statement_list(p, &nthen);
    expect(p, TOK_RBRACE);

    stmt_t **else_body = NULL;
    int nelse = 0;
    if (match(p, TOK_ELSE)) {
        if (at(p, TOK_IF)) {
            /* else if */
            else_body = malloc(sizeof(stmt_t *));
            else_body[0] = parse_if(p);
            nelse = 1;
        } else {
            expect(p, TOK_LBRACE);
            else_body = parse_statement_list(p, &nelse);
            expect(p, TOK_RBRACE);
        }
    }

    stmt_t *s = stmt_alloc(STMT_IF);
    s->u.if_else.cond = cond;
    s->u.if_else.then_body = then_body;
    s->u.if_else.nthen = nthen;
    s->u.if_else.else_body = else_body;
    s->u.if_else.nelse = nelse;
    return s;
}

static stmt_t *parse_expression_stmt(parser_t *p) {
    expr_t *e = parse_expression(p);
    if (!e) return NULL;

    /* Check for Circom-specific operators */
    if (at(p, TOK_CONSTRAIN_ASSIGN)) {
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_CONSTRAIN_ASSIGN);
        s->u.constrain_assign.target = e;
        s->u.constrain_assign.value = val;
        return s;
    }

    if (at(p, TOK_CONSTRAIN_ASSIGN_R)) {
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_CONSTRAIN_ASSIGN);
        s->u.constrain_assign.target = val;
        s->u.constrain_assign.value = e;
        return s;
    }

    if (at(p, TOK_WITNESS_ASSIGN)) {
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_WITNESS_ASSIGN);
        s->u.witness_assign.target = e;
        s->u.witness_assign.value = val;
        return s;
    }

    if (at(p, TOK_WITNESS_ASSIGN_R)) {
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_WITNESS_ASSIGN);
        s->u.witness_assign.target = val;
        s->u.witness_assign.value = e;
        return s;
    }

    if (at(p, TOK_CONSTRAIN_EQ)) {
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_CONSTRAIN_EQ);
        s->u.constrain_eq.left = e;
        s->u.constrain_eq.right = val;
        return s;
    }

    /* Assignment operators */
    if (at(p, TOK_ASSIGN) || at(p, TOK_PLUS_ASSIGN) || at(p, TOK_MINUS_ASSIGN) ||
        at(p, TOK_STAR_ASSIGN) || at(p, TOK_SLASH_ASSIGN) || at(p, TOK_PERCENT_ASSIGN) ||
        at(p, TOK_POWER_ASSIGN) || at(p, TOK_SHL_ASSIGN) || at(p, TOK_SHR_ASSIGN) ||
        at(p, TOK_AND_ASSIGN) || at(p, TOK_OR_ASSIGN) || at(p, TOK_XOR_ASSIGN) ||
        at(p, TOK_BACKSLASH_ASSIGN)) {
        assign_op_t op;
        switch (peek(p)) {
            case TOK_ASSIGN: op = ASGN_EQ; break;
            case TOK_PLUS_ASSIGN: op = ASGN_PLUS_EQ; break;
            case TOK_MINUS_ASSIGN: op = ASGN_MINUS_EQ; break;
            case TOK_STAR_ASSIGN: op = ASGN_STAR_EQ; break;
            case TOK_SLASH_ASSIGN: op = ASGN_SLASH_EQ; break;
            case TOK_PERCENT_ASSIGN: op = ASGN_MOD_EQ; break;
            case TOK_POWER_ASSIGN: op = ASGN_POW_EQ; break;
            case TOK_SHL_ASSIGN: op = ASGN_SHL_EQ; break;
            case TOK_SHR_ASSIGN: op = ASGN_SHR_EQ; break;
            case TOK_AND_ASSIGN: op = ASGN_AND_EQ; break;
            case TOK_OR_ASSIGN: op = ASGN_OR_EQ; break;
            case TOK_XOR_ASSIGN: op = ASGN_XOR_EQ; break;
            default: op = ASGN_EQ; break;
        }
        advance(p);
        expr_t *val = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
        s->u.var_assign.target = e;
        s->u.var_assign.op = op;
        s->u.var_assign.value = val;
        return s;
    }

    expect(p, TOK_SEMICOLON);
    /* Bare expression - wrap as dummy assign */
    stmt_t *s = stmt_alloc(STMT_VAR_ASSIGN);
    s->u.var_assign.target = e;
    s->u.var_assign.op = ASGN_EQ;
    s->u.var_assign.value = expr_number(0);
    return s;
}

static stmt_t *parse_log_stmt(parser_t *p) {
    expect(p, TOK_LOG);
    expect(p, TOK_LPAREN);
    stmt_t *s = stmt_alloc(STMT_LOG);
    s->u.log_stmt.nargs = 0;
    if (!at(p, TOK_RPAREN)) {
        s->u.log_stmt.args[s->u.log_stmt.nargs++] = parse_expression(p);
        while (match(p, TOK_COMMA)) {
            if (s->u.log_stmt.nargs >= MAX_PARAMS) break;
            s->u.log_stmt.args[s->u.log_stmt.nargs++] = parse_expression(p);
        }
    }
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMICOLON);
    return s;
}

static stmt_t *parse_assert_stmt(parser_t *p) {
    expect(p, TOK_ASSERT);
    expect(p, TOK_LPAREN);
    expr_t *e = parse_expression(p);
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMICOLON);
    stmt_t *s = stmt_alloc(STMT_ASSERT);
    s->u.assert_stmt.expr = e;
    return s;
}

static stmt_t *parse_statement(parser_t *p) {
    if (at(p, TOK_SIGNAL)) return parse_signal_decl(p);
    if (at(p, TOK_VAR)) return parse_var_decl(p);
    if (at(p, TOK_COMPONENT)) return parse_component_stmt(p);
    if (at(p, TOK_FOR)) return parse_for(p);
    if (at(p, TOK_IF)) return parse_if(p);
    if (at(p, TOK_LOG)) return parse_log_stmt(p);
    if (at(p, TOK_ASSERT)) return parse_assert_stmt(p);
    if (at(p, TOK_RETURN)) {
        advance(p);
        expr_t *e = parse_expression(p);
        expect(p, TOK_SEMICOLON);
        stmt_t *s = stmt_alloc(STMT_RETURN);
        s->u.return_stmt.expr = e;
        return s;
    }
    if (at(p, TOK_LBRACE)) {
        advance(p);
        int n = 0;
        stmt_t **list = parse_statement_list(p, &n);
        expect(p, TOK_RBRACE);
        stmt_t *s = stmt_alloc(STMT_BLOCK);
        s->u.block.stmts = list;
        s->u.block.nstmts = n;
        return s;
    }
    return parse_expression_stmt(p);
}

static stmt_t **parse_statement_list(parser_t *p, int *count) {
    int cap = 64;
    stmt_t **list = malloc(cap * sizeof(stmt_t *));
    *count = 0;

    while (!at(p, TOK_RBRACE) && !at(p, TOK_EOF)) {
        stmt_t *s = parse_statement(p);
        if (!s) break;
        if (*count >= cap) {
            cap *= 2;
            list = realloc(list, cap * sizeof(stmt_t *));
        }
        list[(*count)++] = s;
    }
    return list;
}

/* --- Top-level parsing --- */

static void parse_pragma(parser_t *p, program_t *prog) {
    expect(p, TOK_PRAGMA);
    expect(p, TOK_CIRCOM);
    /* Version: "2" "." "1" "." "4" etc */
    char version[64] = "";
    if (at(p, TOK_NUMBER)) {
        strcat(version, advance(p)->value);
        while (match(p, TOK_DOT)) {
            strcat(version, ".");
            if (at(p, TOK_NUMBER) || at(p, TOK_IDENT))
                strcat(version, advance(p)->value);
        }
    }
    expect(p, TOK_SEMICOLON);
    if (prog->npragmas < 8) {
        strncpy(prog->pragmas[prog->npragmas].version, version, 63);
        prog->npragmas++;
    }
}

static void parse_include(parser_t *p, program_t *prog) {
    expect(p, TOK_INCLUDE);
    token_t *path = expect(p, TOK_STRING);
    expect(p, TOK_SEMICOLON);
    if (prog->nincludes < 32 && path) {
        strncpy(prog->includes[prog->nincludes].path, path->value, MAX_NAME_LEN - 1);
        prog->nincludes++;
    }
}

static void parse_template(parser_t *p, program_t *prog) {
    expect(p, TOK_TEMPLATE);
    token_t *name = expect(p, TOK_IDENT);
    if (!name) return;

    int tidx = prog->ntemplates;
    if (tidx >= MAX_TEMPLATES) {
        fprintf(stderr, "Too many templates\n");
        return;
    }

    template_t *tmpl = &prog->templates[tidx];
    strncpy(tmpl->name, name->value, MAX_NAME_LEN - 1);

    expect(p, TOK_LPAREN);
    tmpl->nparams = 0;
    if (!at(p, TOK_RPAREN)) {
        token_t *param = expect(p, TOK_IDENT);
        if (param) strncpy(tmpl->params[tmpl->nparams++], param->value, MAX_NAME_LEN - 1);
        while (match(p, TOK_COMMA)) {
            param = expect(p, TOK_IDENT);
            if (param && tmpl->nparams < MAX_PARAMS)
                strncpy(tmpl->params[tmpl->nparams++], param->value, MAX_NAME_LEN - 1);
        }
    }
    expect(p, TOK_RPAREN);

    expect(p, TOK_LBRACE);
    tmpl->body = parse_statement_list(p, &tmpl->nbody);
    expect(p, TOK_RBRACE);

    prog->ntemplates++;
}

static void parse_main_component(parser_t *p, program_t *prog) {
    expect(p, TOK_COMPONENT);
    expect(p, TOK_MAIN);

    main_component_t *mc = calloc(1, sizeof(main_component_t));

    if (match(p, TOK_LBRACE)) {
        expect(p, TOK_PUBLIC);
        expect(p, TOK_LBRACKET);
        if (!at(p, TOK_RBRACKET)) {
            token_t *t = expect(p, TOK_IDENT);
            if (t) strncpy(mc->public_inputs[mc->npublic++], t->value, MAX_NAME_LEN - 1);
            while (match(p, TOK_COMMA)) {
                t = expect(p, TOK_IDENT);
                if (t && mc->npublic < MAX_PARAMS)
                    strncpy(mc->public_inputs[mc->npublic++], t->value, MAX_NAME_LEN - 1);
            }
        }
        expect(p, TOK_RBRACKET);
        expect(p, TOK_RBRACE);
    }

    expect(p, TOK_ASSIGN);
    token_t *tmpl_name = expect(p, TOK_IDENT);
    if (tmpl_name) strncpy(mc->template_name, tmpl_name->value, MAX_NAME_LEN - 1);

    expect(p, TOK_LPAREN);
    mc->nargs = 0;
    if (!at(p, TOK_RPAREN)) {
        mc->args[mc->nargs++] = parse_expression(p);
        while (match(p, TOK_COMMA)) {
            if (mc->nargs >= MAX_PARAMS) break;
            mc->args[mc->nargs++] = parse_expression(p);
        }
    }
    expect(p, TOK_RPAREN);
    expect(p, TOK_SEMICOLON);

    prog->main_comp = mc;
}

/* --- Public API --- */

void parser_init(parser_t *p, token_t *tokens, int ntokens) {
    p->tokens = tokens;
    p->ntokens = ntokens;
    p->pos = 0;
}

int parser_parse(parser_t *p, program_t *prog) {
    program_init(prog);

    while (!at(p, TOK_EOF)) {
        if (at(p, TOK_PRAGMA)) {
            parse_pragma(p, prog);
        } else if (at(p, TOK_INCLUDE)) {
            parse_include(p, prog);
        } else if (at(p, TOK_TEMPLATE)) {
            parse_template(p, prog);
        } else if (at(p, TOK_COMPONENT)) {
            parse_main_component(p, prog);
        } else {
            fprintf(stderr, "Parse error at line %d: unexpected token %s '%s'\n",
                    cur(p)->line, token_type_name(peek(p)), cur(p)->value);
            return 1;
        }
    }

    return 0;
}
