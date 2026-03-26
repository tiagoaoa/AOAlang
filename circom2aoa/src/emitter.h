/*
 * Emitter: flat operations → valid .aoa text
 */

#ifndef EMITTER_H
#define EMITTER_H

#include <stdio.h>
#include "flattener.h"

void emitter_emit(FILE *out, flattener_t *f);

#endif /* EMITTER_H */
