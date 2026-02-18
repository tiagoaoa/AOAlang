pragma circom 2.0.0;

// Num2Bits-style: decompose a number into n bits
// Proves that in = sum(out[i] * 2^i)
template Num2Bits(n) {
    signal input in;
    signal output out[n];

    var lc = 0;
    var e2 = 1;

    for (var i = 0; i < n; i++) {
        out[i] * (out[i] - 1) === 0;  // boolean constraint
        lc += out[i] * e2;
        e2 = e2 * 2;
    }

    lc === in;
}

component main = Num2Bits(4);
