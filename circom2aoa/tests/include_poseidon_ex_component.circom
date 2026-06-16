pragma circom 2.0.0;

include "poseidon.circom";

template HashEx() {
    signal input a;
    signal input b;
    signal input initialState;
    signal output out;

    component h = PoseidonEx(2, 1);
    h.initialState <== initialState;
    h.inputs[0] <== a;
    h.inputs[1] <== b;
    out <== h.out[0];
}

component main {public [a, initialState]} = HashEx();
