/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     error.h
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#ifndef ERROR_H
#define ERROR_H

void error_report(int line, const char *format, ...);
int error_get_count(void);
void error_reset(void);

#endif
