/* PCE NPX architectural state. GPL-2.0-or-later. */
#ifndef PCE_CPU_E8087_H
#define PCE_CPU_E8087_H
#include "real80.h"

typedef enum { E87_8087, E87_80287 } e87_model_t;
typedef struct {
    e87_model_t model;
    e87_real80_t reg[8]; /* Physical register order; TOP lives in status. */
    uint16_t control, status, tags;
    uint16_t opcode;
    uint32_t instruction_pointer, operand_pointer;
    uint16_t instruction_selector, operand_selector;
    unsigned protected_mode;
    unsigned wait_error; /* 8087 WAIT override of IEM, cleared with IR. */
    unsigned transfer_blocked; /* Segment overrun: cleared by FNINIT/reset. */
} e87_t;

/* Construction initializes unspecified physical register contents for repeatability.
 * FNINIT and hardware reset invalidate tags, without clearing register payloads.
 * Mode changes only on hardware reset or 287 FSETPM. */
int e87_init(e87_t *n, e87_model_t model);
void e87_reset(e87_t *n);
void e87_fninit(e87_t *n);
/* Core pending-exception query; does not deliver a board interrupt. */
int e87_pending_error(const e87_t *n);
/* Error-signaling state; precision execution and CPU WAIT use these APIs.
 * Board interrupt delivery is not connected.
 * Raise only newly detected exception bits (0..5). IR/ES remains latched.
 * error_signal returns logical assertion, not the electrical polarity of ERROR#. */
void e87_raise_exception(e87_t *n, unsigned flags);
int e87_error_signal(const e87_t *n);
int e87_error_wait(e87_t *n);
unsigned e87_top(const e87_t *n);
unsigned e87_index(const e87_t *n, unsigned logical);
unsigned e87_tag(const e87_t *n, unsigned logical);

/* Execute ONLY the listed non-memory control forms. Caller performs CPU/MSW,
 * pending-exception and synchronization checks. 1 = executed, 0 = not handled;
 * not-handled leaves all state/output untouched, and is not a guest #UD verdict.
 * Optional AX output is required for 287 DF E0. CPU attachment is optional; board signaling is not implemented yet. */
int e87_control(e87_t *n, unsigned opcode, unsigned modrm, uint16_t *ax);
/* Staged 287 memory controls. Returns required bytes (2/14/94), or zero.
 * query is side-effect free; caller must validate the entire operand before
 * execution and commit stores. No CPU attachment or address tracking yet.
 * FNSTENV remains unsupported pending resolution of conflicting early manuals. */
unsigned e87_memory_control(e87_t *n, unsigned op, unsigned mr,
                            int query, unsigned char *bytes);
#endif
