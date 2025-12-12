/*
 * AOA Compiler Main Entry Point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include "symbol_table.h"
#include "error.h"
#include "r1cs.h"

extern int yyparse(void);
extern FILE *yyin;
extern int yylineno;
extern int error_count;
extern int generate_r1cs;

static int verbose = 0;
static int generate = 0;
static char *output_file = NULL;

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] FILE\n", prog_name);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -v              Verbose output (show symbol table)\n");
    fprintf(stderr, "  -g, --generate  Generate R1CS JSON output\n");
    fprintf(stderr, "  -o FILE         Output file (default: <input>.r1cs.json)\n");
    fprintf(stderr, "  -h, --help      Show this help message\n");
    fprintf(stderr, "\nValidates AOA (.aoa) constraint files and optionally generates R1CS JSON.\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s circuit.aoa              # Validate only\n", prog_name);
    fprintf(stderr, "  %s -g circuit.aoa           # Validate and generate JSON\n", prog_name);
    fprintf(stderr, "  %s -g -o out.json circuit.aoa\n", prog_name);
    fprintf(stderr, "  %s -v examples/quadratic.aoa\n", prog_name);
}

/* Extract circuit name from filename (without path and extension) */
char *get_circuit_name(const char *filename) {
    /* Make a copy since basename may modify the string */
    char *copy = strdup(filename);
    char *base = basename(copy);

    /* Remove .aoa extension if present */
    char *dot = strrchr(base, '.');
    if (dot && strcmp(dot, ".aoa") == 0) {
        *dot = '\0';
    }

    char *result = strdup(base);
    free(copy);
    return result;
}

/* Generate default output filename */
char *get_default_output(const char *input_file) {
    size_t len = strlen(input_file);
    char *output = malloc(len + 12);  /* .r1cs.json + null */

    /* Check if input ends with .aoa */
    if (len > 4 && strcmp(input_file + len - 4, ".aoa") == 0) {
        strncpy(output, input_file, len - 4);
        output[len - 4] = '\0';
        strcat(output, ".r1cs.json");
    } else {
        strcpy(output, input_file);
        strcat(output, ".r1cs.json");
    }

    return output;
}

int main(int argc, char **argv) {
    int opt;
    const char *filename = NULL;

    /* Parse command-line options */
    while ((opt = getopt(argc, argv, "vgho:")) != -1) {
        switch (opt) {
            case 'v':
                verbose = 1;
                break;
            case 'g':
                generate = 1;
                break;
            case 'o':
                output_file = strdup(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Check for --generate and --help long options */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--generate") == 0) {
            generate = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    /* Find input file (first non-option argument) */
    for (int i = optind; i < argc; i++) {
        if (argv[i][0] != '-') {
            filename = argv[i];
            break;
        }
    }

    /* Check for input file */
    if (!filename) {
        fprintf(stderr, "Error: No input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Open input file */
    yyin = fopen(filename, "r");
    if (!yyin) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return 1;
    }

    if (generate) {
        printf("Compiling: %s\n", filename);
    } else {
        printf("Validating: %s\n", filename);
    }

    /* Initialize symbol table and error tracking */
    symbol_table_init();
    error_reset();
    error_count = 0;

    /* Initialize R1CS if generating */
    if (generate) {
        generate_r1cs = 1;
        r1cs_init();
        r1cs_add_constant_one();
    }

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
        if (generate) {
            /* Generate output file */
            if (!output_file) {
                output_file = get_default_output(filename);
            }

            FILE *out = fopen(output_file, "w");
            if (!out) {
                fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
                symbol_table_free();
                r1cs_free();
                return 1;
            }

            char *circuit_name = get_circuit_name(filename);
            r1cs_generate_json(out, circuit_name);
            fclose(out);

            printf("Generated R1CS JSON: %s\n", output_file);
            printf("  Witnesses: %d\n", r1cs.n_witnesses);
            printf("  Constraints: %d\n", r1cs.n_constraints);
            printf("  Public inputs: %d\n", r1cs.n_public_inputs);

            if (verbose) {
                r1cs_print();
            }

            free(circuit_name);
            r1cs_free();
        } else {
            printf("Validation successful - %s is valid AOA\n", filename);
        }

        symbol_table_free();
        if (output_file) free(output_file);
        return 0;
    } else {
        printf("Compilation failed - %d error(s) found\n", total_errors);
        symbol_table_free();
        if (generate) r1cs_free();
        if (output_file) free(output_file);
        return 1;
    }
}
