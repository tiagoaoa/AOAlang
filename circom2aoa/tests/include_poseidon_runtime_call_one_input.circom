pragma circom 2.0.0;

include "poseidon.circom";

template Hash1RuntimeCall() {
    signal input a;
    signal output out;

    component h = Poseidon(1);
    h.inputs[0] <== a;
    out <== h.out;
}

component main {public [a]} = Hash1RuntimeCall();
