/*
 * Error Reporting Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "error.h"

static int total_errors = 0;

void error_report(int line, const char *format, ...) {
    va_list args;
    fprintf(stderr, "Error (line %d): ", line);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    total_errors++;
}

int error_get_count(void) {
    return total_errors;
}

void error_reset(void) {
    total_errors = 0;
}
