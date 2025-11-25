%{
/*
 * AOA Lexer - Lexical analyzer for Arithmetic Optimization Algebra
 * Uses Flex (lex) for tokenization
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aoa.tab.h"
#include "error.h"

extern int yylineno;
extern char *yytext;

%}

%option yylineno
%option noyywrap

DIGIT       [0-9]
LETTER      [a-zA-Z]
IDENTIFIER  {LETTER}({LETTER}|{DIGIT}|_)*
INTEGER     {DIGIT}+
FLOAT       {DIGIT}+\.{DIGIT}+
WHITESPACE  [ \t]+
NEWLINE     \n|\r\n

%%

    /* Keywords */
"decl"      { return DECL; }
"private"   { return PRIVATE; }
"public"    { return PUBLIC; }
"deferred"  { return DEFERRED; }

    /* Operators */
"=="        { return EQ_OP; }
"="         { return ASSIGN; }
"+"         { return PLUS; }
"-"         { return MINUS; }
"*"         { return MULT; }

    /* Delimiters */
"["         { return LBRACKET; }
"]"         { return RBRACKET; }
","         { return COMMA; }

    /* Literals */
{INTEGER}   {
                yylval.str_val = strdup(yytext);
                return NUMBER;
            }

{FLOAT}     {
                yylval.str_val = strdup(yytext);
                return NUMBER;
            }

    /* Identifiers */
{IDENTIFIER} {
                yylval.str_val = strdup(yytext);
                return IDENTIFIER;
            }

    /* Comments - ignore everything after # */
"#".*       { /* Single-line comment */ }

    /* Whitespace */
{WHITESPACE} { /* Ignore spaces and tabs */ }

    /* Newlines - significant for line-oriented parsing */
{NEWLINE}   { return NEWLINE; }

    /* Catch-all for invalid characters */
.           {
                error_report(yylineno, "Invalid character: '%s'", yytext);
                return ERROR;
            }

%%
