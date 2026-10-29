/* PCE numeric operand representation. GPL-2.0-or-later. */
#ifndef PCE_E8087_REAL80_H
#define PCE_E8087_REAL80_H
#include <stdint.h>

/* Raw architectural bits, independent of host floating point and padding.
 * No arithmetic interpretation or normalization occurs during a transfer. */
typedef struct {
    uint64_t significand;
    uint16_t sign_exponent;
} e87_real80_t;

void e87_real80_load(e87_real80_t *value, const unsigned char bytes[10]);
void e87_real80_store(unsigned char bytes[10], const e87_real80_t *value);
#endif
