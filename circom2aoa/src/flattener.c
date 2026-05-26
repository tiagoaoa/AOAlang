/*
 * Flattener: Circom AST → flat AOA operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "flattener.h"
#include "poseidon_lib.h"

/* Counter for unique Poseidon instance prefixes */
static int poseidon_instance_counter = 0;

/* --- Helpers --- */

void flattener_record_overflow(flattener_t *f, const char *kind, int limit) {
    if (!f->overflowed) {
        fprintf(stderr, "Flattener: %s limit exceeded (%d)\n", kind, limit);
    }
    f->overflowed = 1;
}

static void add_op(flattener_t *f, const char *target, char op,
                   const char *left, const char *right) {
    if (f->nops >= MAX_OPS) {
        flattener_record_overflow(f, "operation", MAX_OPS);
        return;
    }
    flat_op_t *o = &f->ops[f->nops++];
    strncpy(o->target, target, MAX_NAME_LEN - 1);
    o->op = op;
    strncpy(o->left, left, MAX_NAME_LEN - 1);
    if (right) strncpy(o->right, right, MAX_NAME_LEN - 1);
    else o->right[0] = '\0';
    o->comment[0] = '\0';
}

static void add_comment(flattener_t *f, const char *comment) {
    if (f->nops >= MAX_OPS) {
        flattener_record_overflow(f, "operation", MAX_OPS);
        return;
    }
    flat_op_t *o = &f->ops[f->nops++];
    o->target[0] = '\0';
    o->op = '#';
    o->left[0] = '\0';
    o->right[0] = '\0';
    strncpy(o->comment, comment, MAX_NAME_LEN - 1);
}

static char *fresh_temp(flattener_t *f, char *buf) {
    snprintf(buf, MAX_NAME_LEN, "t%d", f->temp_counter++);
    return buf;
}

static char *fresh_check(flattener_t *f, char *buf) {
    snprintf(buf, MAX_NAME_LEN, "chk%d", f->check_counter++);
    return buf;
}

static void prefixed_name(const char *prefix, const char *name, char *out) {
    if (prefix[0]) snprintf(out, MAX_NAME_LEN, "%s%s", prefix, name);
    else strncpy(out, name, MAX_NAME_LEN - 1);
    out[MAX_NAME_LEN - 1] = '\0';
}

static void array_elem_name(const char *base, long long idx, char *out) {
    snprintf(out, MAX_NAME_LEN, "%s[%lld]", base, idx);
}

static void mark_assigned(flattener_t *f, const char *name) {
    for (int i = 0; i < f->nassigned; i++)
        if (strcmp(f->assigned[i], name) == 0) return;
    if (f->nassigned >= MAX_VARS) {
        flattener_record_overflow(f, "assigned variable", MAX_VARS);
        return;
    }
    strncpy(f->assigned[f->nassigned++], name, MAX_NAME_LEN - 1);
}

static int is_assigned(flattener_t *f, const char *name) {
    for (int i = 0; i < f->nassigned; i++)
        if (strcmp(f->assigned[i], name) == 0) return 1;
    return 0;
}

/* Compile-time variable lookup (returns 0 if not found or is runtime) */
static int ct_lookup(flattener_t *f, const char *name, long long *val) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            if (f->vars[i].is_runtime) return 0;
            *val = f->vars[i].value;
            return 1;
        }
    }
    return 0;
}

/* Lookup a var's runtime gate name (returns NULL if compile-time) */
static const char *ct_lookup_runtime(flattener_t *f, const char *name) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0 && f->vars[i].is_runtime)
            return f->vars[i].runtime_name;
    }
    return NULL;
}

/* Check if name is a known var (compile-time or runtime) */
static ct_var_t *ct_find(flattener_t *f, const char *name) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) return &f->vars[i];
    }
    return NULL;
}

static void ct_set(flattener_t *f, const char *name, long long val) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            f->vars[i].value = val;
            f->vars[i].is_runtime = 0;
            f->vars[i].runtime_name[0] = '\0';
            return;
        }
    }
    if (f->nvars < MAX_VARS) {
        memset(&f->vars[f->nvars], 0, sizeof(ct_var_t));
        strncpy(f->vars[f->nvars].name, name, MAX_NAME_LEN - 1);
        f->vars[f->nvars].value = val;
        f->nvars++;
        return;
    }
    flattener_record_overflow(f, "variable", MAX_VARS);
}

/* Set a var to be a runtime alias for a gate variable */
static void ct_set_runtime(flattener_t *f, const char *name, const char *gate_name) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            f->vars[i].is_runtime = 1;
            strncpy(f->vars[i].runtime_name, gate_name, MAX_NAME_LEN - 1);
            return;
        }
    }
    if (f->nvars < MAX_VARS) {
        memset(&f->vars[f->nvars], 0, sizeof(ct_var_t));
        strncpy(f->vars[f->nvars].name, name, MAX_NAME_LEN - 1);
        f->vars[f->nvars].is_runtime = 1;
        strncpy(f->vars[f->nvars].runtime_name, gate_name, MAX_NAME_LEN - 1);
        f->nvars++;
        return;
    }
    flattener_record_overflow(f, "variable", MAX_VARS);
}

/* Set a var to a big string constant (doesn't fit in long long) */
static void ct_set_big(flattener_t *f, const char *name, const char *bigval) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            f->vars[i].is_big = 1;
            f->vars[i].is_runtime = 0;
            strncpy(f->vars[i].bigvalue, bigval, 79);
            f->vars[i].bigvalue[79] = '\0';
            return;
        }
    }
    if (f->nvars < MAX_VARS) {
        memset(&f->vars[f->nvars], 0, sizeof(ct_var_t));
        strncpy(f->vars[f->nvars].name, name, MAX_NAME_LEN - 1);
        f->vars[f->nvars].is_big = 1;
        strncpy(f->vars[f->nvars].bigvalue, bigval, 79);
        f->vars[f->nvars].bigvalue[79] = '\0';
        f->nvars++;
        return;
    }
    flattener_record_overflow(f, "variable", MAX_VARS);
}

/* Signal lookup */
static signal_info_t *signal_lookup(flattener_t *f, const char *name) {
    for (int i = 0; i < f->nsignals; i++)
        if (strcmp(f->signals[i].name, name) == 0) return &f->signals[i];
    return NULL;
}

static void signal_add(flattener_t *f, const char *name, int dir, int is_public, int size) {
    if (f->nsignals >= MAX_SIGNALS) {
        flattener_record_overflow(f, "signal", MAX_SIGNALS);
        return;
    }
    signal_info_t *s = &f->signals[f->nsignals++];
    strncpy(s->name, name, MAX_NAME_LEN - 1);
    s->direction = dir;
    s->is_public = is_public;
    s->size = size;
}

/* Template lookup */
static template_t *template_lookup(flattener_t *f, const char *name) {
    for (int i = 0; i < f->prog->ntemplates; i++)
        if (strcmp(f->prog->templates[i].name, name) == 0)
            return &f->prog->templates[i];
    return NULL;
}

static int is_public_name(const char *name, const char **public_set, int npublic) {
    for (int i = 0; i < npublic; i++) {
        if (strcmp(name, public_set[i]) == 0) return 1;
    }
    return 0;
}

static void add_boolean_constraint(flattener_t *f, const char *var) {
    char minus_one[MAX_NAME_LEN], product[MAX_NAME_LEN], check[MAX_NAME_LEN];
    fresh_temp(f, minus_one);
    add_op(f, minus_one, '-', var, "1");
    mark_assigned(f, minus_one);

    fresh_temp(f, product);
    add_op(f, product, '*', var, minus_one);
    mark_assigned(f, product);

    fresh_check(f, check);
    add_op(f, check, 'E', product, "0");
}

static void add_horner_reconstruction(flattener_t *f, const char *bits_name,
                                      long long nbits, const char *value_name) {
    char bit_var[MAX_NAME_LEN];
    char acc[MAX_NAME_LEN], dbl[MAX_NAME_LEN], next[MAX_NAME_LEN], check[MAX_NAME_LEN];

    if (nbits <= 0) {
        fresh_check(f, check);
        add_op(f, check, 'E', "0", value_name);
        return;
    }

    array_elem_name(bits_name, nbits - 1, bit_var);
    strncpy(acc, bit_var, MAX_NAME_LEN - 1);
    acc[MAX_NAME_LEN - 1] = '\0';

    for (long long i = nbits - 2; i >= 0; i--) {
        fresh_temp(f, dbl);
        add_op(f, dbl, '+', acc, acc);
        mark_assigned(f, dbl);

        array_elem_name(bits_name, i, bit_var);
        fresh_temp(f, next);
        add_op(f, next, '+', dbl, bit_var);
        mark_assigned(f, next);

        strncpy(acc, next, MAX_NAME_LEN - 1);
        acc[MAX_NAME_LEN - 1] = '\0';
    }

    fresh_check(f, check);
    add_op(f, check, 'E', acc, value_name);
}

static void emit_greater_eq_than(flattener_t *f, const char *prefix,
                                 const char **public_set, int npublic,
                                 long long nbits) {
    char a_name[MAX_NAME_LEN], b_name[MAX_NAME_LEN], out_name[MAX_NAME_LEN];
    char a_bits[MAX_NAME_LEN], b_bits[MAX_NAME_LEN], diff_bits[MAX_NAME_LEN];
    char borrow_bits[MAX_NAME_LEN], no_borrow[MAX_NAME_LEN];

    prefixed_name(prefix, "a", a_name);
    prefixed_name(prefix, "b", b_name);
    prefixed_name(prefix, "out", out_name);
    prefixed_name(prefix, "a_bits", a_bits);
    prefixed_name(prefix, "b_bits", b_bits);
    prefixed_name(prefix, "diff_bits", diff_bits);
    prefixed_name(prefix, "borrow_bits", borrow_bits);
    prefixed_name(prefix, "no_borrow", no_borrow);

    if (prefix[0] == '\0') {
        signal_add(f, a_name, 1, is_public_name("a", public_set, npublic), 0);
        signal_add(f, b_name, 1, is_public_name("b", public_set, npublic), 0);
        signal_add(f, out_name, 2, 1, 0);
    }

    signal_add(f, a_bits, 1, 0, (int)nbits);
    signal_add(f, b_bits, 1, 0, (int)nbits);
    signal_add(f, diff_bits, 1, 0, (int)nbits);
    signal_add(f, borrow_bits, 1, 0, (int)nbits);
    signal_add(f, no_borrow, 1, 0, 0);

    for (long long i = 0; i < nbits; i++) {
        char bit_name[MAX_NAME_LEN];

        array_elem_name(a_bits, i, bit_name);
        add_boolean_constraint(f, bit_name);

        array_elem_name(b_bits, i, bit_name);
        add_boolean_constraint(f, bit_name);

        array_elem_name(diff_bits, i, bit_name);
        add_boolean_constraint(f, bit_name);

        array_elem_name(borrow_bits, i, bit_name);
        add_boolean_constraint(f, bit_name);
    }

    add_horner_reconstruction(f, a_bits, nbits, a_name);
    add_horner_reconstruction(f, b_bits, nbits, b_name);

    for (long long i = 0; i < nbits; i++) {
        char a_bit[MAX_NAME_LEN], b_bit[MAX_NAME_LEN], diff_bit[MAX_NAME_LEN];
        char borrow_bit[MAX_NAME_LEN], prev_borrow[MAX_NAME_LEN];
        char raw_diff[MAX_NAME_LEN], adjusted[MAX_NAME_LEN];
        char borrow_doubled[MAX_NAME_LEN], sum[MAX_NAME_LEN], check[MAX_NAME_LEN];

        array_elem_name(a_bits, i, a_bit);
        array_elem_name(b_bits, i, b_bit);
        array_elem_name(diff_bits, i, diff_bit);
        array_elem_name(borrow_bits, i, borrow_bit);

        fresh_temp(f, raw_diff);
        add_op(f, raw_diff, '-', a_bit, b_bit);
        mark_assigned(f, raw_diff);

        strncpy(adjusted, raw_diff, MAX_NAME_LEN - 1);
        adjusted[MAX_NAME_LEN - 1] = '\0';
        if (i > 0) {
            array_elem_name(borrow_bits, i - 1, prev_borrow);
            fresh_temp(f, adjusted);
            add_op(f, adjusted, '-', raw_diff, prev_borrow);
            mark_assigned(f, adjusted);
        }

        fresh_temp(f, borrow_doubled);
        add_op(f, borrow_doubled, '+', borrow_bit, borrow_bit);
        mark_assigned(f, borrow_doubled);

        fresh_temp(f, sum);
        add_op(f, sum, '+', diff_bit, borrow_doubled);
        mark_assigned(f, sum);

        fresh_check(f, check);
        add_op(f, check, 'E', adjusted, sum);
    }

    if (nbits > 0) {
        char last_borrow[MAX_NAME_LEN];
        array_elem_name(borrow_bits, nbits - 1, last_borrow);
        add_op(f, no_borrow, '-', "1", last_borrow);
    } else {
        add_op(f, no_borrow, 'I', "1", NULL);
    }
    mark_assigned(f, no_borrow);

    add_op(f, out_name, 'I', no_borrow, NULL);
    mark_assigned(f, out_name);
}

/* --- Compile-time expression evaluator --- */

static int eval_const(flattener_t *f, expr_t *e, long long *out) {
    if (!e) return 0;

    switch (e->type) {
    case EXPR_NUMBER: {
        /* Try to parse as long long; fail gracefully for overflow */
        char *endptr;
        errno = 0;
        long long val = strtoll(e->u.numstr, &endptr, 10);
        if (errno == ERANGE || *endptr != '\0') {
            return 0;  /* Number too large for long long */
        }
        *out = val;
        return 1;
    }

    case EXPR_IDENT:
        return ct_lookup(f, e->u.name, out);

    case EXPR_BINOP: {
        long long l, r;
        if (!eval_const(f, e->u.binop.left, &l)) return 0;
        if (!eval_const(f, e->u.binop.right, &r)) return 0;
        switch (e->u.binop.op) {
            case OP_ADD: *out = l + r; return 1;
            case OP_SUB: *out = l - r; return 1;
            case OP_MUL: *out = l * r; return 1;
            case OP_DIV: if (r == 0) return 0; *out = l / r; return 1;
            case OP_MOD: if (r == 0) return 0; *out = l % r; return 1;
            case OP_INTDIV: if (r == 0) return 0; *out = l / r; return 1;
            case OP_POW: {
                long long result = 1;
                for (long long i = 0; i < r; i++) result *= l;
                *out = result; return 1;
            }
            case OP_SHL: *out = l << r; return 1;
            case OP_SHR: *out = l >> r; return 1;
            case OP_BIT_AND: *out = l & r; return 1;
            case OP_BIT_OR: *out = l | r; return 1;
            case OP_BIT_XOR: *out = l ^ r; return 1;
            case OP_EQ: *out = (l == r); return 1;
            case OP_NEQ: *out = (l != r); return 1;
            case OP_LT: *out = (l < r); return 1;
            case OP_GT: *out = (l > r); return 1;
            case OP_LEQ: *out = (l <= r); return 1;
            case OP_GEQ: *out = (l >= r); return 1;
            case OP_AND: *out = (l && r); return 1;
            case OP_OR: *out = (l || r); return 1;
        }
        return 0;
    }

    case EXPR_UNARYOP: {
        long long v;
        if (!eval_const(f, e->u.unary.operand, &v)) return 0;
        if (e->u.unary.op == '-') { *out = -v; return 1; }
        if (e->u.unary.op == '!') { *out = !v; return 1; }
        if (e->u.unary.op == '~') { *out = ~v; return 1; }
        return 0;
    }

    case EXPR_TERNARY: {
        long long cond;
        if (!eval_const(f, e->u.ternary.cond, &cond)) return 0;
        if (cond) return eval_const(f, e->u.ternary.then_e, out);
        else return eval_const(f, e->u.ternary.else_e, out);
    }

    case EXPR_ARRAY_ACCESS: {
        /* Check if base is a compile-time var */
        return ct_lookup(f, e->u.array.name, out);
    }

    default:
        return 0;
    }
}

/* --- Expression flattener --- */

/* Forward declarations */
static void flatten_stmt(flattener_t *f, stmt_t *s, const char *prefix,
                         const char **public_set, int npublic);
static const char *flatten_expr(flattener_t *f, expr_t *e, const char *prefix,
                                const char *target_hint, char *result_buf);
static void flush_component(flattener_t *f, const char *comp_name, const char *prefix);

/* flatten_expr returns the variable name holding the result in result_buf.
   If target_hint is non-NULL, use it for the root operation. */
static const char *flatten_expr(flattener_t *f, expr_t *e, const char *prefix,
                                const char *target_hint, char *result_buf) {
    if (!e) { strcpy(result_buf, "0"); return result_buf; }

    switch (e->type) {
    case EXPR_NUMBER: {
        long long val;
        if (eval_const(f, e, &val)) {
            snprintf(result_buf, MAX_NAME_LEN, "%lld", val);
        } else {
            /* Big number: emit the string directly */
            strncpy(result_buf, e->u.numstr, MAX_NAME_LEN - 1);
            result_buf[MAX_NAME_LEN - 1] = '\0';
        }
        return result_buf;
    }

    case EXPR_IDENT: {
        /* Check runtime var alias first */
        const char *rt = ct_lookup_runtime(f, e->u.name);
        if (rt) {
            strncpy(result_buf, rt, MAX_NAME_LEN - 1);
            return result_buf;
        }
        long long val;
        if (ct_lookup(f, e->u.name, &val)) {
            snprintf(result_buf, MAX_NAME_LEN, "%lld", val);
            return result_buf;
        }
        /* Check if it's a big compile-time var */
        ct_var_t *var = ct_find(f, e->u.name);
        if (var && var->is_big) {
            strncpy(result_buf, var->bigvalue, MAX_NAME_LEN - 1);
            result_buf[MAX_NAME_LEN - 1] = '\0';
            return result_buf;
        }
        /* Try prefixed signal */
        char full[MAX_NAME_LEN];
        if (prefix[0]) snprintf(full, MAX_NAME_LEN, "%s%s", prefix, e->u.name);
        else strncpy(full, e->u.name, MAX_NAME_LEN);

        if (signal_lookup(f, full) || is_assigned(f, full)) {
            strcpy(result_buf, full);
            return result_buf;
        }
        if (is_assigned(f, e->u.name)) {
            strcpy(result_buf, e->u.name);
            return result_buf;
        }
        strcpy(result_buf, full);
        return result_buf;
    }

    case EXPR_ARRAY_ACCESS: {
        long long idx;
        if (!eval_const(f, e->u.array.index, &idx)) {
            fprintf(stderr, "Flattener: array index must be compile-time constant\n");
            strcpy(result_buf, "ERROR");
            return result_buf;
        }

        /* Check for component.field[idx] pattern (dotted name from parser) */
        char *dot = strchr(e->u.array.name, '.');
        if (dot) {
            char comp[MAX_NAME_LEN], field[MAX_NAME_LEN];
            int dot_pos = (int)(dot - e->u.array.name);
            strncpy(comp, e->u.array.name, dot_pos);
            comp[dot_pos] = '\0';
            strncpy(field, dot + 1, MAX_NAME_LEN - 1);
            field[MAX_NAME_LEN - 1] = '\0';

            /* Flush the component's body */
            flush_component(f, comp, prefix);

            char comp_prefix[MAX_NAME_LEN];
            if (prefix[0])
                snprintf(comp_prefix, MAX_NAME_LEN, "%s%s_", prefix, comp);
            else
                snprintf(comp_prefix, MAX_NAME_LEN, "%s_", comp);
            snprintf(result_buf, MAX_NAME_LEN, "%s%s_%lld", comp_prefix, field, idx);
            return result_buf;
        }

        char base[MAX_NAME_LEN];
        if (prefix[0]) snprintf(base, MAX_NAME_LEN, "%s%s", prefix, e->u.array.name);
        else strncpy(base, e->u.array.name, MAX_NAME_LEN);

        signal_info_t *sig = signal_lookup(f, base);
        if (sig && sig->size > 0) {
            /* Declared array signal: use AOA array syntax */
            snprintf(result_buf, MAX_NAME_LEN, "%s[%lld]", base, idx);
        } else {
            /* Intermediate array: flatten to scalar */
            snprintf(result_buf, MAX_NAME_LEN, "%s_%lld", base, idx);
        }
        return result_buf;
    }

    case EXPR_COMPONENT_ACCESS: {
        /* Flush the component's body if not yet flattened */
        flush_component(f, e->u.comp_access.comp, prefix);
        char comp_prefix[MAX_NAME_LEN];
        if (prefix[0])
            snprintf(comp_prefix, MAX_NAME_LEN, "%s%s_", prefix, e->u.comp_access.comp);
        else
            snprintf(comp_prefix, MAX_NAME_LEN, "%s_", e->u.comp_access.comp);
        snprintf(result_buf, MAX_NAME_LEN, "%s%s", comp_prefix, e->u.comp_access.field);
        return result_buf;
    }

    case EXPR_UNARYOP: {
        if (e->u.unary.op == '-') {
            long long val;
            if (eval_const(f, e->u.unary.operand, &val)) {
                snprintf(result_buf, MAX_NAME_LEN, "%lld", -val);
                return result_buf;
            }
            char operand[MAX_NAME_LEN];
            flatten_expr(f, e->u.unary.operand, prefix, NULL, operand);
            char target[MAX_NAME_LEN];
            if (target_hint) strncpy(target, target_hint, MAX_NAME_LEN);
            else fresh_temp(f, target);
            add_op(f, target, '-', "0", operand);
            mark_assigned(f, target);
            strcpy(result_buf, target);
            return result_buf;
        }
        if (e->u.unary.op == '!') {
            long long val;
            if (eval_const(f, e->u.unary.operand, &val)) {
                snprintf(result_buf, MAX_NAME_LEN, "%d", val == 0 ? 1 : 0);
                return result_buf;
            }
            char operand[MAX_NAME_LEN];
            flatten_expr(f, e->u.unary.operand, prefix, NULL, operand);
            char target[MAX_NAME_LEN];
            if (target_hint) strncpy(target, target_hint, MAX_NAME_LEN);
            else fresh_temp(f, target);
            add_op(f, target, '-', "1", operand);
            mark_assigned(f, target);
            strcpy(result_buf, target);
            return result_buf;
        }
        /* Other unary: try eval */
        char operand[MAX_NAME_LEN];
        flatten_expr(f, e->u.unary.operand, prefix, NULL, operand);
        strcpy(result_buf, operand);
        return result_buf;
    }

    case EXPR_BINOP: {
        /* Try compile-time evaluation first */
        long long val;
        if (eval_const(f, e, &val)) {
            snprintf(result_buf, MAX_NAME_LEN, "%lld", val);
            return result_buf;
        }

        binop_t op = e->u.binop.op;

        if (op == OP_ADD || op == OP_SUB || op == OP_MUL) {
            char left[MAX_NAME_LEN], right[MAX_NAME_LEN];
            flatten_expr(f, e->u.binop.left, prefix, NULL, left);
            flatten_expr(f, e->u.binop.right, prefix, NULL, right);
            char target[MAX_NAME_LEN];
            if (target_hint) strncpy(target, target_hint, MAX_NAME_LEN);
            else fresh_temp(f, target);
            char aoa_op = (op == OP_ADD) ? '+' : (op == OP_SUB) ? '-' : '*';
            add_op(f, target, aoa_op, left, right);
            mark_assigned(f, target);
            strcpy(result_buf, target);
            return result_buf;
        }

        if (op == OP_POW) {
            long long exp_val;
            if (!eval_const(f, e->u.binop.right, &exp_val)) {
                fprintf(stderr, "Flattener: exponent must be compile-time constant\n");
                strcpy(result_buf, "ERROR");
                return result_buf;
            }
            if (exp_val == 0) { strcpy(result_buf, "1"); return result_buf; }

            char base_var[MAX_NAME_LEN];
            flatten_expr(f, e->u.binop.left, prefix, NULL, base_var);
            if (exp_val == 1) {
                if (target_hint) {
                    add_op(f, target_hint, 'I', base_var, NULL);
                    mark_assigned(f, target_hint);
                    strcpy(result_buf, target_hint);
                } else {
                    strcpy(result_buf, base_var);
                }
                return result_buf;
            }
            char cur_result[MAX_NAME_LEN];
            strcpy(cur_result, base_var);
            for (long long i = 1; i < exp_val; i++) {
                char target[MAX_NAME_LEN];
                if (i == exp_val - 1 && target_hint)
                    strncpy(target, target_hint, MAX_NAME_LEN);
                else
                    fresh_temp(f, target);
                add_op(f, target, '*', cur_result, base_var);
                mark_assigned(f, target);
                strcpy(cur_result, target);
            }
            strcpy(result_buf, cur_result);
            return result_buf;
        }

        /* Other ops: compile-time only */
        if (eval_const(f, e, &val)) {
            snprintf(result_buf, MAX_NAME_LEN, "%lld", val);
            return result_buf;
        }

        fprintf(stderr, "Flattener: cannot flatten operator %d\n", op);
        strcpy(result_buf, "ERROR");
        return result_buf;
    }

    case EXPR_TERNARY: {
        long long cond;
        if (eval_const(f, e->u.ternary.cond, &cond)) {
            if (cond)
                return flatten_expr(f, e->u.ternary.then_e, prefix, target_hint, result_buf);
            else
                return flatten_expr(f, e->u.ternary.else_e, prefix, target_hint, result_buf);
        }
        fprintf(stderr, "Flattener: ternary condition must be compile-time constant\n");
        strcpy(result_buf, "ERROR");
        return result_buf;
    }

    case EXPR_CALL: {
        /* Try compile-time evaluation first */
        long long val;
        if (eval_const(f, e, &val)) {
            snprintf(result_buf, MAX_NAME_LEN, "%lld", val);
            return result_buf;
        }

        /* Check for library function: Poseidon(x, salt) or Poseidon(x) */
        if (strcmp(e->u.call.name, "Poseidon") == 0) {
            if (e->u.call.nargs < 1 || e->u.call.nargs > 2) {
                fprintf(stderr, "Poseidon() requires 1 or 2 arguments\n");
                strcpy(result_buf, "ERROR");
                return result_buf;
            }
            /* Flatten arguments */
            char arg0_buf[MAX_NAME_LEN], arg1_buf[MAX_NAME_LEN];
            const char *arg0 = flatten_expr(f, e->u.call.args[0], prefix, NULL, arg0_buf);
            const char *arg1;
            if (e->u.call.nargs == 2) {
                arg1 = flatten_expr(f, e->u.call.args[1], prefix, NULL, arg1_buf);
            } else {
                arg1 = "0";  /* Poseidon(x) → Poseidon(x, 0) */
            }

            /* Generate unique prefix for this instance */
            char inst_prefix[MAX_NAME_LEN];
            snprintf(inst_prefix, MAX_NAME_LEN, "%spos%d_",
                     prefix, poseidon_instance_counter++);

            /* Expand inline */
            poseidon_expand(f, inst_prefix, arg0, arg1, result_buf);
            return result_buf;
        }

        fprintf(stderr, "Flattener: unknown runtime function '%s'\n", e->u.call.name);
        strcpy(result_buf, "ERROR");
        return result_buf;
    }

    default:
        strcpy(result_buf, "ERROR");
        return result_buf;
    }
}

/* Flatten an assignment: flatten expr, emit ops ending with target */
static void flatten_assign(flattener_t *f, const char *target, expr_t *e, const char *prefix) {
    char result[MAX_NAME_LEN];
    flatten_expr(f, e, prefix, target, result);
    if (strcmp(result, target) != 0) {
        add_op(f, target, 'I', result, NULL);
    }
    mark_assigned(f, target);
}

/* Resolve signal name from expression */
static void resolve_signal_name(flattener_t *f, expr_t *e, const char *prefix, char *out) {
    if (e->type == EXPR_IDENT) {
        if (prefix[0]) snprintf(out, MAX_NAME_LEN, "%s%s", prefix, e->u.name);
        else strncpy(out, e->u.name, MAX_NAME_LEN);
    } else if (e->type == EXPR_ARRAY_ACCESS) {
        long long idx;
        eval_const(f, e->u.array.index, &idx);

        /* Check for component.field[idx] pattern */
        char *dot = strchr(e->u.array.name, '.');
        if (dot) {
            char comp[MAX_NAME_LEN], field[MAX_NAME_LEN];
            int dot_pos = (int)(dot - e->u.array.name);
            strncpy(comp, e->u.array.name, dot_pos);
            comp[dot_pos] = '\0';
            strncpy(field, dot + 1, MAX_NAME_LEN - 1);
            field[MAX_NAME_LEN - 1] = '\0';
            if (prefix[0])
                snprintf(out, MAX_NAME_LEN, "%s%s_%s_%lld", prefix, comp, field, idx);
            else
                snprintf(out, MAX_NAME_LEN, "%s_%s_%lld", comp, field, idx);
        } else {
            char base[MAX_NAME_LEN];
            if (prefix[0]) snprintf(base, MAX_NAME_LEN, "%s%s", prefix, e->u.array.name);
            else strncpy(base, e->u.array.name, MAX_NAME_LEN);

            signal_info_t *sig = signal_lookup(f, base);
            if (sig && sig->size > 0)
                snprintf(out, MAX_NAME_LEN, "%s[%lld]", base, idx);
            else
                snprintf(out, MAX_NAME_LEN, "%s_%lld", base, idx);
        }
    } else if (e->type == EXPR_COMPONENT_ACCESS) {
        if (prefix[0])
            snprintf(out, MAX_NAME_LEN, "%s%s_%s", prefix, e->u.comp_access.comp, e->u.comp_access.field);
        else
            snprintf(out, MAX_NAME_LEN, "%s_%s", e->u.comp_access.comp, e->u.comp_access.field);
    } else {
        strcpy(out, "ERROR");
    }
}

/* Get simple name from target expr (without prefix) */
static void get_target_name(flattener_t *f, expr_t *e, char *out) {
    if (e->type == EXPR_IDENT) {
        strncpy(out, e->u.name, MAX_NAME_LEN);
    } else if (e->type == EXPR_ARRAY_ACCESS) {
        long long idx;
        eval_const(f, e->u.array.index, &idx);
        snprintf(out, MAX_NAME_LEN, "%s_%lld", e->u.array.name, idx);
    } else if (e->type == EXPR_COMPONENT_ACCESS) {
        snprintf(out, MAX_NAME_LEN, "%s_%s", e->u.comp_access.comp, e->u.comp_access.field);
    } else {
        strcpy(out, "ERROR");
    }
}

/* Flush a pending component's body (flatten it now) */
static void flush_component(flattener_t *f, const char *comp_name, const char *prefix) {
    for (int i = 0; i < f->ncomps; i++) {
        if (strcmp(f->comps[i].name, comp_name) == 0 && !f->comps[i].flattened) {
            f->comps[i].flattened = 1;
            comp_inst_t *ci = &f->comps[i];
            template_t *tmpl = &f->prog->templates[ci->template_idx];
            for (int p = 0; p < ci->nparams && p < tmpl->nparams; p++)
                ct_set(f, tmpl->params[p], ci->param_values[p]);
            if (strcmp(tmpl->name, "GreaterEqThan") == 0 && ci->nparams >= 1) {
                emit_greater_eq_than(f, ci->prefix, NULL, 0, ci->param_values[0]);
                return;
            }
            for (int j = 0; j < tmpl->nbody; j++) {
                flatten_stmt(f, tmpl->body[j], ci->prefix, NULL, 0);
            }
            return;
        }
    }
    /* Also check with prefix */
    char full_name[MAX_NAME_LEN];
    if (prefix[0]) snprintf(full_name, MAX_NAME_LEN, "%s%s", prefix, comp_name);
    else strncpy(full_name, comp_name, MAX_NAME_LEN - 1);
    for (int i = 0; i < f->ncomps; i++) {
        if (strcmp(f->comps[i].name, full_name) == 0 && !f->comps[i].flattened) {
            f->comps[i].flattened = 1;
            comp_inst_t *ci = &f->comps[i];
            template_t *tmpl = &f->prog->templates[ci->template_idx];
            for (int p = 0; p < ci->nparams && p < tmpl->nparams; p++)
                ct_set(f, tmpl->params[p], ci->param_values[p]);
            if (strcmp(tmpl->name, "GreaterEqThan") == 0 && ci->nparams >= 1) {
                emit_greater_eq_than(f, ci->prefix, NULL, 0, ci->param_values[0]);
                return;
            }
            for (int j = 0; j < tmpl->nbody; j++) {
                flatten_stmt(f, tmpl->body[j], ci->prefix, NULL, 0);
            }
            return;
        }
    }
}

/* --- Statement flattening --- */

static void handle_signal_decl(flattener_t *f, stmt_t *s, const char *prefix,
                                const char **public_set, int npublic) {
    signal_dir_t dir = s->u.signal_decl.dir;

    /* Sub-component signals are just gate variables, not declared inputs */
    if (prefix[0] != '\0') return;

    for (int i = 0; i < s->u.signal_decl.count; i++) {
        char full[MAX_NAME_LEN];
        strncpy(full, s->u.signal_decl.names[i], MAX_NAME_LEN);

        int size = 0;
        if (s->u.signal_decl.sizes[i]) {
            long long sz;
            eval_const(f, s->u.signal_decl.sizes[i], &sz);
            size = (int)sz;
        }

        int is_pub = 0;
        if (s->u.signal_decl.vis == SIG_VIS_PUBLIC) {
            is_pub = 1;
        } else if (s->u.signal_decl.vis == SIG_VIS_PRIVATE) {
            is_pub = 0;
        } else {
            for (int j = 0; j < npublic; j++) {
                if (strcmp(s->u.signal_decl.names[i], public_set[j]) == 0) {
                    is_pub = 1;
                    break;
                }
            }
        }

        signal_add(f, full, (int)dir, is_pub, size);
    }
}

static void handle_var_decl(flattener_t *f, stmt_t *s) {
    for (int i = 0; i < s->u.var_decl.count; i++) {
        long long val = 0;
        if (s->u.var_decl.inits[i]) {
            if (eval_const(f, s->u.var_decl.inits[i], &val)) {
                ct_set(f, s->u.var_decl.names[i], val);
            } else if (s->u.var_decl.inits[i]->type == EXPR_NUMBER) {
                /* Big number literal: store as big string value */
                ct_set_big(f, s->u.var_decl.names[i], s->u.var_decl.inits[i]->u.numstr);
            } else {
                ct_set(f, s->u.var_decl.names[i], 0);
            }
        } else {
            ct_set(f, s->u.var_decl.names[i], 0);
        }
    }
}

static void handle_var_assign(flattener_t *f, stmt_t *s, const char *prefix,
                               const char **public_set, int npublic) {
    /* Check if this is a component instantiation */
    if (s->u.var_assign.value && s->u.var_assign.value->type == EXPR_CALL) {
        const char *tmpl_name = s->u.var_assign.value->u.call.name;
        template_t *tmpl = template_lookup(f, tmpl_name);
        if (tmpl) {
            char comp_name[MAX_NAME_LEN];
            get_target_name(f, s->u.var_assign.target, comp_name);

            char comp_prefix[MAX_NAME_LEN];
            if (prefix[0])
                snprintf(comp_prefix, MAX_NAME_LEN, "%s%s_", prefix, comp_name);
            else
                snprintf(comp_prefix, MAX_NAME_LEN, "%s_", comp_name);

            /* Set template params from call args */
            for (int i = 0; i < tmpl->nparams && i < s->u.var_assign.value->u.call.nargs; i++) {
                long long val;
                eval_const(f, s->u.var_assign.value->u.call.args[i], &val);
                ct_set(f, tmpl->params[i], val);
            }

            /* Register as pending component — body will be flattened later */
            if (f->ncomps < 256) {
                comp_inst_t *ci = &f->comps[f->ncomps++];
                strncpy(ci->name, comp_name, MAX_NAME_LEN - 1);
                strncpy(ci->prefix, comp_prefix, MAX_NAME_LEN - 1);
                ci->template_idx = (int)(tmpl - f->prog->templates);
                ci->nparams = 0;
                for (int i = 0; i < tmpl->nparams && i < s->u.var_assign.value->u.call.nargs; i++) {
                    long long val = 0;
                    eval_const(f, s->u.var_assign.value->u.call.args[i], &val);
                    ci->param_values[ci->nparams++] = val;
                }
                ci->flattened = 0;
            }
            return;
        }
    }

    /* Check if target is component access: c.signal = expr */
    if (s->u.var_assign.target->type == EXPR_COMPONENT_ACCESS) {
        char target_full[MAX_NAME_LEN];
        if (prefix[0])
            snprintf(target_full, MAX_NAME_LEN, "%s%s_%s", prefix,
                     s->u.var_assign.target->u.comp_access.comp,
                     s->u.var_assign.target->u.comp_access.field);
        else
            snprintf(target_full, MAX_NAME_LEN, "%s_%s",
                     s->u.var_assign.target->u.comp_access.comp,
                     s->u.var_assign.target->u.comp_access.field);
        flatten_assign(f, target_full, s->u.var_assign.value, prefix);
        return;
    }

    /* Regular compile-time var assignment */
    char target_name[MAX_NAME_LEN];
    get_target_name(f, s->u.var_assign.target, target_name);

    /* Try compile-time evaluation */
    long long val;
    if (s->u.var_assign.op == ASGN_EQ && eval_const(f, s->u.var_assign.value, &val)) {
        ct_set(f, target_name, val);
        return;
    }

    /* Check for big number literal assignment: var X = BIG_NUMBER */
    if (s->u.var_assign.op == ASGN_EQ && s->u.var_assign.value &&
        s->u.var_assign.value->type == EXPR_NUMBER) {
        ct_set_big(f, target_name, s->u.var_assign.value->u.numstr);
        return;
    }

    /* Check if target is a known signal */
    char full_target[MAX_NAME_LEN];
    if (prefix[0]) snprintf(full_target, MAX_NAME_LEN, "%s%s", prefix, target_name);
    else strncpy(full_target, target_name, MAX_NAME_LEN);

    if (signal_lookup(f, full_target)) {
        if (s->u.var_assign.op == ASGN_EQ) {
            flatten_assign(f, full_target, s->u.var_assign.value, prefix);
        }
        return;
    }

    /* Compile-time compound assignment */
    long long cur_val = 0;
    int is_ct = ct_lookup(f, target_name, &cur_val);
    long long rhs;
    if (eval_const(f, s->u.var_assign.value, &rhs)) {
        switch (s->u.var_assign.op) {
            case ASGN_EQ:       cur_val = rhs; break;
            case ASGN_PLUS_EQ:  cur_val += rhs; break;
            case ASGN_MINUS_EQ: cur_val -= rhs; break;
            case ASGN_STAR_EQ:  cur_val *= rhs; break;
            case ASGN_SLASH_EQ: if (rhs) cur_val /= rhs; break;
            case ASGN_MOD_EQ:   if (rhs) cur_val %= rhs; break;
            case ASGN_SHL_EQ:   cur_val <<= rhs; break;
            case ASGN_SHR_EQ:   cur_val >>= rhs; break;
            case ASGN_AND_EQ:   cur_val &= rhs; break;
            case ASGN_OR_EQ:    cur_val |= rhs; break;
            case ASGN_XOR_EQ:   cur_val ^= rhs; break;
            default: break;
        }
        ct_set(f, target_name, cur_val);
    } else {
        /* RHS involves signals — var becomes a runtime gate variable.
           This handles patterns like: lc += out[i] * e2 */
        char rhs_var[MAX_NAME_LEN];
        flatten_expr(f, s->u.var_assign.value, prefix, NULL, rhs_var);

        const char *old_rt = ct_lookup_runtime(f, target_name);
        char new_gate[MAX_NAME_LEN];
        fresh_temp(f, new_gate);

        if (s->u.var_assign.op == ASGN_EQ) {
            /* Simple assignment: var = signal_expr */
            ct_set_runtime(f, target_name, rhs_var);
        } else if (s->u.var_assign.op == ASGN_PLUS_EQ) {
            if (old_rt) {
                /* lc += expr  →  new_gate = old_gate + rhs_var */
                add_op(f, new_gate, '+', old_rt, rhs_var);
            } else if (is_ct && cur_val == 0) {
                /* lc was 0, lc += expr  →  just alias to rhs_var */
                ct_set_runtime(f, target_name, rhs_var);
                return;
            } else if (is_ct) {
                /* lc was a constant, lc += expr  →  new = const + rhs */
                char const_str[MAX_NAME_LEN];
                snprintf(const_str, MAX_NAME_LEN, "%lld", cur_val);
                add_op(f, new_gate, '+', const_str, rhs_var);
            } else {
                add_op(f, new_gate, '+', "0", rhs_var);
            }
            mark_assigned(f, new_gate);
            ct_set_runtime(f, target_name, new_gate);
        } else if (s->u.var_assign.op == ASGN_STAR_EQ) {
            if (old_rt) {
                add_op(f, new_gate, '*', old_rt, rhs_var);
            } else {
                char const_str[MAX_NAME_LEN];
                snprintf(const_str, MAX_NAME_LEN, "%lld", cur_val);
                add_op(f, new_gate, '*', const_str, rhs_var);
            }
            mark_assigned(f, new_gate);
            ct_set_runtime(f, target_name, new_gate);
        } else if (s->u.var_assign.op == ASGN_MINUS_EQ) {
            if (old_rt) {
                add_op(f, new_gate, '-', old_rt, rhs_var);
            } else {
                char const_str[MAX_NAME_LEN];
                snprintf(const_str, MAX_NAME_LEN, "%lld", cur_val);
                add_op(f, new_gate, '-', const_str, rhs_var);
            }
            mark_assigned(f, new_gate);
            ct_set_runtime(f, target_name, new_gate);
        }
    }

    (void)public_set;
    (void)npublic;
}

static void handle_for(flattener_t *f, stmt_t *s, const char *prefix,
                        const char **public_set, int npublic) {
    /* Evaluate init */
    if (s->u.for_loop.init)
        flatten_stmt(f, s->u.for_loop.init, prefix, public_set, npublic);

    int max_iter = 10000;
    for (int count = 0; count < max_iter; count++) {
        long long cond;
        if (!eval_const(f, s->u.for_loop.cond, &cond) || !cond)
            break;

        for (int i = 0; i < s->u.for_loop.nbody; i++)
            flatten_stmt(f, s->u.for_loop.body[i], prefix, public_set, npublic);

        if (s->u.for_loop.update)
            flatten_stmt(f, s->u.for_loop.update, prefix, public_set, npublic);
    }
}

static void handle_if(flattener_t *f, stmt_t *s, const char *prefix,
                       const char **public_set, int npublic) {
    long long cond;
    if (!eval_const(f, s->u.if_else.cond, &cond)) {
        fprintf(stderr, "Flattener: if condition must be compile-time constant\n");
        return;
    }
    if (cond) {
        for (int i = 0; i < s->u.if_else.nthen; i++)
            flatten_stmt(f, s->u.if_else.then_body[i], prefix, public_set, npublic);
    } else {
        for (int i = 0; i < s->u.if_else.nelse; i++)
            flatten_stmt(f, s->u.if_else.else_body[i], prefix, public_set, npublic);
    }
}

static void flatten_stmt(flattener_t *f, stmt_t *s, const char *prefix,
                         const char **public_set, int npublic) {
    if (!s) return;

    switch (s->type) {
    case STMT_SIGNAL_DECL:
        handle_signal_decl(f, s, prefix, public_set, npublic);
        break;

    case STMT_VAR_DECL:
        handle_var_decl(f, s);
        break;

    case STMT_CONSTRAIN_ASSIGN: {
        char target[MAX_NAME_LEN];
        resolve_signal_name(f, s->u.constrain_assign.target, prefix, target);
        flatten_assign(f, target, s->u.constrain_assign.value, prefix);
        break;
    }

    case STMT_WITNESS_ASSIGN: {
        char target[MAX_NAME_LEN];
        resolve_signal_name(f, s->u.witness_assign.target, prefix, target);

        if (prefix[0] != '\0') {
            /* Sub-component witness: declare as private input.
               The prover provides these values; === constraints verify them. */
            if (!signal_lookup(f, target)) {
                signal_add(f, target, 1, 0, 0);  /* input, not public, scalar */
            }
        }

        if (f->verbose) {
            char comment[MAX_NAME_LEN];
            snprintf(comment, MAX_NAME_LEN, "witness hint: %s <-- (no constraint)", target);
            add_comment(f, comment);
        }
        break;
    }

    case STMT_CONSTRAIN_EQ: {
        char left[MAX_NAME_LEN], right[MAX_NAME_LEN];
        flatten_expr(f, s->u.constrain_eq.left, prefix, NULL, left);
        flatten_expr(f, s->u.constrain_eq.right, prefix, NULL, right);
        char check[MAX_NAME_LEN];
        fresh_check(f, check);
        add_op(f, check, 'E', left, right);
        break;
    }

    case STMT_VAR_ASSIGN:
        handle_var_assign(f, s, prefix, public_set, npublic);
        break;

    case STMT_FOR:
        handle_for(f, s, prefix, public_set, npublic);
        break;

    case STMT_IF:
        handle_if(f, s, prefix, public_set, npublic);
        break;

    case STMT_BLOCK:
        for (int i = 0; i < s->u.block.nstmts; i++)
            flatten_stmt(f, s->u.block.stmts[i], prefix, public_set, npublic);
        break;

    case STMT_COMPONENT_DECL:
        /* Just register, instantiation handled in VAR_ASSIGN */
        break;

    case STMT_LOG:
    case STMT_ASSERT:
    case STMT_RETURN:
        break;  /* Skip */
    }
}

/* --- Public API --- */

void flattener_init(flattener_t *f, program_t *prog, int verbose) {
    memset(f, 0, sizeof(flattener_t));
    f->prog = prog;
    f->verbose = verbose;
}

int flattener_run(flattener_t *f) {
    if (!f->prog->main_comp) {
        fprintf(stderr, "Flattener: no 'component main' found\n");
        return 1;
    }

    main_component_t *mc = f->prog->main_comp;
    template_t *tmpl = template_lookup(f, mc->template_name);
    if (!tmpl) {
        fprintf(stderr, "Flattener: template '%s' not found\n", mc->template_name);
        return 1;
    }

    /* Set template params from main args */
    for (int i = 0; i < tmpl->nparams && i < mc->nargs; i++) {
        long long val;
        if (eval_const(f, mc->args[i], &val)) {
            ct_set(f, tmpl->params[i], val);
        } else {
            fprintf(stderr, "Flattener: main component arg %d must be constant\n", i);
            return 1;
        }
    }

    /* Build public set */
    const char *public_set[MAX_PARAMS];
    for (int i = 0; i < mc->npublic; i++)
        public_set[i] = mc->public_inputs[i];

    if (strcmp(tmpl->name, "GreaterEqThan") == 0) {
        long long nbits = 0;
        if (tmpl->nparams > 0 && !ct_lookup(f, tmpl->params[0], &nbits)) {
            fprintf(stderr, "Flattener: GreaterEqThan requires constant bit width\n");
            return 1;
        }
        emit_greater_eq_than(f, "", public_set, mc->npublic, nbits);
        return f->overflowed ? 1 : 0;
    }

    /* Flatten template body */
    for (int i = 0; i < tmpl->nbody; i++) {
        flatten_stmt(f, tmpl->body[i], "", public_set, mc->npublic);
    }

    return f->overflowed ? 1 : 0;
}
