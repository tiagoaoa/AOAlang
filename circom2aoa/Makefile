# Makefile for circom2aoa transpiler

CC = gcc
CFLAGS = -Wall -Wextra -g -O2
LDFLAGS =

SRCDIR = src
BINDIR = ../bin
TESTDIR = tests

TARGET = $(BINDIR)/circom2aoa

SOURCES = $(SRCDIR)/main.c $(SRCDIR)/lexer.c $(SRCDIR)/ast.c $(SRCDIR)/parser.c \
          $(SRCDIR)/flattener.c $(SRCDIR)/emitter.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(BINDIR):
	mkdir -p $(BINDIR)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET): $(BINDIR) $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS)
	@echo ""
	@echo "Build successful!"
	@echo "Binary: $(TARGET)"

test: $(TARGET)
	@echo "Running circom2aoa tests..."
	@pass=0; fail=0; \
	for f in $(TESTDIR)/*.circom; do \
		name=$$(basename "$$f" .circom); \
		echo -n "  $$name ... "; \
		$(TARGET) "$$f" -o /tmp/$$name.aoa > /dev/null 2>&1; \
		if [ $$? -ne 0 ]; then \
			echo "FAIL (transpile)"; \
			fail=$$((fail + 1)); \
			continue; \
		fi; \
		if [ -f "$(TESTDIR)/expected/$$name.aoa" ]; then \
			diff -u "$(TESTDIR)/expected/$$name.aoa" "/tmp/$$name.aoa" > /dev/null 2>&1; \
			if [ $$? -ne 0 ]; then \
				echo "FAIL (diff)"; \
				diff -u "$(TESTDIR)/expected/$$name.aoa" "/tmp/$$name.aoa"; \
				fail=$$((fail + 1)); \
				continue; \
			fi; \
		fi; \
		$(BINDIR)/aoac "/tmp/$$name.aoa" > /dev/null 2>&1; \
		if [ $$? -ne 0 ]; then \
			echo "FAIL (aoac validate)"; \
			$(BINDIR)/aoac "/tmp/$$name.aoa"; \
			fail=$$((fail + 1)); \
			continue; \
		fi; \
		echo "PASS"; \
		pass=$$((pass + 1)); \
	done; \
	echo ""; \
	echo "Results: $$pass passed, $$fail failed"; \
	if [ $$fail -ne 0 ]; then exit 1; fi

clean:
	rm -f $(OBJECTS)

distclean: clean
	rm -f $(TARGET)

.PHONY: all test clean distclean
