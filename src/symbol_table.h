/*
 * Symbol Table - Track variable declarations, types, visibility, and witness indices
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

typedef enum {
    VISIBILITY_PRIVATE,
    VISIBILITY_PUBLIC,
    VISIBILITY_DEFERRED
} visibility_t;

typedef struct symbol {
    char *name;
    symbol_type_t type;
    symbol_origin_t origin;
    visibility_t visibility;
    int size;              /* For arrays: number of elements; for scalars: 0 */
    int assigned;          /* 1 if value has been assigned in constraints */
    int witness_index;     /* Position in witness vector (-1 if not assigned yet) */
    struct symbol *next;
} symbol_t;

/* Initialize symbol table */
void symbol_table_init(void);

/* Add a symbol to the table */
void symbol_add(const char *name, symbol_type_t type, int size);

/* Add a symbol with origin tracking */
void symbol_add_with_origin(const char *name, symbol_type_t type, int size, symbol_origin_t origin);

/* Add a symbol with full attributes */
void symbol_add_full(const char *name, symbol_type_t type, int size,
                     symbol_origin_t origin, visibility_t visibility);

/* Lookup a symbol by name */
symbol_t *symbol_lookup(const char *name);

/* Mark a symbol as assigned */
void symbol_mark_assigned(const char *name);

/* Check if a symbol has been assigned */
int symbol_is_assigned(const char *name);

/* Set witness index for a symbol */
void symbol_set_witness_index(const char *name, int index);

/* Get witness index for a symbol */
int symbol_get_witness_index(const char *name);

/* Set visibility for a symbol */
void symbol_set_visibility(const char *name, visibility_t vis);

/* Get the current visibility being used for declarations */
visibility_t symbol_get_current_visibility(void);

/* Set the current visibility for subsequent declarations */
void symbol_set_current_visibility(visibility_t vis);

/* Get total witness count */
int symbol_get_witness_count(void);

/* Free all symbols and cleanup */
void symbol_table_free(void);

/* Print symbol table (for debugging) */
void symbol_table_print(void);

/* Get head of symbol table (for iteration) */
symbol_t *symbol_table_get_head(void);

/* Convert visibility enum to string */
const char *visibility_to_string(visibility_t vis);

#endif /* SYMBOL_TABLE_H */
