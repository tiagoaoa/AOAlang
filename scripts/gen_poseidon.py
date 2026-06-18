#!/usr/bin/env python3
"""
Generate the BN254 Poseidon Circom library used by circom2aoa.

The generated library exposes:
- PoseidonEx(n, 1) for any compile-time n > 0
- Poseidon(n) as a zero-initial-state wrapper

Backends use alpha=5 with standard Grain-LFSR round constants and a Cauchy
MDS matrix. The supported parameter sets are:
- t=2 (1 input): Rf=8, Rp=56
- t=3 (2 inputs): Rf=8, Rp=57
"""

import sys

# BN254 scalar field prime
P = 21888242871839275222246405745257275088548364400416034343698204186575808495617


def modinv(a, p):
    """Modular inverse via extended Euclidean algorithm."""
    if a == 0:
        raise ValueError("no inverse for 0")
    g, x, _ = extended_gcd(a % p, p)
    if g != 1:
        raise ValueError(f"no inverse: gcd={g}")
    return x % p


def extended_gcd(a, b):
    if a == 0:
        return b, 0, 1
    g, x, y = extended_gcd(b % a, a)
    return g, y - (b // a) * x, x


def grain_lfsr_init(field_size_bits, t, rf, rp):
    """Initialize 80-bit Grain LFSR state for Poseidon constant generation."""
    state = []

    # 2 bits: field type (1 = prime field)
    state += [1, 0]

    # 4 bits: alpha=5 = 0b0101 -> [1, 0, 1, 0] (LSB first)
    state += [1, 0, 1, 0]

    # 12 bits: field size
    for i in range(12):
        state.append((field_size_bits >> i) & 1)

    # 12 bits: t
    for i in range(12):
        state.append((t >> i) & 1)

    # 10 bits: Rf
    for i in range(10):
        state.append((rf >> i) & 1)

    # 10 bits: Rp
    for i in range(10):
        state.append((rp >> i) & 1)

    # Remaining bits set to 1
    state += [1] * 30

    assert len(state) == 80
    return state


def grain_get_bit(state):
    """Clock LFSR and return one output bit."""
    new_bit = state[0] ^ state[13] ^ state[23] ^ state[38] ^ state[51] ^ state[62]
    state.pop(0)
    state.append(new_bit)
    return new_bit


def grain_get_field_element(state, n_bits, p):
    """Generate one field element from the Grain LFSR."""
    while True:
        bits = []
        for _ in range(n_bits):
            while grain_get_bit(state) == 0:
                grain_get_bit(state)
            bits.append(grain_get_bit(state))

        value = sum(bit * (2 ** i) for i, bit in enumerate(bits))
        if value < p:
            return value


def generate_round_constants(t, rf, rp):
    """Generate all Poseidon round constants for BN254."""
    n_bits = 254
    state = grain_lfsr_init(n_bits, t, rf, rp)

    for _ in range(160):
        grain_get_bit(state)

    constants = []
    for _ in range((rf + rp) * t):
        constants.append(grain_get_field_element(state, n_bits, P))
    return constants


def generate_mds_matrix(t):
    """Generate a t x t Cauchy MDS matrix over BN254."""
    xs = list(range(t))
    ys = list(range(t, 2 * t))

    matrix = []
    for i in range(t):
        row = []
        for j in range(t):
            row.append(modinv((xs[i] + ys[j]) % P, P))
        matrix.append(row)
    return matrix


def emit_sigma(w):
    w("// S-box: x^5")
    w("template Sigma() {")
    w("    signal input in;")
    w("    signal output out;")
    w("    signal in2;")
    w("    signal in4;")
    w("    in2 <== in * in;")
    w("    in4 <== in2 * in2;")
    w("    out <== in4 * in;")
    w("}")
    w("")


def emit_poseidon_backend(w, template_name, n_inputs, rf, rp):
    """Emit one fixed backend template with explicit signals only."""
    t = n_inputs + 1
    n_rounds = rf + rp
    half_rf = rf // 2
    constants = generate_round_constants(t, rf, rp)
    matrix = generate_mds_matrix(t)

    w(f"template {template_name}() {{")
    w(f"    signal input inputs[{n_inputs}];")
    w("    signal input initialState;")
    w("    signal output out[1];")
    w("")
    w(f"    var t = {t};")
    w(f"    var nRoundsF = {rf};")
    w(f"    var nRoundsP = {rp};")
    w(f"    var nRounds = {n_rounds};")
    w("")

    w(f"    // MDS matrix constants ({t}x{t} = {t * t} signals)")
    for i in range(t):
        for j in range(t):
            w(f"    signal mds_{i}_{j};")
    for i in range(t):
        for j in range(t):
            w(f"    mds_{i}_{j} <== {matrix[i][j]};")
    w("")

    w("    // Initial state")
    w("    signal state_0_0;")
    w("    state_0_0 <== initialState;")
    for i in range(n_inputs):
        w(f"    signal state_0_{i + 1};")
        w(f"    state_0_{i + 1} <== inputs[{i}];")
    w("")

    for r in range(n_rounds):
        is_full_round = r < half_rf or r >= half_rf + rp
        round_kind = "full" if is_full_round else "partial"
        w(f"    // === Round {r} ({round_kind}) ===")

        for i in range(t):
            const_idx = r * t + i
            w(f"    signal rc_{r}_{i};")
            w(f"    rc_{r}_{i} <== {constants[const_idx]};")
        w("")

        for i in range(t):
            w(f"    signal ark_{r}_{i};")
            w(f"    ark_{r}_{i} <== state_{r}_{i} + rc_{r}_{i};")
        w("")

        if is_full_round:
            for i in range(t):
                w(f"    component sbox_{r}_{i} = Sigma();")
                w(f"    sbox_{r}_{i}.in <== ark_{r}_{i};")
            w("")
            sbox_out = lambda i, r=r: f"sbox_{r}_{i}.out"
        else:
            w(f"    component sbox_{r}_0 = Sigma();")
            w(f"    sbox_{r}_0.in <== ark_{r}_0;")
            w("")
            sbox_out = lambda i, r=r: f"sbox_{r}_0.out" if i == 0 else f"ark_{r}_{i}"

        for i in range(t):
            for j in range(t):
                w(f"    signal mix_{r}_{i}_{j};")
                w(f"    mix_{r}_{i}_{j} <== mds_{i}_{j} * {sbox_out(j)};")
        w("")

        next_round = r + 1
        for i in range(t):
            acc = f"mix_{r}_{i}_0"
            for j in range(1, t):
                sum_name = f"mixsum_{r}_{i}_{j}"
                w(f"    signal {sum_name};")
                w(f"    {sum_name} <== {acc} + mix_{r}_{i}_{j};")
                acc = sum_name
            w(f"    signal state_{next_round}_{i};")
            w(f"    state_{next_round}_{i} <== {acc};")
        w("")

    w(f"    out[0] <== state_{n_rounds}_1;")
    w("}")
    w("")


def generate_library(output_file=None):
    """Generate the combined Poseidon library used by circom2aoa."""
    print("Generating Poseidon library for BN254")
    print("  Backends:")
    print("    - t=2, Rf=8, Rp=56 (1 input)")
    print("    - t=3, Rf=8, Rp=57 (2 inputs)")
    print("  Recursive wrappers:")
    print("    - PoseidonEx(n, 1) for arbitrary compile-time n > 0")
    print("    - Poseidon(n) as PoseidonEx(n, 1) with initialState = 0")

    lines = []
    w = lines.append

    w("pragma circom 2.0.0;")
    w("")
    w("// =============================================================")
    w("// Poseidon Hash for BN254 (alpha=5)")
    w("// Generated by gen_poseidon.py")
    w("// Fixed backends: PoseidonEx(1, 1), PoseidonEx(2, 1)")
    w("// Recursive wrapper: PoseidonEx(n, 1) for any compile-time n > 0")
    w("// Convenience wrapper: Poseidon(n) with initialState = 0")
    w("// =============================================================")
    w("")

    emit_sigma(w)
    emit_poseidon_backend(w, "PoseidonEx1_1", 1, 8, 56)
    emit_poseidon_backend(w, "PoseidonEx2_1", 2, 8, 57)

    w("template PoseidonEx(nInputs, nOuts) {")
    w("    signal input inputs[nInputs];")
    w("    signal input initialState;")
    w("    signal output out[nOuts];")
    w("")
    w("    assert(nOuts == 1);")
    w("    assert(nInputs > 0);")
    w("")
    w("    if (nInputs == 1) {")
    w("        component p1 = PoseidonEx1_1();")
    w("        p1.initialState <== initialState;")
    w("        p1.inputs[0] <== inputs[0];")
    w("        out[0] <== p1.out[0];")
    w("    } else if (nInputs == 2) {")
    w("        component p2 = PoseidonEx2_1();")
    w("        p2.initialState <== initialState;")
    w("        p2.inputs[0] <== inputs[0];")
    w("        p2.inputs[1] <== inputs[1];")
    w("        out[0] <== p2.out[0];")
    w("    } else {")
    w("        component prefix = PoseidonEx(nInputs - 1, 1);")
    w("        prefix.initialState <== initialState;")
    w("        for (var i = 0; i < nInputs - 1; i++) {")
    w("            prefix.inputs[i] <== inputs[i];")
    w("        }")
    w("")
    w("        component tail = PoseidonEx(1, 1);")
    w("        tail.initialState <== prefix.out[0];")
    w("        tail.inputs[0] <== inputs[nInputs - 1];")
    w("        out[0] <== tail.out[0];")
    w("    }")
    w("}")
    w("")

    w("template Poseidon(nInputs) {")
    w("    signal input inputs[nInputs];")
    w("    signal output out;")
    w("")
    w("    component pEx = PoseidonEx(nInputs, 1);")
    w("    pEx.initialState <== 0;")
    w("    for (var i = 0; i < nInputs; i++) {")
    w("        pEx.inputs[i] <== inputs[i];")
    w("    }")
    w("    out <== pEx.out[0];")
    w("}")
    w("")

    content = "\n".join(lines)

    if output_file:
        with open(output_file, "w", encoding="ascii") as handle:
            handle.write(content)
        print(f"  Written to {output_file}")
    else:
        print(content)

    return content


if __name__ == "__main__":
    outfile = sys.argv[1] if len(sys.argv) > 1 else None
    generate_library(outfile)
