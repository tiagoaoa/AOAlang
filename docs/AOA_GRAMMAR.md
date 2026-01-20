# AOA Grammar Specification
## Formal Grammar for Zyga Arithmetic Optimization Algebra (.aoa) Files

**Version**: 1.0
**Date**: 2025-11-21
**Author**: Tiago Alves

---

## Table of Contents

1. [Introduction](#introduction)
2. [Formal Grammar (EBNF)](#formal-grammar-ebnf)
3. [Lexical Structure](#lexical-structure)
4. [Semantic Rules](#semantic-rules)
5. [R1CS Constraint Mapping](#r1cs-constraint-mapping)
6. [Examples](#examples)
7. [Implementation Notes](#implementation-notes)

---

## Introduction

The AOA (Arithmetic Optimization Algebra) file format is a domain-specific language for expressing arithmetic constraint systems that compile to Rank-1 Constraint System (R1CS) format. AOA files are used in the Zyga zero-knowledge proof system to specify circuits with explicit separation of private and public (deferred) inputs.

### Design Goals

1. **R1CS Correspondence**: Each non-declaration line maps to one R1CS gate (A·B = C)
2. **Visibility Tracking**: Explicit separation of private vs public/deferred variables
3. **Human Readability**: Simple syntax for constraint specification
4. **Field Arithmetic**: All operations over finite field (e.g., BN254 scalar field)

### Key Properties

- **Declarations first**: All variable declarations must appear before any constraints
- **Single assignment**: Each variable is assigned exactly once
- **Gate-level**: Each assignment corresponds to one multiplicative gate or linear combination
- **Symbolic evaluation**: Public (deferred) variables remain symbolic during compilation

---

## Formal Grammar (EBNF)

Extended Backus-Naur Form specification:

```ebnf
(* Top-level structure *)
program         ::= declaration_section constraint_section ;

declaration_section ::= { declaration | comment | empty_line } ;

constraint_section  ::= { constraint | comment | empty_line } ;

line            ::= declaration | constraint | comment | empty_line ;

empty_line      ::= [ whitespace ] newline ;

comment         ::= "#" { any_character } newline ;

(* Declarations *)
declaration     ::= "decl" visibility var_list newline ;

visibility      ::= "private" | "public" | "deferred" ;

var_list        ::= var_decl { "," var_decl } ;

var_decl        ::= identifier [ "[" size "]" ] ;

size            ::= positive_integer ;

(* Constraints and gates *)
constraint      ::= assignment | equality_constraint | constant_assignment | standalone_equality ;

assignment      ::= identifier "=" expression newline ;

equality_constraint ::= identifier "=" expression "==" expression newline ;

constant_assignment ::= identifier "=" number newline ;

standalone_equality ::= expression "==" expression newline ;

(* Expressions - ONE operation per line for R1CS *)
expression      ::= binary_op | unary_expr ;

binary_op       ::= term ( "+" | "-" ) term | term "*" term ;

term            ::= [ coefficient "*" ] operand ;

operand         ::= variable | array_access ;

coefficient     ::= number ;

unary_expr      ::= variable | array_access | number ;

(* Note: Expressions limited to single binary operation per line *)
(* Multi-operation expressions must use intermediate variables *)
(* Theoretical grammar: coefficients allowed (e.g., 3*a + 5*b) *)
(* Current parser: coefficients NOT supported - variables only *)

variable        ::= identifier ;

array_access    ::= identifier "[" index "]" ;

index           ::= non_negative_integer ;

number          ::= [ "-" ] digit { digit } [ "." digit { digit } ] ;

identifier      ::= letter { letter | digit | "_" } ;

(* Lexical primitives *)
letter          ::= "a" | "b" | ... | "z" | "A" | "B" | ... | "Z" ;

digit           ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;

positive_integer ::= digit_nonzero { digit } ;

non_negative_integer ::= "0" | positive_integer ;

digit_nonzero   ::= "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;

whitespace      ::= " " | "\t" ;

newline         ::= "\n" | "\r\n" ;

any_character   ::= ? any UTF-8 character except newline ? ;
```

---

## Lexical Structure

### Comments

- Begin with `#` and extend to end of line
- Can appear on their own line or after code (inline comments)
- Stripped during parsing

```aoa
# Full line comment
x = a + b  # Inline comment
```

### Whitespace

- Spaces and tabs allowed around operators and tokens
- Line structure is significant (one statement per line)
- Blank lines ignored

### Identifiers

**Rules**:
- Start with letter (a-z, A-Z)
- Followed by letters, digits, or underscores
- Case-sensitive

**Conventions**:
- Use snake_case: `carry_bit1`, `sum_result`
- Array names typically lowercase: `a`, `b`, `x`
- Intermediate values use descriptive names: `xor_temp`, `prod_final`

### Literals

**Integer literals**: `0`, `1`, `42`, `1000`

**Decimal literals**: `0.5`, `3.14159`

**Negative literals**: `-1`, `-0.5`

**Note**: All literals are interpreted as field elements modulo field prime.

---

## Semantic Rules

### 1. Variable Declaration

All variables must be declared before use with explicit visibility. **All declarations must appear at the beginning of the file, before any constraints.**

**Variable declarations** (comma-separated list):
```ebnf
decl <visibility> <name1>[, <name2>, ...]
```

Where each name can optionally have `[size]` to declare an array:
```ebnf
decl <visibility> <scalar_name>, <array_name>[<size>], ...
```

**Distinction**: The presence of `[<size>]` distinguishes arrays from scalars. Variables without `[size]` are scalars (single values).

**Visibility semantics**:

| Visibility | Meaning | Usage |
|-----------|---------|-------|
| `private` | Secret witness value | Private inputs, intermediate gates |
| `public` | Public input (checked) | Publicly verifiable parameters |
| `deferred` | Symbolic/dynamic public | Public inputs treated symbolically during GB elimination |

**Examples**:
```aoa
# Single scalar declaration
decl private x                    # Single private value

# Multiple scalars on one line
decl private x, y, z              # Three private values

# Array declarations (identified by [size])
decl private bits[8]              # 8-element private array
decl deferred a[64], b[64]        # Two 64-element public arrays

# Mixed scalar and array declarations
decl public threshold, data[4]    # One scalar and one 4-element array

# Multiple declaration lines
decl private x[1]                 # Private array
decl deferred a[2], b[2]          # Public arrays
decl public threshold             # Public scalar
```

### 2. Constraint Assignment

Each assignment creates **exactly one R1CS constraint gate**. The result variable (left-hand side) corresponds to the **C vector** in the R1CS constraint `(w·A) × (w·B) = (w·C)`.

**Multiplication gate** (R1CS: `A·B = C`):
```aoa
result = left * right
```
Maps to: `left · right = result`

**Addition gate** (Binary):
```aoa
sum = a + b
```
Maps to: `(a + b) · 1 = sum`

**Important**: Only **one binary operation** per line. For `a + b + c`, use intermediate variables:
```aoa
temp = a + b
sum = temp + c
```

**Subtraction gate** (Binary):
```aoa
diff = a - b
```
Maps to: `(a - b) · 1 = diff`

**Identity gate**:
```aoa
y = x
```
Maps to: `x · 1 = y`

#### Coefficient Support (Theoretical vs. Current)

**Theoretically valid R1CS** (but NOT supported by current parser):

Linear combinations with coefficients:
```aoa
# THEORETICAL: result = 3*a + 5*b
# R1CS: (3·a + 5·b) × 1 = result
# NOT CURRENTLY SUPPORTED
```

Bilinear with coefficients:
```aoa
# THEORETICAL: result = 3*a * 5*b
# R1CS: (3·a) × (5·b) = result
# NOT CURRENTLY SUPPORTED
```

**Current parser limitation**: The parser expects variable names only in expressions. Coefficients must be encoded using addition:
```aoa
# CURRENT: To compute 3*a, use:
temp1 = a + a
result = temp1 + a  # result = 3*a
```

### 3. Equality Constraints

Enforces equality between two expressions:

```aoa
check = expr1 == expr2
```

**Semantics**: Creates constraint `(expr1 - expr2) · 1 = 0`

**Variable `check`**: Assigned value 0 (private)

**Example**:
```aoa
zero_const = 0
check = poly_result == zero_const  # Enforce poly_result = 0
```

**Standalone form** (without explicit assignment):
```aoa
expr1 == expr2
```

This creates an implicit constraint without assigning a result variable. Commonly used for final verification constraints:

```aoa
no_borrow == result  # Enforce no_borrow equals result
```

**Note**: Both forms are semantically equivalent and create the same R1CS constraint.

### 4. Constant Assignment

Assign constant value to variable:

```aoa
var = 42
```

**R1CS encoding**: `(var - 42) · 1 = 0`

**Common pattern**:
```aoa
claimed_carry = 0
carry_check = actual_carry == claimed_carry
```

### 5. Expression Evaluation

**Critical constraint**: Each assignment must be **one binary operation** (or constant/identity).

**One operation per line** - Each of these is valid:
```aoa
c = a + b      # One addition
c = a - b      # One subtraction
c = a * b      # One multiplication
c = x          # Identity
c = 42         # Constant
```

**Multiple operations require intermediate variables**:

**Invalid** (multiple operations):
```aoa
# WRONG: Two additions
result = a + b + c

# WRONG: Two multiplications
result = a * b * c

# WRONG: Addition then multiplication
result = (a + b) * c
```

**Valid** (one operation per line):
```aoa
# CORRECT: Split additions
temp = a + b
result = temp + c

# CORRECT: Split multiplications
temp = a * b
result = temp * c

# CORRECT: Separate addition from multiplication
sum = a + b
result = sum * c
```

**Reason**: Each line maps to exactly one R1CS constraint `(w·A) × (w·B) = (w·C)`, where the left-hand variable is the C vector.

### 6. Array Indexing

Access array elements with bracket notation:

```aoa
element = array_name[index]
```

**Index requirements**:
- Non-negative integer literal
- Within bounds: `0 <= index < array_size`
- No computed indices (indices must be constants)

**Examples**:
```aoa
decl private array bits[8]
first = bits[0]
last = bits[7]
```

---

## R1CS Constraint Mapping

Every AOA constraint maps to **exactly one** R1CS triplet `(A, B, C)` where each is a vector of coefficients.

### General Form

R1CS constraint:
```
(A · witness) × (B · witness) = (C · witness)
```

Where `witness = [1, var1, var2, ...]` (constant 1 in position 0).

**Key principle**: The left-hand side variable of each AOA assignment corresponds to the **C vector** (result of the multiplication). Each assignment creates exactly one R1CS gate.

### Mapping Rules

#### 1. Multiplication: `c = a * b`

```
A = [0, ..., 0, 1, 0, ...]  (1 at position of 'a')
B = [0, ..., 0, 1, 0, ...]  (1 at position of 'b')
C = [0, ..., 0, 1, 0, ...]  (1 at position of 'c')
```

**Semantics**: `a × b = c`

#### 2. Addition: `c = a + b`

```
A = [0, ..., 0, 1, 0, ..., 1, 0, ...]  (1 at 'a', 1 at 'b')
B = [1, 0, ...]                          (constant 1)
C = [0, ..., 0, 1, 0, ...]              (1 at position of 'c')
```

**Semantics**: `(a + b) × 1 = c`

#### 3. Subtraction: `c = a - b`

```
A = [0, ..., 1, 0, ..., -1, 0, ...]  (1 at 'a', -1 at 'b')
B = [1, 0, ...]                        (constant 1)
C = [0, ..., 0, 1, 0, ...]            (1 at position of 'c')
```

**Semantics**: `(a - b) × 1 = c`

#### 4. Constant: `c = k`

```
A = [−k, ..., 0, 1, 0, ...]  (-k at constant, 1 at 'c')
B = [1, 0, ...]               (constant 1)
C = [0, ...]                  (all zeros)
```

**Semantics**: `(c - k) × 1 = 0`

#### 5. Equality: `check = a == b`

Compiles to: `(a - b) × 1 = 0`

```
A = [0, ..., 1, 0, ..., -1, 0, ...]  (1 at 'a', -1 at 'b')
B = [1, 0, ...]                        (constant 1)
C = [0, ...]                           (all zeros)
```

Variable `check` is assigned value 0.

---

## Examples

### Example 1: Basic Arithmetic (Scalar Variables)

**AOA code**:
```aoa
decl private x, y, z

# Compute x^2
x_squared = x * x

# Compute x^2 + y
sum = x_squared + y

# Verify sum equals z
check = sum == z
```

**Witness** (concrete): `x=3, y=7, z=16`
- `x_squared = 9`
- `sum = 16`
- `check = 0` (constraint satisfied)

**Variables**: `[1, x, y, z, x_squared, sum, check]`

**R1CS constraints**:
1. `x × x = x_squared`
2. `(x_squared + y) × 1 = sum`
3. `(sum - z) × 1 = 0`

---

### Example 1b: Same Circuit with Arrays

**AOA code**:
```aoa
decl private x[1], y[1], z[1]

# Compute x^2
x_squared = x[0] * x[0]

# Compute x^2 + y
sum = x_squared + y[0]

# Verify sum equals z
check = sum == z[0]
```

**Note**: Functionally equivalent to scalar version, but uses array indexing.

---

### Example 2: XOR Gate

Boolean XOR: `a ⊕ b = a + b - 2·a·b`

**AOA code (using scalar variables)**:
```aoa
decl private a, b

# Compute a * b
ab = a * b

# Compute 2 * (a * b)
ab_doubled = ab + ab

# Compute a + b
sum_ab = a + b

# XOR: a + b - 2*a*b
xor_result = sum_ab - ab_doubled
```

**Alternative (using arrays)**:
```aoa
decl private a[1], b[1]

ab = a[0] * b[0]
ab_doubled = ab + ab
sum_ab = a[0] + b[0]
xor_result = sum_ab - ab_doubled
```

**Truth table** (field arithmetic):
| a | b | a×b | 2×a×b | a+b | XOR |
|---|---|-----|-------|-----|-----|
| 0 | 0 | 0   | 0     | 0   | 0   |
| 0 | 1 | 0   | 0     | 1   | 1   |
| 1 | 0 | 0   | 0     | 1   | 1   |
| 1 | 1 | 1   | 2     | 2   | 0   |

---

### Example 3: Range Check (Non-Trivial Discriminants)

Check if private value is less than public threshold.

**AOA code**:
```aoa
decl private value[4]          # 4-bit private value
decl deferred threshold[4]     # 4-bit public threshold

# Compute value - threshold (with borrow propagation)
# ... (subtraction circuit, 40+ gates)

# Final borrow indicates value < threshold
# If no_borrow = 0, then value >= threshold (valid)
# If no_borrow != 0, then value < threshold (invalid)

zero_const = 0
check = final_borrow == zero_const
```

**Discriminants**: GB elimination produces polynomials in `threshold[0..3]` only.

For `threshold = [0,0,0,0]` (value=0): No valid private witness if value must be ≥ 0
For `threshold = [1,1,1,1]` (value=15): Many valid witnesses (0-14)

**This demonstrates non-trivial discriminants**: Different public inputs yield different satisfiability.

---

### Example 4: Public Input Computation

**AOA code**:
```aoa
decl deferred a[2]  # 2-bit public value

# Compute numeric value: a_val = a[0] + 2*a[1]
two_a1 = a[1] + a[1]
a_val = a[0] + two_a1

# Now a_val ∈ {0, 1, 2, 3} can be used in constraints
# ...
```

**Symbolic evaluation**: During GB elimination, `a_val` is symbolic expression `a[0] + 2·a[1]`.

---

## Implementation Notes

### Parser Implementation

The reference implementation is in Python (`zyga_grothish/zyga.py:compile_constraints`):

1. **Two-pass parsing**:
   - Pass 1: Count variables to allocate witness vector
   - Pass 2: Process declarations and constraints

2. **Witness vector**: `[1, var1, var2, ...]` with constant 1 at index 0

3. **Symbolic vs Concrete**:
   - Private variables: Concrete `PrivateValue` if provided, else `Symbol`
   - Deferred variables: Always `Symbol` (remain symbolic)
   - Public variables: Can be substituted with concrete values

4. **Constraint accumulation**: Each line appends to `(A, B, C)` matrices

### Field Arithmetic

All operations are modulo field prime `p` (e.g., BN254: `p = 21888242871839275222246405745257275088548364400416034343698204186575808495617`).

**Implications**:
- `-1 ≡ p - 1`
- Division is multiplication by modular inverse: `a / b = a · b^(-1) mod p`
- No integer overflow (all values reduced mod p)

### Array Bounds

- Checked at compile time
- Out-of-bounds access causes parse error
- No runtime bounds checking (all indices are constants)

### Variable Naming Restrictions

**Reserved**: `1` (constant one)

**Conventions**:
- Avoid Python keywords: `and`, `or`, `not`, `if`, etc.
- Avoid operators: `+`, `-`, `*`, `==`
- Use descriptive names: `carry_bit0`, `sum_final`, `xor_temp`

### Debugging

**Verbose mode**: Prints each constraint and witness assignment during compilation.

**Example output**:
```
Declaration: private array x[1]
  x[0] = x[0] (PRIVATE/SYMBOLIC)
Constraint: x_squared = x[0] * x[0]
  Result: x_squared = 9 (PRIVATE)
```

---

## Common Patterns

### Pattern 1: Boolean Constraint

Enforce variable is boolean (0 or 1):

```aoa
# bit * (bit - 1) = 0
# If bit = 0: 0 * (-1) = 0 ✓
# If bit = 1: 1 * 0 = 0 ✓
# If bit = 2: 2 * 1 = 2 ≠ 0 ✗

decl private bit
one = 1
bit_minus_1 = bit - one
bit_check = bit * bit_minus_1
zero = 0
constraint = bit_check == zero
```

### Pattern 2: OR Gate

Boolean OR: `a ∨ b = a + b - a·b`

```aoa
# a OR b
ab_prod = a * b
ab_sum = a + b
or_result = ab_sum - ab_prod
```

### Pattern 3: AND Gate

Boolean AND: `a ∧ b = a · b`

```aoa
and_result = a * b
```

### Pattern 4: NOT Gate

Boolean NOT: `¬a = 1 - a`

```aoa
one = 1
not_a = one - a
```

### Pattern 5: Conditional

If-then-else using boolean selector:

```aoa
# result = selector ? true_val : false_val
# result = selector * true_val + (1 - selector) * false_val

one = 1
not_selector = one - selector
true_branch = selector * true_val
false_branch = not_selector * false_val
result = true_branch + false_branch
```

---

## Limitations and Extensions

### Current Limitations

1. **One operation per line**: Each assignment must be one binary operation (R1CS gate correspondence)
   - `a + b + c` requires intermediate: `temp = a + b; result = temp + c`
2. **No coefficient support**: Expressions like `3*a + 5*b` not supported (theoretically valid R1CS)
   - Parser expects variable names only
   - Coefficients must be encoded via repeated addition: `temp = a + a; result = temp + a` for `3*a`
   - R1CS constraints `(3·a + 5·b) × 1 = c` and `(3·a) × (5·b) = c` are valid but require parser extension
3. **No loops**: Each line is explicit (code generation recommended for repetitive patterns)
4. **No functions**: All constraints inline
5. **No computed indices**: Array indices must be literal integers
6. **Single multiplication per line**: Max one `*` per expression (R1CS bilinearity)
7. **No division operator**: Must use inverse (or decompose into constraints)
8. **No operator precedence beyond binary**: Parentheses not supported - decompose instead

### Potential Extensions

1. **Macros/Templates**: Reusable constraint patterns
2. **Higher-level constructs**: For-loops, functions (compile to AOA)
3. **Type system**: Distinguish bits, field elements, arrays
4. **Optimization passes**: Dead code elimination, constant folding
5. **Automatic bit decomposition**: High-level range checks

---

## Grammar Validation

**Valid AOA program**:
```aoa
# Example: Quadratic equation x^2 + a*x + b = 0
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

**Parse tree**:
1. 2 declaration lines declaring 3 arrays (x, a, b)
2. 11 constraints (assignments)
3. 1 equality constraint (check)
4. Total: 15 R1CS constraints

---

## References

1. **Zyga Implementation**: `zyga_grothish/zyga.py` (Python parser)
2. **Example Circuits**: `rust/test-fixtures/*.aoa`
3. **R1CS Specification**: Standard constraint system format for SNARKs
4. **Field Arithmetic**: BN254 elliptic curve scalar field

---

## Appendix: Complete Example

### Circuit: 4-bit Unsigned Greater-Than

**File**: `uint4gt.aoa`

**Purpose**: Compare two 4-bit unsigned integers via subtraction with borrow.

**Algorithm**:
- Compute `a - b` bit-by-bit
- Track borrow propagation
- Final borrow = 0 if `a >= b`, else `a < b`

**Code** (abbreviated, see `test-fixtures/uint4gt.aoa` for full version):

```aoa
decl private a[4]
decl deferred b[4]

# Bit 0: a[0] XOR b[0]
sym1 = a[0] + b[0]
sym2 = a[0] * b[0]
sym3 = sym2 + sym2
sum_bit0 = sym1 - sym3

# Bit 1: (a[1] XOR b[1]) XOR carry_bit0
sym4 = a[1] + b[1]
sym5 = a[1] * b[1]
sym6 = sym5 + sym5
xor1_temp = sym4 - sym6

sym7 = xor1_temp + sym2
sym8 = xor1_temp * sym2
sym9 = sym8 + sym8
sum_bit1 = sym7 - sym9

# Carry generation for bit 1
sym10 = sym2 * xor1_temp
sym11 = sym5 + sym10
sym12 = sym5 * sym10
sym13 = sym12 + sym12
carry_bit1 = sym11 - sym13

# ... (bits 2-3 similar)

# Final check: carry_out == 0 (enforces a >= b)
claimed_carry = 0
carry_check = actual_carry_out == claimed_carry
```

**Total constraints**: ~50 (multiplication, addition, equality gates)

**Witness size**: ~45 variables (4 public + 41 intermediate)

---

**End of AOA Grammar Specification**

Version 1.0 - Complete formal specification for Zyga .aoa file format.
