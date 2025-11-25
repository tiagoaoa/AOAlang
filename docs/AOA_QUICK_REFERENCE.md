# AOA Quick Reference Card

**Zyga Arithmetic Optimization Algebra (.aoa) - Cheat Sheet**

---

## Basic Syntax

```aoa
# Comments start with #

# Variable declarations (comma-separated, arrays identified by [size])
decl private x                # Single private scalar
decl private bits[8]          # Private array (8 elements)
decl deferred a[4], b[4]      # Two 4-element public arrays
decl public threshold, max    # Two public scalars
decl private x, y, data[16]   # Mixed: scalars and array

# Assignments (each = ONE R1CS gate, ONE operation only)
result = a * b                # Multiplication gate
sum = a + b                   # Addition gate (binary only!)
diff = a - b                  # Subtraction gate
copy = x                      # Identity gate

# Equality constraints
check = expr1 == expr2        # Enforces expr1 = expr2

# Constants
zero = 0
one = 1
```

---

## Quick Rules

| Rule | Requirement |
|------|-------------|
| **Declarations first** | All `decl` statements must appear before any constraints |
| **Variables** | Must be declared as scalar or array before use |
| **One statement per line** | No semicolons, newline-terminated |
| **One operation per line** | Each assignment = ONE binary op (one R1CS gate) |
| **Array indexing** | `array[index]` with constant integer index |
| **No chaining** | `a + b + c` invalid - use intermediate variables |
| **No coefficients** | `3*a + 5*b` not supported (parser limitation, R1CS valid) |
| **Field arithmetic** | All operations mod field prime (BN254) |

---

## Visibility Types

| Keyword | Meaning | Use Case |
|---------|---------|----------|
| `private` | Secret witness | Private inputs, intermediates |
| `public` | Public input (verified) | Public parameters |
| `deferred` | Symbolic public | Dynamic public inputs (GB elimination) |

---

## Common Patterns

### XOR (a ⊕ b)
```aoa
ab = a * b
ab2 = ab + ab
sum = a + b
xor = sum - ab2
```

### AND (a ∧ b)
```aoa
and_result = a * b
```

### OR (a ∨ b)
```aoa
ab = a * b
sum = a + b
or_result = sum - ab
```

### NOT (¬a)
```aoa
one = 1
not_a = one - a
```

### Boolean Constraint (a ∈ {0,1})
```aoa
one = 1
a_minus_1 = a - one
a_check = a * a_minus_1    # a*(a-1) = 0
zero = 0
constraint = a_check == zero
```

### If-Then-Else (selector ? true_val : false_val)
```aoa
one = 1
not_sel = one - selector
true_branch = selector * true_val
false_branch = not_sel * false_val
result = true_branch + false_branch
```

---

## R1CS Mapping

Each AOA constraint maps to `(A·w) × (B·w) = (C·w)` where `w` is witness vector.

| AOA | R1CS Semantics |
|-----|----------------|
| `c = a * b` | `a × b = c` |
| `c = a + b` | `(a + b) × 1 = c` |
| `c = a - b` | `(a - b) × 1 = c` |
| `c = k` (constant) | `(c - k) × 1 = 0` |
| `check = a == b` | `(a - b) × 1 = 0` |

---

## Example: Quadratic Equation

**Circuit**: Prove knowledge of `x` such that `x² + a·x + b = 0`

```aoa
decl private x[1]         # Secret value
decl deferred a[2], b[2]  # Public: a = a[0] + 2*a[1], b = b[0] + 2*b[1]

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

**Variables**: 14 total
**Constraints**: 8 R1CS gates
**Public inputs**: `a[0], a[1], b[0], b[1]` (4 bits = 16 combinations)

---

## Error Prevention

### ❌ WRONG
```aoa
# Multiple operations (violates one-op-per-line rule)
result = a + b + c      # TWO additions
result = a * b * c      # TWO multiplications
result = (a + b) * c    # Addition AND multiplication

# Coefficients (theoretically valid R1CS, but parser unsupported)
result = 3*a + 5*b      # Coefficients not supported
result = 2*x * 3*y      # Coefficients not supported

# Missing declaration
x = a + b  # x not declared!

# Computed array index
index = 2
value = array[index]

# Division operator
quotient = a / b
```

###✅ CORRECT
```aoa
# Split operations - one per line
temp = a + b
result = temp + c

temp = a * b
result = temp * c

sum = a + b
result = sum * c

# Encode coefficients via repeated addition
# For 3*a:
temp = a + a
three_a = temp + a

# Declare before use (scalar or array)
decl private x
x = a + b

# Literal array indices only
value = array[2]

# No division (use constraints)
# For a/b, constrain: b * quotient = a
```

---

## Debugging Tips

1. **Run with verbose mode** - See constraint compilation
2. **Check variable count** - Should match witness vector size
3. **Verify R1CS** - Each assignment = one gate
4. **Test with concrete values** - Substitute private inputs to check
5. **Symbolic trace** - Track deferred variables through GB elimination

---

## Performance Notes

- **Small circuits** (≤16 public bits): Consider lookup table optimization
- **Large circuits** (>100 vars): GB elimination may struggle, use slicing
- **Intermediate variables**: Each assignment adds one witness element
- **Gate count**: Roughly equals number of non-declaration lines

---

## References

- **Full Grammar**: See `AOA_GRAMMAR.md`
- **Parser**: `zyga_grothish/zyga.py:compile_constraints()`
- **Examples**: `rust/test-fixtures/*.aoa`
- **Lookup Optimization**: `lookup_optimization_paper.pdf`

---

**Quick Syntax Check**

Valid `.aoa` file structure:
```
[comments and blank lines]

# DECLARATION SECTION - All declarations first
decl <visibility> <name1>, <name2>, ...      # comma-separated names
decl <visibility> <name>[<size>]             # array (has [size])
decl <visibility> <scalar>, <array>[<size>]  # mixed
[more declarations...]

# CONSTRAINT SECTION - Constraints after all declarations
<var> = <operand1> <op> <operand2>           # ONE binary operation
[more constraints...]
<check> = <expr> == <expr>                   # equality
```

Where:
- `<op>` is one of: `+`, `-`, `*`
- `<operand>` is: variable, array[index], or constant
- Arrays are identified by `[<size>]` suffix (no "array" keyword)

---

*AOA Quick Reference v1.0 - 2025-11-21*
