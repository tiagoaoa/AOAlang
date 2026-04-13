/*
 * circom2aoa - Transpile Circom circuits to AOAlang (.aoa)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
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

static int file_exists(const char *path) {
    return access(path, R_OK) == 0;
}

static char *xstrdup(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static char *path_join(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int needs_slash = dlen > 0 && dir[dlen - 1] != '/';
    char *path = malloc(dlen + needs_slash + nlen + 1);
    if (!path) return NULL;
    memcpy(path, dir, dlen);
    if (needs_slash) path[dlen++] = '/';
    memcpy(path + dlen, name, nlen);
    path[dlen + nlen] = '\0';
    return path;
}

static char *dirname_dup(const char *path) {
    char *tmp = xstrdup(path);
    if (!tmp) return NULL;
    char *dir = dirname(tmp);
    char *result = xstrdup(dir);
    free(tmp);
    return result;
}

static int append_text(char **buffer, size_t *len, size_t *cap, const char *text) {
    size_t add = strlen(text);
    if (*len + add + 1 > *cap) {
        size_t new_cap = *cap ? *cap : 1024;
        while (*len + add + 1 > new_cap) new_cap *= 2;
        char *grown = realloc(*buffer, new_cap);
        if (!grown) return -1;
        *buffer = grown;
        *cap = new_cap;
    }
    memcpy(*buffer + *len, text, add);
    *len += add;
    (*buffer)[*len] = '\0';
    return 0;
}

static int parse_include_line(const char *line, char *include_name, size_t include_cap) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "include", 7) != 0) return 0;
    p += 7;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return 0;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return 0;
    size_t len = (size_t)(end - p);
    if (len + 1 > include_cap) return -1;
    memcpy(include_name, p, len);
    include_name[len] = '\0';
    end++;
    while (*end == ' ' || *end == '\t') end++;
    if (*end != ';') return 0;
    end++;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
    return *end == '\0' ? 1 : 0;
}

static char *resolve_include_path(const char *current_dir, const char *lib_dir, const char *include_name) {
    const char *dirs[2] = { current_dir, lib_dir };
    for (int i = 0; i < 2; i++) {
        const char *dir = dirs[i];
        if (!dir || !*dir) continue;

        char *candidate = path_join(dir, include_name);
        if (!candidate) return NULL;
        if (file_exists(candidate)) return candidate;
        free(candidate);

        size_t len = strlen(include_name);
        if (len < 7 || strcmp(include_name + len - 7, ".circom") != 0) {
            char *with_ext = malloc(len + 8);
            if (!with_ext) return NULL;
            memcpy(with_ext, include_name, len);
            memcpy(with_ext + len, ".circom", 8);
            candidate = path_join(dir, with_ext);
            free(with_ext);
            if (!candidate) return NULL;
            if (file_exists(candidate)) return candidate;
            free(candidate);
        }
    }
    return NULL;
}

static char *detect_default_lib_dir(const char *argv0) {
    char resolved[PATH_MAX];
    if (!realpath(argv0, resolved)) return NULL;

    char *bin_path = xstrdup(resolved);
    if (!bin_path) return NULL;
    char *bin_dir = dirname(bin_path);
    char *parent_path = xstrdup(bin_dir);
    free(bin_path);
    if (!parent_path) return NULL;
    char *parent_dir = dirname(parent_path);
    char *lib_dir = path_join(parent_dir, "circom2aoa/lib");
    free(parent_path);
    return lib_dir;
}

static char *expand_includes_recursive(const char *path, const char *lib_dir, int keep_pragma, int depth) {
    if (depth > 32) {
        fprintf(stderr, "Error: include nesting too deep near '%s'\n", path);
        return NULL;
    }

    char *source = read_file(path);
    if (!source) {
        fprintf(stderr, "Error: Cannot open include file '%s'\n", path);
        return NULL;
    }

    char *current_dir = dirname_dup(path);
    if (!current_dir) {
        free(source);
        return NULL;
    }

    char *output = NULL;
    size_t out_len = 0, out_cap = 0;
    char *cursor = source;

    while (*cursor) {
        char *line_end = strchr(cursor, '\n');
        size_t line_len = line_end ? (size_t)(line_end - cursor + 1) : strlen(cursor);
        char *line = malloc(line_len + 1);
        if (!line) goto fail;
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';

        char include_name[512];
        int include_rc = parse_include_line(line, include_name, sizeof(include_name));
        if (include_rc == 1) {
            if (strcmp(include_name, "poseidon") == 0) {
                if (append_text(&output, &out_len, &out_cap, line) != 0) {
                    free(line);
                    goto fail;
                }
            } else {
                char *include_path = resolve_include_path(current_dir, lib_dir, include_name);
                if (!include_path) {
                    fprintf(stderr, "Error: Cannot resolve include '%s' from '%s'\n", include_name, path);
                    free(line);
                    goto fail;
                }
                char *expanded = expand_includes_recursive(include_path, lib_dir, 0, depth + 1);
                free(include_path);
                if (!expanded) {
                    free(line);
                    goto fail;
                }
                if (append_text(&output, &out_len, &out_cap, expanded) != 0) {
                    free(expanded);
                    free(line);
                    goto fail;
                }
                free(expanded);
            }
        } else if (include_rc == 0) {
            const char *trim = line;
            while (*trim == ' ' || *trim == '\t') trim++;
            if (!keep_pragma && strncmp(trim, "pragma circom", 13) == 0) {
                /* Skip duplicate pragma lines from included files. */
            } else if (append_text(&output, &out_len, &out_cap, line) != 0) {
                free(line);
                goto fail;
            }
        } else {
            fprintf(stderr, "Error: include path too long in '%s'\n", path);
            free(line);
            goto fail;
        }

        free(line);
        if (!line_end) break;
        cursor = line_end + 1;
    }

    free(current_dir);
    free(source);
    return output;

fail:
    free(output);
    free(current_dir);
    free(source);
    return NULL;
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
    char *lib_dir = NULL;

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
    lib_dir = detect_default_lib_dir(argv[0]);
    char *source = expand_includes_recursive(filename, lib_dir, 1, 0);
    if (!source) {
        fprintf(stderr, "Error: Cannot process file '%s'\n", filename);
        free(lib_dir);
        return 1;
    }

    /* Lexer */
    if (verbose) fprintf(stderr, "[lexer] Tokenizing %s...\n", filename);
    lexer_t lex;
    lexer_init(&lex, source);
    if (lexer_tokenize(&lex)) {
        fprintf(stderr, "Lexer failed\n");
        free(source);
        free(lib_dir);
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
        free(lib_dir);
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
        free(source); free(lib_dir); lexer_free(&lex); program_free(&prog);
        return 1;
    }
    flattener_init(flat, &prog, verbose);
    if (flattener_run(flat)) {
        fprintf(stderr, "Flattener failed\n");
        free(source); free(lib_dir); lexer_free(&lex); program_free(&prog); free(flat);
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
        free(source); free(lib_dir); lexer_free(&lex); program_free(&prog);
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
            free(source); free(lib_dir); lexer_free(&lex); program_free(&prog);
            free(flat); free(output_file);
            return 1;
        }
        fprintf(stderr, "Validation passed\n");
    }

    /* Cleanup */
    free(source);
    free(lib_dir);
    lexer_free(&lex);
    program_free(&prog);
    free(flat);
    free(output_file);
    return 0;
}
