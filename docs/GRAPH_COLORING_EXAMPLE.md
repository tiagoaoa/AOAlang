# Graph Coloring: A Zero-Knowledge Proof in AOA

This document walks through a zero-knowledge proof circuit written in AOA. The prover demonstrates they know a k-coloring of a specific graph where no two adjacent vertices share the same color — without revealing the coloring itself.

Graph coloring is one of the foundational NP-complete problems, and proving a valid coloring in zero knowledge is a classic result in complexity theory. Here we show how it compiles from AOA source through R1CS matrices to QAP polynomials.

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

Vertices `{0, 1, 2}` form a triangle, with `3` hanging off `1` and `4` hanging off `2`, then `3--4` closing the bottom.

## The Proof

Given a graph G and a public coloring number k:

**For every edge (u, v) in G, c[u] != c[v].**

That's it. The prover knows a k-coloring. The verifier sees only the graph structure (baked into the circuit) and k (a public deferred input). The coloring itself is never revealed.

## The Circuit

Source file: [`examples/graph_coloring.aoa`](../examples/graph_coloring.aoa)

```aoa
# Graph Coloring ZK Proof
#
# Proves: "I know a k-coloring of this graph where no two
# adjacent vertices share the same color."

decl private c[5]
decl private inv
decl deferred k

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

# ============ K BINDING ============
# k is the public coloring number (verifier sees this)

three = 3
k_chk = k == three
```

### Declarations

| Declaration | Role |
|---|---|
| `decl private c[5]` | Secret color assignments (the witness) |
| `decl private inv` | Multiplicative inverse helper (secret) |
| `decl deferred k` | Number of colors (public, verifier-supplied) |

`private` variables are never revealed. `deferred` means the verifier supplies the value at verification time — `k` is part of the public proof statement, and the constraint `k == 3` binds this circuit to 3-coloring.

---

## How It Works

For each edge `(u, v)`, compute the color difference `d = c[u] - c[v]`. If any edge has `c[u] = c[v]`, that difference is 0. Multiply all six differences into one product:

```
p5 = (c[0]-c[1]) * (c[0]-c[2]) * (c[1]-c[2]) * (c[1]-c[3]) * (c[2]-c[4]) * (c[3]-c[4])
```

If all edges have different colors, `p5 != 0`. If any edge shares a color, `p5 = 0`.

Now we need to assert `p5 != 0`. But R1CS can only express equality constraints (`A * B = C`). There is no "not equal" gate.

**The inverse trick:** The prover supplies `inv = 1/p5` as a private witness, and the circuit checks:

```
p5 * inv == 1
```

- If `p5 != 0`: the prover computes `inv = p5^(-1)` in the finite field, and the constraint is satisfied.
- If `p5 = 0`: no value of `inv` satisfies `0 * inv = 1`. The proof cannot be constructed.

One private witness, one multiplication, one equality check — that's the entire adjacency proof.

### Equality constraints in R1CS

When AOA sees `chk = prod == one`, it generates:

```
A = [prod: 1, one: -1]    B = [1: 1]    C = []
```

This is `(prod - one) * 1 = 0`, forcing `prod = one`.

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
k_chk                scalar     private    23         gate
three                scalar     private    22         gate
chk                  scalar     private    21         gate
prod                 scalar     private    20         gate
one                  scalar     private    19         gate
p5                   scalar     private    18         gate
p4                   scalar     private    17         gate
p3                   scalar     private    16         gate
p2                   scalar     private    15         gate
p1                   scalar     private    14         gate
d5                   scalar     private    13         gate
d4                   scalar     private    12         gate
d3                   scalar     private    11         gate
d2                   scalar     private    10         gate
d1                   scalar     private    9          gate
d0                   scalar     private    8          gate
k                    scalar     deferred   7          declared
inv                  scalar     private    6          declared
c                    array      private    1          declared [size=5]
Total witness slots: 24
```

The witness vector has 24 slots:

| Range | Contents |
|-------|----------|
| `w[0]` | Constant `1` (implicit) |
| `w[1..5]` | `c[0]` through `c[4]` (private inputs) |
| `w[6]` | `inv` (private input) |
| `w[7]` | `k` (deferred public input) |
| `w[8..23]` | 16 gate outputs (intermediate computations) |

### R1CS Dimensions (`-d`)

```
$ bin/aoac -d examples/graph_coloring.aoa
Compiling: examples/graph_coloring.aoa
Generated R1CS dense: examples/graph_coloring.r1cs
  Witnesses: 24
  Constraints: 16
  Public inputs: 2
```

**16 constraints** over **24 witness variables**, with **2 public inputs** (`1` and `k`).

| Section | Count | Description |
|---------|-------|-------------|
| Edge differences | 6 | `d0`..`d5`: one subtraction per edge |
| Chain products | 5 | `p1`..`p5`: multiply differences together |
| Non-zero check | 3 | `one`, `prod`, `chk` |
| k binding | 2 | `three`, `k_chk` |
| **Total** | **16** | |

### R1CS JSON (`-g`)

The JSON output provides the R1CS in structured form with named witnesses and sparse matrix entries:

```json
{
  "circuit": "graph_coloring",
  "witness": {
    "total": 24,
    "partition": {
      "constant": {"indices": [0], "names": ["1"]},
      "private":  {"indices": [1,2,3,4,5,6],
                   "names": ["c[0]","c[1]","c[2]","c[3]","c[4]","inv"]},
      "deferred": {"indices": [7], "names": ["k"]},
      "gates":    {"indices": [8..23],
                   "names": ["d0","d1","d2","d3","d4","d5",
                             "p1","p2","p3","p4","p5",
                             "one","prod","chk","three","k_chk"]}
    }
  },
  "r1cs": {
    "n_constraints": 16,
    "n_variables": 24
  }
}
```

Each R1CS row carries a comment tracing back to the AOA source:

| Row | A | B | C | Comment |
|-----|---|---|---|---------|
| 0 | `c[0] - c[1]` | `1` | `d0` | `d0 = c[0] - c[1]` |
| 6 | `d0` | `d1` | `p1` | `p1 = d0 * d1` |
| 12 | `p5` | `inv` | `prod` | `prod = p5 * inv` |
| 13 | `prod - one` | `1` | `0` | `chk: prod == one` |
| 15 | `k - three` | `1` | `0` | `k_chk: k == three` |

### QAP Polynomials (`-q`)

```
$ bin/aoac -q examples/graph_coloring.aoa
Generated QAP: examples/graph_coloring.qap
  Witnesses: 24
  Constraints: 16
  Public inputs: 2
```

The QAP transforms the 16 R1CS constraints into polynomials via Lagrange interpolation at points `x = 1, 2, ..., 16`. Each witness variable gets polynomials `A_i(x)`, `B_i(x)`, `C_i(x)` of degree at most 15.

The core QAP equation:

```
P(x) = (w . A(x)) * (w . B(x)) - (w . C(x)) = H(x) * T(x)
```

where `T(x) = (x-1)(x-2)...(x-16)`. If the prover knows a valid witness, `P(x)` is divisible by `T(x)`.

Structural sparsity shows up in the polynomials: `A_inv(x) = 0` because `inv` only appears on the B side of constraint 12.

### C Sanity Checker (`-c`)

```
$ bin/aoac -c examples/graph_coloring.aoa
Generated C checker: examples/graph_coloring_checker.c
```

Testing with the valid coloring `{0:0, 1:1, 2:2, 3:0, 4:1}`:

```
$ gcc -o gc_checker examples/graph_coloring_checker.c
$ ./gc_checker 0 1 2 0 1 0 3 -1 -2 -1 1 1 -1 2 -2 -2 -2 2 1 0 0 3 0
```

```
  w[1] = 0  (c[0])       <- color 0
  w[2] = 1  (c[1])       <- color 1
  w[3] = 2  (c[2])       <- color 2
  w[4] = 0  (c[3])       <- color 0
  w[5] = 1  (c[4])       <- color 1
  w[6] = 0  (inv)        <- placeholder
  w[7] = 3  (k)
  ...
  w[18] = 2  (p5)        <- product of all edge differences

Constraint 13 FAILED: chk: prod == one
```

**15 of 16 constraints pass.** Only constraint 13 (`prod == one`) fails because `p5 = 2` and `inv = 1/2` does not exist in integer arithmetic.

This is expected: the inverse trick requires **finite field arithmetic**. In a ZK proving system over a prime field of order `p`, `inv = 2^(p-2) mod p` by Fermat's little theorem. The C checker uses `long long`, which validates the structure but not field inversion.

---

## Witness Walkthrough

Coloring `{0:0, 1:1, 2:2, 3:0, 4:1}`:

```
    0 (red)
   / \
  1---2 (green--blue)
  |   |
  3---4 (red--green)
```

### Edge differences

| Edge | `c[u] - c[v]` | Value |
|------|---------------|-------|
| (0,1) | `0 - 1` | `-1` |
| (0,2) | `0 - 2` | `-2` |
| (1,2) | `1 - 2` | `-1` |
| (1,3) | `1 - 0` | `1` |
| (2,4) | `2 - 1` | `1` |
| (3,4) | `0 - 1` | `-1` |

All non-zero. Every edge connects differently-colored vertices.

### Chain product

```
p1 = (-1) * (-2) = 2
p2 = 2 * (-1) = -2
p3 = (-2) * 1 = -2
p4 = (-2) * 1 = -2
p5 = (-2) * (-1) = 2     <- non-zero
```

In the finite field: `inv = 2^(-1)`, then `p5 * inv = 1`.

### What if vertex 3 had the same color as vertex 1?

`c[3] = 1` (same as `c[1]`), edge `(1,3)` gives `d3 = 0`:

```
p3 = p2 * 0 = 0
p4 = 0 * d4 = 0
p5 = 0 * d5 = 0
```

`0 * inv = 0` for any `inv`. The constraint `0 == 1` is unsatisfiable — no valid proof exists.

---

## Circuit Size

| Metric | Value |
|--------|-------|
| Vertices | 5 |
| Edges | 6 |
| Witnesses | 24 |
| Constraints | 16 |
| Public inputs | 2 (`1`, `k`) |
| Private inputs | 6 (5 colors + 1 inverse) |

For a general graph with `E` edges: **`2E + 4`** constraints (E differences + E-1 products + 3 non-zero check + 2 k binding).

For this graph: `2*6 + 4 = 16`.
