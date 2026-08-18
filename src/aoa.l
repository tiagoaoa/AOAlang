%{
/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     aoa.l
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
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

{IDENTIFIER} {
		yylval.str_val = strdup(yytext);
		return IDENTIFIER;
            }

"#".*       { /* comment, ignored to the end of the line */ }

{WHITESPACE} { /* ignored */ }

{NEWLINE}   { return NEWLINE; }	/* newlines are significant, parsing is line oriented */

.           {
		error_report(yylineno, "Invalid character: '%s'", yytext);
		return ERROR;
            }

%%
