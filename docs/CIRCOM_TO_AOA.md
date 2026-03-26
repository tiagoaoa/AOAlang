# circom2aoa - Circom to AOAlang Transpiler

A self-contained C transpiler that compiles Circom circuits into flat AOAlang (`.aoa`) constraint files.

For the Circom language reference (grammar, signals, constraints, operators, and AOAlang extensions), see [CIRCOM_REFERENCE.md](CIRCOM_REFERENCE.md).

## Overview

circom2aoa implements a **superset of Circom 2.0** with AOAlang-specific extensions (notably `signal public/private input`). It takes high-level circuits with templates, components, loops, and signal arrays, and produces flat AOAlang output where every operation is a single R1CS-compatible assignment.

**Pipeline:**

```
.circom → lexer → parser → flattener → emitter → .aoa
```

| Stage     | Description |
|-----------|-------------|
| Lexer     | Hand-written tokenizer for Circom syntax (`lexer.c`) |
| Parser    | Recursive descent parser producing an AST (`parser.c`, `ast.c`) |
| Flattener | Template inlining, loop unrolling, compile-time evaluation (`flattener.c`) |
| Emitter   | AOA output with signal declarations and flat constraints (`emitter.c`) |

## Build

```bash
cd circom2aoa
make
```

Requires only `gcc` (no Flex/Bison). Binary is output to `bin/circom2aoa`.

## Usage

```bash
# Transpile a Circom file (output: FILE.aoa)
bin/circom2aoa circuit.circom

# Specify output file
bin/circom2aoa -o output.aoa circuit.circom

# Verbose mode (show pipeline stages)
bin/circom2aoa -v circuit.circom

# Transpile and validate with aoac
bin/circom2aoa --validate circuit.circom
```

### Options

| Option | Description |
|--------|-------------|
| `-o FILE` | Output file (default: input with `.aoa` extension) |
| `-v` | Verbose output showing lexer/parser/flattener stages |
| `--validate` | Run `aoac` on the output to verify valid AOA |
| `-h`, `--help` | Show help |

## Supported Features

### Standard Circom 2.0

| Feature | Status | Notes |
|---------|--------|-------|
| `pragma circom 2.0.0` | Supported | Parsed and stored |
| `template` with parameters | Supported | Inlined at instantiation |
| `signal input/output` | Supported | Mapped to AOA `decl private`/`decl deferred` |
| `component main {public [...]}` | Supported | Controls deferred vs private when no explicit visibility |
| `component` instantiation | Supported | Deferred (lazy) body flattening |
| `<==` (constrain + assign) | Supported | Produces AOA assignment or equality check |
| `===` (constrain only) | Supported | Produces AOA equality check |
| `<--` (witness hint) | Supported | Sub-component signals declared as `decl private` |
| `var` declarations | Supported | Compile-time with linear combination tracking |
| `for` loops | Supported | Unrolled at compile time |
| `if`/`else` (compile-time) | Supported | Evaluated at flattening time |
| `+=`, `*=`, `++`, `--` | Supported | Expanded during flattening |
| Array signals (`signal output out[n]`) | Supported | Flattened to `name_0`, `name_1`, ... |
| Nested component access (`m1.c`) | Supported | Flattened to `m1_c` |
| `comp.field[idx]` access | Supported | Flattened to `comp_field_idx` |
| `include` | Parsed, not resolved | Files must be self-contained |
| Runtime `if`/`else` on signals | Not supported | Requires signal-dependent branching |
| Parallel component `parallel` | Not supported | |

### AOAlang Extensions (not in standard Circom)

| Feature | AOA Mapping | Notes |
|---------|-------------|-------|
| `signal private input x` | `decl private x` | Explicit private visibility on the signal |
| `signal public input x` | `decl deferred x` | Explicit public visibility on the signal |

These extensions allow signal visibility to be declared where the signal is defined, rather than requiring all public inputs to be listed in the `component main {public [...]}` declaration. When both mechanisms are present, explicit visibility on the signal takes precedence.

## Signal Mapping

Signal visibility in AOA is resolved with the following precedence:

1. **Explicit visibility on the signal** (AOAlang extension) -- `signal public input` or `signal private input`
2. **`component main {public [...]}`** (standard Circom 2.0) -- names listed are public
3. **Default** -- unlisted inputs are private, outputs are always public

| Circom | AOA | Rule |
|--------|-----|------|
| `signal public input x` | `decl deferred x` | Explicit (extension) |
| `signal private input x` | `decl private x` | Explicit (extension) |
| `signal input x` + listed in `{public [x]}` | `decl deferred x` | Implicit (standard) |
| `signal input x` + not listed | `decl private x` | Implicit (standard) |
| `signal output x` | `decl deferred x` | Always public |
| Sub-component witness (`<--`) | `decl private` | Prover provides, `===` verifies |

## Examples

### Simple Multiplication

**Circom:**
```circom
pragma circom 2.0.0;

template SimpleMult() {
    signal input a;
    signal input b;
    signal output c;
    c <== a * b;
}

component main {public [a, b]} = SimpleMult();
```

**AOA output:**
```aoa
decl deferred a, b, c

c = a * b
```

### Explicit Signal Visibility (AOAlang Extension)

This example uses the `signal public input` / `signal private input` syntax, which is an AOAlang extension not present in standard Circom 2.0. Visibility is declared directly on the signal -- no `component main {public [...]}` needed.

**Circom (with AOAlang extensions):**
```circom
pragma circom 2.0.0;

template CheckGE() {
    signal public input a;    // AOAlang extension
    signal private input b;   // AOAlang extension
    signal output result;

    signal diff;
    diff <== a - b;
    result <== diff * diff;
}

component main = CheckGE();
```

**AOA output:**
```aoa
decl private b
decl deferred a, result

diff = a - b
computedresult = diff * diff
outcheckresult = computedresult == result
```

`signal public input a` maps directly to `decl deferred`, `signal private input b` to `decl private`.

### Components (Chained Multipliers)

**Circom:**
```circom
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
```

**AOA output:**
```aoa
decl private y, z
decl deferred x, out

m1_a = x
m1_b = y
m1_c = m1_a * m1_b
m2_a = m1_c
m2_b = z
m2_c = m2_a * m2_b
computedout = m2_c
outcheckout = computedout == out
```

Components are inlined with prefixed signal names (`m1_a`, `m1_b`, `m1_c`). Component body flattening is deferred until an output signal is accessed.

### Loop Unrolling (Num2Bits)

**Circom:**
```circom
pragma circom 2.0.0;

template Num2Bits(n) {
    signal input in;
    signal output out[n];

    var lc = 0;
    var e2 = 1;

    for (var i = 0; i < n; i++) {
        out[i] * (out[i] - 1) === 0;
        lc += out[i] * e2;
        e2 = e2 * 2;
    }

    lc === in;
}

component main = Num2Bits(4);
```

**AOA output:**
```aoa
decl private in
decl deferred out[4]

t0 = out[0] - 1
t1 = out[0] * t0
chk0 = t1 == 0
t2 = out[0] * 1
t4 = out[1] - 1
t5 = out[1] * t4
chk1 = t5 == 0
t6 = out[1] * 2
t7 = t2 + t6
t8 = out[2] - 1
t9 = out[2] * t8
chk2 = t9 == 0
t10 = out[2] * 4
t11 = t7 + t10
t12 = out[3] - 1
t13 = out[3] * t12
chk3 = t13 == 0
t14 = out[3] * 8
t15 = t11 + t14
chk4 = t15 == in
```

The `for` loop is unrolled at compile time. `var` variables (`lc`, `e2`, `i`) are evaluated during flattening and substituted as constants. The boolean constraint `out[i] * (out[i] - 1) === 0` ensures each bit is 0 or 1. The reconstruction `lc === in` verifies the bit decomposition.

### GreaterEqThan (circomlib pattern)

**Circom:**
```circom
pragma circom 2.0.0;

template Num2Bits(n) {
    signal input in;
    signal output out[n];

    var lc = 0;
    var e2 = 1;

    for (var i = 0; i < n; i++) {
        out[i] <-- (in >> i) & 1;
        out[i] * (out[i] - 1) === 0;
        lc += out[i] * e2;
        e2 = e2 * 2;
    }

    lc === in;
}

template GreaterEqThan(nbits) {
    signal input a;
    signal input b;

    component n2b = Num2Bits(nbits + 1);
    n2b.in <== a - b + (1 << nbits);

    n2b.out[nbits] === 1;
}

component main {public [b]} = GreaterEqThan(4);
```

**AOA output:**
```aoa
decl private a, n2b_out_0, n2b_out_1, n2b_out_2, n2b_out_3, n2b_out_4
decl deferred b

t0 = a - b
n2b_in = t0 + 16
t1 = n2b_out_0 - 1
t2 = n2b_out_0 * t1
chk0 = t2 == 0
t3 = n2b_out_0 * 1
t5 = n2b_out_1 - 1
t6 = n2b_out_1 * t5
chk1 = t6 == 0
t7 = n2b_out_1 * 2
t8 = t3 + t7
...
chk5 = t20 == n2b_in
chk6 = n2b_out_4 == 1
```

The `<--` witness hints are non-arithmetic (bitshift, AND) and cannot be computed in R1CS. The transpiler declares sub-component witness signals as `decl private` -- the prover provides them, and the `===` constraints verify correctness.

The pattern: compute `a - b + 2^n`, decompose into `n+1` bits. If `a >= b`, the MSB is 1; if `a < b`, the MSB is 0.

## Architecture Details

### Compile-Time Variables

Circom `var` can hold linear combinations of signals, not just constants. The flattener tracks `var` values as compile-time state and substitutes them when they appear in constraints. For example:

```circom
var lc = 0;
lc += out[0] * 1;   // lc is now a linear combination
lc += out[1] * 2;   // lc accumulates terms
lc === in;           // emitted as: accumulated_sum == in
```

### Deferred Component Flattening

Component bodies are not flattened when declared. Instead:

1. `component m = Template()` -- registers the component
2. `m.input <== expr` -- wires input signals
3. `m.output` is accessed -- triggers body flattening

This handles forward references where inputs must be wired before the body can be evaluated.

### Flattener Memory

The `flattener_t` struct contains large static arrays (~20MB total) and must be heap-allocated with `calloc`. Stack allocation will cause segfaults.

## Tests

```bash
cd circom2aoa
make test
```

Each test transpiles a `.circom` file, diffs against `tests/expected/*.aoa`, and validates the output with `aoac`.

| Test | Description |
|------|-------------|
| `simple_mult` | Basic signal multiplication |
| `multiplier2` | Two-input multiplier |
| `quadratic` | Quadratic constraint with intermediate signals |
| `vitalik_cubic` | Vitalik's QAP example (u^3 + u + k = 35) |
| `with_loop` | Num2Bits with for-loop unrolling |
| `with_component` | Chained component instantiation |
| `bitwise_ge` | GreaterEqThan using Num2Bits sub-component |
| `visibility` | Explicit `signal private input` / `signal public input` |

## Known Limitations

- **`include` not resolved**: Parsed but ignored. All templates must be in the same file.
- **Template parameter scoping**: Nested templates with same-named parameters may conflict. Use distinct parameter names (e.g., `nbits` instead of `n`) when nesting.
- **Runtime branching**: `if`/`else` on signal values is not supported (only compile-time `var` conditions).
- **No standard library**: circomlib templates must be inlined manually into source files.
