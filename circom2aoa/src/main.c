/*
 * circom2aoa - Transpile Circom circuits to AOAlang (.aoa)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include "lexer.h"
#include "parser.h"
#include "flattener.h"
#include "emitter.h"

static int verbose = 0;
static int validate = 0;
static char *output_file = NULL;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTIONS] FILE.circom\n", prog);
    fprintf(stderr, "\nTranspile Circom circuits to AOAlang (.aoa)\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o FILE      Output file (default: FILE.aoa)\n");
    fprintf(stderr, "  -v           Verbose (show flattening steps)\n");
    fprintf(stderr, "  --validate   Run aoac on output to verify validity\n");
    fprintf(stderr, "  -h, --help   Show this help message\n");
}

/* Read entire file into a malloc'd string */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

/* Generate default output filename (.circom → .aoa) */
static char *default_output(const char *input) {
    size_t len = strlen(input);
    char *out = malloc(len + 8);
    strcpy(out, input);
    /* Replace .circom with .aoa */
    if (len > 7 && strcmp(input + len - 7, ".circom") == 0) {
        strcpy(out + len - 7, ".aoa");
    } else {
        strcat(out, ".aoa");
    }
    return out;
}

int main(int argc, char **argv) {
    const char *filename = NULL;

    /* Parse args */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0) { verbose = 1; continue; }
        if (strcmp(argv[i], "--validate") == 0) { validate = 1; continue; }
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = strdup(argv[++i]);
            continue;
        }
        if (argv[i][0] != '-') {
            filename = argv[i];
            continue;
        }
        fprintf(stderr, "Unknown option: %s\n", argv[i]);
        print_usage(argv[0]);
        return 1;
    }

    if (!filename) {
        fprintf(stderr, "Error: No input file specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Read source */
    char *source = read_file(filename);
    if (!source) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return 1;
    }

    /* Lexer */
    if (verbose) fprintf(stderr, "[lexer] Tokenizing %s...\n", filename);
    lexer_t lex;
    lexer_init(&lex, source);
    if (lexer_tokenize(&lex)) {
        fprintf(stderr, "Lexer failed\n");
        free(source);
        lexer_free(&lex);
        return 1;
    }
    if (verbose) fprintf(stderr, "[lexer] %d tokens\n", lex.ntokens);

    /* Parser */
    if (verbose) fprintf(stderr, "[parser] Parsing...\n");
    parser_t parser;
    parser_init(&parser, lex.tokens, lex.ntokens);
    program_t prog;
    if (parser_parse(&parser, &prog)) {
        fprintf(stderr, "Parser failed\n");
        free(source);
        lexer_free(&lex);
        return 1;
    }
    if (verbose) {
        fprintf(stderr, "[parser] %d templates", prog.ntemplates);
        if (prog.main_comp)
            fprintf(stderr, ", main = %s(%d args), %d public inputs",
                    prog.main_comp->template_name, prog.main_comp->nargs, prog.main_comp->npublic);
        fprintf(stderr, "\n");
    }

    /* Flattener (heap-allocated — struct is too large for stack) */
    if (verbose) fprintf(stderr, "[flattener] Flattening...\n");
    flattener_t *flat = calloc(1, sizeof(flattener_t));
    if (!flat) {
        fprintf(stderr, "Error: Cannot allocate flattener\n");
        free(source); lexer_free(&lex); program_free(&prog);
        return 1;
    }
    flattener_init(flat, &prog, verbose);
    if (flattener_run(flat)) {
        fprintf(stderr, "Flattener failed\n");
        free(source); lexer_free(&lex); program_free(&prog); free(flat);
        return 1;
    }
    if (verbose)
        fprintf(stderr, "[flattener] %d operations, %d signals\n", flat->nops, flat->nsignals);

    /* Emitter */
    if (!output_file)
        output_file = default_output(filename);

    FILE *out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot create output file '%s'\n", output_file);
        free(source); lexer_free(&lex); program_free(&prog);
        free(flat);
        return 1;
    }

    emitter_emit(out, flat);
    fclose(out);
    fprintf(stderr, "Wrote %s\n", output_file);

    /* Validate with aoac */
    if (validate) {
        /* Find aoac relative to this binary */
        char cmd[1024];
        char *dir = strdup(argv[0]);
        char *base = dirname(dir);
        snprintf(cmd, sizeof(cmd), "%s/aoac %s", base, output_file);
        free(dir);
        int rc = system(cmd);
        if (rc != 0) {
            fprintf(stderr, "Validation FAILED\n");
            free(source); lexer_free(&lex); program_free(&prog);
            free(flat); free(output_file);
            return 1;
        }
        fprintf(stderr, "Validation passed\n");
    }

    /* Cleanup */
    free(source);
    lexer_free(&lex);
    program_free(&prog);
    free(flat);
    free(output_file);
    return 0;
}
