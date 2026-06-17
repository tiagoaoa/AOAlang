pragma circom 2.0.0;

include "poseidon.circom";

template Hash1RuntimeCall() {
    signal input a;
    signal output out;

    out <== Poseidon(a);
}

component main {public [a]} = Hash1RuntimeCall();
