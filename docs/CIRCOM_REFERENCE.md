# Circom Language Reference (circom2aoa dialect)

This document specifies the Circom dialect accepted by circom2aoa -- a **superset of Circom 2.0** with AOAlang-specific extensions for explicit signal visibility.

For standard Circom documentation, see [docs.circom.io](https://docs.circom.io/).

---

## Table of Contents

1. [Program Structure](#program-structure)
2. [Templates](#templates)
3. [Signals](#signals)
4. [Variables](#variables)
5. [Components](#components)
6. [Constraints](#constraints)
7. [Expressions and Operators](#expressions-and-operators)
8. [Control Flow](#control-flow)
9. [Built-in Statements](#built-in-statements)
10. [AOAlang Extensions](#aoalang-extensions)
11. [Formal Grammar](#formal-grammar)

---

## Program Structure

A Circom file consists of:

```circom
pragma circom 2.0.0;              // Version declaration (required)

include "path/to/file.circom";    // External file (parsed, not resolved)

template Name(params) { ... }     // Template definitions

component main {public [x]} = Name(args);  // Main component (exactly one)
```

### Pragma

```circom
pragma circom 2.0.0;
```

Declares the Circom language version. Required as the first non-comment line.

### Include

```circom
include "relative/path.circom";
```

Declares a dependency on an external file. circom2aoa parses but does not resolve includes -- all templates must be defined in the same file.

### Main Component

```circom
component main = Template(args);
component main {public [a, b]} = Template(args);
```

Every file must have exactly one `component main` declaration. The optional `{public [...]}` list specifies which input signals are public (mapped to `decl deferred` in AOA). Inputs not listed are private.

---

## Templates

Templates are parameterized circuit definitions -- the fundamental building block in Circom.

```circom
template Name(param1, param2) {
    // signal declarations
    // variable declarations
    // component instantiations
    // constraints
}
```

### Parameters

Template parameters are compile-time constants. They can be used in:
- Signal array sizes: `signal output out[n]`
- Loop bounds: `for (var i = 0; i < n; i++)`
- Expressions: `(1 << nbits)`
- Nested template arguments: `component sub = Other(n + 1)`

```circom
template Num2Bits(n) {
    signal input in;
    signal output out[n];        // n determines array size
    for (var i = 0; i < n; i++) {  // n determines loop iterations
        // ...
    }
}
```

### Template Instantiation

Templates are instantiated through components (see [Components](#components)).

---

## Signals

Signals are the values that participate in the constraint system. They are the wires of the arithmetic circuit.

### Declaration

```circom
signal input a;                  // Input signal (scalar)
signal output b;                 // Output signal (scalar)
signal c;                        // Intermediate signal
signal input x[4];               // Input array of 4 signals
signal output out[n];            // Array sized by template parameter
```

### Direction

| Direction | Keyword | Description |
|-----------|---------|-------------|
| Input | `signal input` | Value provided from outside the template |
| Output | `signal output` | Value produced by the template, accessible from outside |
| Intermediate | `signal` | Internal to the template, not externally visible |

### Visibility (AOAlang extension)

Standard Circom 2.0 controls visibility only through `component main {public [...]}`. circom2aoa extends this with explicit keywords:

```circom
signal public input a;           // Explicitly public (decl deferred)
signal private input b;          // Explicitly private (decl private)
```

See [AOAlang Extensions](#aoalang-extensions) for details.

### Signal Access

```circom
a                                // Scalar signal
x[0]                             // Array element
comp.out                         // Component output signal
comp.out[3]                      // Component output array element
```

### Restrictions

- Signals can only be assigned once
- Signals cannot appear on the left side of `=` (use `<==` or `<--`)
- Array sizes must be compile-time constants

---

## Variables

Variables hold compile-time values for control flow and intermediate computation. Unlike signals, they do not participate directly in the constraint system.

### Declaration

```circom
var x;                           // Uninitialized
var y = 5;                       // Initialized
var a, b, c;                     // Multiple declarations
```

### Assignment

```circom
x = 10;
x += 1;
x -= 2;
x *= 3;
x /= 4;
x %= 5;
x **= 2;
x <<= 1;
x >>= 1;
x &= 0xFF;
x |= 0x01;
x ^= mask;
x++;
x--;
```

### Linear Combinations

Variables can accumulate linear combinations of signals:

```circom
var lc = 0;
lc += out[0] * 1;    // lc tracks: 1*out[0]
lc += out[1] * 2;    // lc tracks: 1*out[0] + 2*out[1]
lc === in;            // constraint: 1*out[0] + 2*out[1] == in
```

The flattener tracks these symbolically and emits the accumulated expression when the variable appears in a constraint.

---

## Components

Components instantiate templates within other templates.

### Declaration and Instantiation

```circom
component m = Multiplier();               // No-parameter template
component n2b = Num2Bits(8);              // With parameter
component arr[4] = SomeTemplate();        // Array of components (parsed)
```

### Wiring

Connect signals to component inputs and read outputs:

```circom
component m = Multiplier();
m.a <== x;              // Wire input
m.b <== y;              // Wire input
out <== m.c;            // Read output
```

Component body flattening is **deferred** -- the component's constraints are not emitted until an output signal is accessed. This allows inputs to be wired before the body is evaluated.

### Component Signal Access

```circom
m.a                      // Scalar signal on component m
n2b.out[3]               // Array signal on component n2b
```

In the flattened AOA output, component signals are prefixed: `m.a` becomes `m_a`, `n2b.out[3]` becomes `n2b_out_3`.

---

## Constraints

Circom has three constraint operators that control how values flow through the circuit.

### Constraint Assignment: `<==`

Assigns a value **and** creates an R1CS constraint.

```circom
c <== a * b;             // c gets value a*b, constraint: a*b = c
out <== in + 1;          // out gets value in+1, constraint: in+1 = out
```

**Reverse form:**
```circom
a * b ==> c;             // Equivalent to c <== a * b
```

This is the most common operator. Use it whenever the right-hand side is an arithmetic expression over signals.

### Witness Assignment: `<--`

Assigns a value **without** creating a constraint.

```circom
out[i] <-- (in >> i) & 1;   // Prover computes the bit
```

**Reverse form:**
```circom
(in >> i) & 1 --> out[i];   // Equivalent
```

Use `<--` when the computation involves non-arithmetic operations (bitshift, bitwise AND, etc.) that cannot be expressed as R1CS constraints. The prover provides the value, and separate `===` constraints must verify correctness.

**In circom2aoa:** Witness hints are not emitted as AOA operations. For sub-component signals assigned via `<--`, the transpiler declares them as `decl private` so the prover can supply them.

### Constraint Equality: `===`

Creates an R1CS constraint **without** assignment. Both sides must already have values.

```circom
out[i] * (out[i] - 1) === 0;     // Boolean: out[i] is 0 or 1
lc === in;                         // Reconstruction: sum of bits == input
n2b.out[nbits] === 1;             // Assert MSB is 1
```

Use `===` to add verification constraints that enforce properties of witness values.

### Constraint Patterns

**Boolean constraint** -- enforce `x` is 0 or 1:
```circom
x * (x - 1) === 0;
```

**Bit decomposition** -- verify bits reconstruct to original value:
```circom
var lc = 0;
var e2 = 1;
for (var i = 0; i < n; i++) {
    out[i] <-- (in >> i) & 1;       // Witness: extract bit
    out[i] * (out[i] - 1) === 0;    // Constraint: bit is boolean
    lc += out[i] * e2;              // Accumulate: bit * 2^i
    e2 = e2 * 2;
}
lc === in;                           // Constraint: reconstruction matches
```

**Greater-or-equal** -- `a >= b` via Num2Bits:
```circom
component n2b = Num2Bits(nbits + 1);
n2b.in <== a - b + (1 << nbits);
n2b.out[nbits] === 1;               // MSB = 1 means no underflow
```

---

## Expressions and Operators

### Operator Precedence (highest to lowest)

| Precedence | Operators | Associativity | Description |
|------------|-----------|---------------|-------------|
| 1 | `()` `[]` `.` | Left | Grouping, array index, field access |
| 2 | `-x` `!x` `~x` | Right | Unary negation, NOT, bitwise NOT |
| 3 | `**` | Right | Exponentiation |
| 4 | `*` `/` `%` `\` | Left | Multiply, divide, modulo, integer divide |
| 5 | `+` `-` | Left | Add, subtract |
| 6 | `<<` `>>` | Left | Bit shift left/right |
| 7 | `<` `>` `<=` `>=` | Left | Comparison |
| 8 | `==` `!=` | Left | Equality |
| 9 | `&` | Left | Bitwise AND |
| 10 | `^` | Left | Bitwise XOR |
| 11 | `\|` | Left | Bitwise OR |
| 12 | `&&` | Left | Logical AND |
| 13 | `\|\|` | Left | Logical OR |
| 14 | `? :` | Right | Ternary conditional |

### Arithmetic

```circom
a + b          // Addition
a - b          // Subtraction
a * b          // Multiplication
a / b          // Division (field inverse)
a % b          // Modulo
a \ b          // Integer division
a ** b         // Exponentiation
```

### Comparison

```circom
a == b         // Equal
a != b         // Not equal
a < b          // Less than
a > b          // Greater than
a <= b         // Less or equal
a >= b         // Greater or equal
```

### Logical

```circom
a && b         // Logical AND
a || b         // Logical OR
!a             // Logical NOT
```

### Bitwise

```circom
a & b          // Bitwise AND
a | b          // Bitwise OR
a ^ b          // Bitwise XOR
~a             // Bitwise NOT
a << n         // Shift left
a >> n         // Shift right
```

### Ternary

```circom
condition ? true_expr : false_expr
```

### Literals

```circom
42             // Integer
0              // Zero
```

---

## Control Flow

### For Loop

```circom
for (var i = 0; i < n; i++) {
    // body
}
```

Loops are **unrolled at compile time**. The loop variable must be a `var`, and the bound must be evaluable at compile time (constants or template parameters). The loop body is emitted once per iteration with the loop variable substituted.

```circom
// This loop with n=3:
for (var i = 0; i < 3; i++) {
    out[i] * (out[i] - 1) === 0;
}

// Produces three constraints:
// out[0] * (out[0] - 1) === 0
// out[1] * (out[1] - 1) === 0
// out[2] * (out[2] - 1) === 0
```

### If / Else

```circom
if (condition) {
    // then
} else if (other_condition) {
    // else if
} else {
    // else
}
```

Conditions must be evaluable at compile time (over `var` values, not signals). The flattener evaluates the condition and emits only the taken branch.

---

## Built-in Statements

### Log

```circom
log("message");
log(expr1, expr2);
log("value:", variable);
```

Debugging output. Parsed but not emitted in AOA output.

### Assert

```circom
assert(condition);
```

Compile-time assertion. Parsed but not emitted in AOA output.

### Return

```circom
return value;
```

Returns a value from a template or function context. Parsed but not commonly used in circuit templates.

---

## AOAlang Extensions

These features are **not part of standard Circom 2.0** and are specific to the circom2aoa dialect.

### Explicit Signal Visibility

Standard Circom 2.0 determines input visibility solely through `component main {public [...]}`. circom2aoa adds `public` and `private` keywords directly on signal declarations:

```circom
signal public input a;      // Maps to: decl deferred a
signal private input b;     // Maps to: decl private b
```

#### Precedence Rules

When both mechanisms are present, explicit visibility on the signal wins:

| Signal Declaration | Main Component | AOA Result |
|-------------------|----------------|------------|
| `signal public input a` | (any) | `decl deferred a` |
| `signal private input a` | (any) | `decl private a` |
| `signal input a` | `{public [a]}` | `decl deferred a` |
| `signal input a` | `a` not listed | `decl private a` |
| `signal output a` | (any) | `decl deferred a` |

#### Motivation

In standard Circom 2.0, you must cross-reference the `component main` declaration to determine which inputs are public. With explicit visibility, each signal is self-documenting:

**Standard Circom 2.0 (implicit):**
```circom
template Circuit() {
    signal input a;    // Is this public or private? Check component main...
    signal input b;    // Same question
    // ...
}
component main {public [a]} = Circuit();  // Answer is here
```

**circom2aoa dialect (explicit):**
```circom
template Circuit() {
    signal public input a;     // Public -- clear at point of declaration
    signal private input b;    // Private -- clear at point of declaration
    // ...
}
component main = Circuit();   // No public list needed
```

Both styles are fully supported and can be mixed within the same file.

---

## Formal Grammar

### Lexical Tokens

```
TOK_PRAGMA       "pragma"
TOK_CIRCOM       "circom"
TOK_INCLUDE      "include"
TOK_TEMPLATE     "template"
TOK_COMPONENT    "component"
TOK_MAIN         "main"
TOK_SIGNAL       "signal"
TOK_INPUT        "input"
TOK_OUTPUT       "output"
TOK_PUBLIC       "public"
TOK_PRIVATE      "private"
TOK_VAR          "var"
TOK_IF           "if"
TOK_ELSE         "else"
TOK_FOR          "for"
TOK_WHILE        "while"
TOK_RETURN       "return"
TOK_LOG          "log"
TOK_ASSERT       "assert"

TOK_LARROW       "<=="
TOK_RARROW       "==>"
TOK_LWITNESS     "<--"
TOK_RWITNESS     "-->"
TOK_TRIPLE_EQ    "==="
TOK_DBL_EQ       "=="
TOK_NEQ          "!="
TOK_LEQ          "<="
TOK_GEQ          ">="
TOK_LAND         "&&"
TOK_LOR          "||"
TOK_SHL          "<<"
TOK_SHR          ">>"
TOK_POW          "**"
TOK_PLUSEQ       "+="
TOK_MINUSEQ      "-="
TOK_MULEQ        "*="
TOK_DIVEQ        "/="
TOK_MODEQ        "%="
TOK_POWEQ        "**="
TOK_SHLEQ        "<<="
TOK_SHREQ        ">>="
TOK_ANDEQ        "&="
TOK_OREQ         "|="
TOK_XOREQ        "^="
TOK_INTDIVEQ     "\="
TOK_PLUSPLUS      "++"
TOK_MINUSMINUS   "--"

TOK_NUMBER       [0-9]+
TOK_STRING       "..."
TOK_IDENT        [a-zA-Z_$][a-zA-Z0-9_$]*
```

### Syntax (EBNF)

```ebnf
program         = { pragma | include | template_def | main_decl } ;

pragma          = "pragma" "circom" version ";" ;
version         = NUMBER "." NUMBER "." NUMBER ;

include         = "include" STRING ";" ;

template_def    = "template" IDENT "(" [ param_list ] ")" "{" { statement } "}" ;
param_list      = IDENT { "," IDENT } ;

main_decl       = "component" "main" [ "{" "public" "[" ident_list "]" "}" ]
                  "=" IDENT "(" [ expr_list ] ")" ";" ;

statement       = signal_decl
                | var_decl
                | component_decl
                | constrain_assign
                | witness_assign
                | constrain_eq
                | var_assign
                | for_stmt
                | if_stmt
                | block
                | log_stmt
                | assert_stmt
                | return_stmt ;

signal_decl     = "signal" [ "public" | "private" ] [ "input" | "output" ]
                  signal_names ";" ;
signal_names    = signal_name { "," signal_name } ;
signal_name     = IDENT [ "[" expr "]" ] ;

var_decl        = "var" var_init { "," var_init } ";" ;
var_init        = IDENT [ "=" expr ] ;

component_decl  = "component" IDENT [ "[" expr "]" ] "=" IDENT "(" [ expr_list ] ")" ";" ;

constrain_assign = expr ( "<==" | "==>" ) expr ";" ;
witness_assign   = expr ( "<--" | "-->" ) expr ";" ;
constrain_eq     = expr "===" expr ";" ;

var_assign      = expr ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "**="
                        | "<<=" | ">>=" | "&=" | "|=" | "^=" | "\=" ) expr ";"
                | expr ( "++" | "--" ) ";" ;

for_stmt        = "for" "(" var_decl_or_assign ";" expr ";" var_assign_expr ")"
                  block_or_stmt ;

if_stmt         = "if" "(" expr ")" block_or_stmt [ "else" block_or_stmt ] ;

block           = "{" { statement } "}" ;

log_stmt        = "log" "(" [ log_args ] ")" ";" ;
log_args        = ( expr | STRING ) { "," ( expr | STRING ) } ;

assert_stmt     = "assert" "(" expr ")" ";" ;

return_stmt     = "return" expr ";" ;

expr            = ternary ;
ternary         = logic_or [ "?" expr ":" expr ] ;
logic_or        = logic_and { "||" logic_and } ;
logic_and       = bit_or { "&&" bit_or } ;
bit_or          = bit_xor { "|" bit_xor } ;
bit_xor         = bit_and { "^" bit_and } ;
bit_and         = equality { "&" equality } ;
equality        = comparison { ( "==" | "!=" ) comparison } ;
comparison      = shift { ( "<" | ">" | "<=" | ">=" ) shift } ;
shift           = additive { ( "<<" | ">>" ) additive } ;
additive        = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative  = power { ( "*" | "/" | "%" | "\" ) power } ;
power           = unary [ "**" power ] ;           (* right-associative *)
unary           = ( "-" | "!" | "~" ) unary | postfix ;
postfix         = primary { "[" expr "]" | "." IDENT } ;
primary         = NUMBER | IDENT | IDENT "(" [ expr_list ] ")" | "(" expr ")" ;

expr_list       = expr { "," expr } ;
ident_list      = IDENT { "," IDENT } ;
```

### Comments

```circom
// Single-line comment
/* Multi-line
   comment */
```

---

## Known Limitations

| Limitation | Description |
|------------|-------------|
| No `include` resolution | Parsed but ignored; all templates must be in one file |
| Template parameter scoping | Nested templates with same-named parameters may conflict |
| No runtime branching | `if`/`else` on signal values not supported (compile-time only) |
| No standard library | circomlib templates must be inlined into source files |
| No `parallel` keyword | Parallel component execution not supported |
