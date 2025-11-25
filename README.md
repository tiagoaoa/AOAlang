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
