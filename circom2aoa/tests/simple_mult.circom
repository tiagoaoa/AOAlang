pragma circom 2.0.0;

template SimpleMult() {
    signal input a;
    signal input b;
    signal output c;

    c <== a * b;
}

component main {public [a, b]} = SimpleMult();
