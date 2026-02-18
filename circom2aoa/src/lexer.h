/*
 * Lexer for Circom - Hand-written tokenizer
 */

#ifndef LEXER_H
#define LEXER_H

typedef enum {
    /* Keywords */
    TOK_PRAGMA, TOK_CIRCOM, TOK_INCLUDE, TOK_TEMPLATE, TOK_COMPONENT,
    TOK_MAIN, TOK_SIGNAL, TOK_INPUT, TOK_OUTPUT, TOK_PUBLIC, TOK_PRIVATE,
    TOK_VAR, TOK_IF, TOK_ELSE, TOK_FOR, TOK_WHILE, TOK_RETURN,
    TOK_LOG, TOK_ASSERT,
    /* Literals */
    TOK_NUMBER, TOK_STRING,
    /* Identifier */
    TOK_IDENT,
    /* Circom-specific operators */
    TOK_CONSTRAIN_ASSIGN,   /* <== */
    TOK_CONSTRAIN_ASSIGN_R, /* ==> */
    TOK_WITNESS_ASSIGN,     /* <-- */
    TOK_WITNESS_ASSIGN_R,   /* --> */
    TOK_CONSTRAIN_EQ,       /* === */
    /* Arithmetic / comparison */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_POWER,              /* ** */
    TOK_BACKSLASH,          /* \ (integer division) */
    TOK_EQ,                 /* == */
    TOK_NEQ,                /* != */
    TOK_LT, TOK_GT, TOK_LEQ, TOK_GEQ,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_BIT_AND, TOK_BIT_OR, TOK_BIT_XOR, TOK_BIT_NOT,
    TOK_SHL, TOK_SHR,
    /* Assignment */
    TOK_ASSIGN,             /* = */
    TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN, TOK_STAR_ASSIGN,
    TOK_SLASH_ASSIGN, TOK_PERCENT_ASSIGN, TOK_POWER_ASSIGN,
    TOK_SHL_ASSIGN, TOK_SHR_ASSIGN,
    TOK_AND_ASSIGN, TOK_OR_ASSIGN, TOK_XOR_ASSIGN,
    TOK_BACKSLASH_ASSIGN,
    /* Delimiters */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_SEMICOLON, TOK_DOT, TOK_COLON,
    TOK_QUESTION,
    TOK_INCREMENT,      /* ++ */
    TOK_DECREMENT,      /* -- */
    /* Special */
    TOK_EOF, TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    char *value;        /* owned string */
    int line;
    int col;
} token_t;

typedef struct {
    const char *source;
    int pos;
    int len;
    int line;
    int col;
    token_t *tokens;
    int ntokens;
    int capacity;
} lexer_t;

void lexer_init(lexer_t *lex, const char *source);
int  lexer_tokenize(lexer_t *lex);  /* returns 0 on success */
void lexer_free(lexer_t *lex);

const char *token_type_name(token_type_t t);

#endif /* LEXER_H */
