/*
 * Recursive Descent Parser for Circom
 */

#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    token_t *tokens;
    int ntokens;
    int pos;
} parser_t;

void parser_init(parser_t *p, token_t *tokens, int ntokens);
int  parser_parse(parser_t *p, program_t *prog);  /* returns 0 on success */

#endif /* PARSER_H */
