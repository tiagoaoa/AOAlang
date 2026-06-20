/*
    Copyright 2018 0KIMS association.

    This file is part of circom (Zero Knowledge Circuit Compiler).

    circom is a free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    circom is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
    License for more details.

    You should have received a copy of the GNU General Public License
    along with circom. If not, see <https://www.gnu.org/licenses/>.
*/

// circomlib's comparators.circom.

pragma circom 2.0.0;

include "bitify.circom";

template IsZero() {
    signal input in;
    signal output out;

    signal inv;

    inv <-- in!=0 ? 1/in : 0;

    out <== -in*inv +1;
    in*out === 0;
}


template IsEqual() {
    signal input in[2];
    signal output out;

    component isz = IsZero();

    in[1] - in[0] ==> isz.in;

    isz.out ==> out;
}

template ForceEqualIfEnabled() {
    signal input enabled;
    signal input in[2];

    component isz = IsZero();

    in[1] - in[0] ==> isz.in;

    (1 - isz.out)*enabled === 0;
}

// N is the number of bits the input  have.
// The MSF is the sign bit.
template LessThan(n) {
    assert(n <= 252);
    signal input in[2];
    signal output out;

    component n2b = Num2Bits(n+1);

    n2b.in <== in[0]+ (1<<n) - in[1];

    out <== 1-n2b.out[n];
}

// N is the number of bits the input  have.
// The MSF is the sign bit.
template LessEqThan(n) {
    signal input in[2];
    signal output out;

    component lt = LessThan(n);

    lt.in[0] <== in[0];
    lt.in[1] <== in[1]+1;
    lt.out ==> out;
}

// N is the number of bits the input  have.
// The MSF is the sign bit.
template GreaterThan(n) {
    signal input in[2];
    signal output out;

    component lt = LessThan(n);

    lt.in[0] <== in[1];
    lt.in[1] <== in[0];
    lt.out ==> out;
}

// N is the number of bits the input  have.
// The MSF is the sign bit.
template GreaterEqThan(n) {
    signal input in[2];
    signal output out;

    component lt = LessThan(n);

    lt.in[0] <== in[1];
    lt.in[1] <== in[0]+1;
    lt.out ==> out;
}

// Horner-form bit reconstruction. Takes n boolean input bits (supplied by the
// prover) and reconstructs the field element they encode using repeated doubling
// (acc = acc + acc + bit) instead of the Sum(bit_i * 2^i) form, while enforcing
// each input is boolean. circom2aoa lowers each constant-coefficient product
// bit_i * 2^i as an explicit field multiplication, so the doubling form removes
// ~n multiplications per decomposition (additions are free in the AOA backend).
//
// Bits are taken as inputs (not derived with `<--`) to match the zyga witness
// model: aoac classifies `<--` outputs as declared private inputs that the
// prover must supply anyway, so explicit named bit inputs keep the witness
// interface stable instead of relying on mangled internal signal names.
//
// Used as a range check + comparison primitive: decomposing `a - b` into n bits
// proves `a >= b` for n-bit operands (the difference is non-negative).
template Bits2NumHorner(n) {
    signal input bits[n];
    signal output out;
    signal acc[n];
    var i;

    // Use a named zero rather than the literal `0`: aoac miscompiles `x == 0`
    // (it maps the literal 0 onto the constant-1 wire, enforcing `x == 1`), so
    // booleanity must be asserted against a named zero signal.
    signal zero;
    zero <== 0;

    for (i = 0; i < n; i++) {
        bits[i] * (bits[i] - 1) === zero;
    }

    acc[n - 1] <== bits[n - 1];
    for (i = n - 2; i >= 0; i--) {
        acc[i] <== acc[i + 1] + acc[i + 1] + bits[i];
    }

    out <== acc[0];
}
