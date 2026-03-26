pragma circom 2.0.0;

// Vitalik's QAP example: u^3 + u + k = 35
// u is private, k is deferred (symbolic/public)
template VitalikCubic() {
    signal input u;
    signal input k;
    signal output out;

    signal sym_1;
    signal y;
    signal sym_2;

    sym_1 <== u * u;
    y <== sym_1 * u;
    sym_2 <== y + u;
    out <== sym_2 + k;
}

component main {public [k]} = VitalikCubic();
