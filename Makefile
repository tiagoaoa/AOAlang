# Makefile for AOAlang Parser

# Compiler and tools
CC = gcc
LEX = flex
YACC = bison
CFLAGS = -Wall -Wextra -g -O2
LDFLAGS =

# Directories
SRCDIR = src
BINDIR = bin
EXAMPLESDIR = examples

# Target binary
TARGET = $(BINDIR)/aoac

# Source files
YACC_SRC = $(SRCDIR)/aoa.y
LEX_SRC = $(SRCDIR)/aoa.l
C_SOURCES = $(SRCDIR)/main.c $(SRCDIR)/symbol_table.c $(SRCDIR)/error.c $(SRCDIR)/r1cs.c

# Generated files
YACC_C = $(SRCDIR)/aoa.tab.c
YACC_H = $(SRCDIR)/aoa.tab.h
LEX_C = $(SRCDIR)/lex.yy.c

# Object files
OBJECTS = $(SRCDIR)/aoa.tab.o $(SRCDIR)/lex.yy.o \
          $(SRCDIR)/main.o $(SRCDIR)/symbol_table.o $(SRCDIR)/error.o $(SRCDIR)/r1cs.o

# Default target
all: $(TARGET)

# Create bin directory if it doesn't exist
$(BINDIR):
	mkdir -p $(BINDIR)

# Generate parser from yacc specification
$(YACC_C) $(YACC_H): $(YACC_SRC)
	$(YACC) -d -o $(YACC_C) $(YACC_SRC)

# Generate lexer from lex specification
$(LEX_C): $(LEX_SRC) $(YACC_H)
	$(LEX) -o $(LEX_C) $(LEX_SRC)

# Compile generated parser
$(SRCDIR)/aoa.tab.o: $(YACC_C)
	$(CC) $(CFLAGS) -c -o $@ $(YACC_C)

# Compile generated lexer
$(SRCDIR)/lex.yy.o: $(LEX_C)
	$(CC) $(CFLAGS) -c -o $@ $(LEX_C)

# Compile other source files
$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Link everything together
$(TARGET): $(BINDIR) $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "Build successful!"
	@echo "Binary: $(TARGET)"
	@echo ""
	@echo "Usage:"
	@echo "  $(TARGET) <file.aoa>              # Validate only"
	@echo "  $(TARGET) -g <file.aoa>           # Generate R1CS JSON"
	@echo "  $(TARGET) -g -o out.json <file.aoa>"
	@echo ""
	@echo "Example: $(TARGET) -g examples/simple_quad.aoa"
	@echo ""

# Run quick validation tests on valid example files only
test: $(TARGET)
	@echo "Running quick validation tests on valid examples..."
	@test_failed=0; \
	for file in $(EXAMPLESDIR)/*.aoa; do \
		echo "Testing $$file..."; \
		$(TARGET) "$$file" || test_failed=$$((test_failed + 1)); \
		echo ""; \
	done; \
	if [ $$test_failed -eq 0 ]; then \
		echo "✓ All tests passed!"; \
	else \
		echo "✗ $$test_failed test(s) failed"; \
		exit 1; \
	fi

# Run comprehensive test suite (valid + error tests)
test-all: $(TARGET)
	@./tests/run_tests.sh

# Run error detection tests only
test-errors: $(TARGET)
	@echo "Running error detection tests..."
	@test_failed=0; \
	for file in $(EXAMPLESDIR)/examples_with_errors/*.aoa; do \
		if [ -f "$$file" ]; then \
			echo "Testing $$(basename $$file)..."; \
			if $(TARGET) "$$file" > /dev/null 2>&1; then \
				echo "  FAIL: Expected error but passed"; \
				test_failed=$$((test_failed + 1)); \
			else \
				echo "  PASS: Error detected"; \
			fi; \
		fi; \
	done; \
	if [ $$test_failed -eq 0 ]; then \
		echo "All error tests passed!"; \
	else \
		echo "$$test_failed test(s) failed"; \
		exit 1; \
	fi

# Test R1CS JSON generation
test-generate: $(TARGET)
	@echo "Testing R1CS JSON generation..."
	@test_failed=0; \
	for file in $(EXAMPLESDIR)/*.aoa; do \
		echo "Generating R1CS for $$file..."; \
		$(TARGET) -g "$$file" || test_failed=$$((test_failed + 1)); \
		echo ""; \
	done; \
	if [ $$test_failed -eq 0 ]; then \
		echo "All generation tests passed!"; \
	else \
		echo "$$test_failed test(s) failed"; \
		exit 1; \
	fi

# Install to system (requires root)
install: $(TARGET)
	install -d /usr/local/bin
	install -m 755 $(TARGET) /usr/local/bin/aoac
	@echo "Installed to /usr/local/bin/aoac"

# Uninstall from system
uninstall:
	rm -f /usr/local/bin/aoac
	@echo "Uninstalled aoac"

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(YACC_C) $(YACC_H) $(LEX_C)
	rm -f $(SRCDIR)/*.o
	@echo "Cleaned build artifacts"

# Clean everything including binary
distclean: clean
	rm -f $(TARGET)
	rm -rf $(BINDIR)
	@echo "Cleaned everything"

# Show help
help:
	@echo "AOAlang Compiler Makefile"
	@echo ""
	@echo "Build Targets:"
	@echo "  all           - Build the compiler (default)"
	@echo "  clean         - Remove build artifacts"
	@echo "  distclean     - Remove everything including binary"
	@echo ""
	@echo "Test Targets:"
	@echo "  test          - Quick test: validate all valid examples"
	@echo "  test-all      - Comprehensive test suite (valid + error tests)"
	@echo "  test-errors   - Test error detection only"
	@echo "  test-generate - Test R1CS JSON generation for all examples"
	@echo ""
	@echo "Install Targets:"
	@echo "  install       - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall     - Remove from /usr/local/bin"
	@echo ""
	@echo "Other Targets:"
	@echo "  help          - Show this help message"

.PHONY: all test test-all test-errors test-generate install uninstall clean distclean help
