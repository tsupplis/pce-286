/* PCE NPX numeric execution foundation. GPL-2.0-or-later. */
#ifndef PCE_E87_EXECUTE_H
#define PCE_E87_EXECUTE_H
#include "e8087.h"
/* Development subset: returns 1 on completion, 2 on a handled store exception
 * with NO output write, 0 for unsupported (without mutation).
 * Caller supplies a checked, staged 10-byte operand; this API never accesses
 * guest memory. Store output and next state must be committed together AFTER
 * guest write validation. No pointer bookkeeping or board error signaling.
 * Supports m80 finite/NaN/infinity/denormal/unnormal transfers and register
 * FADD/P, including early-model tagged NaNs and mixed exception ordering.
 * Both models support masked/unmasked stack, precision, denormal and true
 * exponent-range responses, including exponent-zero/integer-bit-one operands.
 * 8087 pending numeric operations continue with IEM set, except during a WAIT
 * override; other pending numeric operations defer. The 287 CPU ERROR
 * adapter supplies direct #16 synchronization (not IBM AT board IRQ wiring).
 * Reduced-PC finite saturation, unmasked rounding-carry overflow, ambiguous
 * unnormal directed overflow remain explicitly unsupported.
 * A return of 0 is NOT an architectural exception or a successful instruction. */
int e87_numeric(e87_t *n, unsigned opcode, unsigned modrm,
                const unsigned char *input, unsigned char *output);
/* Optional CPU callback adapters; still a development subset. */
int e87_cpu_control(void *n, unsigned op, unsigned mr, unsigned short *ax);
int e87_cpu_memory(void *n, unsigned op, unsigned mr, int query, unsigned char *bytes);
int e87_cpu_event(void *n, unsigned event);
#endif
