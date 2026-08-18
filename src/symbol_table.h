/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     symbol_table.h
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

typedef enum {
	SYMBOL_SCALAR,
	SYMBOL_ARRAY
} symbol_type_t;

typedef enum {
	SYMBOL_DECLARED,	//declared input variable
	SYMBOL_GATE		//gate variable, computed by a constraint
} symbol_origin_t;

typedef enum {
	VISIBILITY_PRIVATE,
	VISIBILITY_PUBLIC,
	VISIBILITY_DEFERRED	//symbolic public input, resolved later (GB elimination)
} visibility_t;

typedef struct symbol {
	char *name;
	symbol_type_t type;
	symbol_origin_t origin;
	visibility_t visibility;
	int size;		//number of elements for arrays, 0 for scalars
	int assigned;		//1 once the variable is assigned in a constraint
	int witness_index;	//position in the witness vector (-1 if not set)
	struct symbol *next;
} symbol_t;



void symbol_table_init(void);
void symbol_table_free(void);
void symbol_table_print(void);
symbol_t *symbol_table_get_head(void);

void symbol_add(const char *name, symbol_type_t type, int size);
void symbol_add_with_origin(const char *name, symbol_type_t type, int size, symbol_origin_t origin);
void symbol_add_full(const char *name, symbol_type_t type, int size,
                     symbol_origin_t origin, visibility_t visibility);
symbol_t *symbol_lookup(const char *name);

void symbol_mark_assigned(const char *name);
int symbol_is_assigned(const char *name);

void symbol_set_witness_index(const char *name, int index);
int symbol_get_witness_index(const char *name);
int symbol_get_witness_count(void);

void symbol_set_visibility(const char *name, visibility_t vis);
visibility_t symbol_get_current_visibility(void);
void symbol_set_current_visibility(visibility_t vis);

const char *visibility_to_string(visibility_t vis);

#endif
