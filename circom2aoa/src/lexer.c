/*
 * Lexer for Circom - Hand-written tokenizer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

static void add_token(lexer_t *lex, token_type_t type, const char *val, int line, int col) {
    if (lex->ntokens >= lex->capacity) {
        lex->capacity *= 2;
        lex->tokens = realloc(lex->tokens, lex->capacity * sizeof(token_t));
    }
    token_t *t = &lex->tokens[lex->ntokens++];
    t->type = type;
    t->value = strdup(val);
    t->line = line;
    t->col = col;
}

static char peek(lexer_t *lex) {
    if (lex->pos >= lex->len) return '\0';
    return lex->source[lex->pos];
}

static char peek2(lexer_t *lex) {
    if (lex->pos + 1 >= lex->len) return '\0';
    return lex->source[lex->pos + 1];
}

static char peek3(lexer_t *lex) {
    if (lex->pos + 2 >= lex->len) return '\0';
    return lex->source[lex->pos + 2];
}

static char advance_char(lexer_t *lex) {
    char ch = lex->source[lex->pos++];
    if (ch == '\n') { lex->line++; lex->col = 1; }
    else { lex->col++; }
    return ch;
}

/* Try to match and emit a 3-char token */
static int try3(lexer_t *lex, const char *s, token_type_t type) {
    if (lex->pos + 2 < lex->len &&
        lex->source[lex->pos] == s[0] &&
        lex->source[lex->pos+1] == s[1] &&
        lex->source[lex->pos+2] == s[2]) {
        int line = lex->line, col = lex->col;
        char buf[4] = { s[0], s[1], s[2], '\0' };
        lex->pos += 3; lex->col += 3;
        add_token(lex, type, buf, line, col);
        return 1;
    }
    return 0;
}

/* Try to match and emit a 2-char token */
static int try2(lexer_t *lex, const char *s, token_type_t type) {
    if (lex->pos + 1 < lex->len &&
        lex->source[lex->pos] == s[0] &&
        lex->source[lex->pos+1] == s[1]) {
        int line = lex->line, col = lex->col;
        char buf[3] = { s[0], s[1], '\0' };
        lex->pos += 2; lex->col += 2;
        add_token(lex, type, buf, line, col);
        return 1;
    }
    return 0;
}

static token_type_t keyword_lookup(const char *word) {
    static const struct { const char *kw; token_type_t type; } kws[] = {
        {"pragma", TOK_PRAGMA}, {"circom", TOK_CIRCOM},
        {"include", TOK_INCLUDE}, {"template", TOK_TEMPLATE},
        {"component", TOK_COMPONENT}, {"main", TOK_MAIN},
        {"signal", TOK_SIGNAL}, {"input", TOK_INPUT},
        {"output", TOK_OUTPUT}, {"public", TOK_PUBLIC},
        {"private", TOK_PRIVATE}, {"var", TOK_VAR},
        {"if", TOK_IF}, {"else", TOK_ELSE},
        {"for", TOK_FOR}, {"while", TOK_WHILE},
        {"return", TOK_RETURN}, {"log", TOK_LOG},
        {"assert", TOK_ASSERT},
        {NULL, TOK_ERROR}
    };
    for (int i = 0; kws[i].kw; i++) {
        if (strcmp(word, kws[i].kw) == 0) return kws[i].type;
    }
    return TOK_IDENT;
}

void lexer_init(lexer_t *lex, const char *source) {
    lex->source = source;
    lex->pos = 0;
    lex->len = (int)strlen(source);
    lex->line = 1;
    lex->col = 1;
    lex->capacity = 1024;
    lex->tokens = malloc(lex->capacity * sizeof(token_t));
    lex->ntokens = 0;
}

int lexer_tokenize(lexer_t *lex) {
    while (lex->pos < lex->len) {
        char ch = peek(lex);

        /* Whitespace */
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            advance_char(lex);
            continue;
        }

        /* Newlines */
        if (ch == '\n') {
            advance_char(lex);
            continue;
        }

        /* Single-line comment */
        if (ch == '/' && peek2(lex) == '/') {
            lex->pos += 2; lex->col += 2;
            while (lex->pos < lex->len && peek(lex) != '\n')
                lex->pos++;
            continue;
        }

        /* Multi-line comment */
        if (ch == '/' && peek2(lex) == '*') {
            lex->pos += 2; lex->col += 2;
            while (lex->pos + 1 < lex->len) {
                if (peek(lex) == '*' && peek2(lex) == '/') {
                    lex->pos += 2; lex->col += 2;
                    goto next;
                }
                advance_char(lex);
            }
            fprintf(stderr, "Lexer error at line %d: Unterminated block comment\n", lex->line);
            return 1;
            next: continue;
        }

        int start_line = lex->line;
        int start_col = lex->col;

        /* Numbers */
        if (isdigit(ch)) {
            int start = lex->pos;
            while (lex->pos < lex->len && isdigit(peek(lex))) {
                lex->pos++; lex->col++;
            }
            if (lex->pos < lex->len && peek(lex) == '.' &&
                lex->pos + 1 < lex->len && isdigit(lex->source[lex->pos + 1])) {
                lex->pos++; lex->col++;
                while (lex->pos < lex->len && isdigit(peek(lex))) {
                    lex->pos++; lex->col++;
                }
            }
            int slen = lex->pos - start;
            char *buf = malloc(slen + 1);
            memcpy(buf, lex->source + start, slen);
            buf[slen] = '\0';
            add_token(lex, TOK_NUMBER, buf, start_line, start_col);
            free(buf);
            continue;
        }

        /* Identifiers / keywords */
        if (isalpha(ch) || ch == '_' || ch == '$') {
            int start = lex->pos;
            while (lex->pos < lex->len &&
                   (isalnum(peek(lex)) || peek(lex) == '_' || peek(lex) == '$')) {
                lex->pos++; lex->col++;
            }
            int slen = lex->pos - start;
            char *buf = malloc(slen + 1);
            memcpy(buf, lex->source + start, slen);
            buf[slen] = '\0';
            token_type_t tt = keyword_lookup(buf);
            add_token(lex, tt, buf, start_line, start_col);
            free(buf);
            continue;
        }

        /* String literals */
        if (ch == '"') {
            lex->pos++; lex->col++;
            int start = lex->pos;
            while (lex->pos < lex->len && peek(lex) != '"') {
                if (peek(lex) == '\\') { lex->pos++; lex->col++; }
                lex->pos++; lex->col++;
            }
            if (lex->pos >= lex->len) {
                fprintf(stderr, "Lexer error at line %d: Unterminated string\n", start_line);
                return 1;
            }
            int slen = lex->pos - start;
            char *buf = malloc(slen + 1);
            memcpy(buf, lex->source + start, slen);
            buf[slen] = '\0';
            add_token(lex, TOK_STRING, buf, start_line, start_col);
            free(buf);
            lex->pos++; lex->col++; /* skip closing quote */
            continue;
        }

        /* 3-char operators (must check before 2-char) */
        if (try3(lex, "<==", TOK_CONSTRAIN_ASSIGN)) continue;
        if (try3(lex, "==>", TOK_CONSTRAIN_ASSIGN_R)) continue;
        if (try3(lex, "<--", TOK_WITNESS_ASSIGN)) continue;
        if (try3(lex, "-->", TOK_WITNESS_ASSIGN_R)) continue;
        if (try3(lex, "===", TOK_CONSTRAIN_EQ)) continue;
        if (try3(lex, "**=", TOK_POWER_ASSIGN)) continue;
        if (try3(lex, "<<=", TOK_SHL_ASSIGN)) continue;
        if (try3(lex, ">>=", TOK_SHR_ASSIGN)) continue;

        /* 2-char operators */
        if (try2(lex, "++", TOK_INCREMENT)) continue;
        if (try2(lex, "--", TOK_DECREMENT)) continue;
        if (try2(lex, "**", TOK_POWER)) continue;
        if (try2(lex, "==", TOK_EQ)) continue;
        if (try2(lex, "!=", TOK_NEQ)) continue;
        if (try2(lex, "<=", TOK_LEQ)) continue;
        if (try2(lex, ">=", TOK_GEQ)) continue;
        if (try2(lex, "&&", TOK_AND)) continue;
        if (try2(lex, "||", TOK_OR)) continue;
        if (try2(lex, ">>", TOK_SHR)) continue;
        if (try2(lex, "<<", TOK_SHL)) continue;
        if (try2(lex, "+=", TOK_PLUS_ASSIGN)) continue;
        if (try2(lex, "-=", TOK_MINUS_ASSIGN)) continue;
        if (try2(lex, "*=", TOK_STAR_ASSIGN)) continue;
        if (try2(lex, "/=", TOK_SLASH_ASSIGN)) continue;
        if (try2(lex, "%=", TOK_PERCENT_ASSIGN)) continue;
        if (try2(lex, "&=", TOK_AND_ASSIGN)) continue;
        if (try2(lex, "|=", TOK_OR_ASSIGN)) continue;
        if (try2(lex, "^=", TOK_XOR_ASSIGN)) continue;
        if (try2(lex, "\\=", TOK_BACKSLASH_ASSIGN)) continue;

        /* Single-char tokens */
        {
            token_type_t tt = TOK_ERROR;
            char buf[2] = { ch, '\0' };
            switch (ch) {
                case '+': tt = TOK_PLUS; break;
                case '-': tt = TOK_MINUS; break;
                case '*': tt = TOK_STAR; break;
                case '/': tt = TOK_SLASH; break;
                case '%': tt = TOK_PERCENT; break;
                case '\\': tt = TOK_BACKSLASH; break;
                case '<': tt = TOK_LT; break;
                case '>': tt = TOK_GT; break;
                case '!': tt = TOK_NOT; break;
                case '&': tt = TOK_BIT_AND; break;
                case '|': tt = TOK_BIT_OR; break;
                case '^': tt = TOK_BIT_XOR; break;
                case '~': tt = TOK_BIT_NOT; break;
                case '=': tt = TOK_ASSIGN; break;
                case '(': tt = TOK_LPAREN; break;
                case ')': tt = TOK_RPAREN; break;
                case '{': tt = TOK_LBRACE; break;
                case '}': tt = TOK_RBRACE; break;
                case '[': tt = TOK_LBRACKET; break;
                case ']': tt = TOK_RBRACKET; break;
                case ',': tt = TOK_COMMA; break;
                case ';': tt = TOK_SEMICOLON; break;
                case '.': tt = TOK_DOT; break;
                case ':': tt = TOK_COLON; break;
                case '?': tt = TOK_QUESTION; break;
                default:
                    fprintf(stderr, "Lexer error at line %d, col %d: Unexpected character '%c'\n",
                            lex->line, lex->col, ch);
                    return 1;
            }
            lex->pos++; lex->col++;
            add_token(lex, tt, buf, start_line, start_col);
        }
    }

    add_token(lex, TOK_EOF, "", lex->line, lex->col);
    return 0;
}

void lexer_free(lexer_t *lex) {
    for (int i = 0; i < lex->ntokens; i++) {
        free(lex->tokens[i].value);
    }
    free(lex->tokens);
    lex->tokens = NULL;
    lex->ntokens = 0;
}

const char *token_type_name(token_type_t t) {
    static const char *names[] = {
        "PRAGMA","CIRCOM","INCLUDE","TEMPLATE","COMPONENT","MAIN",
        "SIGNAL","INPUT","OUTPUT","PUBLIC","PRIVATE","VAR","IF","ELSE",
        "FOR","WHILE","RETURN","LOG","ASSERT","NUMBER","STRING","IDENT",
        "CONSTRAIN_ASSIGN","CONSTRAIN_ASSIGN_R","WITNESS_ASSIGN","WITNESS_ASSIGN_R",
        "CONSTRAIN_EQ","PLUS","MINUS","STAR","SLASH","PERCENT","POWER","BACKSLASH",
        "EQ","NEQ","LT","GT","LEQ","GEQ","AND","OR","NOT",
        "BIT_AND","BIT_OR","BIT_XOR","BIT_NOT","SHL","SHR",
        "ASSIGN","PLUS_ASSIGN","MINUS_ASSIGN","STAR_ASSIGN",
        "SLASH_ASSIGN","PERCENT_ASSIGN","POWER_ASSIGN",
        "SHL_ASSIGN","SHR_ASSIGN","AND_ASSIGN","OR_ASSIGN","XOR_ASSIGN",
        "BACKSLASH_ASSIGN",
        "INCREMENT","DECREMENT",
        "LPAREN","RPAREN","LBRACE","RBRACE","LBRACKET","RBRACKET",
        "COMMA","SEMICOLON","DOT","COLON","QUESTION","EOF","ERROR"
    };
    if (t >= 0 && t <= TOK_ERROR) return names[t];
    return "UNKNOWN";
}
