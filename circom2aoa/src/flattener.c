/*
 * Flattener: Circom AST → flat AOA operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "flattener.h"

/* --- Helpers --- */

void flattener_record_overflow(flattener_t *f, const char *kind, int limit) {
    if (!f->overflowed) {
        fprintf(stderr, "Flattener: %s limit exceeded (%d)\n", kind, limit);
    }
    f->overflowed = 1;
}

static unsigned __int128 ct_int_max_u(void) {
    return (((unsigned __int128)1) << 127) - 1;
}

static int parse_ct_int(const char *s, ct_int_t *out) {
    const char *p = s;
    int neg = 0;
    if (*p == '-') {
        neg = 1;
        p++;
    }
    if (*p < '0' || *p > '9') return 0;

    unsigned __int128 val = 0;
    unsigned __int128 max = ct_int_max_u();
    while (*p >= '0' && *p <= '9') {
        unsigned digit = (unsigned)(*p - '0');
        if (val > (max - digit) / 10) return 0;
        val = val * 10 + digit;
        p++;
    }
    if (*p != '\0') return 0;

    *out = neg ? -(ct_int_t)val : (ct_int_t)val;
    return 1;
}

static void format_ct_int(ct_int_t val, char *out, size_t out_size) {
    if (out_size == 0) return;
    if (val == 0) {
        snprintf(out, out_size, "0");
        return;
    }

    char tmp[128];
    int pos = 0;
    int neg = val < 0;
    unsigned __int128 u = neg ? (unsigned __int128)(-val) : (unsigned __int128)val;
    while (u > 0 && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (u % 10));
        u /= 10;
    }

    size_t w = 0;
    if (neg && w + 1 < out_size) out[w++] = '-';
    while (pos > 0 && w + 1 < out_size) out[w++] = tmp[--pos];
    out[w] = '\0';
}

static int ct_int_to_ll(ct_int_t val, long long *out) {
    if (val < (ct_int_t)LLONG_MIN || val > (ct_int_t)LLONG_MAX) return 0;
    *out = (long long)val;
    return 1;
}

static int ct_add_checked(ct_int_t a, ct_int_t b, ct_int_t *out) {
    if (a >= 0 && b >= 0) {
        unsigned __int128 ua = (unsigned __int128)a;
        unsigned __int128 ub = (unsigned __int128)b;
        if (ua > ct_int_max_u() - ub) return 0;
        *out = (ct_int_t)(ua + ub);
        return 1;
    }
    *out = a + b;
    return 1;
}

static int ct_sub_checked(ct_int_t a, ct_int_t b, ct_int_t *out) {
    if (a >= 0 && b >= 0) {
        unsigned __int128 ua = (unsigned __int128)a;
        unsigned __int128 ub = (unsigned __int128)b;
        if (ua >= ub) {
            *out = (ct_int_t)(ua - ub);
        } else {
            *out = -(ct_int_t)(ub - ua);
        }
        return 1;
    }
    *out = a - b;
    return 1;
}

static int ct_mul_checked(ct_int_t a, ct_int_t b, ct_int_t *out) {
    if (a >= 0 && b >= 0) {
        unsigned __int128 ua = (unsigned __int128)a;
        unsigned __int128 ub = (unsigned __int128)b;
        if (ua != 0 && ub > ct_int_max_u() / ua) return 0;
        *out = (ct_int_t)(ua * ub);
        return 1;
    }
    *out = a * b;
    return 1;
}

static int ct_shl_checked(ct_int_t a, ct_int_t b, ct_int_t *out) {
    long long shift;
    if (!ct_int_to_ll(b, &shift) || shift < 0 || shift >= 127) return 0;
    if (a >= 0) {
        unsigned __int128 ua = (unsigned __int128)a;
        if (ua > (ct_int_max_u() >> shift)) return 0;
        *out = (ct_int_t)(ua << shift);
        return 1;
    }
    *out = a << shift;
    return 1;
}

static int ct_shr_checked(ct_int_t a, ct_int_t b, ct_int_t *out) {
    long long shift;
    if (!ct_int_to_ll(b, &shift) || shift < 0 || shift >= 127) return 0;
    if (a >= 0) {
        *out = (ct_int_t)(((unsigned __int128)a) >> shift);
        return 1;
    }
    *out = a >> shift;
    return 1;
}

static int ct_pow_checked(ct_int_t base, ct_int_t exp, ct_int_t *out) {
    long long e;
    if (!ct_int_to_ll(exp, &e) || e < 0) return 0;
    ct_int_t result = 1;
    for (long long i = 0; i < e; i++) {
        if (!ct_mul_checked(result, base, &result)) return 0;
    }
    *out = result;
    return 1;
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

/* Compile-time variable lookup (returns 0 if not found, runtime, or big literal) */
static int ct_lookup(flattener_t *f, const char *name, ct_int_t *val) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            if (f->vars[i].is_runtime) return 0;
            if (f->vars[i].is_big) return 0;
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

static void ct_set(flattener_t *f, const char *name, ct_int_t val) {
    for (int i = 0; i < f->nvars; i++) {
        if (strcmp(f->vars[i].name, name) == 0) {
            f->vars[i].value = val;
            f->vars[i].is_big = 0;
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

static int eval_const(flattener_t *f, expr_t *e, ct_int_t *out);
static void flatten_stmt(flattener_t *f, stmt_t *s, const char *prefix,
                         const char **public_set, int npublic);
static const char *flatten_expr(flattener_t *f, expr_t *e, const char *prefix,
                                const char *target_hint, char *result_buf);
static void flush_component(flattener_t *f, const char *comp_name, const char *prefix);

typedef struct {
    int uses_array_input;
    char array_input_name[MAX_NAME_LEN];
    char scalar_input_names[MAX_PARAMS][MAX_NAME_LEN];
    int nscalar_inputs;
    char output_name[MAX_NAME_LEN];
    int output_index;  /* -1 for scalar output, >=0 for fixed array element */
    ct_int_t param_values[MAX_PARAMS];
    int nparams;
} template_call_sig_t;

static int eval_const_ll(flattener_t *f, expr_t *e, long long *out) {
    ct_int_t val;
    return eval_const(f, e, &val) && ct_int_to_ll(val, out);
}

static int expr_is_ident_named(expr_t *e, const char *name) {
    return e && e->type == EXPR_IDENT && strcmp(e->u.name, name) == 0;
}

static int register_component_instance(flattener_t *f, template_t *tmpl,
                                       const char *comp_name, const char *prefix,
                                       const ct_int_t *param_values, int nparams,
                                       char *comp_id_out, char *comp_prefix_out) {
    char comp_id[MAX_NAME_LEN];
    if (prefix[0])
        snprintf(comp_id, MAX_NAME_LEN, "%s%s", prefix, comp_name);
    else
        strncpy(comp_id, comp_name, MAX_NAME_LEN - 1);
    comp_id[MAX_NAME_LEN - 1] = '\0';

    char comp_prefix[MAX_NAME_LEN];
    if (prefix[0])
        snprintf(comp_prefix, MAX_NAME_LEN, "%s%s_", prefix, comp_name);
    else
        snprintf(comp_prefix, MAX_NAME_LEN, "%s_", comp_name);
    comp_prefix[MAX_NAME_LEN - 1] = '\0';

    if (f->ncomps >= MAX_COMPONENTS) {
        flattener_record_overflow(f, "component", MAX_COMPONENTS);
        return 0;
    }

    comp_inst_t *ci = &f->comps[f->ncomps++];
    memset(ci, 0, sizeof(*ci));
    strncpy(ci->name, comp_id, MAX_NAME_LEN - 1);
    strncpy(ci->prefix, comp_prefix, MAX_NAME_LEN - 1);
    ci->template_idx = (int)(tmpl - f->prog->templates);
    ci->nparams = nparams;
    for (int i = 0; i < nparams; i++)
        ci->param_values[i] = param_values[i];
    ci->flattened = 0;

    if (comp_id_out) {
        strncpy(comp_id_out, comp_id, MAX_NAME_LEN - 1);
        comp_id_out[MAX_NAME_LEN - 1] = '\0';
    }
    if (comp_prefix_out) {
        strncpy(comp_prefix_out, comp_prefix, MAX_NAME_LEN - 1);
        comp_prefix_out[MAX_NAME_LEN - 1] = '\0';
    }
    return 1;
}

static int infer_template_call_signature(flattener_t *f, template_t *tmpl, int nargs,
                                         template_call_sig_t *sig) {
    memset(sig, 0, sizeof(*sig));
    sig->output_index = -1;

    for (int i = 0; i < tmpl->nbody; i++) {
        stmt_t *s = tmpl->body[i];
        if (!s || s->type != STMT_SIGNAL_DECL) continue;

        for (int j = 0; j < s->u.signal_decl.count; j++) {
            expr_t *size_expr = s->u.signal_decl.sizes[j];
            const char *name = s->u.signal_decl.names[j];

            if (s->u.signal_decl.dir == SIG_INPUT) {
                if (size_expr) {
                    long long size_val = 0;
                    if (sig->uses_array_input || sig->nscalar_inputs > 0) return 0;

                    sig->uses_array_input = 1;
                    strncpy(sig->array_input_name, name, MAX_NAME_LEN - 1);

                    if (tmpl->nparams == 1 && expr_is_ident_named(size_expr, tmpl->params[0])) {
                        sig->nparams = 1;
                        sig->param_values[0] = nargs;
                    } else if (tmpl->nparams == 0 &&
                               eval_const_ll(f, size_expr, &size_val) &&
                               size_val == nargs) {
                        sig->nparams = 0;
                    } else {
                        return 0;
                    }
                } else {
                    if (sig->uses_array_input || tmpl->nparams != 0 ||
                        sig->nscalar_inputs >= MAX_PARAMS)
                        return 0;
                    strncpy(sig->scalar_input_names[sig->nscalar_inputs], name, MAX_NAME_LEN - 1);
                    sig->nscalar_inputs++;
                }
            } else if (s->u.signal_decl.dir == SIG_OUTPUT) {
                long long size_val = 0;
                if (sig->output_name[0] != '\0') return 0;

                strncpy(sig->output_name, name, MAX_NAME_LEN - 1);
                if (!size_expr) continue;

                if (eval_const_ll(f, size_expr, &size_val) && size_val == 1) {
                    sig->output_index = 0;
                } else {
                    return 0;
                }
            }
        }
    }

    if (sig->output_name[0] == '\0') return 0;
    if (sig->uses_array_input)
        return tmpl->nparams == sig->nparams;
    return tmpl->nparams == 0 && sig->nscalar_inputs == nargs;
}

static int flatten_template_call(flattener_t *f, template_t *tmpl, expr_t *e,
                                 const char *prefix, char *result_buf) {
    template_call_sig_t sig;
    if (!infer_template_call_signature(f, tmpl, e->u.call.nargs, &sig))
        return 0;

    char comp_name[MAX_NAME_LEN];
    snprintf(comp_name, MAX_NAME_LEN, "calltmp%d", f->call_counter++);

    char comp_prefix[MAX_NAME_LEN];
    if (!register_component_instance(f, tmpl, comp_name, prefix,
                                     sig.param_values, sig.nparams,
                                     NULL, comp_prefix)) {
        strcpy(result_buf, "ERROR");
        return 1;
    }

    if (sig.uses_array_input) {
        for (int i = 0; i < e->u.call.nargs; i++) {
            char arg_value[MAX_NAME_LEN];
            char target[MAX_NAME_LEN];
            flatten_expr(f, e->u.call.args[i], prefix, NULL, arg_value);
            snprintf(target, MAX_NAME_LEN, "%s%s_%d", comp_prefix, sig.array_input_name, i);
            add_op(f, target, 'I', arg_value, NULL);
            mark_assigned(f, target);
        }
    } else {
        for (int i = 0; i < sig.nscalar_inputs; i++) {
            char arg_value[MAX_NAME_LEN];
            char target[MAX_NAME_LEN];
            flatten_expr(f, e->u.call.args[i], prefix, NULL, arg_value);
            snprintf(target, MAX_NAME_LEN, "%s%s", comp_prefix, sig.scalar_input_names[i]);
            add_op(f, target, 'I', arg_value, NULL);
            mark_assigned(f, target);
        }
    }

    flush_component(f, comp_name, prefix);

    if (sig.output_index >= 0)
        snprintf(result_buf, MAX_NAME_LEN, "%s%s_%d", comp_prefix, sig.output_name, sig.output_index);
    else
        snprintf(result_buf, MAX_NAME_LEN, "%s%s", comp_prefix, sig.output_name);
    return 1;
}

/* --- Compile-time expression evaluator --- */

static int eval_const(flattener_t *f, expr_t *e, ct_int_t *out) {
    if (!e) return 0;

    switch (e->type) {
    case EXPR_NUMBER: {
        return parse_ct_int(e->u.numstr, out);
    }

    case EXPR_IDENT:
        return ct_lookup(f, e->u.name, out);

    case EXPR_BINOP: {
        ct_int_t l, r;
        if (!eval_const(f, e->u.binop.left, &l)) return 0;
        if (!eval_const(f, e->u.binop.right, &r)) return 0;
        switch (e->u.binop.op) {
            case OP_ADD: return ct_add_checked(l, r, out);
            case OP_SUB: return ct_sub_checked(l, r, out);
            case OP_MUL: return ct_mul_checked(l, r, out);
            case OP_DIV: if (r == 0) return 0; *out = l / r; return 1;
            case OP_MOD: if (r == 0) return 0; *out = l % r; return 1;
            case OP_INTDIV: if (r == 0) return 0; *out = l / r; return 1;
            case OP_POW: return ct_pow_checked(l, r, out);
            case OP_SHL: return ct_shl_checked(l, r, out);
            case OP_SHR: return ct_shr_checked(l, r, out);
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
        ct_int_t v;
        if (!eval_const(f, e->u.unary.operand, &v)) return 0;
        if (e->u.unary.op == '-') { *out = -v; return 1; }
        if (e->u.unary.op == '!') { *out = !v; return 1; }
        if (e->u.unary.op == '~') { *out = ~v; return 1; }
        return 0;
    }

    case EXPR_TERNARY: {
        ct_int_t cond;
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
        ct_int_t val;
        if (eval_const(f, e, &val)) {
            format_ct_int(val, result_buf, MAX_NAME_LEN);
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
        ct_int_t val;
        if (ct_lookup(f, e->u.name, &val)) {
            format_ct_int(val, result_buf, MAX_NAME_LEN);
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
        ct_int_t idx_val;
        long long idx;
        if (!eval_const(f, e->u.array.index, &idx_val) || !ct_int_to_ll(idx_val, &idx)) {
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
            ct_int_t val;
            if (eval_const(f, e->u.unary.operand, &val)) {
                format_ct_int(-val, result_buf, MAX_NAME_LEN);
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
            ct_int_t val;
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
        ct_int_t val;
        if (eval_const(f, e, &val)) {
            format_ct_int(val, result_buf, MAX_NAME_LEN);
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
            ct_int_t exp_ct;
            long long exp_val;
            if (!eval_const(f, e->u.binop.right, &exp_ct) || !ct_int_to_ll(exp_ct, &exp_val)) {
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
            format_ct_int(val, result_buf, MAX_NAME_LEN);
            return result_buf;
        }

        fprintf(stderr, "Flattener: cannot flatten operator %d\n", op);
        strcpy(result_buf, "ERROR");
        return result_buf;
    }

    case EXPR_TERNARY: {
        ct_int_t cond;
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
        ct_int_t val;
        if (eval_const(f, e, &val)) {
            format_ct_int(val, result_buf, MAX_NAME_LEN);
            return result_buf;
        }

        template_t *tmpl = template_lookup(f, e->u.call.name);
        if (tmpl) {
            if (flatten_template_call(f, tmpl, e, prefix, result_buf))
                return result_buf;
            fprintf(stderr, "Flattener: template '%s' cannot be used as an expression call\n",
                    e->u.call.name);
            strcpy(result_buf, "ERROR");
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
        ct_int_t idx_val;
        long long idx = 0;
        if (!eval_const(f, e->u.array.index, &idx_val) || !ct_int_to_ll(idx_val, &idx)) {
            strcpy(out, "ERROR");
            return;
        }

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
        ct_int_t idx_val;
        long long idx = 0;
        if (!eval_const(f, e->u.array.index, &idx_val) || !ct_int_to_ll(idx_val, &idx)) {
            strcpy(out, "ERROR");
            return;
        }
        snprintf(out, MAX_NAME_LEN, "%s_%lld", e->u.array.name, idx);
    } else if (e->type == EXPR_COMPONENT_ACCESS) {
        snprintf(out, MAX_NAME_LEN, "%s_%s", e->u.comp_access.comp, e->u.comp_access.field);
    } else {
        strcpy(out, "ERROR");
    }
}

/* Flatten a pending component's body with its params bound.
   Vars share one global table, so the parent's bindings for any names the
   child reuses (params like "n", body vars, loop counters) are restored
   afterwards, and vars the child created are dropped: template vars are
   local in Circom. */
static void flatten_component_body(flattener_t *f, comp_inst_t *ci) {
    template_t *tmpl = &f->prog->templates[ci->template_idx];

    int nvars_before = f->nvars;
    ct_var_t saved[MAX_PARAMS];
    int existed[MAX_PARAMS];
    int np = ci->nparams < tmpl->nparams ? ci->nparams : tmpl->nparams;
    for (int p = 0; p < np; p++) {
        ct_var_t *v = ct_find(f, tmpl->params[p]);
        existed[p] = (v != NULL);
        if (v) saved[p] = *v;
        ct_set(f, tmpl->params[p], ci->param_values[p]);
    }

    for (int j = 0; j < tmpl->nbody; j++) {
        flatten_stmt(f, tmpl->body[j], ci->prefix, NULL, 0);
    }

    for (int p = 0; p < np; p++) {
        if (!existed[p]) continue;
        ct_var_t *v = ct_find(f, tmpl->params[p]);
        if (v) *v = saved[p];
    }
    /* During the body flatten no parent statements run, so every entry
       past the snapshot belongs to the child. */
    if (f->nvars > nvars_before) f->nvars = nvars_before;
}

/* Flush a pending component's body (flatten it now) */
static void flush_component(flattener_t *f, const char *comp_name, const char *prefix) {
    char full_name[MAX_NAME_LEN];
    if (prefix[0]) snprintf(full_name, MAX_NAME_LEN, "%s%s", prefix, comp_name);
    else strncpy(full_name, comp_name, MAX_NAME_LEN - 1);
    for (int i = 0; i < f->ncomps; i++) {
        if (strcmp(f->comps[i].name, full_name) == 0 && !f->comps[i].flattened) {
            f->comps[i].flattened = 1;
            flatten_component_body(f, &f->comps[i]);
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
            ct_int_t sz_ct;
            long long sz = 0;
            if (!eval_const(f, s->u.signal_decl.sizes[i], &sz_ct) ||
                !ct_int_to_ll(sz_ct, &sz)) {
                fprintf(stderr, "Flattener: signal array size must be compile-time constant\n");
                f->overflowed = 1;
            }
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
        ct_int_t val = 0;
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

            /* Register as pending component — body will be flattened later.
               Param args are evaluated now, in the parent's scope; the
               params themselves are only bound when the body is flushed. */
            ct_int_t param_values[MAX_PARAMS];
            int nparams = 0;
            for (int i = 0; i < tmpl->nparams && i < s->u.var_assign.value->u.call.nargs; i++) {
                ct_int_t val = 0;
                eval_const(f, s->u.var_assign.value->u.call.args[i], &val);
                param_values[nparams++] = val;
            }
            register_component_instance(f, tmpl, comp_name, prefix,
                                        param_values, nparams, NULL, NULL);
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
    ct_int_t val;
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
    ct_int_t cur_val = 0;
    int is_ct = ct_lookup(f, target_name, &cur_val);
    ct_int_t rhs;
    if (eval_const(f, s->u.var_assign.value, &rhs)) {
        int ok = 1;
        switch (s->u.var_assign.op) {
            case ASGN_EQ:       cur_val = rhs; break;
            case ASGN_PLUS_EQ:  ok = ct_add_checked(cur_val, rhs, &cur_val); break;
            case ASGN_MINUS_EQ: ok = ct_sub_checked(cur_val, rhs, &cur_val); break;
            case ASGN_STAR_EQ:  ok = ct_mul_checked(cur_val, rhs, &cur_val); break;
            case ASGN_SLASH_EQ: if (rhs) cur_val /= rhs; break;
            case ASGN_MOD_EQ:   if (rhs) cur_val %= rhs; break;
            case ASGN_SHL_EQ:   ok = ct_shl_checked(cur_val, rhs, &cur_val); break;
            case ASGN_SHR_EQ:   ok = ct_shr_checked(cur_val, rhs, &cur_val); break;
            case ASGN_AND_EQ:   cur_val &= rhs; break;
            case ASGN_OR_EQ:    cur_val |= rhs; break;
            case ASGN_XOR_EQ:   cur_val ^= rhs; break;
            default: break;
        }
        if (!ok) {
            fprintf(stderr, "Flattener: compile-time arithmetic overflow\n");
            f->overflowed = 1;
            return;
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
                format_ct_int(cur_val, const_str, MAX_NAME_LEN);
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
                format_ct_int(cur_val, const_str, MAX_NAME_LEN);
                add_op(f, new_gate, '*', const_str, rhs_var);
            }
            mark_assigned(f, new_gate);
            ct_set_runtime(f, target_name, new_gate);
        } else if (s->u.var_assign.op == ASGN_MINUS_EQ) {
            if (old_rt) {
                add_op(f, new_gate, '-', old_rt, rhs_var);
            } else {
                char const_str[MAX_NAME_LEN];
                format_ct_int(cur_val, const_str, MAX_NAME_LEN);
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
        ct_int_t cond;
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
    ct_int_t cond;
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

        /* Witness-assigned signals are provided by the prover: declare them
           as private inputs; === constraints verify them. Top-level
           intermediates are promoted, sub-component signals added fresh. */
        signal_info_t *sig = signal_lookup(f, target);
        if (sig) {
            if (sig->direction == 0) sig->direction = 1;
        } else if (prefix[0] != '\0') {
            signal_add(f, target, 1, 0, 0);  /* input, not public, scalar */
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

    case STMT_ASSERT:
    {
        ct_int_t val;
        if (!eval_const(f, s->u.assert_stmt.expr, &val)) {
            fprintf(stderr, "Flattener: assert expression must be compile-time constant\n");
            f->overflowed = 1;
            break;
        }
        if (!val) {
            fprintf(stderr, "Flattener: assert failed\n");
            f->overflowed = 1;
        }
        break;
    }

    case STMT_LOG:
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
        ct_int_t val;
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

    /* Flatten template body */
    for (int i = 0; i < tmpl->nbody; i++) {
        flatten_stmt(f, tmpl->body[i], "", public_set, mc->npublic);
    }

    return f->overflowed ? 1 : 0;
}
