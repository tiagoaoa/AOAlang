pragma circom 2.0.0;

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

    component n2b = Num2Bits(nbits + 1);
    n2b.in <== a - b + (1 << nbits);

    n2b.out[nbits] === 1;
}

component main {public [b]} = GreaterEqThan(4);
