/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8086/e8086.c                                        *
 * Created:     1996-04-28 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 1996-2021 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include "e8086.h"
#include "internal.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>


static unsigned char e86_get_mem_uint8 (void *mem, unsigned long addr);
static unsigned short e86_get_mem_uint16 (void *mem, unsigned long addr);
static void e86_set_mem_uint8 (void *mem, unsigned long addr, unsigned char val);
static void e86_set_mem_uint16 (void *mem, unsigned long addr, unsigned short val);


static char *dreg16[8] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
static char *dreg8[8] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
static char *sreg[4] = { "es", "cs", "ss", "ds" };


void e86_init (e8086_t *c)
{
	unsigned i;

	e86_nec_init (c);

	c->cpu = E86_CPU_REP_BUG;
	c->npx_ext = NULL;
	c->npx_control = NULL;
	c->npx_memory = NULL;
    c->npx_event = NULL;

	c->mem = NULL;
	c->mem_get_uint8 = e86_get_mem_uint8;
	c->mem_get_uint16 = e86_get_mem_uint16;
	c->mem_set_uint8 = e86_set_mem_uint8;
	c->mem_set_uint16 = e86_set_mem_uint16;

	c->prt = NULL;
	c->prt_get_uint8 = e86_get_mem_uint8;
	c->prt_get_uint16 = e86_get_mem_uint16;
	c->prt_set_uint8 = e86_set_mem_uint8;
	c->prt_set_uint16 = e86_set_mem_uint16;

	c->ram = NULL;
	c->ram_cnt = 0;

	c->addr_mask = 0xfffff;
	c->reset_addr_mask = 0xfffff;
	c->a20_mask = ~0UL;

	c->inta_ext = NULL;
	c->inta = NULL;

	c->op_ext = NULL;
	c->op_hook = NULL;
	c->op_stat = NULL;
	c->op_undef = NULL;
	c->op_int = NULL;

	c->hook_ext = NULL;
	c->hook = NULL;

	c->trap_ext = NULL;
	c->trap = NULL;

	c->pq_size = 4;
	c->pq_fill = 6;

	c->irq = 0;

	c->state = 0;

	c->int_cnt = 0;
	c->int_vec = 0;
	c->int_cs = 0;
	c->int_ip = 0;

	for (i = 0; i < 256; i++) {
		c->op[i] = e86_opcodes[i];
	}

	c->clock = 0;
	c->opcnt = 0;
	c->delay = 0;

	c->msw = 0;
	c->gdtr_base = 0;
	c->gdtr_limit = 0;
	c->idtr_base = 0;
	c->idtr_limit = 0;

	c->cpl = 0;
	c->pm_event_depth = 0;
	c->pm_error_valid = c->pm_error = 0;
	c->ldtr_sel = 0;
	c->ldtr_base = 0;
	c->ldtr_limit = 0;
	c->tr_sel = 0;
	c->tr_base = 0;
	c->tr_limit = 0;

	for (i = 0; i < 4; i++) {
		c->seg_base[i]  = 0;
		c->seg_limit[i] = 0xffff;
		c->seg_ar[i]    = 0x93; /* present, data, R/W */
	}
}

void e86_set_npx_control(e8086_t *c, void *ext, e86_npx_control_f control)
{
    c->npx_ext = control ? ext : NULL;
    c->npx_control = control;
    c->npx_memory = NULL;
    c->npx_event = NULL;
}

void e86_set_npx_memory(e8086_t *c, e86_npx_memory_f memory)
{
    c->npx_memory = c->npx_control ? memory : NULL;
}

void e86_set_npx_event(e8086_t *c, e86_npx_event_f event)
{
    c->npx_event = c->npx_control ? event : NULL;
}

void e86_free (e8086_t *c)
{
    e86_set_npx_control(c, NULL, NULL);
}

e8086_t *e86_new (void)
{
	e8086_t *c;

	c = malloc (sizeof (e8086_t));
	if (c == NULL) {
		return (NULL);
	}

	e86_init (c);

	return (c);
}

void e86_del (e8086_t *c)
{
	if (c != NULL) {
		e86_free (c);
		free (c);
	}
}

void e86_set_8086 (e8086_t *c)
{
	unsigned i;

	c->cpu = E86_CPU_REP_BUG;

	for (i = 0; i < 256; i++) {
		c->op[i] = e86_opcodes[i];
	}

	e86_set_pq_size (c, 6);
}

void e86_set_8088 (e8086_t *c)
{
	e86_set_8086 (c);

	c->cpu |= E86_CPU_8BIT;

	e86_set_pq_size (c, 4);
}

void e86_set_pq_size (e8086_t *c, unsigned size)
{
	if (size > E86_PQ_MAX) {
		size = E86_PQ_MAX;
	}

	c->pq_size = size;
	c->pq_fill = (size < 6) ? 6 : size;
	c->pq_cnt = 0;
}

void e86_set_options (e8086_t *c, unsigned opt, int set)
{
	if (set) {
		c->cpu |= opt;
	}
	else {
		c->cpu &= ~opt;
	}
}

/* The gate is external to the CPU; CPU RESET does not change it. */
void e86_set_a20 (e8086_t *c, int enabled)
{
	unsigned long mask;

	c->a20_mask = enabled ? ~0UL : ~0x100000UL;
	if ((c->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) ==
		(E86_CPU_FLAGS286 | E86_CPU_INT6))
	{
		mask = 0xffffffUL;
	}
	else {
		/* Preserve the legacy PCE XMS/HMA extension on pre-286 CPUs. */
		mask = enabled ? ~0UL : 0xfffffUL;
	}
	c->addr_mask = mask & c->a20_mask;
}

void e86_set_addr_mask (e8086_t *c, unsigned long msk)
{
	c->addr_mask = msk;
}

unsigned long e86_get_addr_mask (e8086_t *c)
{
	return (c->addr_mask);
}

void e86_set_inta_fct (e8086_t *c, void *ext, void *fct)
{
	c->inta_ext = ext;
	c->inta = fct;
}

void e86_set_hook_fct (e8086_t *c, void *ext, void *fct)
{
	c->hook_ext = ext;
	c->hook = fct;
}

void e86_set_trap_fct (e8086_t *c, void *ext, void *fct)
{
	c->trap_ext = ext;
	c->trap = fct;
}

void e86_set_ram (e8086_t *c, unsigned char *ram, unsigned long cnt)
{
	c->ram = ram;
	c->ram_cnt = cnt;
}

void e86_set_mem (e8086_t *c, void *mem,
	e86_get_uint8_f get8, e86_set_uint8_f set8,
	e86_get_uint16_f get16, e86_set_uint16_f set16)
{
	c->mem = mem;
	c->mem_get_uint8 = get8;
	c->mem_set_uint8 = set8;
	c->mem_get_uint16 = get16;
	c->mem_set_uint16 = set16;
}

void e86_set_prt (e8086_t *c, void *prt,
	e86_get_uint8_f get8, e86_set_uint8_f set8,
	e86_get_uint16_f get16, e86_set_uint16_f set16)
{
	c->prt = prt;
	c->prt_get_uint8 = get8;
	c->prt_set_uint8 = set8;
	c->prt_get_uint16 = get16;
	c->prt_set_uint16 = set16;
}

static
unsigned char e86_get_mem_uint8 (void *mem, unsigned long addr)
{
	return (0xaa);
}

static
unsigned short e86_get_mem_uint16 (void *mem, unsigned long addr)
{
	return (0xaaaa);
}

static
void e86_set_mem_uint8 (void *mem, unsigned long addr, unsigned char val)
{
}

static
void e86_set_mem_uint16 (void *mem, unsigned long addr, unsigned short val)
{
}

int e86_get_reg (e8086_t *c, const char *reg, unsigned long *val)
{
	unsigned i;

	if (*reg == '%') {
		reg += 1;
	}

	for (i = 0; i < 8; i++) {
		if (strcmp (reg, dreg16[i]) == 0) {
			*val = e86_get_reg16 (c, i);
			return (0);
		}

		if (strcmp (reg, dreg8[i]) == 0) {
			*val = e86_get_reg8 (c, i);
			return (0);
		}
	}

	for (i = 0; i < 4; i++) {
		if (strcmp (reg, sreg[i]) == 0) {
			*val = e86_get_sreg (c, i);
			return (0);
		}
	}

	if (strcmp (reg, "ip") == 0) {
		*val = e86_get_ip (c);
		return (0);
	}

	if (strcmp (reg, "flags") == 0) {
		*val = e86_get_flags (c);
		return (0);
	}

	return (1);
}

int e86_set_reg (e8086_t *c, const char *reg, unsigned long val)
{
	unsigned i;

	if (*reg == '%') {
		reg += 1;
	}

	for (i = 0; i < 8; i++) {
		if (strcmp (reg, dreg16[i]) == 0) {
			e86_set_reg16 (c, i, val);
			return (0);
		}

		if (strcmp (reg, dreg8[i]) == 0) {
			e86_set_reg8 (c, i, val);
			return (0);
		}
	}

	for (i = 0; i < 4; i++) {
		if (strcmp (reg, sreg[i]) == 0) {
			e86_set_sreg (c, i, val);
			return (0);
		}
	}

	if (strcmp (reg, "ip") == 0) {
		e86_set_ip (c, val);
		return (0);
	}

	if (strcmp (reg, "flags") == 0) {
		e86_set_flags (c, val);
		return (0);
	}

	return (1);
}

int e86_hook (e8086_t *c)
{
	if (c->hook != NULL) {
		return (c->hook (c->hook_ext));
	}

	return (1);
}

void e86_trap (e8086_t *c, unsigned n)
{
	unsigned short ip;

	if (c->state & (E86_STATE_SHUTDOWN | E86_STATE_NPX_STOP)) {
		return;
	}

	/* An IDT too small for even one gate cannot deliver the original
	 * exception, #GP or #DF. Signal shutdown before touching the stack.
	 * This covers the empty-IDT reset sequence; general nested exception
	 * delivery still requires separate validation. */
	if ((c->msw & E286_MSW_PE) && c->idtr_limit < 7) {
		c->state = E86_STATE_SHUTDOWN;
		return;
	}

	if (c->op_int != NULL) {
		c->op_int (c->op_ext, n);
	}

	if (c->prefix & E86_PREFIX_KEEP) {
		ip = e86_get_cur_ip (c);

		if (c->cpu & E86_CPU_REP_BUG) {
			if (c->prefix & (E86_PREFIX_REP | E86_PREFIX_REPN)) {
				if (c->prefix & E86_PREFIX_SEG) {
					ip += 1;
				}
			}
		}

		c->prefix = 0;
	}
	else {
		ip = e86_get_ip (c);
	}

	if ((c->cpu & E86_CPU_NEC) && !(c->flg & 0x8000))
		c->state &= ~E86_STATE_HALT;

	c->int_cnt += 1;
	c->int_vec = n;
	c->int_cs = e86_get_cs (c);
	c->int_ip = ip;

	if (c->msw & E286_MSW_PE) {
		e86_interrupt_pm(c,n,ip);
	}
	else {
		/* Real-mode SP wraps between word pushes. Only a word that straddles
		 * FFFF:0000 is invalid (SP=1,3,5 for a six-byte INT frame).
		 * Preflight each wrapped word, retaining cached SS limit checks. */
		if ((c->cpu & (E86_CPU_FLAGS286|E86_CPU_INT6)) == (E86_CPU_FLAGS286|E86_CPU_INT6)) {
			unsigned sp=e86_get_sp(c), ar=c->seg_ar[E86_REG_SS], limit=c->seg_limit[E86_REG_SS];
			if ((n & 0xff)*4+3 > c->idtr_limit) {
				e86_fault(c,13);return;
			}
			unsigned i;
			if ((ar&0x9a)!=0x92) {
				c->state=E86_STATE_SHUTDOWN;return;
			}
			for (i=2; i<=6; i+=2) {
				unsigned ofs=(sp-i)&0xffff;
				if (ofs==0xffff || ((ar&4) ? ofs<=limit : ofs+1>limit)) {
					c->state=E86_STATE_SHUTDOWN;return;
				}
			}
		}
		e86_push(c,e86_get_flags(c));
		e86_push(c,e86_get_cs(c));
		e86_push(c,ip);
		if (c->cpu & E86_CPU_NEC) c->flg |= 0x8000;
		/* Real-mode: read CS:IP from IVT at 0000:n*4 */
		unsigned short ofs = (unsigned short)(n & 0xff) << 2;
		if (c->cpu & E86_CPU_FLAGS286) {
			e86_set_ip(c,e86_get_mem16_phys(c,c->idtr_base+ofs));
			e86_set_cs(c,e86_get_mem16_phys(c,c->idtr_base+ofs+2));
		} else {
			e86_set_ip (c, e86_get_mem16 (c, 0, ofs));
			e86_set_cs (c, e86_get_mem16 (c, 0, ofs + 2));
		}
		c->flg &= ~(E86_FLG_I | E86_FLG_T);
	}

	c->save_flags = e86_get_flags (c);

	e86_pq_init (c);
}

/* Fault delivery owns the complete frame, including its optional error word. */
static void e86_raise_fault (e8086_t *c, unsigned n, int has_error, unsigned error)
{
 unsigned active=c->pm_event_depth, pending=c->pm_error_valid, old_error=c->pm_error;
 if ((c->msw & E286_MSW_PE) || (c->cpu & E86_CPU_FLAGS286)) {
  if(active==9) {c->state=E86_STATE_SHUTDOWN;return;}
  /* Intel 286 PRM 9.6.2: a second fault after #DE/#TS/#NP/#SS/#GP is #DF. */
  if(active==1 || (active>=11 && active<=14)) {n=8;has_error=1;error=0;}
  c->pm_event_depth=n+1;
 }
 c->pm_error_valid=has_error;c->pm_error=error;
 e86_set_ip(c,c->cur_ip);c->prefix=0;e86_trap(c,n);
 c->pm_event_depth=active;c->pm_error_valid=pending;c->pm_error=old_error;
}

void e86_fault (e8086_t *c, unsigned n)
{
 e86_raise_fault(c,n,0,0);
}

void e86_fault_error (e8086_t *c, unsigned n, unsigned error)
{
 e86_raise_fault(c,n,1,error);
}

void e86_irq (e8086_t *cpu, unsigned char val)
{
	cpu->irq = (val != 0);
}

void e86_irq_ack (e8086_t *c)
{
	unsigned char irq;

	c->irq = 0;
	c->state &= ~E86_STATE_HALT;

	if (c->inta != NULL) {
		irq = c->inta (c->inta_ext);

		e86_set_clk (c, 61);

		e86_trap (c, irq);
	}
}

int e86_interrupt (e8086_t *cpu, unsigned n)
{
	if (cpu->state & E86_STATE_SHUTDOWN) {
		return (1);
	}
	if (e86_get_if (cpu)) {
		cpu->state &= ~E86_STATE_HALT;
		e86_trap (cpu, n);
		return (0);
	}

	return (1);
}

unsigned e86_undefined (e8086_t *c)
{
	if (c->op_undef != NULL) {
		c->op_undef (c->op_ext, c->pq[0], c->pq[1]);
	}

	if (c->cpu & E86_CPU_INT6) {
		e86_fault (c, 6);
		e86_set_clk (c, 50);
		return (0);
	}

	return (1);
}

void e86_reset (e8086_t *c)
{
	unsigned i;
	c->nec_md_write = 0;
	e8080_reset (&c->nec8080);

	c->opcnt = 0;

	c->addr_mask = c->reset_addr_mask & c->a20_mask;

	/* On RESET the 286 returns to real mode */
	c->msw = 0;
	c->cpl = 0;
	c->pm_event_depth = 0;
	c->pm_error_valid = c->pm_error = 0;
	c->ldtr_sel = 0;
	c->ldtr_base = 0;
	c->ldtr_limit = 0;
	c->tr_sel = 0;
	c->tr_base = 0;
	c->tr_limit = 0;

	for (i = 0; i < 8; i++) {
		c->dreg[i] = 0;
	}

	for (i = 0; i < 4; i++) {
		c->sreg[i] = 0;
		c->seg_base[i]  = 0;
		c->seg_limit[i] = 0xffff;
		c->seg_ar[i]    = 0x93;
	}

	e86_set_cs (c, 0xf000);
	if ((c->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) ==
		(E86_CPU_FLAGS286 | E86_CPU_INT6))
	{
		c->seg_base[E86_REG_CS] = 0xff0000UL;
		c->msw = 0xfff0;
		c->cpu &= ~E86_CPU_INT7;
		c->idtr_base = 0;
		c->idtr_limit = 0x3ff;
	}
	e86_set_ip (c, 0xfff0);
	e86_set_flags (c, 0x0000);
	if (c->cpu & E86_CPU_NEC) {
		e86_set_cs(c,0xffff);e86_set_ip(c,0);c->flg=0x8000;
	}

	c->save_flags = 0;

	c->pq_cnt = 0;

	c->irq = 0;

	c->state = 0;

	c->prefix = 0;
}

void e86_reset_callback (e8086_t *c)
{
	e86_reset (c);

	c->state = E86_STATE_RESET;
}

void e86_execute (e8086_t *c)
{
	unsigned cnt;
	char     irq;

	if ((c->cpu & E86_CPU_NEC) && !(c->flg & 0x8000) &&
	    !(c->state & E86_STATE_RESET)) {
		e86_nec_step (c);
		return;
	}

	if (c->state) {
		if (c->state & E86_STATE_RESET) {
			e86_reset (c);
		}
		else if (c->state & (E86_STATE_SHUTDOWN | E86_STATE_NPX_STOP)) {
			e86_set_clk (c, 2);
			return;
		}
		else if (c->state & E86_STATE_HALT) {
			e86_set_clk (c, 2);

			if (c->irq && e86_get_if (c)) {
				e86_irq_ack (c);
			}

			return;
		}

		c->state = 0;
	}

	if ((c->prefix & E86_PREFIX_KEEP) == 0) {
		c->prefix = 0;
		c->cur_ip = c->ip;
	}

	c->save_flags = e86_get_flags (c);

	irq = c->irq;

	do {
		if (e86_check_instruction (c, 0)) return;
		e86_pq_fill (c);
		if (e86_check_instruction (c, 1)) return;

		c->prefix &= ~E86_PREFIX_NEW;

		if (c->op_stat != NULL) {
			c->op_stat (c->op_ext, c->pq[0], c->pq[1]);
		}

		cnt = c->op[c->pq[0]] (c);

		if (cnt > 0) {
			c->ip = (c->ip + cnt) & 0xffff;
			e86_pq_adjust (c, cnt);
		}
		else {
			c->delay += 10;
		}
	} while (c->prefix & E86_PREFIX_NEW);

	c->opcnt += 1;

	if (c->state & (E86_STATE_SHUTDOWN | E86_STATE_NPX_STOP)) {
		return;
	}

	if (c->save_flags & E86_FLG_T) {
		c->state &= ~E86_STATE_HALT;
		e86_trap (c, 1);
	}
	else if (irq && c->irq && (c->save_flags & e86_get_flags (c) & E86_FLG_I)) {
		e86_irq_ack (c);
	}
}

void e86_clock (e8086_t *c, unsigned n)
{
	while (n >= c->delay) {
		n -= c->delay;
		c->clock += c->delay;
		c->delay = 0;
		e86_execute (c);
	}

	c->delay -= n;
	c->clock += n;
}
