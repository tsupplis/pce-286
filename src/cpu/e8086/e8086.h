/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8086/e8086.h                                        *
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


#ifndef PCE_E8086_H
#define PCE_E8086_H 1


#include <stdio.h>
#include <cpu/e8080/e8080.h>


/* CPU options */
#define E86_CPU_REP_BUG    0x01         /* enable rep/seg bug */
#define E86_CPU_MASK_SHIFT 0x02         /* mask shift count */
#define E86_CPU_PUSH_FIRST 0x04         /* push sp before decrementing it */
#define E86_CPU_INT6       0x08         /* throw illegal opcode exception */
#define E86_CPU_INT7       0x10		/* throw escape opcode exception */
#define E86_CPU_FLAGS286   0x20         /* Allow clearing flags 12-15 */
#define E86_CPU_NEC        0x80         /* NEC native ISA/model policy */
#define E86_CPU_8BIT       0x40		/* 16 bit accesses take more time */

/* 80286 Machine Status Word (CR0 low 16 bits) bits */
#define E286_MSW_PE 0x0001  /* Protected Mode Enable */
#define E286_MSW_MP 0x0002  /* Math Present */
#define E286_MSW_EM 0x0004  /* Emulate coprocessor */
#define E286_MSW_TS 0x0008  /* Task Switched */

/* CPU flags */
#define E86_FLG_C 0x0001
#define E86_FLG_P 0x0004
#define E86_FLG_A 0x0010
#define E86_FLG_Z 0x0040
#define E86_FLG_S 0x0080
#define E86_FLG_T 0x0100
#define E86_FLG_I 0x0200
#define E86_FLG_D 0x0400
#define E86_FLG_O 0x0800

/* 16 bit register values */
#define E86_REG_AX 0
#define E86_REG_CX 1
#define E86_REG_DX 2
#define E86_REG_BX 3
#define E86_REG_SP 4
#define E86_REG_BP 5
#define E86_REG_SI 6
#define E86_REG_DI 7

/* 8 bit register values */
#define E86_REG_AL 0
#define E86_REG_CL 1
#define E86_REG_DL 2
#define E86_REG_BL 3
#define E86_REG_AH 4
#define E86_REG_CH 5
#define E86_REG_DH 6
#define E86_REG_BH 7

/* Segment register values */
#define E86_REG_ES 0
#define E86_REG_CS 1
#define E86_REG_SS 2
#define E86_REG_DS 3

#define E86_PREFIX_NEW  0x0001
#define E86_PREFIX_SEG  0x0002
#define E86_PREFIX_REP  0x0004
#define E86_PREFIX_REPN 0x0008
#define E86_PREFIX_LOCK 0x0010
#define E86_PREFIX_KEEP 0x0020
#define E86_PREFIX_CARRY 0x0040 /* NEC comparison repeat tests CF, not ZF */

#define E86_PQ_MAX 16

#define E86_STATE_HALT  1
#define E86_STATE_RESET 2
#define E86_STATE_SHUTDOWN 4
/* Development stop: attached NPX rejected an unimplemented operation. */
#define E86_STATE_NPX_STOP 8


struct e8086_t;


typedef unsigned char (*e86_get_uint8_f) (void *ext, unsigned long addr);
typedef unsigned short (*e86_get_uint16_f) (void *ext, unsigned long addr);
typedef void (*e86_set_uint8_f) (void *ext, unsigned long addr, unsigned char val);
typedef void (*e86_set_uint16_f) (void *ext, unsigned long addr, unsigned short val);

typedef unsigned (*e86_opcode_f) (struct e8086_t *c);


/* Optional non-owning NPX interface. Called after CPU #7 checks.
 * Return 1 for completion, 0 for an unsupported operation (development stop).
 * Memory attachment is optional; WAIT wiring is a separate milestone. */
typedef int (*e86_npx_control_f)(void *, unsigned, unsigned, unsigned short *);
/* Staged NPX transfer: query=1 must not mutate state/buffer and returns
 * 1 for read, 2 for write (legacy 10 bytes), 0 unsupported. Other sizes
 * encode (size << 2) | direction, with size 2..94. query=0 uses checked
 * bytes, returning 1 to commit, 2 for a handled exception with NO memory
 * write, or 0 for unsupported with no mutation. CPU preflights
 * guest access before execution. Guest memory callbacks cannot report faults. */
typedef int (*e86_npx_memory_f)(void *, unsigned, unsigned, int, unsigned char *);
/* Optional data-channel events. BUSY is a side-effect-free query; OVERRUN
 * latches a stalled transfer. WAIT observes CPU synchronization (may assert
 * an error signal) and returns whether to stall. ERROR queries the optional
 * direct 286 ERROR pin for #16; board IRQ13 wiring is separate.
 * Unknown events return zero.
 * FNINIT is delivered through control as usual. */
enum { E86_NPX_BUSY, E86_NPX_OVERRUN, E86_NPX_WAIT, E86_NPX_ERROR };
typedef int (*e86_npx_event_f)(void *, unsigned);
/* Successful ESC metadata; callback decides which numeric forms track pointers.
 * ip includes prefixes, esc_ip does not; linear addresses precede bus masking. */
typedef struct e86_npx_address {
    unsigned op, mr, ip, esc_ip, cs, memory, offset, selector;
    unsigned long linear_ip, linear_esc, linear_operand;
} e86_npx_address_t;
typedef void (*e86_npx_address_f)(void *, const e86_npx_address_t *);

typedef struct e8086_t {
	/* Registers remain authoritative here; engine is synchronized per step. */
	e8080_t nec8080;
	unsigned char nec_md_write;
	unsigned         cpu;
	void             *npx_ext;
	e86_npx_control_f npx_control;
	e86_npx_memory_f npx_memory;
	e86_npx_event_f npx_event;
    e86_npx_address_f npx_address;

	unsigned short   dreg[8];
	unsigned short   sreg[4];
	unsigned short   ip;
	unsigned short   flg;

	unsigned short   save_flags;

	void             *mem;
	e86_get_uint8_f  mem_get_uint8;
	e86_set_uint8_f  mem_set_uint8;
	e86_get_uint16_f mem_get_uint16;
	e86_set_uint16_f mem_set_uint16;

	void             *prt;
	e86_get_uint8_f  prt_get_uint8;
	e86_set_uint8_f  prt_set_uint8;
	e86_get_uint16_f prt_get_uint16;
	e86_set_uint16_f prt_set_uint16;

	unsigned char    *ram;
	unsigned long    ram_cnt;

	unsigned long    addr_mask;
	unsigned long    a20_mask;         /* external gate, retained on CPU reset */
	unsigned long    reset_addr_mask;  /* addr_mask restored on RESET */

	void             *inta_ext;
	unsigned char    (*inta) (void *ext);

	void             *op_ext;
	void             (*op_hook) (void *ext, unsigned char op1, unsigned char op2);
	void             (*op_stat) (void *ext, unsigned char op1, unsigned char op2);
	void             (*op_undef) (void *ext, unsigned char op1, unsigned char op2);
	void             (*op_int) (void *ext, unsigned char n);

	void             *hook_ext;
	int              (*hook) (void *ext);

	void             *trap_ext;
	int              (*trap) (void *ext, unsigned n);

	unsigned short   cur_ip;

	unsigned         pq_size;
	unsigned         pq_fill;
	unsigned         pq_cnt;
	unsigned char    pq[E86_PQ_MAX];

	unsigned         prefix;

	unsigned short   seg_override;
	unsigned char    seg_override_reg;

	unsigned char    state;

	char             irq;
	unsigned char    nmi_line, nmi_pending, nmi_blocked, nmi_shadow;

	unsigned         int_cnt;
	unsigned char    int_vec;
	unsigned short   int_cs;
	unsigned short   int_ip;

	e86_opcode_f     op[256];

	struct {
		int            is_mem;
		unsigned char  *data;
		unsigned short seg;
		unsigned short ofs;
		unsigned short cnt;
	} ea;

	unsigned long    delay;
	unsigned long    clock;
	unsigned         opcnt;

	/* 80286 system registers */
	unsigned short   msw;          /* Machine Status Word (CR0 low 16 bits) */
	unsigned long    gdtr_base;    /* GDT base address (24-bit) */
	unsigned short   gdtr_limit;   /* GDT limit */
	unsigned long    idtr_base;    /* IDT base address (24-bit) */
	unsigned short   idtr_limit;   /* IDT limit */

	/* 80286 protected-mode fields */
	unsigned short   pm_event_depth; /* Active fault vector + 1, zero outside delivery */
	unsigned short   pm_error_valid, pm_error;
	unsigned short   cpl;          /* Current Privilege Level (0=kernel..3=user) */

	/* LDTR: selector + hidden descriptor cache */
	unsigned short   ldtr_sel;     /* LDT selector */
	unsigned long    ldtr_base;    /* LDT base (24-bit) */
	unsigned short   ldtr_limit;   /* LDT limit */

	/* TR: Task Register selector + hidden cache */
	unsigned short   tr_sel;       /* Task Register selector */
	unsigned long    tr_base;      /* TSS base (24-bit) */
	unsigned short   tr_limit;     /* TSS limit */

	/*
	 * Hidden segment descriptor cache: each segment register has a
	 * base, limit and access-rights byte loaded from the GDT/LDT when
	 * a selector is loaded in protected mode.  In real mode the base is
	 * (selector << 4) and the limit is 0xFFFF.
	 * Index: 0=ES 1=CS 2=SS 3=DS  (matches sreg[] layout)
	 */
	unsigned long    seg_base[4];  /* cached segment base  (24-bit) */
	unsigned short   seg_limit[4]; /* cached segment limit */
	unsigned char    seg_ar[4];    /* cached access rights byte */
} e8086_t;


#define e86_get_reg8(cpu, reg) \
	((((reg) & 4) ? ((cpu)->dreg[(reg) & 3] >> 8) : (cpu)->dreg[(reg) & 3]) & 0xff)

#define e86_get_al(cpu) ((cpu)->dreg[E86_REG_AX] & 0xff)
#define e86_get_bl(cpu) ((cpu)->dreg[E86_REG_BX] & 0xff)
#define e86_get_cl(cpu) ((cpu)->dreg[E86_REG_CX] & 0xff)
#define e86_get_dl(cpu) ((cpu)->dreg[E86_REG_DX] & 0xff)
#define e86_get_ah(cpu) (((cpu)->dreg[E86_REG_AX] >> 8) & 0xff)
#define e86_get_bh(cpu) (((cpu)->dreg[E86_REG_BX] >> 8) & 0xff)
#define e86_get_ch(cpu) (((cpu)->dreg[E86_REG_CX] >> 8) & 0xff)
#define e86_get_dh(cpu) (((cpu)->dreg[E86_REG_DX] >> 8) & 0xff)


#define e86_get_reg16(cpu, reg) ((cpu)->dreg[(reg) & 7])
#define e86_get_ax(cpu) ((cpu)->dreg[E86_REG_AX])
#define e86_get_bx(cpu) ((cpu)->dreg[E86_REG_BX])
#define e86_get_cx(cpu) ((cpu)->dreg[E86_REG_CX])
#define e86_get_dx(cpu) ((cpu)->dreg[E86_REG_DX])
#define e86_get_sp(cpu) ((cpu)->dreg[E86_REG_SP])
#define e86_get_bp(cpu) ((cpu)->dreg[E86_REG_BP])
#define e86_get_si(cpu) ((cpu)->dreg[E86_REG_SI])
#define e86_get_di(cpu) ((cpu)->dreg[E86_REG_DI])


#define e86_get_sreg(cpu, reg) ((cpu)->sreg[(reg) & 3])
#define e86_get_cs(cpu) ((cpu)->sreg[E86_REG_CS])
#define e86_get_ds(cpu) ((cpu)->sreg[E86_REG_DS])
#define e86_get_es(cpu) ((cpu)->sreg[E86_REG_ES])
#define e86_get_ss(cpu) ((cpu)->sreg[E86_REG_SS])


#define e86_set_reg8(cpu, reg, val) \
	do { \
		unsigned char v = (val) & 0xff; \
		unsigned      r = (reg); \
		if (r & 4) { \
			(cpu)->dreg[r & 3] &= 0x00ff; \
			(cpu)->dreg[r & 3] |= v << 8; \
		} \
		else { \
			(cpu)->dreg[r & 3] &= 0xff00; \
			(cpu)->dreg[r & 3] |= v; \
		} \
	} while (0)

#define e86_set_al(cpu, val) e86_set_reg8 (cpu, E86_REG_AL, val)
#define e86_set_bl(cpu, val) e86_set_reg8 (cpu, E86_REG_BL, val)
#define e86_set_cl(cpu, val) e86_set_reg8 (cpu, E86_REG_CL, val)
#define e86_set_dl(cpu, val) e86_set_reg8 (cpu, E86_REG_DL, val)
#define e86_set_ah(cpu, val) e86_set_reg8 (cpu, E86_REG_AH, val)
#define e86_set_bh(cpu, val) e86_set_reg8 (cpu, E86_REG_BH, val)
#define e86_set_ch(cpu, val) e86_set_reg8 (cpu, E86_REG_CH, val)
#define e86_set_dh(cpu, val) e86_set_reg8 (cpu, E86_REG_DH, val)


#define e86_set_reg16(cpu, reg, val) \
	do { (cpu)->dreg[(reg) & 7] = (val) & 0xffff; } while (0)

#define e86_set_ax(cpu, val) do { (cpu)->dreg[E86_REG_AX] = (val) & 0xffff; } while (0)
#define e86_set_bx(cpu, val) do { (cpu)->dreg[E86_REG_BX] = (val) & 0xffff; } while (0)
#define e86_set_cx(cpu, val) do { (cpu)->dreg[E86_REG_CX] = (val) & 0xffff; } while (0)
#define e86_set_dx(cpu, val) do { (cpu)->dreg[E86_REG_DX] = (val) & 0xffff; } while (0)
#define e86_set_sp(cpu, val) do { (cpu)->dreg[E86_REG_SP] = (val) & 0xffff; } while (0)
#define e86_set_bp(cpu, val) do { (cpu)->dreg[E86_REG_BP] = (val) & 0xffff; } while (0)
#define e86_set_si(cpu, val) do { (cpu)->dreg[E86_REG_SI] = (val) & 0xffff; } while (0)
#define e86_set_di(cpu, val) do { (cpu)->dreg[E86_REG_DI] = (val) & 0xffff; } while (0)


/* A real-mode segment load refreshes the hidden base. Fetch does not. */
static inline
void e86_set_sreg (e8086_t *c, unsigned reg, unsigned short val)
{
	reg &= 3;
	c->sreg[reg] = val;
	if ((c->msw & E286_MSW_PE) == 0) {
		c->seg_base[reg] = (unsigned long) val << 4;
		c->seg_limit[reg] = 0xffff;
		c->seg_ar[reg] = 0x93;
	}
}

#define e86_set_cs(cpu, val) e86_set_sreg (cpu, E86_REG_CS, val)
#define e86_set_ds(cpu, val) e86_set_sreg (cpu, E86_REG_DS, val)
#define e86_set_es(cpu, val) e86_set_sreg (cpu, E86_REG_ES, val)
#define e86_set_ss(cpu, val) e86_set_sreg (cpu, E86_REG_SS, val)


#define e86_get_ip(cpu) ((cpu)->ip)
#define e86_get_cur_ip(cpu) ((cpu)->cur_ip)
#define e86_set_ip(cpu, val) do { (cpu)->ip = (val) & 0xffff; } while (0)


#define e86_get_flags(cpu) ((cpu)->flg)
#define e86_get_f(cpu, f) (((cpu)->flg & (f)) != 0)
#define e86_get_cf(cpu) (((cpu)->flg & E86_FLG_C) != 0)
#define e86_get_pf(cpu) (((cpu)->flg & E86_FLG_P) != 0)
#define e86_get_af(cpu) (((cpu)->flg & E86_FLG_A) != 0)
#define e86_get_zf(cpu) (((cpu)->flg & E86_FLG_Z) != 0)
#define e86_get_of(cpu) (((cpu)->flg & E86_FLG_O) != 0)
#define e86_get_sf(cpu) (((cpu)->flg & E86_FLG_S) != 0)
#define e86_get_df(cpu) (((cpu)->flg & E86_FLG_D) != 0)
#define e86_get_if(cpu) (((cpu)->flg & E86_FLG_I) != 0)
#define e86_get_tf(cpu) (((cpu)->flg & E86_FLG_T) != 0)


#define e86_set_flags(c, v) do { (c)->flg = (v) & 0xffffU; } while (0)

#define e86_set_f(c, f, v) \
	do { if (v) (c)->flg |= (f); else (c)->flg &= ~(f); } while (0)

#define e86_set_cf(c, v) e86_set_f (c, E86_FLG_C, v)
#define e86_set_pf(c, v) e86_set_f (c, E86_FLG_P, v)
#define e86_set_af(c, v) e86_set_f (c, E86_FLG_A, v)
#define e86_set_zf(c, v) e86_set_f (c, E86_FLG_Z, v)
#define e86_set_of(c, v) e86_set_f (c, E86_FLG_O, v)
#define e86_set_sf(c, v) e86_set_f (c, E86_FLG_S, v)
#define e86_set_df(c, v) e86_set_f (c, E86_FLG_D, v)
#define e86_set_if(c, v) e86_set_f (c, E86_FLG_I, v)
#define e86_set_tf(c, v) e86_set_f (c, E86_FLG_T, v)


#define e86_get_halt(c) (((c)->state & E86_STATE_HALT) != 0)
#define e86_get_reset(c) (((c)->state & E86_STATE_RESET) != 0)


#define e86_get_linear(seg, ofs) \
	((((seg) & 0xffffUL) << 4) + ((ofs) & 0xffff))

/*
 * Legacy selector-value translation for external/debugger callers.
 * CPU instruction operands use e86_sreg_to_linear below.
 *
 * In real mode (MSW.PE=0): physical = (seg << 4) + ofs  (classic x86)
 * In protected mode (MSW.PE=1): look up seg in sreg[] to get the cached
 *   seg_base[] value; physical = seg_base[idx] + ofs.
 *   If the selector is not found in any cached segment register, fall back
 *   to real-mode formula (handles INT vector table accesses at seg=0).
 *
 * Index mapping: 0=ES 1=CS 2=SS 3=DS  (matches sreg[]/seg_base[] layout)
 */
static inline
unsigned long e86_seg_to_linear (e8086_t *c, unsigned short seg, unsigned short ofs)
{
	if (c->msw & E286_MSW_PE) {
		unsigned i;
		for (i = 0; i < 4; i++) {
			if (c->sreg[i] == seg) {
				return (c->seg_base[i] + (ofs & 0xffff));
			}
		}
		/* seg not matched (e.g. literal 0 for IVT access): real-mode formula */
		return e86_get_linear (seg, ofs);
	}
	return e86_get_linear (seg, ofs);
}

/* Apply the bus mask to each byte, including words crossing a gate boundary. */
static inline
unsigned char e86_get_mem8_phys (e8086_t *c, unsigned long addr)
{
	addr &= c->addr_mask;
	return ((addr < c->ram_cnt) ? c->ram[addr] : c->mem_get_uint8 (c->mem, addr));
}

static inline
void e86_set_mem8_phys (e8086_t *c, unsigned long addr, unsigned char val)
{
	addr &= c->addr_mask;
	if (addr < c->ram_cnt) {
		c->ram[addr] = val;
	}
	else {
		c->mem_set_uint8 (c->mem, addr, val);
	}
}

static inline
unsigned short e86_get_mem16_phys (e8086_t *c, unsigned long addr)
{
	unsigned long lo, hi;

	lo = addr & c->addr_mask;
	hi = (addr + 1) & c->addr_mask;
	if (hi != lo + 1) {
		return (e86_get_mem8_phys (c, lo) | (e86_get_mem8_phys (c, hi) << 8));
	}
	if (hi < c->ram_cnt) {
		return (c->ram[lo] | (c->ram[hi] << 8));
	}
	return (c->mem_get_uint16 (c->mem, lo));
}

static inline
void e86_set_mem16_phys (e8086_t *c, unsigned long addr, unsigned short val)
{
	unsigned long lo, hi;

	lo = addr & c->addr_mask;
	hi = (addr + 1) & c->addr_mask;
	if (hi != lo + 1) {
		e86_set_mem8_phys (c, lo, val & 0xff);
		e86_set_mem8_phys (c, hi, val >> 8);
	}
	else if (hi < c->ram_cnt) {
		c->ram[lo] = val & 0xff;
		c->ram[hi] = val >> 8;
	}
	else {
		c->mem_set_uint16 (c->mem, lo, val);
	}
}

static inline
unsigned char e86_get_mem8 (e8086_t *c, unsigned short seg, unsigned short ofs)
{
	return (e86_get_mem8_phys (c, e86_seg_to_linear (c, seg, ofs)));
}

static inline
void e86_set_mem8 (e8086_t *c, unsigned short seg, unsigned short ofs, unsigned char val)
{
	e86_set_mem8_phys (c, e86_seg_to_linear (c, seg, ofs), val);
}

static inline
unsigned short e86_get_mem16 (e8086_t *c, unsigned short seg, unsigned short ofs)
{
	return (e86_get_mem16_phys (c, e86_seg_to_linear (c, seg, ofs)));
}

static inline
void e86_set_mem16 (e8086_t *c, unsigned short seg, unsigned short ofs, unsigned short val)
{
	e86_set_mem16_phys (c, e86_seg_to_linear (c, seg, ofs), val);
}

/* Instruction operands identify a register, never search by selector value.
 * 286 caches remain authoritative with PE clear (reset and LOADALL). */
static inline
unsigned long e86_sreg_to_linear (e8086_t *c, unsigned reg, unsigned short ofs)
{
	if (c->cpu & E86_CPU_FLAGS286) {
		return (c->seg_base[reg] + ofs);
	}
	return (e86_get_linear (c->sreg[reg], ofs));
}

static inline
unsigned char e86_get_mem8_sreg (e8086_t *c, unsigned reg, unsigned short ofs)
{
	return (e86_get_mem8_phys (c, e86_sreg_to_linear (c, reg, ofs)));
}

static inline
void e86_set_mem8_sreg (e8086_t *c, unsigned reg, unsigned short ofs, unsigned char val)
{
	e86_set_mem8_phys (c, e86_sreg_to_linear (c, reg, ofs), val);
}

static inline
unsigned short e86_get_mem16_sreg (e8086_t *c, unsigned reg, unsigned short ofs)
{
	return (e86_get_mem16_phys (c, e86_sreg_to_linear (c, reg, ofs)));
}

static inline
void e86_set_mem16_sreg (e8086_t *c, unsigned reg, unsigned short ofs, unsigned short val)
{
	e86_set_mem16_phys (c, e86_sreg_to_linear (c, reg, ofs), val);
}

#define e86_get_prt8(cpu, ofs) \
	(cpu)->prt_get_uint8 ((cpu)->prt, ofs)

#define e86_get_prt16(cpu, ofs) \
	(cpu)->prt_get_uint16 ((cpu)->prt, ofs)

#define e86_set_prt8(cpu, ofs, val) \
	do { (cpu)->prt_set_uint8 ((cpu)->prt, ofs, val); } while (0)

#define e86_set_prt16(cpu, ofs, val) \
	do { (cpu)->prt_set_uint16 ((cpu)->prt, ofs, val); } while (0)

#define e86_get_delay(c) ((c)->delay)
#define e86_get_clock(c) ((c)->clock)
#define e86_get_opcnt(c) ((c)->opcnt)


void e86_init (e8086_t *c);
void e86_free (e8086_t *c);
/* Pass NULL callback to detach; CPU reset retains attachment, not NPX reset. */
void e86_set_npx_control(e8086_t *c, void *ext, e86_npx_control_f control);
/* Set after control attachment. Replacing/detaching control clears memory. */
void e86_set_npx_memory(e8086_t *c, e86_npx_memory_f memory);
void e86_set_npx_event(e8086_t *c, e86_npx_event_f event);
void e86_set_npx_address(e8086_t *c, e86_npx_address_f address);

e8086_t *e86_new (void);
void e86_del (e8086_t *c);

void e86_set_8086 (e8086_t *c);
void e86_set_8088 (e8086_t *c);
void e86_set_80186 (e8086_t *c);
void e86_set_80188 (e8086_t *c);
void e86_set_80286 (e8086_t *c);

/*
 * e86_load_seg_pm: load a segment descriptor from the GDT/LDT into the hidden
 * cache for segment register index `reg` (0=ES,1=CS,2=SS,3=DS).
 * `sel` is the selector value just written to sreg[reg].
 * No-op in real mode. A null selector clears the cache. CS loads update CPL.
 */
void e86_load_seg_pm (e8086_t *c, unsigned reg, unsigned short sel);
void e86_set_v30 (e8086_t *c);
void e86_set_v20 (e8086_t *c);

/*!***************************************************************************
 * @short Set the prefetch queue size
 * @param size The emulated prefetch queue size
 *****************************************************************************/
void e86_set_pq_size (e8086_t *c, unsigned size);

/*!***************************************************************************
 * @short Set CPU options
 * @param opt A bit mask indicating the desired options
 * @param set If true, the options in opt are set otherwise they are reset
 *****************************************************************************/
void e86_set_options (e8086_t *c, unsigned opt, int set);

void e86_set_a20 (e8086_t *c, int enabled);

void e86_set_addr_mask (e8086_t *c, unsigned long msk);
unsigned long e86_get_addr_mask (e8086_t *c);

void e86_set_inta_fct (e8086_t *c, void *ext, void *fct);

void e86_set_hook_fct (e8086_t *c, void *ext, void *fct);

void e86_set_trap_fct (e8086_t *c, void *ext, void *fct);

void e86_set_ram (e8086_t *c, unsigned char *ram, unsigned long cnt);

void e86_set_mem (e8086_t *c, void *mem,
	e86_get_uint8_f get8, e86_set_uint8_f set8,
	e86_get_uint16_f get16, e86_set_uint16_f set16
);

void e86_set_prt (e8086_t *c, void *prt,
	e86_get_uint8_f get8, e86_set_uint8_f set8,
	e86_get_uint16_f get16, e86_set_uint16_f set16
);

int e86_get_reg (e8086_t *c, const char *reg, unsigned long *val);
int e86_set_reg (e8086_t *c, const char *reg, unsigned long val);

void e86_irq (e8086_t *cpu, unsigned char val);
void e86_nmi (e8086_t *cpu, unsigned char val);

int e86_interrupt (e8086_t *cpu, unsigned n);

unsigned e86_undefined (e8086_t *c);

void e86_reset (e8086_t *c);
void e86_reset_callback (e8086_t *c);

void e86_execute (e8086_t *c);

void e86_clock (e8086_t *c, unsigned n);


void e86_push (e8086_t *c, unsigned short val);
unsigned short e86_pop (e8086_t *c);
void e86_nec_init (e8086_t *c);
void e86_nec_step (e8086_t *c);
void e86_nec_brkem (e8086_t *c, unsigned vector);
void e86_irq_ack (e8086_t *c);
void e86_trap (e8086_t *c, unsigned n);
void e86_fault (e8086_t *c, unsigned n);
void e86_fault_error (e8086_t *c, unsigned n, unsigned error);

void e86_pq_init (e8086_t *c);
void e86_pq_fill (e8086_t *c);


#define E86_DFLAGS_186  0x0001
#define E86_DFLAGS_286  0x0002
#define E86_DFLAGS_V20  0x0004
#define E86_DFLAGS_CALL 0x0100
#define E86_DFLAGS_LOOP 0x0200

typedef struct {
	unsigned       flags;

	unsigned       seg;

	unsigned short ip;
	unsigned       dat_n;
	unsigned char  dat[16];

	char           op[64];

	unsigned       arg_n;
	char           arg1[64];
	char           arg2[64];
} e86_disasm_t;


void e86_disasm (e86_disasm_t *op, unsigned char *src, unsigned short ip);
void e86_disasm_model (e86_disasm_t *op, unsigned char *src, unsigned short ip, unsigned cpu);
void e86_disasm_mem (e8086_t *c, e86_disasm_t *op, unsigned short, unsigned short ip);
void e86_disasm_cur (e8086_t *c, e86_disasm_t *op);


#endif
