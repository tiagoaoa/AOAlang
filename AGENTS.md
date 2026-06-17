# Agent Workflow for AOAlang

This file is guidance for LLM agents such as Codex, Claude, or other coding assistants working with AOAlang circuits.

## Repository Setup

If the current project does not already have a local AOAlang checkout, clone one before generating or validating circuits:

```bash
git clone git@github.com:tiagoaoa/AOAlang.git
cd AOAlang
./configure
make
```

If SSH access is unavailable, use the equivalent HTTPS remote for the same repository. After setup, use the checkout root as `AOALANG_ROOT` when invoking tools from another project.

Required tools:

- `bin/aoac`: validates and compiles `.aoa` files.
- `bin/circom2aoa`: transpiles `.circom` files to `.aoa`.

Build or rebuild them with:

```bash
./configure
make
make -C circom2aoa
```

## Default Circuit Workflow

Unless the user explicitly asks for handwritten AOA, create or modify a `.circom` circuit first and let `circom2aoa` generate the `.aoa` file.

Default flow:

```bash
bin/circom2aoa path/to/circuit.circom -o path/to/circuit.aoa
bin/aoac path/to/circuit.aoa
```

Equivalent one-step validation:

```bash
bin/circom2aoa --validate path/to/circuit.circom
```

Use extra `aoac` flags when useful:

```bash
bin/aoac -v path/to/circuit.aoa   # symbol table / visibility inspection
bin/aoac -g path/to/circuit.aoa   # R1CS JSON
bin/aoac -d path/to/circuit.aoa   # dense R1CS
bin/aoac -q path/to/circuit.aoa   # QAP polynomials
bin/aoac -c path/to/circuit.aoa   # C sanity checker
```

Do not consider generated AOA finished until `bin/aoac file.aoa` succeeds.

## Grammar References

Read and follow the grammar docs before introducing new syntax:

- `docs/CIRCOM_REFERENCE.md`: circom2aoa Circom dialect.
- `docs/CIRCOM_TO_AOA.md`: transpiler behavior, signal mapping, and tests.
- `docs/AOA_GRAMMAR.md`: formal AOA grammar and semantic rules.
- `docs/AOA_QUICK_REFERENCE.md`: quick AOA syntax rules.
- `docs/GENERATED_OUTPUT_EXAMPLES.md`: `aoac` output formats.
- `docs/GRAPH_COLORING_EXAMPLE.md`: end-to-end AOA proof example.

Both source and generated files must follow their corresponding grammars. Do not rely on syntax that the docs list as unsupported.

## Circom Rules

Every `.circom` file must have:

- `pragma circom 2.0.0;`
- all required templates, either in the file or included from `circom2aoa/lib`
- exactly one `component main = Template(args);` or `component main {public [...]} = Template(args);`

Signal visibility matters:

- Secret witness inputs should be private.
- Verifier-known/public inputs should be listed in `component main {public [...]}` or declared with `signal public input`.
- Unlisted `signal input` values are private by default.
- `signal output` values become public/deferred in AOA.
- Explicit `signal public input` / `signal private input` wins over the `main {public [...]}` list.

Use Circom operators carefully:

- Use `<==` for assignment plus constraint.
- Use `===` for constraints without assignment.
- Use `<--` only for witness hints that cannot be expressed directly as arithmetic constraints, such as bit extraction with shifts. Every `<--` value must be constrained afterward, usually with boolean and reconstruction checks.
- Use `assert(...)` only for compile-time/template-parameter checks. Assertions are not runtime signal constraints; use `===` for signal-level enforcement.
- `if` conditions and loop bounds must be compile-time values, not signals.

## Library Components

Prefer reusable components from `circom2aoa/lib` instead of copying or reimplementing them:

```circom
include "bitify.circom";
include "comparators";
include "poseidon.circom";
```

Current library files:

- `bitify.circom`: `Num2Bits`, `Bits2Num`
- `comparators.circom`: circomlib-style comparators such as `LessThan`, `GreaterEqThan`, `IsZero`, `IsEqual`
- `poseidon.circom`: Poseidon component templates currently supporting the fixed two-input backend (`Poseidon(2)` / `PoseidonEx(2, 1)`)

Includes are resolved relative to the circuit file and `circom2aoa/lib`. Do not use removed alias paths such as `circomlib/circuits/...` unless they are reintroduced intentionally.

## AOA Rules

If you must write or edit `.aoa` directly, follow `docs/AOA_GRAMMAR.md` exactly:

- All `decl` lines must appear before any constraints.
- Use `decl private`, `decl public`, or `decl deferred` explicitly.
- Arrays are declared with `[size]` and must be accessed with literal integer indexes.
- Scalars must not be indexed; arrays must not be used without an index.
- Each non-declaration line must contain one operation only: `+`, `-`, `*`, identity, constant assignment, or equality.
- Split chained expressions into intermediate variables.
- Do not use unsupported coefficient syntax such as `3*a + 5*b`; encode coefficients with repeated addition or have Circom generate the AOA.
- Do not use division in AOA; express inverse relationships with constraints.
- Validate every edited `.aoa` with `bin/aoac`.

## Common Constraint Patterns

Boolean bit:

```circom
x * (x - 1) === 0;
```

Bit decomposition:

```circom
component n2b = Num2Bits(n);
n2b.in <== value;
```

Non-zero check:

```circom
signal private input inv;
value * inv === 1;
```

Public threshold/range checks should make the threshold public/deferred and keep the private witness secret.

## Testing Expectations

For changes under `circom2aoa`, run:

```bash
make -C circom2aoa test
```

For a new standalone circuit, at minimum run:

```bash
bin/circom2aoa circuit.circom -o circuit.aoa
bin/aoac circuit.aoa
```

When debugging generated output, inspect:

```bash
bin/aoac -v circuit.aoa
bin/aoac -g circuit.aoa
```

The generated C checker (`aoac -c`) is useful for structural sanity checks, but it uses ordinary integer arithmetic and is not a full finite-field proving-system verifier.

## Change Discipline

- Do not manually patch generated `.aoa` to hide a Circom/transpiler problem. Fix the `.circom` source or the transpiler.
- Keep public/private signal decisions explicit in the source.
- Keep generated expected files in sync when modifying tests.
- Do not commit or push unless the user asks.
