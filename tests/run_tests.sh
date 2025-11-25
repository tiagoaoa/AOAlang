#!/bin/bash
#
# AOAlang Parser Test Suite
# Validates parser behavior on both valid and invalid AOA files
#

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Find script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
AOAC="$PROJECT_ROOT/bin/aoac"

# Check if parser exists
if [ ! -f "$AOAC" ]; then
    echo -e "${RED}Error: Parser not found at $AOAC${NC}"
    echo "Run 'make' first to build the parser"
    exit 1
fi

echo "======================================"
echo "  AOAlang Parser Test Suite"
echo "======================================"
echo ""

# Function to run a test expecting success
test_valid() {
    local file="$1"
    local name="$(basename "$file")"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if "$AOAC" "$file" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} PASS: $name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗${NC} FAIL: $name (expected valid, got errors)"
        "$AOAC" "$file" 2>&1 | grep "Error" | head -3
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

# Function to run a test expecting specific error
test_error() {
    local file="$1"
    local expected_error="$2"
    local name="$(basename "$file")"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    # Capture output and exit code correctly
    local output
    output=$("$AOAC" "$file" 2>&1)
    local exit_code=$?

    # Should fail (non-zero exit)
    if [ $exit_code -eq 0 ]; then
        echo -e "${RED}✗${NC} FAIL: $name (expected error, but passed)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi

    # Check if expected error message is present
    if echo "$output" | grep -q "$expected_error"; then
        echo -e "${GREEN}✓${NC} PASS: $name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗${NC} FAIL: $name (wrong error message)"
        echo "  Expected: $expected_error"
        echo "  Got:"
        echo "$output" | grep "Error" | head -2 | sed 's/^/    /'
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

# Function to run a test expecting any error
test_error_any() {
    local file="$1"
    local name="$(basename "$file")"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    if ! "$AOAC" "$file" > /dev/null 2>&1; then
        echo -e "${GREEN}✓${NC} PASS: $name (error detected)"
        PASSED_TESTS=$((PASSED_TESTS + 1))
        return 0
    else
        echo -e "${RED}✗${NC} FAIL: $name (expected error, but passed)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
        return 1
    fi
}

echo -e "${BLUE}Testing Valid AOA Files${NC}"
echo "--------------------------------------"

# Test all valid examples
for file in "$PROJECT_ROOT"/examples/*.aoa; do
    if [ -f "$file" ]; then
        test_valid "$file"
    fi
done

echo ""
echo -e "${BLUE}Testing Syntax Errors${NC}"
echo "--------------------------------------"

# Syntax error tests
test_error_any "$PROJECT_ROOT/examples/examples_with_errors/syntax_unclosed_bracket.aoa"
test_error_any "$PROJECT_ROOT/examples/examples_with_errors/syntax_missing_bracket_in_expr.aoa"
test_error_any "$PROJECT_ROOT/examples/examples_with_errors/syntax_invalid_identifier.aoa"
test_error_any "$PROJECT_ROOT/examples/examples_with_errors/syntax_missing_operator.aoa"
test_error_any "$PROJECT_ROOT/examples/examples_with_errors/syntax_double_equals_alone.aoa"

echo ""
echo -e "${BLUE}Testing Semantic Errors${NC}"
echo "--------------------------------------"

# Semantic error tests with specific error checking
test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_undeclared_variable.aoa" \
    "not declared or assigned yet"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_array_without_index.aoa" \
    "used without index"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_scalar_with_index.aoa" \
    "cannot be indexed"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_array_out_of_bounds.aoa" \
    "out of bounds"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_assign_to_array_element.aoa" \
    "Cannot assign to individual array element"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_use_gate_before_assignment.aoa" \
    "not declared or assigned yet"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_multiple_assignment.aoa" \
    "already assigned"

test_error "$PROJECT_ROOT/examples/examples_with_errors/semantic_declaration_after_constraint.aoa" \
    "Declaration after constraints"

echo ""
echo "======================================"
echo "  Test Results"
echo "======================================"
echo -e "Total:  $TOTAL_TESTS"
echo -e "${GREEN}Passed: $PASSED_TESTS${NC}"
if [ $FAILED_TESTS -gt 0 ]; then
    echo -e "${RED}Failed: $FAILED_TESTS${NC}"
else
    echo -e "Failed: $FAILED_TESTS"
fi
echo "======================================"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
