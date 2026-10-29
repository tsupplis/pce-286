/* PCE NPX numeric execution foundation. GPL-2.0-or-later. */
#ifndef PCE_E87_EXECUTE_H
#define PCE_E87_EXECUTE_H
#include "e8087.h"
/* Optional numeric execution core. Returns 1 on completion, 2 on a handled
 * store exception with NO output write, 0 for unsupported without mutation.
 * input/output point to staged operands of the size reported by e87_cpu_memory
 * (up to 10 bytes for numeric forms). This API never accesses guest memory.
 * State and output must be committed only after guest access validation.
 *
 * Implements basic arithmetic and its real/integer memory forms; seven other
 * arithmetic families; FCOM/FCOMP/FCOMPP/FICOM/FICOMP/FTST/FXAM; real/register
 * transfers, signed integer transfers, FBLD and integral-input FBSTP.
 * Early-model stack, precision, range, denormal and invalid responses apply.
 * Fractional FBSTP and documented historical arithmetic edge cases explicitly
 * defer; this is not exhaustive physical-chip fidelity or board integration.
 * Pointer capture is provided separately by e87_cpu_address.
 *
 * 8087 pending operations may continue with IEM set except during WAIT override.
 * The 287 CPU ERROR adapter supplies direct #16 synchronization, not IBM AT IRQ
 * wiring. Return 0 is a DEVELOPMENT stop, not an architectural guest exception. */
int e87_numeric(e87_t *n, unsigned opcode, unsigned modrm,
                const unsigned char *input, unsigned char *output);
/* Optional CPU callback adapters; still a development subset. Memory also
 * supports 8087/287 control/state operands of 2/14/94 bytes via e87_memory_control.
 * CPU preflights the complete operand before core mutation or guest writes. */
int e87_cpu_control(void *n, unsigned op, unsigned mr, unsigned short *ax);
int e87_cpu_memory(void *n, unsigned op, unsigned mr, int query, unsigned char *bytes);
int e87_cpu_event(void *n, unsigned event);
struct e86_npx_address;
void e87_cpu_address(void *n, const struct e86_npx_address *address);
#endif
