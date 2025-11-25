/*
 * Error Reporting Module
 */

#ifndef ERROR_H
#define ERROR_H

#include <stdarg.h>

/* Report an error with line number and formatted message */
void error_report(int line, const char *format, ...);

/* Get total error count */
int error_get_count(void);

/* Reset error count */
void error_reset(void);

#endif /* ERROR_H */
