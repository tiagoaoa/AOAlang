pragma circom 2.0.0;

template Multiplier() {
    signal input a;
    signal input b;
    signal output c;

    c <== a * b;
}

template Main() {
    signal input x;
    signal input y;
    signal input z;
    signal output out;

    component m1 = Multiplier();
    m1.a <== x;
    m1.b <== y;

    component m2 = Multiplier();
    m2.a <== m1.c;
    m2.b <== z;

    out <== m2.c;
}

component main {public [x]} = Main();
