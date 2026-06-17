pragma circom 2.0.0;

include "poseidon.circom";

template Hash2RuntimeCall() {
    signal input a;
    signal input b;
    signal output out;

    out <== Poseidon(a, b);
}

component main {public [a]} = Hash2RuntimeCall();
