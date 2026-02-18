/*
 * Emitter: flat operations → valid .aoa text
 */

#include <stdio.h>
#include <string.h>
#include "emitter.h"

/* Check if target is an output signal */
static int is_output_signal(flattener_t *f, const char *target) {
    for (int i = 0; i < f->nsignals; i++) {
        signal_info_t *s = &f->signals[i];
        if (s->direction != 2) continue;  /* 2 = output */
        if (strcmp(target, s->name) == 0) return 1;
        /* Array output: check prefix match */
        if (s->size > 0) {
            size_t len = strlen(s->name);
            if (strncmp(target, s->name, len) == 0 &&
                (target[len] == '[' || target[len] == '_'))
                return 1;
        }
    }
    return 0;
}

static void format_op(FILE *out, const char *target, flat_op_t *op) {
    switch (op->op) {
    case '#':
        fprintf(out, "# %s\n", op->comment);
        return;
    case 'I':  /* identity */
        fprintf(out, "%s = %s\n", target, op->left);
        return;
    case 'C':  /* constant */
        fprintf(out, "%s = %s\n", target, op->left);
        return;
    case 'E':  /* equality constraint */
        fprintf(out, "%s = %s == %s\n", target, op->left, op->right);
        return;
    case '+': case '-': case '*':
        fprintf(out, "%s = %s %c %s\n", target, op->left, op->op, op->right);
        return;
    default:
        fprintf(out, "# unknown op: %s = %s %c %s\n", target, op->left, op->op, op->right);
        return;
    }
}

void emitter_emit(FILE *out, flattener_t *f) {
    /* Collect signals by visibility */
    int has_private = 0, has_deferred = 0;

    for (int i = 0; i < f->nsignals; i++) {
        signal_info_t *s = &f->signals[i];
        if (s->direction == 2 || s->is_public) has_deferred = 1;  /* output or public input */
        else if (s->direction == 1) has_private = 1;               /* private input */
    }

    /* Emit private declarations */
    if (has_private) {
        fprintf(out, "decl private ");
        int first = 1;
        for (int i = 0; i < f->nsignals; i++) {
            signal_info_t *s = &f->signals[i];
            if (s->direction == 1 && !s->is_public) {
                if (!first) fprintf(out, ", ");
                first = 0;
                if (s->size > 0) fprintf(out, "%s[%d]", s->name, s->size);
                else fprintf(out, "%s", s->name);
            }
        }
        fprintf(out, "\n");
    }

    /* Emit deferred declarations */
    if (has_deferred) {
        fprintf(out, "decl deferred ");
        int first = 1;
        for (int i = 0; i < f->nsignals; i++) {
            signal_info_t *s = &f->signals[i];
            if (s->direction == 2 || s->is_public) {
                if (!first) fprintf(out, ", ");
                first = 0;
                if (s->size > 0) fprintf(out, "%s[%d]", s->name, s->size);
                else fprintf(out, "%s", s->name);
            }
        }
        fprintf(out, "\n");
    }

    /* Blank separator */
    if (has_private || has_deferred) fprintf(out, "\n");

    /* Emit constraint lines */
    for (int i = 0; i < f->nops; i++) {
        flat_op_t *op = &f->ops[i];

        if (op->op == '#') {
            format_op(out, NULL, op);
            continue;
        }

        const char *target = op->target;

        if (is_output_signal(f, target)) {
            /* Output signal: compute into gate var, then equality constraint */
            char gate_name[MAX_NAME_LEN];
            snprintf(gate_name, MAX_NAME_LEN, "computed%s", target);
            /* Replace any [ ] in gate_name */
            for (char *c = gate_name; *c; c++) {
                if (*c == '[' || *c == ']') *c = '_';
            }
            format_op(out, gate_name, op);
            /* Equality constraint */
            char check_name[MAX_NAME_LEN];
            snprintf(check_name, MAX_NAME_LEN, "outcheck%s", target);
            for (char *c = check_name; *c; c++) {
                if (*c == '[' || *c == ']') *c = '_';
            }
            fprintf(out, "%s = %s == %s\n", check_name, gate_name, target);
        } else {
            format_op(out, target, op);
        }
    }
}
