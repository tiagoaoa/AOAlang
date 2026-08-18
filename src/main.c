/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     main.c
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include "symbol_table.h"
#include "error.h"
#include "r1cs.h"

static void print_usage(const char *prog_name);
static char *get_circuit_name(const char *filename);
static char *get_default_output(const char *input_file);
static int emit_output(const char *filename);

extern int yyparse(void);
extern FILE *yyin;
extern int error_count;
extern int generate_r1cs;


/* ----------------------------
        GLOBAL VARIABLES       */

static int verbose = 0;
static int generate = 0;
static int dense_output = 0;
static int qap_output = 0;
static int c_checker_output = 0;
static char *output_file = NULL;

/*----------------------------*/


static void print_usage(const char *prog_name) {
	fprintf(stderr, "Usage: %s [OPTIONS] FILE\n", prog_name);
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -v              Verbose output (show symbol table)\n");
	fprintf(stderr, "  -g, --generate  Generate R1CS JSON output\n");
	fprintf(stderr, "  -d, --dense     Generate dense R1CS matrix output (.r1cs)\n");
	fprintf(stderr, "  -q, --qap       Generate QAP polynomial output (.qap)\n");
	fprintf(stderr, "  -c, --checker   Generate C sanity checker (.c)\n");
	fprintf(stderr, "  -o FILE         Output file (default: <input>.<ext>)\n");
	fprintf(stderr, "  -h, --help      Show this help message\n");
	fprintf(stderr, "\nValidates AOA (.aoa) constraint files and optionally generates output.\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "  %s circuit.aoa              # Validate only\n", prog_name);
	fprintf(stderr, "  %s -g circuit.aoa           # Generate R1CS JSON\n", prog_name);
	fprintf(stderr, "  %s -d circuit.aoa           # Generate dense R1CS\n", prog_name);
	fprintf(stderr, "  %s -q circuit.aoa           # Generate QAP polynomials\n", prog_name);
	fprintf(stderr, "  %s -c circuit.aoa           # Generate C checker\n", prog_name);
	fprintf(stderr, "  %s -v examples/quadratic.aoa\n", prog_name);
}

//circuit name is the input filename without path and .aoa extension
static char *get_circuit_name(const char *filename) {
	char *copy = strdup(filename);	//basename may modify its argument
	char *base = basename(copy);
	char *dot = strrchr(base, '.');
	char *result;

	if (dot && strcmp(dot, ".aoa") == 0)
		*dot = '\0';

	result = strdup(base);
	free(copy);
	return result;
}

static char *get_default_output(const char *input_file) {
	size_t len = strlen(input_file);
	const char *ext = c_checker_output ? "_checker.c" :
	                  qap_output ? ".qap" :
	                  dense_output ? ".r1cs" : ".r1cs.json";
	char *output = malloc(len + 16);	//room for _checker.c plus terminator

	if (len > 4 && strcmp(input_file + len - 4, ".aoa") == 0) {
		strncpy(output, input_file, len - 4);
		output[len - 4] = '\0';
		strcat(output, ext);
	} else {
		strcpy(output, input_file);
		strcat(output, ext);
	}

	return output;
}

static int emit_output(const char *filename) {
	FILE *out;
	char *circuit_name;

	if (!output_file)
		output_file = get_default_output(filename);

	if (!(out = fopen(output_file, "w"))) {
		fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
		return(1);
	}

	circuit_name = get_circuit_name(filename);

	if (c_checker_output) {
		r1cs_generate_c_checker(out, circuit_name);
		printf("Generated C checker: %s\n", output_file);
	} else if (qap_output) {
		r1cs_generate_qap(out);
		printf("Generated QAP: %s\n", output_file);
	} else if (dense_output) {
		r1cs_generate_dense(out);
		printf("Generated R1CS dense: %s\n", output_file);
	} else {
		r1cs_generate_json(out, circuit_name);
		printf("Generated R1CS JSON: %s\n", output_file);
	}
	fclose(out);

	free(circuit_name);
	printf("  Witnesses: %d\n", r1cs.n_witnesses);
	printf("  Constraints: %d\n", r1cs.n_constraints);
	printf("  Public inputs: %d\n", r1cs.n_public_inputs);

	if (verbose)
		r1cs_print();

	return 0;
}

int main(int argc, char **argv) {
	const char *filename = NULL;
	int opt, i, parse_result, total_errors;

	while ((opt = getopt(argc, argv, "vgdqcho:")) != -1) {
		switch (opt) {
			case 'v':
				verbose = 1;
				break;
			case 'g':
				generate = 1;
				break;
			case 'd':
				generate = 1;
				dense_output = 1;
				break;
			case 'q':
				generate = 1;
				qap_output = 1;
				break;
			case 'c':
				generate = 1;
				c_checker_output = 1;
				break;
			case 'o':
				output_file = strdup(optarg);
				break;
			case 'h':
				print_usage(argv[0]);
				return 0;
			default:
				print_usage(argv[0]);
				return(1);
		}
	}

	//long options, checked by hand since getopt only handles the short ones
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--generate") == 0) {
			generate = 1;
		} else if (strcmp(argv[i], "--dense") == 0) {
			generate = 1;
			dense_output = 1;
		} else if (strcmp(argv[i], "--qap") == 0) {
			generate = 1;
			qap_output = 1;
		} else if (strcmp(argv[i], "--checker") == 0) {
			generate = 1;
			c_checker_output = 1;
		} else if (strcmp(argv[i], "--help") == 0) {
			print_usage(argv[0]);
			return 0;
		}
	}

	for (i = optind; i < argc; i++)
		if (argv[i][0] != '-') {
			filename = argv[i];
			break;
		}

	if (!filename) {
		fprintf(stderr, "Error: No input file specified\n\n");
		print_usage(argv[0]);
		return(1);
	}

	if (!(yyin = fopen(filename, "r"))) {
		fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
		return(1);
	}

	if (generate)
		printf("Compiling: %s\n", filename);
	else
		printf("Validating: %s\n", filename);

	symbol_table_init();
	error_reset();
	error_count = 0;

	if (generate) {
		generate_r1cs = 1;
		r1cs_init();
		r1cs_add_constant_one();
	}

	parse_result = yyparse();
	fclose(yyin);

	if (verbose)
		symbol_table_print();

	total_errors = error_count + error_get_count();

	if (parse_result == 0 && total_errors == 0) {
		if (generate) {
			if (emit_output(filename)) {
				symbol_table_free();
				r1cs_free();
				return(1);
			}
			r1cs_free();
		} else {
			printf("Validation successful - %s is valid AOA\n", filename);
		}

		symbol_table_free();
		free(output_file);
		return 0;
	} else {
		printf("Compilation failed - %d error(s) found\n", total_errors);
		symbol_table_free();
		if (generate)
			r1cs_free();
		free(output_file);
		return(1);
	}
}
