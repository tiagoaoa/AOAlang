# Graph Coloring: A Zero-Knowledge Proof in AOA

This document walks through a complete zero-knowledge proof circuit written in AOA. The prover demonstrates they know a coloring of a specific graph where no two adjacent vertices share the same color — without revealing the coloring itself.

Graph coloring is one of the foundational NP-complete problems, and proving a valid coloring in zero knowledge is a classic result in complexity theory. Here we show how it compiles all the way from AOA source through R1CS matrices to QAP polynomials.

## The Graph

Five vertices, six edges:

```
    0
   / \
  1---2
  |   |
  3---4
```

Edge list: `(0,1) (0,2) (1,2) (1,3) (2,4) (3,4)`

Vertices `{0, 1, 2}` form a triangle, with `3` hanging off `1` and `4` hanging off `2`, then `3` and `4` connected.

A valid coloring exists (e.g. `{0:red, 1:green, 2:blue, 3:red, 4:green}`), but the proof never reveals it.

## What the Proof Proves

For every edge `(u, v)` in the graph, the prover knows an assignment of colors `c[0..4]` such that `c[u] != c[v]`.

The prover's secret witness is the color array `c[5]` and a helper value `inv`. The verifier sees only the graph structure (which is baked into the circuit's constraints).

## The Circuit

Source file: [`examples/graph_coloring.aoa`](../examples/graph_coloring.aoa)

```aoa
# Graph Coloring ZK Proof

decl private c[5]
decl private inv

# ============ EDGE DIFFERENCES ============

d0 = c[0] - c[1]
d1 = c[0] - c[2]
d2 = c[1] - c[2]
d3 = c[1] - c[3]
d4 = c[2] - c[4]
d5 = c[3] - c[4]

# ============ CHAIN PRODUCT ============

p1 = d0 * d1
p2 = p1 * d2
p3 = p2 * d3
p4 = p3 * d4
p5 = p4 * d5

# ============ NON-ZERO CHECK ============
# p5 * inv == 1 proves all edges have different colors

one = 1
prod = p5 * inv
chk = prod == one
```

### Declarations

| Declaration | Role |
|---|---|
| `decl private c[5]` | Secret color assignments (the witness) |
| `decl private inv` | Multiplicative inverse helper (secret) |

Both are `private` — never revealed to the verifier.

---

## Constraint Strategy

For each edge `(u, v)`, compute the difference `d = c[u] - c[v]`. If any edge has `c[u] = c[v]`, then `d = 0`. Multiply all six differences into one product:

```aoa
d0 = c[0] - c[1]         # edge (0,1)
d1 = c[0] - c[2]         # edge (0,2)
d2 = c[1] - c[2]         # edge (1,2)
d3 = c[1] - c[3]         # edge (1,3)
d4 = c[2] - c[4]         # edge (2,4)
d5 = c[3] - c[4]         # edge (3,4)

p1 = d0 * d1              # chain-multiply all differences
p2 = p1 * d2
p3 = p2 * d3
p4 = p3 * d4
p5 = p4 * d5              # p5 = product of all 6 edge diffs
```

Now we need to assert `p5 != 0`. But R1CS can only express equality constraints (`A * B = C`). There is no way to directly write "not equal to zero."

**The inverse trick:** The prover supplies `inv = 1/p5` as an additional private witness, and the circuit checks:

```aoa
prod = p5 * inv
chk = prod == one          # p5 * inv == 1
```

If `p5` is non-zero, the prover can always compute its multiplicative inverse (we work in a finite field). If `p5 = 0` — meaning some edge has equal colors — then no value of `inv` can satisfy `0 * inv = 1`. The constraint is unsatisfiable, so the proof cannot be constructed.

This single inverse witness replaces what would otherwise need per-edge non-zero checks.

### Equality constraints in R1CS

When AOA sees `chk = prod == one`, it generates an R1CS constraint that enforces `prod - one = 0`:

```
A = [prod: 1, one: -1]    B = [1: 1]    C = []
```

The `A * B = C` form becomes `(prod - one) * 1 = 0`, which forces `prod = one`. The gate output `chk` is always 0 (it stores the "residual" which must be zero for the constraint to hold).

---

## Compilation

### Validation

```
$ bin/aoac examples/graph_coloring.aoa
Validating: examples/graph_coloring.aoa
Validation successful - examples/graph_coloring.aoa is valid AOA
```

### Symbol Table (`-v`)

```
$ bin/aoac -v examples/graph_coloring.aoa
```

```
=== Symbol Table ===
Name                 Type       Visibility WitIdx     Origin
----                 ----       ---------- ------     ------
chk                  scalar     private    20         gate
prod                 scalar     private    19         gate
one                  scalar     private    18         gate
p5                   scalar     private    17         gate
p4                   scalar     private    16         gate
p3                   scalar     private    15         gate
p2                   scalar     private    14         gate
p1                   scalar     private    13         gate
d5                   scalar     private    12         gate
d4                   scalar     private    11         gate
d3                   scalar     private    10         gate
d2                   scalar     private    9          gate
d1                   scalar     private    8          gate
d0                   scalar     private    7          gate
inv                  scalar     private    6          declared
c                    array      private    1          declared [size=5]
Total witness slots: 21
```

The witness vector has 21 slots:

| Range | Contents |
|-------|----------|
| `w[0]` | Constant `1` (implicit) |
| `w[1..5]` | `c[0]` through `c[4]` (private inputs) |
| `w[6]` | `inv` (private input) |
| `w[7..20]` | 14 gate outputs (intermediate computations) |

### R1CS Dimensions (`-d`)

```
$ bin/aoac -d examples/graph_coloring.aoa
Compiling: examples/graph_coloring.aoa
Generated R1CS dense: examples/graph_coloring.r1cs
  Witnesses: 21
  Constraints: 14
  Public inputs: 1
```

**14 constraints** over **21 witness variables**, with **1 public input** (the constant `1`).

Constraint breakdown:

| Section | Count | Description |
|---------|-------|-------------|
| Edge differences | 6 | One subtraction per edge |
| Chain products | 5 | `p1` through `p5` |
| Non-zero check | 3 | `one`, `prod`, `chk` |
| **Total** | **14** | |

### R1CS JSON (`-g`)

The JSON output provides the R1CS in a structured format with named witnesses and sparse matrix entries:

```json
{
  "circuit": "graph_coloring",
  "witness": {
    "total": 21,
    "partition": {
      "constant": {"indices": [0], "names": ["1"]},
      "private":  {"indices": [1,2,3,4,5,6],
                   "names": ["c[0]","c[1]","c[2]","c[3]","c[4]","inv"]},
      "gates":    {"indices": [7..20],
                   "names": ["d0","d1","d2","d3","d4","d5",
                             "p1","p2","p3","p4","p5",
                             "one","prod","chk"]}
    }
  },
  "r1cs": {
    "n_constraints": 14,
    "n_variables": 21,
    "A": [
      {"row": 0,  "entries": [{"col": 1, "val": 1}, {"col": 2, "val": -1}],
       "comment": "d0 = c[0] - c[1]"},
      {"row": 6,  "entries": [{"col": 7, "val": 1}],
       "comment": "p1 = d0 * d1"},
      {"row": 12, "entries": [{"col": 17, "val": 1}],
       "comment": "prod = p5 * inv"},
      {"row": 13, "entries": [{"col": 19, "val": 1}, {"col": 18, "val": -1}],
       "comment": "chk: prod == one"},
      ...
    ]
  }
}
```

Each constraint carries a comment showing the original AOA source line, making it easy to trace from R1CS back to the circuit.

Selected R1CS rows explained:

- **Row 0** (`d0 = c[0] - c[1]`): `A = c[0] - c[1]`, `B = 1`, `C = d0`. Encodes `(c[0] - c[1]) * 1 = d0`.
- **Row 6** (`p1 = d0 * d1`): `A = d0`, `B = d1`, `C = p1`. Pure multiplication gate.
- **Row 12** (`prod = p5 * inv`): `A = p5`, `B = inv`, `C = prod`. The key non-zero check.
- **Row 13** (`chk: prod == one`): `A = prod - one`, `B = 1`, `C = 0`. Forces `prod = one`.

### QAP Polynomials (`-q`)

```
$ bin/aoac -q examples/graph_coloring.aoa
Compiling: examples/graph_coloring.aoa
Generated QAP: examples/graph_coloring.qap
  Witnesses: 21
  Constraints: 14
  Public inputs: 1
```

The QAP transforms the 14 R1CS constraints into polynomials via Lagrange interpolation at points `x = 1, 2, ..., 14`. Each witness variable gets three polynomials `A_i(x)`, `B_i(x)`, `C_i(x)` of degree at most 13.

The core QAP equation is:

```
P(x) = (w . A(x)) * (w . B(x)) - (w . C(x)) = H(x) * T(x)
```

where `T(x) = (x-1)(x-2)...(x-14)` is the target polynomial. If the prover knows a valid witness `w`, then `P(x)` is divisible by `T(x)` — this is what the verifier checks.

Example polynomials from the output:

```
A_c[0](x) = 1.93e-09*x^13 - 1.98e-07*x^12 + ... + 218.87*x - 77
A_inv(x)  = 0    (inv never appears on the A side of any constraint)
```

`A_inv(x) = 0` because `inv` only ever appears on the B side of constraint 12 (`prod = p5 * inv`). This structural sparsity is typical: most witnesses participate in only a few constraints.

### C Sanity Checker (`-c`)

```
$ bin/aoac -c examples/graph_coloring.aoa
Compiling: examples/graph_coloring.aoa
Generated C checker: examples/graph_coloring_checker.c
```

The checker compiles to a standalone C program that verifies `(A*w) . (B*w) = (C*w)` for each constraint using integer arithmetic. Testing with the valid coloring `{0:0, 1:1, 2:2, 3:0, 4:1}`:

```
$ gcc -o gc_checker examples/graph_coloring_checker.c
$ ./gc_checker 0 1 2 0 1 0 -1 -2 -1 1 1 -1 2 -2 -2 -2 2 1 0 0
```

```
Witness values:
  w[0] = 1  (1)
  w[1] = 0  (c[0])       <- color 0
  w[2] = 1  (c[1])       <- color 1
  w[3] = 2  (c[2])       <- color 2
  w[4] = 0  (c[3])       <- color 0
  w[5] = 1  (c[4])       <- color 1
  w[6] = 0  (inv)        <- placeholder (see note below)
  w[7..12]               <- edge differences: -1, -2, -1, 1, 1, -1
  w[13..17]              <- chain products: 2, -2, -2, -2, 2
  w[18] = 1  (one)
  w[19] = 0  (prod)
  w[20] = 0  (chk)

Constraint 13 FAILED: chk: prod == one
  (A*w)[13] = -1
  (B*w)[13] = 1
  (A*w)*(B*w) = -1
  (C*w)[13] = 0 (expected)
```

**13 of 14 constraints pass.** The only failure is constraint 13 (`prod == one`), because `p5 = 2` and `inv = 1/2` does not exist in integer arithmetic.

This is expected: the inverse trick requires **finite field arithmetic** (e.g., BN254 where every non-zero element has a multiplicative inverse). In a real ZK proving system operating over a prime field of order `p`, `inv = 2^(p-2) mod p` by Fermat's little theorem. The C checker uses `long long` integers, which is sufficient for validating the edge-difference and product structure but not the non-zero proof via field inversion.

---

## Walking Through a Valid Witness

Using coloring `{0:0, 1:1, 2:2, 3:0, 4:1}` (mapping: 0=red, 1=green, 2=blue):

```
    0 (red)
   / \
  1---2 (green--blue)
  |   |
  3---4 (red--green)
```

### Edge differences:

| Edge | Difference | Value |
|------|-----------|-------|
| (0,1) | `c[0] - c[1]` | `0 - 1 = -1` |
| (0,2) | `c[0] - c[2]` | `0 - 2 = -2` |
| (1,2) | `c[1] - c[2]` | `1 - 2 = -1` |
| (1,3) | `c[1] - c[3]` | `1 - 0 = 1` |
| (2,4) | `c[2] - c[4]` | `2 - 1 = 1` |
| (3,4) | `c[3] - c[4]` | `0 - 1 = -1` |

All non-zero — every edge connects differently-colored vertices.

### Chain product:

```
p1 = (-1) * (-2) = 2
p2 = 2 * (-1) = -2
p3 = (-2) * 1 = -2
p4 = (-2) * 1 = -2
p5 = (-2) * (-1) = 2     <- non-zero: all edges valid
```

In a finite field: `inv = 2^(-1)`, then `p5 * inv = 1`. Constraint satisfied.

### What if vertex 3 had the same color as vertex 1?

With `c[3] = 1` (same as `c[1]`), edge `(1,3)` gives `d3 = c[1] - c[3] = 0`. Then:

```
p3 = p2 * 0 = 0
p4 = 0 * d4 = 0
p5 = 0 * d5 = 0
```

Now `prod = 0 * inv = 0` for any `inv`. The constraint `0 == 1` is unsatisfiable — the prover cannot construct a valid proof.

---

## Circuit Size Analysis

| Metric | Value |
|--------|-------|
| Vertices | 5 |
| Edges | 6 |
| Witness variables | 21 |
| R1CS constraints | 14 |
| Public inputs | 1 |
| Private inputs | 6 (5 colors + 1 inverse) |

For a general graph with `E` edges:

| Component | Constraints |
|-----------|-------------|
| Edge differences | `E` |
| Chain products | `E - 1` |
| Non-zero check | 3 (`one`, `prod`, `chk`) |
| **Total** | **`2E + 2`** |

For our graph: `2*6 + 2 = 14`. Matches.

The circuit scales linearly with the number of edges.
