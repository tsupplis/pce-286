/* GPL-2.0-or-later. */
#ifndef PCE_E87_TRANSCENDENTAL_H
#define PCE_E87_TRANSCENDENTAL_H
#include "real80.h"
/* Caller validates early-model domains. RC uses control-word encoding.
 * Functional approximation; no chip-bit or correctly-rounded guarantee.
 * Return zero for exponent-range results without modifying output. */
int e87_transcendental(unsigned,e87_real80_t,e87_real80_t,unsigned,e87_real80_t *);
#endif
