/*
 * Poseidon Library for circom2aoa
 *
 * Provides inline expansion of Poseidon(x, salt) and Poseidon(x) function
 * calls during circuit flattening.
 *
 * Usage in Circom:
 *   include "poseidon";
 *   hash <== Poseidon(value, blinding);   // 2-input commitment
 *   hash <== Poseidon(value);             // 1-input hash (salt=0)
 */

#ifndef POSEIDON_LIB_H
#define POSEIDON_LIB_H

#include "flattener.h"

/*
 * Expand Poseidon(input0, input1) inline.
 *
 * Emits the full Poseidon permutation as flat operations with a unique
 * prefix. Returns the name of the output signal (element 1 of final state).
 *
 * Parameters:
 *   f       - flattener state
 *   prefix  - prefix for all generated signal names (e.g. "__poseidon_0_")
 *   input0  - name of first input signal (or constant string)
 *   input1  - name of second input signal (or constant string)
 *   out_buf - buffer to receive the output signal name
 *
 * Returns out_buf filled with the output signal name.
 */
char *poseidon_expand(flattener_t *f, const char *prefix,
                      const char *input0, const char *input1,
                      char *out_buf);

#endif /* POSEIDON_LIB_H */
