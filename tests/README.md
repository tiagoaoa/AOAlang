# AOAlang Test Suite

This directory contains the comprehensive test suite for the AOAlang parser.

## Running Tests

### Quick Test (Valid Examples Only)
```bash
make test
```
Runs validation on all valid `.aoa` files in `examples/` directory. Fast sanity check.

### Error Detection Tests Only
```bash
make test-errors
```
Tests that all files in `examples/examples_with_errors/` properly trigger errors.

### Comprehensive Test Suite
```bash
make test-all
```
or
```bash
./tests/run_tests.sh
```

Runs complete test suite including:
- All valid example files (should pass)
- All syntax error tests (should fail with syntax errors)
- All semantic error tests (should fail with semantic errors)

## Test Organization

### Valid Tests (6 tests)
Located in `examples/*.aoa`:
- `quadratic.aoa` - Basic quadratic circuit
- `simple_quad.aoa` - Simplified quadratic
- `uint4gt.aoa` - 4-bit unsigned integer comparison
- `uint16gt.aoa` - 16-bit unsigned integer comparison
- `uint32gt.aoa` - 32-bit unsigned integer comparison
- `uint64gt.aoa` - 64-bit unsigned integer comparison

### Error Tests (14 tests)
Located in `examples/examples_with_errors/`:

#### Syntax Errors (5 tests)
- `syntax_unclosed_bracket.aoa` - Unclosed array bracket
- `syntax_missing_bracket_in_expr.aoa` - Missing bracket in expression
- `syntax_invalid_identifier.aoa` - Identifier starting with number
- `syntax_missing_operator.aoa` - Missing operator between operands
- `syntax_double_equals_alone.aoa` - Malformed equality constraint

#### Semantic Errors (9 tests)
- `semantic_undeclared_variable.aoa` - Using undeclared variable
- `semantic_array_without_index.aoa` - Array used without index
- `semantic_scalar_with_index.aoa` - Scalar indexed like array
- `semantic_array_out_of_bounds.aoa` - Array index out of bounds
- `semantic_assign_to_array_element.aoa` - Assignment to array element
- `semantic_use_gate_before_assignment.aoa` - Gate used before assignment
- `semantic_multiple_assignment.aoa` - Variable assigned twice
- `semantic_declaration_after_constraint.aoa` - Declaration after constraints
- `semantic_assign_to_declared_var.aoa` - Multiple assignment error

## Test Script Features

The `run_tests.sh` script provides:

### Color-Coded Output
- ✓ Green for passing tests
- ✗ Red for failing tests
- Blue for section headers
- Yellow for warnings

### Detailed Reporting
- Shows which test is running
- Reports expected vs actual behavior for failures
- Counts total, passed, and failed tests
- Returns exit code 0 on success, 1 on failure

### Error Validation
- Verifies that error tests actually produce errors
- Checks for specific error messages where applicable
- Ensures parser doesn't crash on invalid input

## Adding New Tests

### Adding Valid Tests
1. Create new `.aoa` file in `examples/`
2. Run `make test` to verify it passes
3. No changes to test script needed (auto-discovered)

### Adding Error Tests
1. Create new `.aoa` file in `examples/examples_with_errors/`
2. Add appropriate test call in `run_tests.sh`:
   ```bash
   # For syntax errors (any error acceptable)
   test_error_any "$PROJECT_ROOT/examples/examples_with_errors/your_test.aoa"

   # For semantic errors (specific error message required)
   test_error "$PROJECT_ROOT/examples/examples_with_errors/your_test.aoa" \
       "expected error substring"
   ```
3. Run `make test-all` to verify
4. Document the test in `examples/examples_with_errors/README.md`

## Test Functions

### `test_valid(file)`
Tests that a file passes validation successfully.

### `test_error(file, expected_error)`
Tests that a file fails with a specific error message.
- Checks exit code is non-zero
- Verifies expected error substring appears in output

### `test_error_any(file)`
Tests that a file fails validation (any error acceptable).
- Only checks exit code is non-zero
- Use for syntax errors where exact message may vary

## Continuous Integration

The test suite is designed for CI/CD integration:
```bash
# In CI pipeline
make clean
make all
make test-all
```

Returns:
- Exit code 0 if all tests pass
- Exit code 1 if any test fails

## Test Coverage

Current coverage:
- **Syntax validation**: All major syntax elements tested
- **Semantic validation**: All semantic rules enforced
- **Error recovery**: Multiple errors detected per file
- **Edge cases**: Boundary conditions and error combinations

## Performance

Test suite typically completes in:
- `make test`: < 1 second (6 tests)
- `make test-errors`: < 1 second (14 tests)
- `make test-all`: < 2 seconds (20 tests)

## Troubleshooting

### Tests fail unexpectedly
1. Rebuild parser: `make clean && make`
2. Check parser exists: `ls -l bin/aoac`
3. Run individual test: `bin/aoac examples/test_file.aoa`
4. Check test script permissions: `ls -l tests/run_tests.sh`

### Test script not found
```bash
chmod +x tests/run_tests.sh
```

### Error tests passing when they should fail
Check that the `.aoa` file actually contains an error and that the parser is detecting it:
```bash
bin/aoac examples/examples_with_errors/test_file.aoa
echo $?  # Should be non-zero
```
