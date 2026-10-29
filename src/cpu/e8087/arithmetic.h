/* PCE NPX arithmetic adapter. GPL-2.0-or-later. */
#ifndef PCE_E87_ARITHMETIC_H
#define PCE_E87_ARITHMETIC_H
#include "real80.h"
/* Raw backend result; caller must apply early NPX exceptions/model semantics.
 * precision: 32,64,80 (SoftFloat formats, significands 24,53,64 bits).
 * rounding: 0 nearest-even, 1 toward zero, 2 down, 3 up.
 * flags: backend IEEE flags, NOT x87 status-word bits.
 * Inputs must be canonical. Single-threaded emulator API; globals restored. */
enum { E87_ARITH_INEXACT = 1 }; /* SoftFloat flag mapping, not NPX PE. */
int e87_add80(e87_real80_t a, e87_real80_t b, unsigned precision,
             unsigned rounding, e87_real80_t *result, unsigned *flags);
/* Early NPX finite addition retaining an unnormal dominant operand's scale.
 * Exponent-range exceptions defer without output mutation. */
int e87_add80_unnormal(e87_real80_t a, e87_real80_t b, unsigned precision,
                      unsigned rounding, e87_real80_t *result, unsigned *flags);
/* Handled true exponent-range exception only; status is NPX SW bits, not
 * backend flags. Returns 0 without mutation for non-range/unsupported cases. */
int e87_add80_range(e87_real80_t a, e87_real80_t b, unsigned precision,
                   unsigned rounding, unsigned control, e87_real80_t *result, unsigned *status);
/* Canonical finite normal/zero operands only, normal/zero results only.
 * Unsupported exceptional/range cases return zero without output mutation. */
int e87_mul80(e87_real80_t a, e87_real80_t b, unsigned precision,
             unsigned rounding, e87_real80_t *result, unsigned *flags);
int e87_div80(e87_real80_t a, e87_real80_t b, unsigned precision,
             unsigned rounding, e87_real80_t *result, unsigned *flags);
#endif
