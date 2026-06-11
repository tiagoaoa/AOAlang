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

// circomlib's comparators.circom, plus a local GreaterEqThan64 extension
// at the end of the file.

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

// --- Local extension (not part of circomlib) ---
// 64-bit greater-or-equal. The templates above are limited to n <= 62 in
// circom2aoa: the transpiler evaluates constants like (1 << n) in signed
// 64-bit arithmetic, so larger widths silently overflow. This template
// splits the operands into 32-bit halves to stay within that range.
template GreaterEqThan64() {
    signal input in[2];
    signal output out;

    signal a_lo;
    signal a_hi;
    signal b_lo;
    signal b_hi;

    signal a_reconstructed;
    signal b_reconstructed;

    a_lo <-- in[0] & 4294967295;
    a_hi <-- in[0] >> 32;
    b_lo <-- in[1] & 4294967295;
    b_hi <-- in[1] >> 32;

    component a_lo_bits = Num2Bits(32);
    component a_hi_bits = Num2Bits(32);
    component b_lo_bits = Num2Bits(32);
    component b_hi_bits = Num2Bits(32);

    a_lo_bits.in <== a_lo;
    a_hi_bits.in <== a_hi;
    b_lo_bits.in <== b_lo;
    b_hi_bits.in <== b_hi;

    a_reconstructed <== a_lo + a_hi * 4294967296;
    b_reconstructed <== b_lo + b_hi * 4294967296;

    a_reconstructed === in[0];
    b_reconstructed === in[1];

    component hi_ge_ab = GreaterEqThan(32);
    hi_ge_ab.in[0] <== a_hi;
    hi_ge_ab.in[1] <== b_hi;

    component hi_ge_ba = GreaterEqThan(32);
    hi_ge_ba.in[0] <== b_hi;
    hi_ge_ba.in[1] <== a_hi;

    component lo_ge = GreaterEqThan(32);
    lo_ge.in[0] <== a_lo;
    lo_ge.in[1] <== b_lo;

    signal hi_eq;
    signal hi_gt;

    hi_eq <== hi_ge_ab.out * hi_ge_ba.out;
    hi_gt <== hi_ge_ab.out * (1 - hi_ge_ba.out);

    out <== hi_gt + hi_eq * lo_ge.out;
    out * (out - 1) === 0;
}
