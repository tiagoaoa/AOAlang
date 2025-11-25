# AOA Error Test Cases

This directory contains `.aoa` files that intentionally contain errors to test the parser's error detection capabilities.

## Syntax Errors

These files contain malformed AOA syntax that should be caught during parsing:

### syntax_unclosed_bracket.aoa
- **Error**: Unclosed array bracket `[`
- **Line**: `x = a[0` (missing closing `]`)
- **Expected**: Syntax error on incomplete array access

### syntax_missing_bracket_in_expr.aoa
- **Error**: Missing closing bracket in complex expression
- **Line**: `x = a[0 + b[1]` (missing `]` after `a[0`)
- **Expected**: Syntax error on malformed expression

### syntax_invalid_identifier.aoa
- **Error**: Identifier starting with number
- **Line**: `123invalid = x + 1`
- **Expected**: Syntax error - identifiers must start with letter

### syntax_missing_operator.aoa
- **Error**: Missing operator between operands
- **Line**: `result = a b` (should be `a + b`, `a * b`, etc.)
- **Expected**: Syntax error on malformed expression

### syntax_double_equals_alone.aoa
- **Error**: Equality operator without proper left operand
- **Line**: `== 5` (standalone, no LHS)
- **Expected**: Syntax error

## Semantic Errors

These files have valid syntax but violate AOA semantic rules:

### semantic_undeclared_variable.aoa
- **Error**: Using variable that was never declared or assigned
- **Line**: `result = undeclared_var + x`
- **Variable**: `undeclared_var` does not exist
- **Expected**: "Variable 'undeclared_var' not declared or assigned yet"

### semantic_array_without_index.aoa
- **Error**: Using array variable without index
- **Line**: `result = a + 1` (where `a` is declared as array)
- **Expected**: "Array 'a' used without index - must use a[index]"

### semantic_scalar_with_index.aoa
- **Error**: Indexing a scalar variable
- **Line**: `result = x[0] + 1` (where `x` is scalar)
- **Expected**: "Scalar variable 'x' cannot be indexed"

### semantic_array_out_of_bounds.aoa
- **Error**: Array index exceeds declared size
- **Declaration**: `decl private a[4]` (size 4, indices 0-3)
- **Line**: `result = a[10] + 1`
- **Expected**: "Array index 10 out of bounds for 'a' (size 4)"

### semantic_assign_to_array_element.aoa
- **Error**: Attempting to assign to individual array element
- **Line**: `a[0] = 5`
- **Rule**: Arrays can only be declared as inputs, elements cannot be assigned individually
- **Expected**: "Cannot assign to individual array element 'a[0]'"

### semantic_use_gate_before_assignment.aoa
- **Error**: Using gate variable before it's assigned
- **Line**: `result = gate1 + x` (before `gate1` is assigned)
- **Rule**: Gate variables can only be used after they're assigned a value
- **Expected**: "Variable 'gate1' not declared or assigned yet"

### semantic_multiple_assignment.aoa
- **Error**: Assigning to same variable twice
- **Lines**:
  - First: `result = x + 1`
  - Second: `result = x + 2`
- **Rule**: Single assignment rule - each variable can only be assigned once
- **Expected**: "Variable 'result' already assigned (single assignment rule)"

### semantic_declaration_after_constraint.aoa
- **Error**: Declaration appearing after constraint section
- **Rule**: All declarations must come before any constraints
- **Expected**: "Declaration after constraints - all declarations must come first"

### semantic_assign_to_declared_var.aoa
- **Error**: Multiple assignment (same as semantic_multiple_assignment.aoa)
- **Note**: This file was updated - assigning to declared variables IS allowed (once)
- **Expected**: Error on second assignment to `y`

## Running the Tests

To test error detection on all files:

```bash
for f in examples/examples_with_errors/*.aoa; do
    echo "Testing: $f"
    ./bin/aoac "$f"
    echo ""
done
```

All files in this directory should fail validation and produce appropriate error messages.

## Error Reporting Format

Errors are reported with:
- Line number where error occurred
- Clear description of what went wrong
- Context about the variable or construct involved

Example:
```
Error (line 5): Variable 'undeclared_var' not declared or assigned yet
```

The parser attempts to find multiple errors in a single pass (where possible) rather than stopping at the first error.
