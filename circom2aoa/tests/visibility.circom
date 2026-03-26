pragma circom 2.0.0;

// Test explicit signal visibility: private and public keywords
template CheckGE() {
    signal public input a;
    signal private input b;
    signal output result;

    signal diff;
    diff <== a - b;

    // Simple non-negativity check: diff * diff (placeholder)
    result <== diff * diff;
}

component main = CheckGE();
