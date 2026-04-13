template Num2Bits(n) {
    signal input in;
    signal output out[n];

    var lc = 0;
    var e2 = 1;

    for (var i = 0; i < n; i++) {
        out[i] <-- (in >> i) & 1;
        out[i] * (out[i] - 1) === 0;
        lc += out[i] * e2;
        e2 = e2 * 2;
    }

    lc === in;
}

template GreaterEqThan(nbits) {
    signal input a;
    signal input b;
    signal output out;

    component n2b = Num2Bits(nbits + 1);
    n2b.in <== a - b + (1 << nbits);

    out <== n2b.out[nbits];
    n2b.out[nbits] === 1;
}

template GreaterEqThan64() {
    signal input a;
    signal input b;
    signal output out;

    signal a_lo;
    signal a_hi;
    signal b_lo;
    signal b_hi;

    signal a_reconstructed;
    signal b_reconstructed;

    a_lo <-- a & 4294967295;
    a_hi <-- a >> 32;
    b_lo <-- b & 4294967295;
    b_hi <-- b >> 32;

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

    a_reconstructed === a;
    b_reconstructed === b;

    component hi_ge_ab = GreaterEqThan(32);
    hi_ge_ab.a <== a_hi;
    hi_ge_ab.b <== b_hi;

    component hi_ge_ba = GreaterEqThan(32);
    hi_ge_ba.a <== b_hi;
    hi_ge_ba.b <== a_hi;

    component lo_ge = GreaterEqThan(32);
    lo_ge.a <== a_lo;
    lo_ge.b <== b_lo;

    signal hi_eq;
    signal hi_gt;

    hi_eq <== hi_ge_ab.out * hi_ge_ba.out;
    hi_gt <== hi_ge_ab.out * (1 - hi_ge_ba.out);

    out <== hi_gt + hi_eq * lo_ge.out;
    out * (out - 1) === 0;
}
