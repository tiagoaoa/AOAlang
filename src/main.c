/*
 * AOA Compiler Main Entry Point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "symbol_table.h"
#include "error.h"

extern int yyparse(void);
extern FILE *yyin;
extern int yylineno;
extern int error_count;

static int verbose = 0;

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] FILE\n", prog_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -v         Verbose output (show symbol table)\n");
    fprintf(stderr, "  -h         Show this help message\n");
    fprintf(stderr, "\nValidates AOA (.aoa) constraint files.\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s circuit.aoa\n", prog_name);
    fprintf(stderr, "  %s -v examples/quadratic.aoa\n", prog_name);
}

int main(int argc, char **argv) {
    int opt;
    const char *filename = NULL;

    /* Parse command-line options */
    while ((opt = getopt(argc, argv, "vh")) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Check for input file */
    if (optind >= argc) {
        fprintf(stderr, "Error: No input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    filename = argv[optind];

    /* Open input file */
    yyin = fopen(filename, "r");
    if (!yyin) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return 1;
    }

    printf("Validating: %s\n", filename);

    /* Initialize symbol table and error tracking */
    symbol_table_init();
    error_reset();
    error_count = 0;

    /* Parse the input file */
    int parse_result = yyparse();

    /* Close input file */
    fclose(yyin);

    /* Print symbol table if verbose */
    if (verbose) {
        symbol_table_print();
    }

    /* Report results */
    int total_errors = error_count + error_get_count();

    if (parse_result == 0 && total_errors == 0) {
        printf("✓ Validation successful - %s is valid AOA\n", filename);
        symbol_table_free();
        return 0;
    } else {
        printf("✗ Validation failed - %d error(s) found\n", total_errors);
        symbol_table_free();
        return 1;
    }
}
