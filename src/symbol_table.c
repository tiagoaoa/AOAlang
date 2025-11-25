/*
 * Symbol Table Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol_table.h"

static symbol_t *symbol_table_head = NULL;

void symbol_table_init(void) {
    symbol_table_head = NULL;
}

void symbol_add(const char *name, symbol_type_t type, int size) {
    symbol_add_with_origin(name, type, size, SYMBOL_DECLARED);
}

void symbol_add_with_origin(const char *name, symbol_type_t type, int size, symbol_origin_t origin) {
    symbol_t *sym = (symbol_t *)malloc(sizeof(symbol_t));
    if (!sym) {
        fprintf(stderr, "Fatal: Memory allocation failed\n");
        exit(1);
    }

    sym->name = strdup(name);
    sym->type = type;
    sym->origin = origin;
    sym->size = size;
    sym->assigned = 0;
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

void symbol_table_free(void) {
    symbol_t *current = symbol_table_head;
    while (current) {
        symbol_t *next = current->next;
        free(current->name);
        free(current);
        current = next;
    }
    symbol_table_head = NULL;
}

void symbol_table_print(void) {
    symbol_t *current = symbol_table_head;
    printf("\n=== Symbol Table ===\n");
    while (current) {
        if (current->type == SYMBOL_SCALAR) {
            printf("  %s: scalar\n", current->name);
        } else {
            printf("  %s: array[%d]\n", current->name, current->size);
        }
        current = current->next;
    }
    printf("==================\n\n");
}
