# AOAlang - Parser and Validator for Arithmetic Optimization Algebra

A C-based parser and validator for the AOA (Arithmetic Optimization Algebra) language, used in zero-knowledge proof systems like Zyga.

## Overview

AOAlang provides a robust parser for `.aoa` files that validates both syntax and semantics:
- **Syntax validation**: Ensures proper grammar conformance
- **Semantic validation**: Checks variable declarations, type consistency, array indexing
- **Comprehensive error reporting**: Clear, actionable error messages with line numbers

## Features

- ✅ Full AOA grammar support (comma-separated declarations, array identification by `[size]`)
- ✅ Semantic analysis (variable declarations, scope checking)
- ✅ Type checking (scalar vs array variables)
- ✅ Index validation (arrays must be indexed, scalars cannot be indexed)
- ✅ R1CS JSON generation with symbolic witness support (`-g` flag)
- ✅ Built with Lex & Yacc for robust parsing
- ✅ Standard POSIX-compatible build system

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
```

## Example AOA Code

### Simple Quadratic Circuit

```aoa
# Prove x^2 + a*x + b = 0
decl private x[1]
decl deferred a[2], b[2]

# Compute a_val = a[0] + 2*a[1]
two_a1 = a[1] + a[1]
a_val = a[0] + two_a1

# Compute b_val = b[0] + 2*b[1]
two_b1 = b[1] + b[1]
b_val = b[0] + two_b1

# Compute x^2
x_squared = x[0] * x[0]

# Compute a*x
ax = a_val * x[0]

# Compute x^2 + a*x + b
sum1 = x_squared + ax
poly_result = sum1 + b_val

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

## R1CS Code Generation

AOAlang can generate R1CS (Rank-1 Constraint System) JSON output for use with zero-knowledge proof systems. The generated JSON includes full witness partitioning, sparse constraint matrices, and symbolic expression tracking.

### Usage

```bash
# Generate R1CS JSON (output: <input>.r1cs.json)
./bin/aoac -g examples/simple_quad.aoa

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
decl private x[1]
decl deferred a[2], b[2]

two_a1 = a[1] + a[1]
a_val = a[0] + two_a1
two_b1 = b[1] + b[1]
b_val = b[0] + two_b1
x_squared = x[0] * x[0]
ax = a_val * x[0]
sum1 = x_squared + ax
poly_result = sum1 + b_val
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
    "total": 16,
    "partition": {
      "constant": {"indices": [0], "names": ["1"]},
      "private": {"indices": [1], "names": ["x[0]"]},
      "deferred": {"indices": [2, 3, 4, 5], "names": ["a[0]", "a[1]", "b[0]", "b[1]"]},
      "gates": {"indices": [6, 7, 8, 9, 10, 11, 12, 13, 14, 15],
               "names": ["two_a1", "a_val", "two_b1", "b_val", "x_squared",
                        "ax", "sum1", "poly_result", "zero_const", "check"]}
    },
    "entries": [
      {"index": 0, "name": "1", "visibility": "public", "origin": "declared", "symbolic": "1"},
      {"index": 1, "name": "x[0]", "visibility": "private", "origin": "declared", "symbolic": "x[0]"},
      {"index": 2, "name": "a[0]", "visibility": "deferred", "origin": "declared", "symbolic": "a[0]"},
      {"index": 3, "name": "a[1]", "visibility": "deferred", "origin": "declared", "symbolic": "a[1]"},
      {"index": 4, "name": "b[0]", "visibility": "deferred", "origin": "declared", "symbolic": "b[0]"},
      {"index": 5, "name": "b[1]", "visibility": "deferred", "origin": "declared", "symbolic": "b[1]"},
      {"index": 6, "name": "two_a1", "visibility": "private", "origin": "gate", "symbolic": "a[1] + a[1]"},
      {"index": 7, "name": "a_val", "visibility": "private", "origin": "gate", "symbolic": "a[0] + two_a1"},
      {"index": 8, "name": "two_b1", "visibility": "private", "origin": "gate", "symbolic": "b[1] + b[1]"},
      {"index": 9, "name": "b_val", "visibility": "private", "origin": "gate", "symbolic": "b[0] + two_b1"},
      {"index": 10, "name": "x_squared", "visibility": "private", "origin": "gate", "symbolic": "x[0] * x[0]"},
      {"index": 11, "name": "ax", "visibility": "private", "origin": "gate", "symbolic": "a_val * x[0]"},
      {"index": 12, "name": "sum1", "visibility": "private", "origin": "gate", "symbolic": "x_squared + ax"},
      {"index": 13, "name": "poly_result", "visibility": "private", "origin": "gate", "symbolic": "sum1 + b_val"},
      {"index": 14, "name": "zero_const", "visibility": "private", "origin": "gate", "symbolic": "0"},
      {"index": 15, "name": "check", "visibility": "private", "origin": "gate", "symbolic": "0"}
    ]
  },

  "r1cs": {
    "n_constraints": 10,
    "n_variables": 16,

    "A": [
      {"row": 0, "entries": [{"col": 3, "val": 1}, {"col": 3, "val": 1}], "comment": "two_a1 = a[1] + a[1]"},
      {"row": 1, "entries": [{"col": 2, "val": 1}, {"col": 6, "val": 1}], "comment": "a_val = a[0] + two_a1"},
      {"row": 2, "entries": [{"col": 5, "val": 1}, {"col": 5, "val": 1}], "comment": "two_b1 = b[1] + b[1]"},
      {"row": 3, "entries": [{"col": 4, "val": 1}, {"col": 8, "val": 1}], "comment": "b_val = b[0] + two_b1"},
      {"row": 4, "entries": [{"col": 1, "val": 1}], "comment": "x_squared = x[0] * x[0]"},
      {"row": 5, "entries": [{"col": 7, "val": 1}], "comment": "ax = a_val * x[0]"},
      {"row": 6, "entries": [{"col": 10, "val": 1}, {"col": 11, "val": 1}], "comment": "sum1 = x_squared + ax"},
      {"row": 7, "entries": [{"col": 12, "val": 1}, {"col": 9, "val": 1}], "comment": "poly_result = sum1 + b_val"},
      {"row": 8, "entries": [{"col": 14, "val": 1}, {"col": 0, "val": 0}], "comment": "zero_const = 0"},
      {"row": 9, "entries": [{"col": 13, "val": 1}, {"col": 14, "val": -1}], "comment": "check: poly_result == zero_const"}
    ],

    "B": [
      {"row": 0, "entries": [{"col": 0, "val": 1}]},
      {"row": 1, "entries": [{"col": 0, "val": 1}]},
      {"row": 2, "entries": [{"col": 0, "val": 1}]},
      {"row": 3, "entries": [{"col": 0, "val": 1}]},
      {"row": 4, "entries": [{"col": 1, "val": 1}]},
      {"row": 5, "entries": [{"col": 1, "val": 1}]},
      {"row": 6, "entries": [{"col": 0, "val": 1}]},
      {"row": 7, "entries": [{"col": 0, "val": 1}]},
      {"row": 8, "entries": [{"col": 0, "val": 1}]},
      {"row": 9, "entries": [{"col": 0, "val": 1}]}
    ],

    "C": [
      {"row": 0, "entries": [{"col": 6, "val": 1}]},
      {"row": 1, "entries": [{"col": 7, "val": 1}]},
      {"row": 2, "entries": [{"col": 8, "val": 1}]},
      {"row": 3, "entries": [{"col": 9, "val": 1}]},
      {"row": 4, "entries": [{"col": 10, "val": 1}]},
      {"row": 5, "entries": [{"col": 11, "val": 1}]},
      {"row": 6, "entries": [{"col": 12, "val": 1}]},
      {"row": 7, "entries": [{"col": 13, "val": 1}]},
      {"row": 8, "entries": []},
      {"row": 9, "entries": []}
    ]
  },

  "public_inputs": ["1", "a[0]", "a[1]", "b[0]", "b[1]"],

  "symbolic_propagation": {
    "two_a1": "a[1] + a[1]",
    "a_val": "a[0] + two_a1",
    "two_b1": "b[1] + b[1]",
    "b_val": "b[0] + two_b1",
    "x_squared": "x[0] * x[0]",
    "ax": "a_val * x[0]",
    "sum1": "x_squared + ax",
    "poly_result": "sum1 + b_val",
    "zero_const": "0",
    "check": "0"
  }
}
```

### Understanding the R1CS Matrices

Each R1CS constraint has the form: **A · B = C** (element-wise dot products with witness vector)

For example, constraint row 4 (`x_squared = x[0] * x[0]`):
- **A[4]**: `[{col: 1, val: 1}]` → selects `w[1]` (which is `x[0]`)
- **B[4]**: `[{col: 1, val: 1}]` → selects `w[1]` (which is `x[0]`)
- **C[4]**: `[{col: 10, val: 1}]` → selects `w[10]` (which is `x_squared`)

This encodes: `x[0] * x[0] = x_squared`

### Symbolic Expressions

The `symbolic_propagation` section tracks how each gate variable is computed symbolically. This enables:
- **Witness computation** - derive gate values from inputs
- **Circuit debugging** - trace how values flow through the circuit
- **Downstream processing** - proof systems can use symbolic info for optimization

### Example: Vitalik's QAP with Symbolic Input

Based on [Vitalik Buterin's QAP tutorial](https://medium.com/@VitalikButerin/quadratic-arithmetic-programs-from-zero-to-hero-f6d558cea649), we modify `x³ + x + 5 = 35` to use a symbolic (deferred) input `k` instead of the constant `5`.

**Input (`vitalik_qap.aoa`):**
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
- ✅ Declarations must come before constraints
- ✅ Variables must be declared before use
- ✅ Arrays require index: `a[0]`
- ✅ Scalars without index: `x`
- ✅ One binary operation per assignment

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
