/*
 * Poseidon Library: inline expansion of Poseidon hash for circom2aoa.
 *
 * Generates the full Poseidon permutation (t=3, Rf=8, Rp=57, alpha=5)
 * as flat AOA operations. Round constants and MDS matrix are embedded
 * from poseidon_constants.h (generated from the standard Grain LFSR).
 */

#include <stdio.h>
#include <string.h>
#include "poseidon_lib.h"
#include "poseidon_constants.h"

/* Internal helpers matching flattener.c's add_op interface */
static void p_add_op(flattener_t *f, const char *target, char op,
                     const char *left, const char *right) {
    if (f->nops >= MAX_OPS) {
        fprintf(stderr, "Poseidon: too many operations (limit %d)\n", MAX_OPS);
        return;
    }
    flat_op_t *o = &f->ops[f->nops++];
    strncpy(o->target, target, MAX_NAME_LEN - 1);
    o->target[MAX_NAME_LEN - 1] = '\0';
    o->op = op;
    strncpy(o->left, left, MAX_NAME_LEN - 1);
    o->left[MAX_NAME_LEN - 1] = '\0';
    if (right) {
        strncpy(o->right, right, MAX_NAME_LEN - 1);
        o->right[MAX_NAME_LEN - 1] = '\0';
    } else {
        o->right[0] = '\0';
    }
    o->comment[0] = '\0';
}

/* Build a prefixed signal name: prefix + suffix → buf */
static void pname(char *buf, const char *prefix, const char *suffix) {
    snprintf(buf, MAX_NAME_LEN, "%s%s", prefix, suffix);
}

/* Build a prefixed signal name with numeric index */
static void pname_i(char *buf, const char *prefix, const char *base, int i) {
    snprintf(buf, MAX_NAME_LEN, "%s%s_%d", prefix, base, i);
}

/* Build a prefixed signal name with two numeric indices */
static void pname_ii(char *buf, const char *prefix, const char *base, int i, int j) {
    snprintf(buf, MAX_NAME_LEN, "%s%s_%d_%d", prefix, base, i, j);
}

/* Build a prefixed signal name with three numeric indices */
static void pname_iii(char *buf, const char *prefix, const char *base, int i, int j, int k) {
    snprintf(buf, MAX_NAME_LEN, "%s%s_%d_%d_%d", prefix, base, i, j, k);
}

/*
 * Emit a single S-box (x^5) inline.
 *
 * in2 = in * in
 * in4 = in2 * in2
 * out = in4 * in
 */
static void emit_sbox(flattener_t *f, const char *prefix,
                       const char *in_name, const char *out_name) {
    char in2[MAX_NAME_LEN], in4[MAX_NAME_LEN];
    snprintf(in2, MAX_NAME_LEN, "%ssq", prefix);
    snprintf(in4, MAX_NAME_LEN, "%sq4", prefix);

    p_add_op(f, in2, '*', in_name, in_name);       /* in2 = in * in */
    p_add_op(f, in4, '*', in2, in2);                /* in4 = in2 * in2 */
    p_add_op(f, (char *)out_name, '*', in4, in_name); /* out = in4 * in */
}

char *poseidon_expand(flattener_t *f, const char *prefix,
                      const char *input0, const char *input1,
                      char *out_buf) {
    const int t = POSEIDON_T;
    const int nRounds = POSEIDON_NROUNDS;
    const int half_Rf = POSEIDON_RF / 2;

    char buf[MAX_NAME_LEN];
    char state[3][MAX_NAME_LEN];     /* current state signals */
    char next_state[3][MAX_NAME_LEN];
    char mds[3][3][MAX_NAME_LEN];    /* MDS constant signals */
    char rc[MAX_NAME_LEN];           /* round constant signal */
    char ark[3][MAX_NAME_LEN];       /* after round-constant addition */
    char sbox_out[3][MAX_NAME_LEN];  /* after S-box */
    char mix[3][3][MAX_NAME_LEN];    /* MDS mix products */
    char mixsum[3][MAX_NAME_LEN];    /* partial MDS sums */

    /* --- Emit MDS matrix constants --- */
    for (int i = 0; i < t; i++) {
        for (int j = 0; j < t; j++) {
            pname_ii(mds[i][j], prefix, "mds", i, j);
            p_add_op(f, mds[i][j], 'I', POSEIDON_MDS[i][j], NULL);
        }
    }

    /* --- Initial state: [0, input0, input1] --- */
    pname_i(state[0], prefix, "s", 0);
    p_add_op(f, state[0], 'I', "0", NULL);
    pname(state[1], prefix, "s_i0");
    p_add_op(f, state[1], 'I', input0, NULL);
    pname(state[2], prefix, "s_i1");
    p_add_op(f, state[2], 'I', input1, NULL);

    /* --- Process rounds --- */
    for (int r = 0; r < nRounds; r++) {
        int is_full = (r < half_Rf) || (r >= half_Rf + POSEIDON_RP);

        /* AddRoundConstants: ark[i] = state[i] + rc[r*t + i] */
        for (int i = 0; i < t; i++) {
            int ci = r * t + i;
            pname_ii(buf, prefix, "rc", r, i);
            p_add_op(f, buf, 'I', POSEIDON_RC[ci], NULL);
            snprintf(rc, MAX_NAME_LEN, "%s", buf);

            pname_ii(ark[i], prefix, "ark", r, i);
            p_add_op(f, ark[i], '+', state[i], rc);
        }

        /* S-box */
        if (is_full) {
            /* Full round: S-box on all elements */
            for (int i = 0; i < t; i++) {
                char sbox_prefix[MAX_NAME_LEN];
                snprintf(sbox_prefix, MAX_NAME_LEN, "%ssb%d_%d_", prefix, r, i);
                pname_ii(sbox_out[i], prefix, "sb", r, i);
                emit_sbox(f, sbox_prefix, ark[i], sbox_out[i]);
            }
        } else {
            /* Partial round: S-box on first element only */
            char sbox_prefix[MAX_NAME_LEN];
            snprintf(sbox_prefix, MAX_NAME_LEN, "%ssb%d_0_", prefix, r);
            pname_ii(sbox_out[0], prefix, "sb", r, 0);
            emit_sbox(f, sbox_prefix, ark[0], sbox_out[0]);
            /* Other elements pass through */
            strcpy(sbox_out[1], ark[1]);
            strcpy(sbox_out[2], ark[2]);
        }

        /* MDS mix: mix[i][j] = mds[i][j] * sbox_out[j] */
        for (int i = 0; i < t; i++) {
            for (int j = 0; j < t; j++) {
                pname_iii(mix[i][j], prefix, "mx", r, i, j);
                p_add_op(f, mix[i][j], '*', mds[i][j], sbox_out[j]);
            }
        }

        /* Sum: next_state[i] = mix[i][0] + mix[i][1] + mix[i][2] */
        for (int i = 0; i < t; i++) {
            pname_iii(mixsum[i], prefix, "ms", r, i, 0);
            p_add_op(f, mixsum[i], '+', mix[i][0], mix[i][1]);

            pname_ii(next_state[i], prefix, "st", r + 1, i);
            p_add_op(f, next_state[i], '+', mixsum[i], mix[i][2]);
        }

        /* Next round's state = this round's output */
        for (int i = 0; i < t; i++)
            strcpy(state[i], next_state[i]);
    }

    /* Output: element 1 of final state */
    strcpy(out_buf, state[1]);
    return out_buf;
}
