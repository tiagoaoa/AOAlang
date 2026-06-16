pragma circom 2.0.0;

include "poseidon.circom";

template Hash2() {
    signal input a;
    signal input b;
    signal output out;

    component h = Poseidon(2);
    h.inputs[0] <== a;
    h.inputs[1] <== b;
    out <== h.out;
}

component main {public [a]} = Hash2();
