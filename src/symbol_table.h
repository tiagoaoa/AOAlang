/*
 * Symbol Table - Track variable declarations and types
 */

#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

typedef enum {
    SYMBOL_SCALAR,
    SYMBOL_ARRAY
} symbol_type_t;

typedef enum {
    SYMBOL_DECLARED,       /* Declared input variable */
    SYMBOL_GATE            /* Gate variable (computed) */
} symbol_origin_t;

typedef struct symbol {
    char *name;
    symbol_type_t type;
    symbol_origin_t origin; /* Whether declared or created as gate */
    int size;              /* For arrays: number of elements; for scalars: 0 */
    int assigned;          /* 1 if value has been assigned in constraints */
    struct symbol *next;
} symbol_t;

/* Initialize symbol table */
void symbol_table_init(void);

/* Add a symbol to the table */
void symbol_add(const char *name, symbol_type_t type, int size);

/* Add a symbol with origin tracking */
void symbol_add_with_origin(const char *name, symbol_type_t type, int size, symbol_origin_t origin);

/* Lookup a symbol by name */
symbol_t *symbol_lookup(const char *name);

/* Mark a symbol as assigned */
void symbol_mark_assigned(const char *name);

/* Check if a symbol has been assigned */
int symbol_is_assigned(const char *name);

/* Free all symbols and cleanup */
void symbol_table_free(void);

/* Print symbol table (for debugging) */
void symbol_table_print(void);

#endif /* SYMBOL_TABLE_H */
