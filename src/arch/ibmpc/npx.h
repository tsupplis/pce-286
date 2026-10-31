/* PC/XT and AT coprocessor wiring. GPL-2.0-or-later. */
#ifndef PCE_IBMPC_NPX_H
#define PCE_IBMPC_NPX_H
#include <cpu/e8087/e8087.h>
#include <libini/libini.h>
typedef struct {
    e87_t core;
    e87_t pending; /* Timed arithmetic commits at busy_start + busy_cycles. */
    unsigned enabled, error, busy_latch, nmi_enabled, stop_reported;
    unsigned timing;
    unsigned long clock_hz, ticks_remaining; /* Fixed-rate mode, motherboard PIT ticks. */
    unsigned long busy_start, busy_cycles;
} pc_npx_t;
struct ibmpc_t;
int pc_npx_config(ini_sct_t *cfg);
void pc_npx_init(struct ibmpc_t *pc, unsigned model);
void pc_npx_board_tick(struct ibmpc_t *pc);
void pc_npx_check_stop(struct ibmpc_t *pc);
void pc_npx_reset(struct ibmpc_t *pc);
void pc_npx_write(struct ibmpc_t *pc, unsigned port, unsigned val);
#endif
