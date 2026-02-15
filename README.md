# AOAlang - Parser and Validator for Arithmetic Optimization Algebra

A C-based parser and validator for the AOA (Arithmetic Optimization Algebra) language, used in zero-knowledge proof systems like Zyga.

## Overview

AOAlang provides a robust parser for `.aoa` files that validates both syntax and semantics:
- **Syntax validation**: Ensures proper grammar conformance
- **Semantic validation**: Checks variable declarations, type consistency, array indexing
- **Comprehensive error reporting**: Clear, actionable error messages with line numbers

## Features

- Full AOA grammar support (comma-separated declarations, array identification by `[size]`)
- Semantic analysis (variable declarations, scope checking)
- Type checking (scalar vs array variables)
- Index validation (arrays must be indexed, scalars cannot be indexed)
- Multiple output formats:
  - R1CS JSON with symbolic witness support (`-g`)
  - Dense R1CS matrices (`-d`)
  - QAP polynomials (`-q`)
  - C sanity checker code (`-c`)
- Built with Lex & Yacc for robust parsing
- Standard POSIX-compatible build system

## Quick Start

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install build-essential flex bison

# macOS
brew install flex bison

# Fedora/RHEL
sudo dnf install gcc flex bison
```

### Build

```bash
./configure
make
```

The compiled binary will be in `bin/aoac`.

### Usage

```bash
# Validate an AOA file
./bin/aoac examples/simple_quad.aoa

# Check multiple files
./bin/aoac examples/*.aoa

# Get verbose output
./bin/aoac -v examples/quadratic.aoa

# Generate R1CS JSON output
./bin/aoac -g examples/simple_quad.aoa

# Generate dense R1CS matrix output (.r1cs)
./bin/aoac -d examples/vitalik_deferred.aoa

# Generate QAP polynomial output (.qap)
./bin/aoac -q examples/vitalik_deferred.aoa

# Generate C sanity checker (.c)
./bin/aoac -c examples/vitalik_deferred.aoa
```

### Command-Line Options

| Option | Long Form | Description |
|--------|-----------|-------------|
| `-v` | | Verbose output (show symbol table) |
| `-g` | `--generate` | Generate R1CS JSON output (.r1cs.json) |
| `-d` | `--dense` | Generate dense R1CS matrix output (.r1cs) |
| `-q` | `--qap` | Generate QAP polynomial output (.qap) |
| `-c` | `--checker` | Generate C sanity checker code (.c) |
| `-o FILE` | | Specify output file (default: `<input>.<ext>`) |
| `-h` | `--help` | Show help message |

## Example AOA Code

### Simple Quadratic Circuit

```aoa
# Prove x^2 + a*x + b = 0
decl private x
decl deferred a, b

# Compute x^2
x_squared = x * x

# Compute a*x
ax = a * x

# Compute x^2 + a*x + b
sum1 = x_squared + ax
poly_result = sum1 + b

# Enforce poly_result == 0
zero_const = 0
check = poly_result == zero_const
```

### Multiple Variables on One Line

```aoa
# Declare multiple scalars
decl private x, y, z

# Mixed declarations
decl private a, b[4], c, d[8]

# Constraints
x_squared = x * x
sum = x_squared + y
check = sum == z
```

## Error Detection

### Syntax Errors

```aoa
decl private x, y
result = x + y +  # Missing operand
```
**Output**: `Error (line 2): Syntax error - incomplete expression`

### Semantic Errors

```aoa
decl private x
result = x + y  # y not declared
```
**Output**: `Error (line 2): Variable 'y' used before declaration`

### Type Errors

```aoa
decl private x
decl private a[4]

value1 = x[0]   # Error: x is scalar, cannot index
value2 = a      # Error: a is array, must be indexed
```

## Documentation

- [**AOA Grammar Specification**](docs/AOA_GRAMMAR.md) - Complete formal grammar
- [**AOA Quick Reference**](docs/AOA_QUICK_REFERENCE.md) - Syntax cheat sheet

## Integration with AI/LLM Workflows

To ensure AI assistants validate generated `.aoa` files, add these instructions to your project's AI configuration files:

### For `CLAUDE.md` or `.claude/CLAUDE.md`

```markdown
## AOA Language Validation

When generating or modifying `.aoa` files:

1. **Always validate** with the AOAlang parser before committing:
   ```bash
   /path/to/AOAlang/bin/aoac your_file.aoa
   ```

2. **Required checks**:
   - All variables declared before use
   - Arrays used with `[index]`, scalars used without
   - One operation per line (binary ops only)
   - Declarations before constraints

3. **On validation errors**: Fix the issues and re-validate until the parser succeeds.

4. **Example workflow**:
   ```bash
   # After generating/editing .aoa file
   aoac circuit.aoa && echo "✓ Valid AOA syntax" || echo "✗ Fix errors above"
   ```
```

### For `AGENTS.md`

```markdown
## AOA File Generation Guidelines

When working with `.aoa` constraint files:

### Validation Requirement
- **MANDATORY**: Run `aoac <file.aoa>` after any `.aoa` file changes
- **Never commit** `.aoa` files that fail validation
- Parser location: `AOAlang/bin/aoac` or system-installed `aoac`

### Common Pitfalls
1. Using variables before declaration
2. Indexing scalar variables: `x[0]` where `x` is `decl private x`
3. Using arrays without index: `result = a` where `a` is `decl private a[4]`
4. Multiple operations per line: `result = a + b + c` (split into multiple lines)

### Validation in CI/CD
```bash
# Add to your CI pipeline
find . -name "*.aoa" -exec aoac {} \;
```
```

### For IDE Integration (VS Code `settings.json`)

```json
{
  "emeraldwalk.runonsave": {
    "commands": [
      {
        "match": "\\.aoa$",
        "cmd": "aoac ${file} && echo 'AOA validation passed' || echo 'AOA validation FAILED'"
      }
    ]
  }
}
```

## Project Structure

```
AOAlang/
├── README.md           # This file
├── configure           # Build configuration script
├── Makefile           # Build system
├── src/               # Source code
│   ├── aoa.l          # Lex lexer specification
│   ├── aoa.y          # Yacc parser specification
│   ├── main.c         # Main program entry
│   ├── symbol_table.c # Symbol table for semantic analysis
│   ├── symbol_table.h
│   ├── r1cs.c         # R1CS constraint generation
│   ├── r1cs.h
│   ├── error.c        # Error reporting
│   └── error.h
├── bin/               # Compiled binaries (after make)
├── examples/          # Example .aoa files
│   ├── simple_quad.aoa
│   ├── quadratic.aoa
│   ├── uint4gt.aoa
│   └── ...
└── docs/              # Documentation
    ├── AOA_GRAMMAR.md
    └── AOA_QUICK_REFERENCE.md
```

## Development

### Building from Source

```bash
# Configure (checks for dependencies)
./configure

# Build
make

# Install (optional, requires sudo)
sudo make install

# Clean build artifacts
make clean
```

### Running Tests

```bash
# Validate all example files
make test

# Or manually
for file in examples/*.aoa; do
    echo "Testing $file..."
    ./bin/aoac "$file" || exit 1
done
```

## Code Generation

AOAlang can generate multiple output formats for use with zero-knowledge proof systems.

### Output Formats

| Flag | Extension | Description |
|------|-----------|-------------|
| `-g` | `.r1cs.json` | R1CS JSON with sparse matrices and symbolic tracking |
| `-d` | `.r1cs` | Dense R1CS matrices (human-readable text format) |
| `-q` | `.qap` | QAP polynomials: w·A(x), w·B(x), w·C(x), P(x), H(x), Z(x) |
| `-c` | `.c` | C sanity checker code to verify witness satisfies constraints |

### Usage

```bash
# Generate R1CS JSON (output: <input>.r1cs.json)
./bin/aoac -g examples/simple_quad.aoa

# Generate dense R1CS (output: <input>.r1cs)
./bin/aoac -d examples/vitalik_deferred.aoa

# Generate QAP polynomials (output: <input>.qap)
./bin/aoac -q examples/vitalik_deferred.aoa

# Generate C sanity checker (output: <input>.c)
./bin/aoac -c examples/vitalik_deferred.aoa

# Specify custom output file
./bin/aoac -g -o circuit.json examples/simple_quad.aoa

# Combine with verbose mode
./bin/aoac -v -g examples/quadratic.aoa
```

### Generated JSON Structure

The R1CS JSON output contains:

| Section | Description |
|---------|-------------|
| `circuit` | Circuit name (derived from input filename) |
| `field` | Target field (bn254) |
| `witness` | Witness vector with partitions and symbolic expressions |
| `r1cs` | Sparse A, B, C matrices with row comments |
| `public_inputs` | List of public/deferred inputs |
| `symbolic_propagation` | Gate expressions for symbolic analysis |

### Witness Partitioning

Witnesses are partitioned into four categories:

- **constant**: The constant `1` at index 0
- **private**: Secret witness values (declared with `decl private`)
- **deferred**: Symbolic public inputs (declared with `decl deferred`)
- **gates**: Intermediate values computed by constraints

### Example: Simple Quadratic Circuit

For the circuit that proves `x² + a·x + b = 0`:

**Input (`simple_quad.aoa`):**
```aoa
decl private x
decl deferred a, b

x_squared = x * x
ax = a * x
sum1 = x_squared + ax
poly_result = sum1 + b
zero_const = 0
check = poly_result == zero_const
```

**Generated R1CS JSON:**
```json
{
  "circuit": "simple_quad",
  "version": "1.0",
  "field": "bn254",

  "witness": {
    "total": 10,
    "partition": {
      "constant": {"indices": [0], "names": ["1"]},
      "private": {"indices": [1], "names": ["x"]},
      "deferred": {"indices": [2, 3], "names": ["a", "b"]},
      "gates": {"indices": [4, 5, 6, 7, 8, 9],
               "names": ["x_squared", "ax", "sum1", "poly_result", "zero_const", "check"]}
    },
    "entries": [
      {"index": 0, "name": "1", "visibility": "public", "origin": "declared", "symbolic": "1"},
      {"index": 1, "name": "x", "visibility": "private", "origin": "declared", "symbolic": "x"},
      {"index": 2, "name": "a", "visibility": "deferred", "origin": "declared", "symbolic": "a"},
      {"index": 3, "name": "b", "visibility": "deferred", "origin": "declared", "symbolic": "b"},
      {"index": 4, "name": "x_squared", "visibility": "private", "origin": "gate", "symbolic": "x * x"},
      {"index": 5, "name": "ax", "visibility": "private", "origin": "gate", "symbolic": "a * x"},
      {"index": 6, "name": "sum1", "visibility": "private", "origin": "gate", "symbolic": "x_squared + ax"},
      {"index": 7, "name": "poly_result", "visibility": "private", "origin": "gate", "symbolic": "sum1 + b"},
      {"index": 8, "name": "zero_const", "visibility": "private", "origin": "gate", "symbolic": "0"},
      {"index": 9, "name": "check", "visibility": "private", "origin": "gate", "symbolic": "0"}
    ]
  },

  "r1cs": {
    "n_constraints": 6,
    "n_variables": 10,

    "A": [
      {"row": 0, "entries": [{"col": 1, "val": 1}], "comment": "x_squared = x * x"},
      {"row": 1, "entries": [{"col": 2, "val": 1}], "comment": "ax = a * x"},
      {"row": 2, "entries": [{"col": 4, "val": 1}, {"col": 5, "val": 1}], "comment": "sum1 = x_squared + ax"},
      {"row": 3, "entries": [{"col": 6, "val": 1}, {"col": 3, "val": 1}], "comment": "poly_result = sum1 + b"},
      {"row": 4, "entries": [{"col": 8, "val": 1}, {"col": 0, "val": 0}], "comment": "zero_const = 0"},
      {"row": 5, "entries": [{"col": 7, "val": 1}, {"col": 8, "val": -1}], "comment": "check: poly_result == zero_const"}
    ],

    "B": [
      {"row": 0, "entries": [{"col": 1, "val": 1}]},
      {"row": 1, "entries": [{"col": 1, "val": 1}]},
      {"row": 2, "entries": [{"col": 0, "val": 1}]},
      {"row": 3, "entries": [{"col": 0, "val": 1}]},
      {"row": 4, "entries": [{"col": 0, "val": 1}]},
      {"row": 5, "entries": [{"col": 0, "val": 1}]}
    ],

    "C": [
      {"row": 0, "entries": [{"col": 4, "val": 1}]},
      {"row": 1, "entries": [{"col": 5, "val": 1}]},
      {"row": 2, "entries": [{"col": 6, "val": 1}]},
      {"row": 3, "entries": [{"col": 7, "val": 1}]},
      {"row": 4, "entries": []},
      {"row": 5, "entries": []}
    ]
  },

  "public_inputs": ["1", "a", "b"],

  "symbolic_propagation": {
    "x_squared": "x * x",
    "ax": "a * x",
    "sum1": "x_squared + ax",
    "poly_result": "sum1 + b",
    "zero_const": "0",
    "check": "0"
  }
}
```

### Understanding the R1CS Matrices

Each R1CS constraint has the form: **A · B = C** (element-wise dot products with witness vector)

For example, constraint row 0 (`x_squared = x * x`):
- **A[0]**: `[{col: 1, val: 1}]` → selects `w[1]` (which is `x`)
- **B[0]**: `[{col: 1, val: 1}]` → selects `w[1]` (which is `x`)
- **C[0]**: `[{col: 4, val: 1}]` → selects `w[4]` (which is `x_squared`)

This encodes: `x * x = x_squared`

### Symbolic Expressions

The `symbolic_propagation` section tracks how each gate variable is computed symbolically. This enables:
- **Witness computation** - derive gate values from inputs
- **Circuit debugging** - trace how values flow through the circuit
- **Downstream processing** - proof systems can use symbolic info for optimization

### Example: Vitalik's QAP with Symbolic Input

Based on [Vitalik Buterin's QAP tutorial](https://medium.com/@VitalikButerin/quadratic-arithmetic-programs-from-zero-to-hero-f6d558cea649), we modify `x³ + x + 5 = 35` to use a symbolic (deferred) input `k` instead of the constant `5`.

**Input (`vitalik_deferred.aoa`):**
```aoa
decl private x
decl deferred k

# Flatten x³
sym_1 = x * x
y = sym_1 * x

# Compute x³ + x + k
sym_2 = y + x
out = sym_2 + k

# Enforce out == 35
thirty_five = 35
check = out == thirty_five
```

**Witness vector (9 variables) - SYMBOLIC:**
```
w = [~one,  x,  k,  sym_1,  y,   sym_2,    out,     35, check]
     [0]   [1] [2]   [3]   [4]    [5]      [6]     [7]   [8]
w = [ 1,    x,  k,   x²,    x³,  x³+x,  x³+x+k,    35,    0 ]
```

With concrete private input x=3 (but k remains symbolic):
```
w = [ 1,    3,  k,    9,   27,    30,    30+k,    35,    0 ]
                ↑                         ↑
            symbolic                  symbolic
```

The verifier substitutes k at verification time. For k=5: `30+k = 35` ✓

**Flattening (6 constraints):**
```
1. sym_1 = x * x           →  x²
2. y = sym_1 * x           →  x³
3. sym_2 = y + x           →  x³ + x
4. out = sym_2 + k         →  x³ + x + k     ← SYMBOLIC (contains k)
5. thirty_five = 35        →  35
6. out == thirty_five      →  x³ + x + k = 35
```

**R1CS Matrices (Vitalik's style):**

Matrix **A** (left input):
```
        ~one   x    k   sym_1   y   sym_2  out   35  check
         [0]  [1]  [2]   [3]   [4]   [5]   [6]  [7]   [8]
    ┌─────────────────────────────────────────────────────┐
 1. │   0     1    0     0      0     0     0    0     0  │  x
 2. │   0     0    0     1      0     0     0    0     0  │  sym_1
 3. │   0     1    0     0      1     0     0    0     0  │  y + x
 4. │   0     0    1     0      0     1     0    0     0  │  sym_2 + k
 5. │  -35    0    0     0      0     0     0    1     0  │  35 - 35·1
 6. │   0     0    0     0      0     0     1   -1     0  │  out - 35
    └─────────────────────────────────────────────────────┘
```

Matrix **B** (right input):
```
        ~one   x    k   sym_1   y   sym_2  out   35  check
         [0]  [1]  [2]   [3]   [4]   [5]   [6]  [7]   [8]
    ┌─────────────────────────────────────────────────────┐
 1. │   0     1    0     0      0     0     0    0     0  │  x
 2. │   0     1    0     0      0     0     0    0     0  │  x
 3. │   1     0    0     0      0     0     0    0     0  │  1
 4. │   1     0    0     0      0     0     0    0     0  │  1
 5. │   1     0    0     0      0     0     0    0     0  │  1
 6. │   1     0    0     0      0     0     0    0     0  │  1
    └─────────────────────────────────────────────────────┘
```

Matrix **C** (output):
```
        ~one   x    k   sym_1   y   sym_2  out   35  check
         [0]  [1]  [2]   [3]   [4]   [5]   [6]  [7]   [8]
    ┌─────────────────────────────────────────────────────┐
 1. │   0     0    0     1      0     0     0    0     0  │  sym_1
 2. │   0     0    0     0      1     0     0    0     0  │  y
 3. │   0     0    0     0      0     1     0    0     0  │  sym_2
 4. │   0     0    0     0      0     0     1    0     0  │  out
 5. │   0     0    0     0      0     0     0    0     0  │  0
 6. │   0     0    0     0      0     0     0    0     0  │  0
    └─────────────────────────────────────────────────────┘
```

**Symbolic propagation chain:**
```
sym_1  = x * x           = x²
y      = sym_1 * x       = x³
sym_2  = y + x           = x³ + x
out    = sym_2 + k       = x³ + x + k    ← contains deferred symbol k
check  = out - 35        = x³ + x + k - 35
```

**How verification works:**
1. Prover commits to the witness with `k` as a symbolic placeholder
2. Verifier provides concrete value for `k` (e.g., k=5)
3. Verifier substitutes k into `out = 30 + k = 35` and checks constraint 6

The deferred input `k` propagates symbolically through the witness. The verifier "completes" the proof by binding `k` to a concrete value at verification time.

### Example: Range Proof with Symbolic Weights

Proves `sum(w_i · x_i) >= t` where:
- `x[i]` are **private** (secret values)
- `w[i]` are **deferred** (symbolic weights - verifier provides)
- `t` is **deferred** (symbolic threshold)

**Input (`weighted_sum_ge.aoa`):**
```aoa
decl private x[2]
decl private b[4]
decl deferred w[2], t

# Weighted sum
prod0 = w[0] * x[0]
prod1 = w[1] * x[1]
weighted_sum = prod0 + prod1

# diff = sum - t (must be >= 0)
diff = weighted_sum - t

# Bit decomposition proves diff >= 0 (4-bit range: 0-15)
# Boolean constraint: b[i] ∈ {0,1} via b[i]² = b[i]
b0_sq = b[0] * b[0]
check_b0 = b0_sq == b[0]
# ... (same for b[1], b[2], b[3])

# Reconstruct: diff = b[0] + 2·b[1] + 4·b[2] + 8·b[3]
two_b1 = b[1] + b[1]
two_b2 = b[2] + b[2]
four_b2 = two_b2 + two_b2
# ... etc
reconstructed = sum_012 + eight_b3

# Verify decomposition
final_check = reconstructed == diff
```

**Symbolic witness propagation:**
```
prod0        = w[0] · x[0]              ← contains w[0]
prod1        = w[1] · x[1]              ← contains w[1]
weighted_sum = w[0]·x[0] + w[1]·x[1]    ← contains w[0], w[1]
diff         = w[0]·x[0] + w[1]·x[1] - t ← contains w[0], w[1], t
```

**How the range proof works:**

1. `diff = sum - t` is computed symbolically
2. Prover decomposes `diff` into bits `b[0..3]` (private)
3. Boolean constraints ensure each `b[i] ∈ {0,1}`: `b[i]² = b[i]`
4. Reconstruction constraint ensures: `diff = b[0] + 2·b[1] + 4·b[2] + 8·b[3]`
5. If `diff` can be expressed as sum of non-negative bit values → `diff >= 0` → `sum >= t`

**Verifier substitutes symbolic inputs:**
```
Given: w[0]=3, w[1]=2, t=10, and prover's x[0]=4, x[1]=5
  weighted_sum = 3·4 + 2·5 = 22
  diff = 22 - 10 = 12
  12 = 0 + 0 + 4 + 8 = b[0]=0, b[1]=0, b[2]=1, b[3]=1  ✓
```

### Example: Bitwise Comparison (8-bit Subtractor)

Proves `x >= t` where both are 8-bit unsigned integers using a **borrow-chain subtractor**.

**Input (`bitwise_ge.aoa`):**
```aoa
decl private x[8]        # 8-bit secret value
decl private borrow[8]   # borrow chain bits
decl private diff[8]     # difference bits
decl deferred t[8]       # 8-bit symbolic threshold

# Boolean constraints: all bits ∈ {0,1}
x0_sq = x[0] * x[0]
x0_bool = x0_sq == x[0]
# ... (for all x[i], borrow[i], diff[i])

# Subtraction using borrow chain:
#   diff[i] = x[i] XOR t[i] XOR borrow[i-1]
#   borrow[i] = (!x[i] & t[i]) | (!x[i] & borrow[i-1]) | (t[i] & borrow[i-1])

# Bit 0 (half subtractor - no incoming borrow):
xt0 = x[0] * t[0]                    # AND
two_xt0 = xt0 + xt0
xor0_sum = x[0] + t[0]
xor0 = xor0_sum - two_xt0            # XOR = a + b - 2ab
chk_d0 = xor0 == diff[0]

not_x0 = 1 - x[0]                    # NOT
bw0_calc = not_x0 * t[0]             # borrow = !x & t
chk_bw0 = bw0_calc == borrow[0]

# Bits 1-7: full subtractors with borrow propagation
# ... (same pattern with 3-input XOR and 3-input OR for borrow)

# FINAL ASSERTION: borrow[7] = 0 means x >= t
zero = 0
no_borrow = borrow[7] == zero
```

**Circuit statistics:** 231 witnesses, 198 constraints, 9 public inputs

**Bitwise operations in R1CS:**
```
XOR(a,b) = a + b - 2·a·b
AND(a,b) = a · b
NOT(a)   = 1 - a
OR(a,b)  = a + b - a·b
```

**Borrow chain logic:**
```
Bit i:  diff[i]   = x[i] ⊕ t[i] ⊕ borrow[i-1]
        borrow[i] = (!x[i] ∧ t[i]) ∨ (!x[i] ∧ borrow[i-1]) ∨ (t[i] ∧ borrow[i-1])
```

**Sign bit interpretation:**
- `borrow[7] = 0` → no underflow → `x - t >= 0` → `x >= t` ✓
- `borrow[7] = 1` → underflow → `x < t` (proof fails)

**Symbolic propagation:**
```
t[0..7] flow through the borrow chain symbolically.
Verifier substitutes concrete threshold bits at verification time.
Example: t = 100 = 0b01100100 → t[2]=1, t[5]=1, t[6]=1, others=0
```

## Grammar Summary

### Declaration Syntax
```
decl <visibility> <name1>, <name2>, ...
```
- **Visibility**: `private`, `public`, or `deferred`
- **Scalars**: No suffix (e.g., `x`, `y`)
- **Arrays**: `[size]` suffix (e.g., `a[4]`, `data[16]`)

### Constraint Syntax
```
<result> = <operand1> <op> <operand2>
<check> = <expr1> == <expr2>
```
- **Operators**: `+`, `-`, `*`
- **One operation per line**

### Rules
- Declarations must come before constraints
- Variables must be declared before use
- Arrays require index: `a[0]`
- Scalars without index: `x`
- One binary operation per assignment

## License

MIT License

## Contributing

Contributions welcome! Please ensure:
1. All tests pass (`make test`)
2. Code follows existing style
3. New features include test cases

## Contact

Tiago Alves - tiagoaoa@gmail.com

## Related Projects

- [Zyga](https://github.com/darklakefi/zyga) - Zero-knowledge proof system using AOA
