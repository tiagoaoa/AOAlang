pragma circom 2.0.0;

// Proves: x^2 + a*x + b == 0
// x is private, a and b are public (deferred)
template Quadratic() {
    signal input x;
    signal input a;
    signal input b;
    signal output result;

    signal x_squared;
    signal ax;
    signal sum1;

    x_squared <== x * x;
    ax <== a * x;
    sum1 <== x_squared + ax;
    result <== sum1 + b;
}

component main {public [a, b]} = Quadratic();
