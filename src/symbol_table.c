/*
 * Symbol Table Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"

static symbol_t *symbol_table_head = NULL;
static visibility_t current_visibility = VISIBILITY_PRIVATE;
static int next_witness_index = 0;

void symbol_table_init(void) {
    symbol_table_head = NULL;
    current_visibility = VISIBILITY_PRIVATE;
    next_witness_index = 1;  /* Index 0 is reserved for constant 1 */
}

void symbol_add(const char *name, symbol_type_t type, int size) {
    symbol_add_full(name, type, size, SYMBOL_DECLARED, current_visibility);
}

void symbol_add_with_origin(const char *name, symbol_type_t type, int size, symbol_origin_t origin) {
    symbol_add_full(name, type, size, origin, VISIBILITY_PRIVATE);
}

void symbol_add_full(const char *name, symbol_type_t type, int size,
                     symbol_origin_t origin, visibility_t visibility) {
    symbol_t *sym = (symbol_t *)malloc(sizeof(symbol_t));
    if (!sym) {
        fprintf(stderr, "Fatal: Memory allocation failed\n");
        exit(1);
    }

    sym->name = strdup(name);
    sym->type = type;
    sym->origin = origin;
    sym->visibility = visibility;
    sym->size = size;
    sym->assigned = 0;

    /* Assign witness indices */
    if (type == SYMBOL_ARRAY) {
        sym->witness_index = next_witness_index;
        next_witness_index += size;
    } else {
        sym->witness_index = next_witness_index;
        next_witness_index += 1;
    }

    sym->next = symbol_table_head;
    symbol_table_head = sym;
}

symbol_t *symbol_lookup(const char *name) {
    symbol_t *current = symbol_table_head;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void symbol_mark_assigned(const char *name) {
    symbol_t *sym = symbol_lookup(name);
    if (sym) {
        sym->assigned = 1;
    }
}

int symbol_is_assigned(const char *name) {
    symbol_t *sym = symbol_lookup(name);
    return sym ? sym->assigned : 0;
}

void symbol_set_witness_index(const char *name, int index) {
    symbol_t *sym = symbol_lookup(name);
    if (sym) {
        sym->witness_index = index;
    }
}

int symbol_get_witness_index(const char *name) {
    symbol_t *sym = symbol_lookup(name);
    return sym ? sym->witness_index : -1;
}

void symbol_set_visibility(const char *name, visibility_t vis) {
    symbol_t *sym = symbol_lookup(name);
    if (sym) {
        sym->visibility = vis;
    }
}

visibility_t symbol_get_current_visibility(void) {
    return current_visibility;
}

void symbol_set_current_visibility(visibility_t vis) {
    current_visibility = vis;
}

int symbol_get_witness_count(void) {
    return next_witness_index;
}

void symbol_table_free(void) {
    symbol_t *current = symbol_table_head;
    while (current) {
        symbol_t *next = current->next;
        free(current->name);
        free(current);
        current = next;
    }
    symbol_table_head = NULL;
    next_witness_index = 1;
}

symbol_t *symbol_table_get_head(void) {
    return symbol_table_head;
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
    symbol_t *current = symbol_table_head;
    printf("\n=== Symbol Table ===\n");
    printf("%-20s %-10s %-10s %-10s %s\n", "Name", "Type", "Visibility", "WitIdx", "Origin");
    printf("%-20s %-10s %-10s %-10s %s\n", "----", "----", "----------", "------", "------");
    while (current) {
        const char *type_str = (current->type == SYMBOL_SCALAR) ? "scalar" : "array";
        const char *origin_str = (current->origin == SYMBOL_DECLARED) ? "declared" : "gate";

        if (current->type == SYMBOL_ARRAY) {
            printf("%-20s %-10s %-10s %-10d %s [size=%d]\n",
                   current->name, type_str, visibility_to_string(current->visibility),
                   current->witness_index, origin_str, current->size);
        } else {
            printf("%-20s %-10s %-10s %-10d %s\n",
                   current->name, type_str, visibility_to_string(current->visibility),
                   current->witness_index, origin_str);
        }
        current = current->next;
    }
    printf("Total witness slots: %d\n", next_witness_index);
    printf("==================\n\n");
}
