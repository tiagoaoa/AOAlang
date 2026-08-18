/*
 * AOAlang - A compiler for AOA (Arithmetic Optimization Algebra) constraint files.
 *
 *
 * File:
 *     symbol_table.c
 *
 * Authors:
 *     Tiago A.O.A. <tiagoaoa@cos.ufrj.br>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"


/* ----------------------------
        GLOBAL VARIABLES       */

static symbol_t *symtab = NULL;
static visibility_t cur_visibility = VISIBILITY_PRIVATE;
static int next_widx = 0;	//next free position in the witness vector

/*----------------------------*/


void symbol_table_init(void) {
	symtab = NULL;
	cur_visibility = VISIBILITY_PRIVATE;
	next_widx = 1;	//index 0 is reserved for the constant 1
}

void symbol_add(const char *name, symbol_type_t type, int size) {
	symbol_add_full(name, type, size, SYMBOL_DECLARED, cur_visibility);
}

void symbol_add_with_origin(const char *name, symbol_type_t type, int size, symbol_origin_t origin) {
	symbol_add_full(name, type, size, origin, VISIBILITY_PRIVATE);
}

void symbol_add_full(const char *name, symbol_type_t type, int size,
                     symbol_origin_t origin, visibility_t visibility) {
	symbol_t *sym;

	if ((sym = (symbol_t *)malloc(sizeof(symbol_t))) == NULL) {
		fprintf(stderr, "Error allocating memory for symbol\n");
		exit(1);
	}

	sym->name = strdup(name);
	sym->type = type;
	sym->origin = origin;
	sym->visibility = visibility;
	sym->size = size;
	sym->assigned = 0;

	sym->witness_index = next_widx;
	next_widx += (type == SYMBOL_ARRAY) ? size : 1;	//arrays take one slot per element

	sym->next = symtab;
	symtab = sym;
}

symbol_t *symbol_lookup(const char *name) {
	symbol_t *s;

	for (s = symtab; s; s = s->next)
		if (strcmp(s->name, name) == 0)
			return s;
	return NULL;
}

void symbol_mark_assigned(const char *name) {
	symbol_t *sym = symbol_lookup(name);

	if (sym)
		sym->assigned = 1;
}

int symbol_is_assigned(const char *name) {
	symbol_t *sym = symbol_lookup(name);

	return sym ? sym->assigned : 0;
}

void symbol_set_witness_index(const char *name, int index) {
	symbol_t *sym = symbol_lookup(name);

	if (sym)
		sym->witness_index = index;
}

int symbol_get_witness_index(const char *name) {
	symbol_t *sym = symbol_lookup(name);

	return sym ? sym->witness_index : -1;
}

void symbol_set_visibility(const char *name, visibility_t vis) {
	symbol_t *sym = symbol_lookup(name);

	if (sym)
		sym->visibility = vis;
}

visibility_t symbol_get_current_visibility(void) {
	return cur_visibility;
}

void symbol_set_current_visibility(visibility_t vis) {
	cur_visibility = vis;
}

int symbol_get_witness_count(void) {
	return next_widx;
}

void symbol_table_free(void) {
	symbol_t *s = symtab, *next;

	while (s) {
		next = s->next;
		free(s->name);
		free(s);
		s = next;
	}
	symtab = NULL;
	next_widx = 1;
}

symbol_t *symbol_table_get_head(void) {
	return symtab;
}

const char *visibility_to_string(visibility_t vis) {
	switch (vis) {
		case VISIBILITY_PRIVATE: return "private";
		case VISIBILITY_PUBLIC: return "public";
		case VISIBILITY_DEFERRED: return "deferred";
		default: return "unknown";
	}
}

void symbol_table_print(void) {
	symbol_t *s;
	const char *type_str, *origin_str;

	printf("\n=== Symbol Table ===\n");
	printf("%-20s %-10s %-10s %-10s %s\n", "Name", "Type", "Visibility", "WitIdx", "Origin");
	printf("%-20s %-10s %-10s %-10s %s\n", "----", "----", "----------", "------", "------");
	for (s = symtab; s; s = s->next) {
		type_str = (s->type == SYMBOL_SCALAR) ? "scalar" : "array";
		origin_str = (s->origin == SYMBOL_DECLARED) ? "declared" : "gate";

		if (s->type == SYMBOL_ARRAY)
			printf("%-20s %-10s %-10s %-10d %s [size=%d]\n",
			       s->name, type_str, visibility_to_string(s->visibility),
			       s->witness_index, origin_str, s->size);
		else
			printf("%-20s %-10s %-10s %-10d %s\n",
			       s->name, type_str, visibility_to_string(s->visibility),
			       s->witness_index, origin_str);
	}
	printf("Total witness slots: %d\n", next_widx);
	printf("==================\n\n");
}
