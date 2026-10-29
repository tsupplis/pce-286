/* PCE numeric operand representation. GPL-2.0-or-later. */
#include "real80.h"

void e87_real80_load(e87_real80_t *value, const unsigned char bytes[10])
{
    unsigned i;
    uint64_t significand = 0;
    for (i = 0; i < 8; ++i)
        significand |= (uint64_t)bytes[i] << (8 * i);
    value->significand = significand;
    value->sign_exponent = (uint16_t)bytes[8] | ((uint16_t)bytes[9] << 8);
}

void e87_real80_store(unsigned char bytes[10], const e87_real80_t *value)
{
    unsigned i;
    for (i = 0; i < 8; ++i)
        bytes[i] = (unsigned char)(value->significand >> (8 * i));
    bytes[8] = (unsigned char)value->sign_exponent;
    bytes[9] = (unsigned char)(value->sign_exponent >> 8);
}
