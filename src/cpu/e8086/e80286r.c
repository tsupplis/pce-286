/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8086/e80286r.c                                      *
 * Created:     2003-10-07 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2003-2013 Hampa Hug <hampa@hampa.ch>                     *
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


/* Reject privileged system operations before reading operands or changing state. */
static int e286_system_privilege (e8086_t *c)
{
 if ((c->msw & E286_MSW_PE) && c->cpl != 0) {
  e86_fault_error (c, 13, 0);
  return 1;
 }
 return 0;
}

/* Machine Status Word bits are defined in e8086.h */


/*
 * Write a 6-byte 80286 descriptor-table register to memory at seg:ofs.
 * Layout: [ofs+0..1] = limit (16-bit), [ofs+2..4] = base (24-bit).
 * The sixth byte is architecturally undefined (Intel 286 PRM B-101).
 */
static void write_dtr (e8086_t *c, unsigned short seg, unsigned short ofs,
	unsigned short limit, unsigned long base)
{
	e86_set_mem16_sreg (c, seg, ofs,     limit);
	e86_set_mem8_sreg (c, seg, ofs + 2, (base)       & 0xff);
	e86_set_mem8_sreg (c, seg, ofs + 3, (base >>  8) & 0xff);
	e86_set_mem8_sreg (c, seg, ofs + 4, (base >> 16) & 0xff);
	e86_set_mem8_sreg (c, seg, ofs + 5, 0xff);
}

/*
 * Read a 6-byte 80286 descriptor-table register from memory at seg:ofs.
 * Layout: [ofs+0..1] = limit, [ofs+2..4] = base (24-bit).
 */
static void read_dtr (e8086_t *c, unsigned short seg, unsigned short ofs,
	unsigned short *limit, unsigned long *base)
{
	*limit = e86_get_mem16_sreg (c, seg, ofs);
	*base  = (unsigned long) e86_get_mem8_sreg (c, seg, ofs + 2)
	       | ((unsigned long) e86_get_mem8_sreg (c, seg, ofs + 3) << 8)
	       | ((unsigned long) e86_get_mem8_sreg (c, seg, ofs + 4) << 16);
}


/* OP 0F 01 /0: SGDT mem — store GDT register to memory */
static
unsigned op_0f_01_00 (e8086_t *c)
{
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 6, 1)) {
		return (0);
	}

	if (!c->ea.is_mem) {
		return (e86_undefined (c));
	}

	write_dtr (c, e86_ea_seg_reg (c), c->ea.ofs, c->gdtr_limit, c->gdtr_base);
	e86_set_clk_ea (c, 11, 11);

	return (c->ea.cnt + 2);
}

/* OP 0F 01 /1: SIDT mem — store IDT register to memory */
static
unsigned op_0f_01_01 (e8086_t *c)
{
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 6, 1)) {
		return (0);
	}

	if (!c->ea.is_mem) {
		return (e86_undefined (c));
	}

	write_dtr (c, e86_ea_seg_reg (c), c->ea.ofs, c->idtr_limit, c->idtr_base);
	e86_set_clk_ea (c, 11, 11);

	return (c->ea.cnt + 2);
}

/* OP 0F 01 /2: LGDT mem — load GDT register from memory */
static
unsigned op_0f_01_02 (e8086_t *c)
{
	if (e286_system_privilege (c)) return (0);
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 6, 0)) {
		return (0);
	}

	if (!c->ea.is_mem) {
		return (e86_undefined (c));
	}

	read_dtr (c, e86_ea_seg_reg (c), c->ea.ofs, &c->gdtr_limit, &c->gdtr_base);
	e86_set_clk_ea (c, 11, 11);

	return (c->ea.cnt + 2);
}

/* OP 0F 01 /3: LIDT mem — load IDT register from memory */
static
unsigned op_0f_01_03 (e8086_t *c)
{
	if (e286_system_privilege (c)) return (0);
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 6, 0)) {
		return (0);
	}

	if (!c->ea.is_mem) {
		return (e86_undefined (c));
	}

	read_dtr (c, e86_ea_seg_reg (c), c->ea.ofs, &c->idtr_limit, &c->idtr_base);
	e86_set_clk_ea (c, 11, 11);

	return (c->ea.cnt + 2);
}

/* OP 0F 01 /4: SMSW r/m16 — store Machine Status Word */
static
unsigned op_0f_01_04 (e8086_t *c)
{
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 1)) {
		return (0);
	}
	e86_set_ea16 (c, c->msw);

	e86_set_clk_ea (c, 2, 3);

	return (c->ea.cnt + 2);
}

/* OP 0F 01 /6: LMSW r/m16 — load Machine Status Word */
static
unsigned op_0f_01_06 (e8086_t *c)
{
	unsigned short val;

	if (e286_system_privilege (c)) return (0);
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 0)) {
		return (0);
	}
	val = e86_get_ea16 (c);

	/*
	 * The PE bit (bit 0) can only be set, never cleared by LMSW.
	 * Only bits 0..3 are writable; the reserved upper bits are retained.
	 */
	c->msw = (c->msw & 0xfff1) | (val & 0x000f);

	/* EM bit controls coprocessor emulation (INT 7) */
	if (c->msw & E286_MSW_EM) {
		c->cpu |= E86_CPU_INT7;
	}
	else {
		c->cpu &= ~E86_CPU_INT7;
	}

	e86_set_clk_ea (c, 3, 6);

	return (c->ea.cnt + 2);
}

/* OP 0F 01 */
static
unsigned op_0f_01 (e8086_t *c)
{
	switch ((c->pq[2] >> 3) & 0x07) {
		case 0x00: return (op_0f_01_00 (c));
		case 0x01: return (op_0f_01_01 (c));
		case 0x02: return (op_0f_01_02 (c));
		case 0x03: return (op_0f_01_03 (c));
		case 0x04: return (op_0f_01_04 (c));
		case 0x06: return (op_0f_01_06 (c));
	}
	return (e86_undefined (c));
}


/* -----------------------------------------------------------------------
 * OP 0F 00: system segment register ops
 *   /0 SLDT r/m16  — store LDTR selector
 *   /1 STR  r/m16  — store TR selector
 *   /2 LLDT r/m16  — load LDTR from selector
 *   /3 LTR  r/m16  — load TR from selector
 *   /4 VERR r/m16  — verify segment for reading
 *   /5 VERW r/m16  — verify segment for writing
 * ----------------------------------------------------------------------- */

/* SLDT r/m16 — store LDTR selector */
static
unsigned op_0f_00_00 (e8086_t *c)
{
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 1)) {
		return (0);
	}
	e86_set_ea16 (c, c->ldtr_sel);
	e86_set_clk_ea (c, 2, 3);
	return (c->ea.cnt + 2);
}

/* STR r/m16 — store task register selector */
static
unsigned op_0f_00_01 (e8086_t *c)
{
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 1)) {
		return (0);
	}
	e86_set_ea16 (c, c->tr_sel);
	e86_set_clk_ea (c, 2, 3);
	return (c->ea.cnt + 2);
}

/* LLDT/LTR validate the global system descriptor before committing caches. */
static unsigned e286_load_system_segment (e8086_t *c, int task)
{
 unsigned sel, ar, vec = 13, index;
 unsigned long addr, base;
 unsigned short limit;

 if (e286_system_privilege (c)) return 0;
 e86_get_ea_ptr (c, c->pq + 2);
 if (e86_check_ea_access (c, 2, 0)) return 0;
 sel = e86_get_ea16 (c);
 if (!task && (sel & ~3U) == 0) {
  c->ldtr_sel = sel; c->ldtr_base = 0; c->ldtr_limit = 0;
 } else {
  index = sel & 0xfff8;
  if ((sel & ~3U) == 0 || (sel & 4) || index + 7 > c->gdtr_limit) goto fault;
  addr = c->gdtr_base + index;
  ar = e86_get_mem8_phys (c, addr + 5);
  if ((ar & 0x1f) != (task ? 1 : 2)) goto fault;
  if (!(ar & 0x80)) { vec = 11; goto fault; }
  limit = e86_get_mem16_phys (c, addr);
  base = e86_get_mem16_phys (c, addr + 2) |
   ((unsigned long)e86_get_mem8_phys (c, addr + 4) << 16);
  if (task) {
   e86_set_mem8_phys (c, addr + 5, ar | 2);
   c->tr_sel = sel; c->tr_base = base; c->tr_limit = limit;
  } else {
   c->ldtr_sel = sel; c->ldtr_base = base; c->ldtr_limit = limit;
  }
 }
 e86_set_clk_ea (c, 17, 19);
 return c->ea.cnt + 2;
fault:
 e86_fault_error (c, vec, sel & 0xfffc);
 return 0;
}

static unsigned op_0f_00_02 (e8086_t *c)
{
 return e286_load_system_segment (c, 0);
}

static unsigned op_0f_00_03 (e8086_t *c)
{
 return e286_load_system_segment (c, 1);
}

/* VERR/VERW validate type and visibility, not the present bit. */
static
int e286_verify_segment (e8086_t *c, unsigned sel, int write)
{
	unsigned long base;
	unsigned limit, index, ar, dpl;

	if ((sel & ~3U) == 0) {
		return (0);
	}
	if (sel & 4) {
		if ((c->ldtr_sel & ~3U) == 0) {
			return (0);
		}
		base = c->ldtr_base;
		limit = c->ldtr_limit;
	}
	else {
		base = c->gdtr_base;
		limit = c->gdtr_limit;
	}
	index = sel & 0xfff8;
	if (index + 7 > limit) {
		return (0);
	}
	ar = e86_get_mem8_phys (c, (base + index + 5) & 0xffffffUL);
	if (!(ar & 0x10)) {
		return (0);
	}
	if (write ? ((ar & 0x0a) != 2) : ((ar & 0x0a) == 8)) {
		return (0);
	}
	/* Readable conforming code is exempt from CPL/RPL visibility checks. */
	if ((ar & 0x0c) == 0x0c) {
		return (1);
	}
	dpl = (ar >> 5) & 3;
	return ((c->cpl <= dpl) && ((sel & 3) <= dpl));
}

static
unsigned op_0f_00_04 (e8086_t *c)   /* VERR */
{
	unsigned sel;

	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 0)) {
		return (0);
	}
	sel = e86_get_ea16 (c);
	e86_set_zf (c, e286_verify_segment (c, sel, 0));
	e86_set_clk_ea (c, 14, 16);
	return (c->ea.cnt + 2);
}

static
unsigned op_0f_00_05 (e8086_t *c)   /* VERW */
{
	unsigned sel;

	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 0)) {
		return (0);
	}
	sel = e86_get_ea16 (c);
	e86_set_zf (c, e286_verify_segment (c, sel, 1));
	e86_set_clk_ea (c, 14, 16);
	return (c->ea.cnt + 2);
}

static
unsigned op_0f_00 (e8086_t *c)
{
	if (!(c->msw & E286_MSW_PE)) return e86_undefined (c);
	switch ((c->pq[2] >> 3) & 7) {
		case 0: return (op_0f_00_00 (c));
		case 1: return (op_0f_00_01 (c));
		case 2: return (op_0f_00_02 (c));
		case 3: return (op_0f_00_03 (c));
		case 4: return (op_0f_00_04 (c));
		case 5: return (op_0f_00_05 (c));
	}
	return (e86_undefined (c));
}


/* LAR/LSL probe descriptors without raising selector faults. A failed probe
 * clears ZF and leaves the destination intact (Intel 286 PRM, 11.3/B-60/B-71).
 * Presence is deliberately not checked: nonresident segments can be queried.
 */
static
int e286_query_descriptor (e8086_t *c, unsigned sel, int limit_query,
	unsigned short *value)
{
	unsigned long base;
	unsigned limit, index, ar, type, dpl;

	if ((sel & ~3U) == 0) return (0);
	if (sel & 4) {
		if ((c->ldtr_sel & ~3U) == 0) return (0);
		base = c->ldtr_base;
		limit = c->ldtr_limit;
	}
	else {
		base = c->gdtr_base;
		limit = c->gdtr_limit;
	}
	index = sel & 0xfff8;
	if (index + 7 > limit) return (0);
	base = (base + index) & 0xffffffUL;
	ar = e86_get_mem8_phys (c, base + 5);
	type = ar & 15;
	if (!(ar & 0x10)) {
		if (type < 1 || type > (limit_query ? 3 : 5)) return (0);
	}
	/* Conforming code is visible independently of CPL and RPL. */
	if ((ar & 0x1c) != 0x1c) {
		dpl = (ar >> 5) & 3;
		if (c->cpl > dpl || (sel & 3) > dpl) return (0);
	}
	*value = limit_query ? e86_get_mem16_phys (c, base) : (ar << 8);
	return (1);
}

static
unsigned e286_query_op (e8086_t *c, int limit_query)
{
	unsigned sel, dst, valid;
	unsigned short value;

	if (!(c->msw & E286_MSW_PE)) return (e86_undefined (c));
	e86_get_ea_ptr (c, c->pq + 2);
	if (e86_check_ea_access (c, 2, 0)) {
		return (0);
	}
	sel = e86_get_ea16 (c);
	dst = (c->pq[2] >> 3) & 7;
	valid = e286_query_descriptor (c, sel, limit_query, &value);
	if (valid) e86_set_reg16 (c, dst, value);
	e86_set_zf (c, valid);
	e86_set_clk_ea (c, 14, 16);
	return (c->ea.cnt + 2);
}

static
unsigned op_0f_02 (e8086_t *c)
{
	return (e286_query_op (c, 0));
}

static
unsigned op_0f_03 (e8086_t *c)
{
	return (e286_query_op (c, 1));
}


/* -----------------------------------------------------------------------
 * OP 0F 05: LOADALL — undocumented Intel 80286 instruction.
 *
 * Loads all CPU registers (including hidden descriptor caches, GDT/IDT
 * registers, MSW) directly from a 102-byte table at physical 0x000800.
 * Used for extended-memory access without mode switching; cannot clear PE.
 *
 * Intel "Undocumented iAPX 286 Test Instruction": physical 800..865.
 * +06 MSW; +16 TR; +18 FLAGS; +1A IP; +1C LDT;
 * +1E DS, +20 SS, +22 CS, +24 ES; +26..34 DI/SI/BP/SP/BX/DX/CX/AX.
 * Eight six-byte caches start at +36: ES/CS/SS/DS/GDT/LDT/IDT/TSS.
 * Cache layout is base24, access8 (reserved for GDT/IDT), limit16.
 * ----------------------------------------------------------------------- */

/* Read a 16-bit word from the LOADALL table at physical 0x000800+ofs. */
static unsigned short
loadall_rd16 (e8086_t *c, unsigned short ofs)
{
	unsigned long addr = (0x000800UL + ofs) & 0xffffffUL;
	return (e86_get_mem16_phys (c, addr));
}

/* Read an 8-bit byte from the LOADALL table at physical 0x000800+ofs. */
static unsigned char
loadall_rd8 (e8086_t *c, unsigned short ofs)
{
	unsigned long addr = (0x000800UL + ofs) & 0xffffffUL;
	return (e86_get_mem8_phys (c, addr));
}

static
unsigned op_0f_05 (e8086_t *c)
{
	unsigned i;

	if (e286_system_privilege (c)) return (0);

	/* LOADALL does not provide an escape from protected mode. */
	c->msw = (c->msw & E286_MSW_PE) | loadall_rd16 (c, 0x06);
	if (c->msw & E286_MSW_EM) {
		c->cpu |= E86_CPU_INT7;
	}
	else {
		c->cpu &= ~E86_CPU_INT7;
	}

	/* System register selectors */
	c->tr_sel   = loadall_rd16 (c, 0x16);
	c->ldtr_sel = loadall_rd16 (c, 0x1C);

	/* FLAGS and IP */
	c->flg = (loadall_rd16 (c, 0x18) & 0x7fd5) | 2;
	c->ip  = loadall_rd16 (c, 0x1A);

	/* Segment selectors: DS SS CS ES */
	c->sreg[E86_REG_DS] = loadall_rd16 (c, 0x1E);
	c->sreg[E86_REG_SS] = loadall_rd16 (c, 0x20);
	c->sreg[E86_REG_CS] = loadall_rd16 (c, 0x22);
	c->sreg[E86_REG_ES] = loadall_rd16 (c, 0x24);

	/* General-purpose registers: DI SI BP SP BX DX CX AX */
	c->dreg[E86_REG_DI] = loadall_rd16 (c, 0x26);
	c->dreg[E86_REG_SI] = loadall_rd16 (c, 0x28);
	c->dreg[E86_REG_BP] = loadall_rd16 (c, 0x2A);
	c->dreg[E86_REG_SP] = loadall_rd16 (c, 0x2C);
	c->dreg[E86_REG_BX] = loadall_rd16 (c, 0x2E);
	c->dreg[E86_REG_DX] = loadall_rd16 (c, 0x30);
	c->dreg[E86_REG_CX] = loadall_rd16 (c, 0x32);
	c->dreg[E86_REG_AX] = loadall_rd16 (c, 0x34);

	/* Physical 836..84D: base24, access8, limit16. */
	for (i = 0; i < 4; i++) {
		unsigned short off = 0x36 + i * 6;
		c->seg_base[i] = loadall_rd16(c,off) |
			((unsigned long)loadall_rd8(c,off+2)<<16);
		c->seg_ar[i] = loadall_rd8(c,off+3);
		c->seg_limit[i] = loadall_rd16(c,off+4);
	}
	c->cpl = (c->msw & E286_MSW_PE) ? (c->seg_ar[E86_REG_CS] >> 5) & 3 : 0;

	/* GDT cache at +4E: base24, reserved8, limit16. */
	c->gdtr_limit = loadall_rd16 (c, 0x52);
	c->gdtr_base  = (unsigned long)loadall_rd8 (c, 0x4E)
	              | ((unsigned long)loadall_rd8 (c, 0x4F) << 8)
	              | ((unsigned long)loadall_rd8 (c, 0x50) << 16);

	/* LDT descriptor cache at +54. */
	c->ldtr_limit = loadall_rd16 (c, 0x58);
	c->ldtr_base  = (unsigned long)loadall_rd8 (c, 0x54)
	              | ((unsigned long)loadall_rd8 (c, 0x55) << 8)
	              | ((unsigned long)loadall_rd8 (c, 0x56) << 16);

	/* IDT cache at +5A: base24, reserved8, limit16. */
	c->idtr_limit = loadall_rd16 (c, 0x5E);
	c->idtr_base  = (unsigned long)loadall_rd8 (c, 0x5A)
	              | ((unsigned long)loadall_rd8 (c, 0x5B) << 8)
	              | ((unsigned long)loadall_rd8 (c, 0x5C) << 16);

	/* TR descriptor cache at +60. */
	c->tr_limit = loadall_rd16 (c, 0x64);
	c->tr_base  = (unsigned long)loadall_rd8 (c, 0x60)
	            | ((unsigned long)loadall_rd8 (c, 0x61) << 8)
	            | ((unsigned long)loadall_rd8 (c, 0x62) << 16);

	c->pq_cnt = 0;       /* flush prefetch queue */
	e86_set_clk (c, 195);
	return (0);          /* IP already set above; returning 0 prevents
	                      * the execution loop from adding cnt to c->ip */
}

/* -----------------------------------------------------------------------
 * OP 0F 06: CLTS — clear task-switched flag in MSW
 * ----------------------------------------------------------------------- */
static
unsigned op_0f_06 (e8086_t *c)
{
	if (e286_system_privilege (c)) return (0);
	c->msw &= ~E286_MSW_TS;
	e86_set_clk (c, 2);
	return (2);
}


/* -----------------------------------------------------------------------
 * OP 63: ARPL r/m16, r16 — adjust RPL field of selector
 *
 * If RPL of r/m16 < RPL of r16, set RPL of r/m16 = RPL of r16, ZF=1.
 * Otherwise ZF=0.
 * Protected mode only, at any privilege level. NEC CALLN has its own handler.
 * ----------------------------------------------------------------------- */
static
unsigned op_arpl (e8086_t *c)
{
	unsigned short dst, src;
	unsigned       reg;

	if (!(c->msw & E286_MSW_PE)) return e86_undefined (c);
	e86_get_ea_ptr (c, c->pq + 1);
	if (e86_check_ea_access (c, 2, 1)) return (0);
	dst = e86_get_ea16 (c);
	reg = (c->pq[1] >> 3) & 7;
	src = e86_get_reg16 (c, reg);

	if ((dst & 3) < (src & 3)) {
		dst = (dst & ~3U) | (src & 3);
		e86_set_ea16 (c, dst);
		e86_set_zf (c, 1);
	}
	else {
		e86_set_zf (c, 0);
	}

	e86_set_clk_ea (c, 10, 11);
	return (c->ea.cnt + 1);
}


/* OP 0F */
static
unsigned op_0f (e8086_t *c)
{
	switch (c->pq[1]) {
		case 0x00: return (op_0f_00 (c));
		case 0x01: return (op_0f_01 (c));
		case 0x02: return (op_0f_02 (c));
		case 0x03: return (op_0f_03 (c));
		case 0x05: return (op_0f_05 (c));
		case 0x06: return (op_0f_06 (c));
	}
	return (e86_undefined (c));
}

/*
 * tss_read16 / tss_write16: read/write a 16-bit word at a physical offset
 * inside the current TSS.  tr_base is already a physical address.
 */
static
unsigned short tss_read16 (e8086_t *c, unsigned long base, unsigned short ofs)
{
	unsigned long addr = (base + ofs) & 0xffffffUL;
	return e86_get_mem16_phys (c, addr);
}

/*
 * Public wrapper used by op_cf (IRET NT=1) in opcodes.c to read the
 * back-link selector from the current TSS without exposing tss_read16.
 */
unsigned short e86_tss_read16 (e8086_t *c, unsigned long base, unsigned short ofs)
{
	return tss_read16 (c, base, ofs);
}

static
void tss_write16 (e8086_t *c, unsigned long base, unsigned short ofs,
	unsigned short val)
{
	unsigned long addr = (base + ofs) & 0xffffffUL;
	e86_set_mem16_phys (c, addr, val);
}

/*
 * 80286 TSS field offsets (relative to TSS base).
 * The 286 TSS is 44 bytes (0x2C) minimum.
 */
#define TSS286_BACKLINK  0x00  /* back-link to previous task (16-bit selector) */
#define TSS286_SP0       0x02
#define TSS286_SS0       0x04
#define TSS286_SP1       0x06
#define TSS286_SS1       0x08
#define TSS286_SP2       0x0A
#define TSS286_SS2       0x0C
#define TSS286_IP        0x0E
#define TSS286_FLAGS     0x10
#define TSS286_AX        0x12
#define TSS286_CX        0x14
#define TSS286_DX        0x16
#define TSS286_BX        0x18
#define TSS286_SP        0x1A
#define TSS286_BP        0x1C
#define TSS286_SI        0x1E
#define TSS286_DI        0x20
#define TSS286_ES        0x22
#define TSS286_CS        0x24
#define TSS286_SS        0x26
#define TSS286_DS        0x28
#define TSS286_LDTR      0x2A
#define TSS286_MIN_LIMIT 0x2B  /* TSS must be at least 0x2C bytes */

/*
 * e86_task_switch_pm: perform an 80286 hardware task switch to the task
 * identified by GDT selector new_sel.
 *
 * Returns 1 if the selector was a TSS (type 0x01 or 0x03 in the access byte)
 * and the switch was carried out.  Returns 0 for any other descriptor type
 * (caller should handle as normal code-segment far JMP/CALL).
 *
 * mode: 0 JMP, 1 CALL, 2 nested IRET, 3 interrupt through an IDT task gate.
 * CALL/interrupt preserve the outgoing busy bit and establish a back-link.
 * IRET requires a busy target and clears NT in the saved outgoing FLAGS.
 *
 * Side-effects: all CPU registers updated from new TSS, TR updated, TS set.
 */


/* 286 privilege transitions (Intel PRM sections 7.5 and 9.4).
 * Validate descriptors and the complete frame before changing CS/SS/SP. */
typedef struct {
	unsigned long addr, base;
	unsigned limit, ar;
} e286_desc_t;

static int e286_desc (e8086_t *c, unsigned sel, e286_desc_t *d)
{
	unsigned long table;
	unsigned limit;
	if ((sel & ~3U) == 0) return 0;
	if (sel & 4) {
		if ((c->ldtr_sel & ~3U) == 0) return 0;
		table=c->ldtr_base; limit=c->ldtr_limit;
	} else { table=c->gdtr_base; limit=c->gdtr_limit; }
	if ((sel & 0xfff8U)+7 > limit) return 0;
	d->addr=(table+(sel & 0xfff8U)) & 0xffffffUL;
	d->limit=e86_get_mem16_phys(c,d->addr);
	d->base=e86_get_mem16_phys(c,d->addr+2) |
	        ((unsigned long)e86_get_mem8_phys(c,d->addr+4)<<16);
	d->ar=e86_get_mem8_phys(c,d->addr+5);
	return 1;
}

static int e286_stack_range (unsigned ar, unsigned limit, unsigned start, unsigned count)
{
	if (!count) return 1;
	if (start+count > 0x10000UL) return 0;
	return (ar & 4) ? start > limit : start+count-1 <= limit;
}

static int e286_current_stack (e8086_t *c, unsigned start, unsigned count)
{
	return e286_stack_range(c->seg_ar[E86_REG_SS],c->seg_limit[E86_REG_SS],start,count);
}

static void e286_cache (e8086_t *c, unsigned reg, unsigned sel, const e286_desc_t *d)
{
	c->sreg[reg]=sel;
	c->seg_base[reg]=d->base; c->seg_limit[reg]=d->limit;
	c->seg_ar[reg]=d->ar | 1;
	e86_set_mem8_phys(c,d->addr+5,d->ar | 1);
	if (reg==E86_REG_CS) c->cpl=sel & 3;
}

static void e286_transfer_fault (e8086_t *c, unsigned vec, unsigned error)
{
	e86_fault_error (c, vec, error);
}

int e86_task_switch_pm (e8086_t *c, unsigned short new_sel, int mode)
{
	unsigned long  new_desc_addr;
	unsigned short new_limit;
	unsigned long  new_base;
	unsigned char  new_ar;
	unsigned       ar_type;
	unsigned short old_tr;
	unsigned vec=(mode==2 || mode==3)?10:13, error=new_sel & 0xfffc, via_gate=0, r;
	e286_desc_t d;
	int is_call = mode == 1 || mode == 3;
	unsigned short new_ip, new_flags;
	unsigned short new_ax, new_cx, new_dx, new_bx;
	unsigned short new_sp, new_bp, new_si, new_di;
	unsigned short new_es, new_cs, new_ss, new_ds, new_ldtr;

	if (!(c->msw & E286_MSW_PE)) return 0;
	if (!e286_desc(c,new_sel,&d)) goto fault;
	if ((d.ar & 0x1f)==5 && mode!=2 && mode!=3) {
		if (c->cpl>((d.ar>>5)&3) || (new_sel&3)>((d.ar>>5)&3)) goto fault;
		if (!(d.ar&0x80)) {vec=11;goto fault;}
		new_sel=e86_get_mem16_phys(c,d.addr+2); error=new_sel&0xfffc;
		via_gate=1;
		if ((new_sel&4) || !e286_desc(c,new_sel,&d)) {vec=10;goto fault;}
	} else if ((d.ar&0x1f)!=1 && (d.ar&0x1f)!=3) {
		if(mode==2 || mode==3) {vec=10;goto fault;}
		return 0;
	}
	if (new_sel&4) {if(mode==2 || mode==3 || via_gate) {vec=10;goto fault;} return 0;}
	if(mode!=2 && mode!=3 && !via_gate &&
	   (c->cpl>((d.ar>>5)&3) || (new_sel&3)>((d.ar>>5)&3))) goto fault;
	if ((d.ar&0x1f)!=(mode==2?3:1)) {if(mode==2)vec=10;goto fault;}
	if (!(d.ar&0x80)) {vec=11;goto fault;}
	if (d.limit<TSS286_MIN_LIMIT) {vec=10;goto fault;}
	new_desc_addr=d.addr;new_ar=d.ar;new_base=d.base;new_limit=d.limit;
	if(mode==2) c->flg &= ~0x4000;

	/* --- Save current task state into old TSS (at tr_base) --- */
	if (c->tr_limit >= TSS286_MIN_LIMIT) {
		tss_write16 (c, c->tr_base, TSS286_IP,    e86_get_ip (c));
		tss_write16 (c, c->tr_base, TSS286_FLAGS, c->flg);
		tss_write16 (c, c->tr_base, TSS286_AX,    e86_get_ax (c));
		tss_write16 (c, c->tr_base, TSS286_CX,    e86_get_cx (c));
		tss_write16 (c, c->tr_base, TSS286_DX,    e86_get_dx (c));
		tss_write16 (c, c->tr_base, TSS286_BX,    e86_get_bx (c));
		tss_write16 (c, c->tr_base, TSS286_SP,    e86_get_sp (c));
		tss_write16 (c, c->tr_base, TSS286_BP,    e86_get_bp (c));
		tss_write16 (c, c->tr_base, TSS286_SI,    e86_get_si (c));
		tss_write16 (c, c->tr_base, TSS286_DI,    e86_get_di (c));
		tss_write16 (c, c->tr_base, TSS286_ES,    e86_get_es (c));
		tss_write16 (c, c->tr_base, TSS286_CS,    e86_get_cs (c));
		tss_write16 (c, c->tr_base, TSS286_SS,    e86_get_ss (c));
		tss_write16 (c, c->tr_base, TSS286_DS,    e86_get_ds (c));
		tss_write16 (c, c->tr_base, TSS286_LDTR,  c->ldtr_sel);
	}

	/* JMP/IRET release the outgoing task; nested CALL/INT keep it busy. */
	if (!is_call && !(c->tr_sel&4) && (c->tr_sel&~3U) &&
	    (c->tr_sel&0xfff8U)+7<=c->gdtr_limit) {
		unsigned long old_addr=c->gdtr_base+(c->tr_sel&0xfff8U)+5;
		e86_set_mem8_phys(c,old_addr,e86_get_mem8_phys(c,old_addr)&~2U);
	}
	if(mode!=2)e86_set_mem8_phys(c,new_desc_addr+5,new_ar|2);
	/* --- CALL: write back-link and set NT in new FLAGS --- */
	old_tr = c->tr_sel;
	if (is_call) {
		tss_write16 (c, new_base, TSS286_BACKLINK, old_tr);
	}

	/* --- Load new task state from new TSS --- */
	new_ip    = tss_read16 (c, new_base, TSS286_IP);
	new_flags = tss_read16 (c, new_base, TSS286_FLAGS);
	new_ax    = tss_read16 (c, new_base, TSS286_AX);
	new_cx    = tss_read16 (c, new_base, TSS286_CX);
	new_dx    = tss_read16 (c, new_base, TSS286_DX);
	new_bx    = tss_read16 (c, new_base, TSS286_BX);
	new_sp    = tss_read16 (c, new_base, TSS286_SP);
	new_bp    = tss_read16 (c, new_base, TSS286_BP);
	new_si    = tss_read16 (c, new_base, TSS286_SI);
	new_di    = tss_read16 (c, new_base, TSS286_DI);
	new_es    = tss_read16 (c, new_base, TSS286_ES);
	new_cs    = tss_read16 (c, new_base, TSS286_CS);
	new_ss    = tss_read16 (c, new_base, TSS286_SS);
	new_ds    = tss_read16 (c, new_base, TSS286_DS);
	new_ldtr  = tss_read16 (c, new_base, TSS286_LDTR);

	/* Update TR to point to new task */
	c->tr_sel   = new_sel;
	c->tr_base  = new_base;
	c->tr_limit = new_limit;

	/* Set TS (Task Switched) bit in MSW */
	c->msw |= E286_MSW_TS;

	/* Set NT (Nested Task) flag for CALL */
	if (is_call) {
		new_flags |= 0x4000;
	}

	/* Load general registers */
	e86_set_ip (c, new_ip);
	c->flg = new_flags;
	e86_set_ax (c, new_ax);
	e86_set_cx (c, new_cx);
	e86_set_dx (c, new_dx);
	e86_set_bx (c, new_bx);
	e86_set_sp (c, new_sp);
	e86_set_bp (c, new_bp);
	e86_set_si (c, new_si);
	e86_set_di (c, new_di);

	/* After commitment, faults belong to the incoming task. All selectors
	 * become visible before their descriptors are validated (Intel 8.3). */
	c->cur_ip=new_ip; c->prefix=0; c->save_flags=0;
	c->sreg[E86_REG_CS]=new_cs; c->sreg[E86_REG_SS]=new_ss;
	c->sreg[E86_REG_DS]=new_ds; c->sreg[E86_REG_ES]=new_es;
	c->cpl=new_cs&3;
	c->ldtr_sel=new_ldtr; c->ldtr_base=0; c->ldtr_limit=0;
	vec=10;error=new_ldtr&0xfffc;
	if(new_ldtr&~3U) {
		if((new_ldtr&4)||!e286_desc(c,new_ldtr,&d)||(d.ar&0x9f)!=0x82)goto fault;
		c->ldtr_base=d.base; c->ldtr_limit=d.limit;
	}
	error=new_cs&0xfffc;
	if(!e286_desc(c,new_cs,&d)||(d.ar&0x18)!=0x18 ||
	   ((d.ar&4)?((d.ar>>5)&3)>c->cpl:((d.ar>>5)&3)!=c->cpl))goto fault;
	if(!(d.ar&0x80)){vec=11;goto fault;}
	e286_cache(c,E86_REG_CS,new_cs,&d);
	error=new_ss&0xfffc;vec=12;
	if(!e286_desc(c,new_ss,&d)||(new_ss&3)!=c->cpl||((d.ar>>5)&3)!=c->cpl)goto fault;
	if((d.ar&0x1a)!=0x12){vec=13;goto fault;}
	if(!(d.ar&0x80))goto fault;
	e286_cache(c,E86_REG_SS,new_ss,&d);
	for(r=0;r<4;r++)if(r==E86_REG_DS||r==E86_REG_ES) {
		if(!e86_load_data_seg(c,r,c->sreg[r]))return 1;
	}
	if(new_ip>c->seg_limit[E86_REG_CS]){vec=13;error=0;goto fault;}

	/* Flush prefetch queue — CS:IP changed */
	e86_pq_init (c);

	return (1);
fault:
	e286_transfer_fault(c,vec,error);
	return 1;
}


/* Instruction data/stack loads validate before changing visible or hidden state.
 * Faulting POP/LDS/LES callers must likewise defer their other destinations. */
int e86_load_data_seg (e8086_t *c, unsigned reg, unsigned sel)
{
	e286_desc_t d;
	unsigned vec=13, error=sel & 0xfffcU;
	if (!(c->msw & E286_MSW_PE)) {
		e86_set_sreg(c,reg,sel);
		return 1;
	}
	if (reg==E86_REG_CS) {error=0; goto fault;}
	if ((sel & ~3U)==0) {
		if (reg==E86_REG_SS) {error=0; goto fault;}
		e86_set_sreg(c,reg,sel);
		c->seg_base[reg]=0; c->seg_limit[reg]=0; c->seg_ar[reg]=0;
		return 1;
	}
	if (!e286_desc(c,sel,&d)) goto fault;
	if (reg==E86_REG_SS) {
		if ((d.ar&0x1a)!=0x12 || (sel&3)!=c->cpl ||
		    ((d.ar>>5)&3)!=c->cpl) goto fault;
	} else {
		if (!(d.ar&0x10) || ((d.ar&8) && !(d.ar&2))) goto fault;
		if ((d.ar&0x1c)!=0x1c &&
		    (c->cpl>((d.ar>>5)&3) || (sel&3)>((d.ar>>5)&3))) goto fault;
	}
	if (!(d.ar&0x80)) {vec=(reg==E86_REG_SS) ? 12 : 11; goto fault;}
	e286_cache(c,reg,sel,&d);
	return 1;
fault:
	e286_transfer_fault(c,vec,error);
	return 0;
}

/* Read the destination ring's SS:SP from the 286 TSS. */
static int e286_ring_stack (e8086_t *c, unsigned level, unsigned *ss, unsigned *sp,
	e286_desc_t *d, unsigned *vec, unsigned *error)
{
	unsigned off=2+4*level;
	*vec=10; *error=c->tr_sel & 0xfffcU;
	if ((c->tr_sel & ~3U)==0 || off+3 > c->tr_limit) return 0;
	*sp=e86_get_mem16_phys(c,c->tr_base+off);
	*ss=e86_get_mem16_phys(c,c->tr_base+off+2);
	*error=*ss & 0xfffcU;
	if (!e286_desc(c,*ss,d) || (*ss & 3)!=level ||
	    ((d->ar>>5)&3)!=level || (d->ar & 0x1a)!=0x12) return 0;
	if (!(d->ar & 0x80)) { *vec=12; return 0; }
	return 1;
}

int e86_call_gate_pm (e8086_t *c, unsigned short sel, unsigned short ret_ip)
{
	e286_desc_t gate, code, stack;
	unsigned dst, ip, level, count, i, ss=0, sp=0, vec=13, error;
	unsigned old_sp=e86_get_sp(c), old_ss=e86_get_ss(c), old_cs=e86_get_cs(c);
	unsigned short params[31];
	if (!(c->msw & E286_MSW_PE)) return 0;
	if (!e286_desc(c,sel,&gate) || (gate.ar & 0x1f)!=4) return 0;
	error=sel & 0xfffcU;
	if (c->cpl > ((gate.ar>>5)&3) || (sel&3) > ((gate.ar>>5)&3)) goto fault;
	if (!(gate.ar & 0x80)) { vec=11; goto fault; }
	dst=e86_get_mem16_phys(c,gate.addr+2); ip=e86_get_mem16_phys(c,gate.addr);
	error=dst & 0xfffcU;
	if (!e286_desc(c,dst,&code) || (code.ar & 0x18)!=0x18 ||
	    ((code.ar>>5)&3)>c->cpl) goto fault;
	if (!(code.ar & 0x80)) { vec=11; goto fault; }
	if (ip>code.limit) { error=0; goto fault; }
	level=(code.ar & 4) ? c->cpl : (code.ar>>5)&3;
	if (level<c->cpl) {
		count=e86_get_mem8_phys(c,gate.addr+4)&31;
		if (!e286_current_stack(c,old_sp,2*count)) {vec=12; error=0; goto fault;}
		if (!e286_ring_stack(c,level,&ss,&sp,&stack,&vec,&error)) goto fault;
		if (!e286_stack_range(stack.ar,stack.limit,(sp-8-2*count)&0xffff,8+2*count)) {
			vec=12; error=ss & 0xfffcU; goto fault;
		}
		for (i=0;i<count;i++) params[i]=e86_get_mem16_phys(c,c->seg_base[E86_REG_SS]+old_sp+2*i);
		e286_cache(c,E86_REG_SS,ss,&stack); e86_set_sp(c,sp);
		e86_push(c,old_ss); e86_push(c,old_sp);
		for (i=count;i>0;i--) e86_push(c,params[i-1]);
	} else if (!e286_current_stack(c,(old_sp-4)&0xffff,4)) {
		vec=12; error=0; goto fault;
	}
	e86_push(c,old_cs); e86_push(c,ret_ip);
	e286_cache(c,E86_REG_CS,(dst & 0xfffcU)|level,&code);
	e86_set_ip(c,ip); e86_pq_init(c); e86_set_clk(c,41);
	return 1;
fault:
	e286_transfer_fault(c,vec,error);
	return 1;
}

/* Direct far code transfers cannot change CPL. In particular, entering
 * conforming code ignores selector RPL and preserves the caller's CPL. */
int e86_far_code_pm (e8086_t *c, unsigned sel, unsigned ip, unsigned ret_ip, int call)
{
	e286_desc_t code;
	unsigned vec=13, error=sel & 0xfffcU, through_gate=0;
	if (!(c->msw & E286_MSW_PE)) return 0;
	if (!e286_desc(c,sel,&code)) goto fault;
	if (!call && (code.ar&0x1f)==4) {
		if (c->cpl>((code.ar>>5)&3) || (sel&3)>((code.ar>>5)&3)) goto fault;
		if (!(code.ar&0x80)) {vec=11; goto fault;}
		ip=e86_get_mem16_phys(c,code.addr);
		sel=e86_get_mem16_phys(c,code.addr+2); error=sel&0xfffcU;
		if (!e286_desc(c,sel,&code)) goto fault;
		through_gate=1;
	}
	if ((code.ar&0x18)!=0x18) goto fault;
	if (code.ar&4) {
		if (((code.ar>>5)&3)>c->cpl) goto fault;
	} else if (((code.ar>>5)&3)!=c->cpl || (!through_gate && (sel&3)>c->cpl)) goto fault;
	if (!(code.ar&0x80)) {vec=11; goto fault;}
	if (ip>code.limit) {error=0; goto fault;}
	if (call) {
		if (!e286_current_stack(c,(e86_get_sp(c)-4)&0xffff,4)) {vec=12; error=0; goto fault;}
		e86_push(c,e86_get_cs(c)); e86_push(c,ret_ip);
	}
	e286_cache(c,E86_REG_CS,(sel&0xfffcU)|c->cpl,&code);
	e86_set_ip(c,ip); e86_pq_init(c); e86_set_clk(c,call ? 28 : 15);
	return 1;
fault:
	e286_transfer_fault(c,vec,error);
	return 1;
}

/* RETF and non-task IRET share outer-level frame validation. */
int e86_return_pm (e8086_t *c, unsigned adjust, int iret)
{
	e286_desc_t code, stack;
	unsigned sp=e86_get_sp(c), size=iret ? 6 : 4, cs, ip, flags=0;
	unsigned ss=0, outer_sp=0, level, old_cpl=c->cpl, vec=13, error=0, r;
	unsigned long base=c->seg_base[E86_REG_SS];
	if (!(c->msw & E286_MSW_PE)) return 0;
	if (!e286_current_stack(c,sp,size)) {vec=12; goto fault;}
	ip=e86_get_mem16_phys(c,base+sp); cs=e86_get_mem16_phys(c,base+sp+2);
	if (iret) flags=e86_get_mem16_phys(c,base+sp+4);
	level=cs & 3; error=cs & 0xfffcU;
	if (level<old_cpl || !e286_desc(c,cs,&code) || (code.ar & 0x18)!=0x18) goto fault;
	if ((code.ar & 4) ? ((code.ar>>5)&3)>level : ((code.ar>>5)&3)!=level) goto fault;
	if (!(code.ar & 0x80)) {vec=11; goto fault;}
	if (ip>code.limit) {error=0; goto fault;}
	if (level>old_cpl) {
		if (!e286_current_stack(c,sp,size+adjust+4)) {vec=12; error=0; goto fault;}
		outer_sp=e86_get_mem16_phys(c,base+sp+size+adjust);
		ss=e86_get_mem16_phys(c,base+sp+size+adjust+2); error=ss & 0xfffcU;
		if (!e286_desc(c,ss,&stack) || (ss&3)!=level ||
		    ((stack.ar>>5)&3)!=level || (stack.ar&0x1a)!=0x12) goto fault;
		if (!(stack.ar&0x80)) {vec=12; goto fault;}
		e286_cache(c,E86_REG_SS,ss,&stack); e86_set_sp(c,outer_sp+adjust);
	} else e86_set_sp(c,sp+size+adjust);
	if (iret) {
		unsigned mask=0x7fd5;
		if (old_cpl) mask &= ~0x3000U;
		if (old_cpl>((c->flg>>12)&3)) mask &= ~E86_FLG_I;
		c->flg=(c->flg & ~mask) | (flags & mask) | 2;
	}
	e286_cache(c,E86_REG_CS,cs,&code); e86_set_ip(c,ip);
	if (level>old_cpl) for (r=0;r<4;r++) {
		unsigned ar=c->seg_ar[r];
		if (r!=E86_REG_DS && r!=E86_REG_ES) continue;
		if ((ar & 0x1c)!=0x1c && ((ar>>5)&3)<level) {
			c->sreg[r]=0; c->seg_base[r]=0; c->seg_limit[r]=0; c->seg_ar[r]=0;
		}
	}
	e86_pq_init(c); e86_set_clk(c,iret ? 32 : 25);
	return 1;
fault:
	e286_transfer_fault(c,vec,error);
	return 1;
}

/* Protected-mode IDT delivery. Software INT DPL checks are performed by
 * the instruction path; hardware interrupts do not check the gate DPL. */
void e86_interrupt_pm (e8086_t *c, unsigned n, unsigned ip)
{
	e286_desc_t code, stack;
	unsigned ar, dst, target_ip, level, ss=0, sp=0, vec=13, error=(n<<3)|2;
	unsigned old_sp=e86_get_sp(c), old_ss=e86_get_ss(c), old_cs=e86_get_cs(c);
	unsigned flags=e86_get_flags(c);
	unsigned has_error=c->pm_error_valid, fault_error=c->pm_error;
	unsigned extra=has_error ? 2 : 0;
	c->pm_error_valid=0;
	unsigned long gate=c->idtr_base+8*n;
	if (8*n+7>c->idtr_limit) goto fault;
	ar=e86_get_mem8_phys(c,gate+5);
	if ((ar & 0x1e)!=6 && (ar & 0x1f)!=5) goto fault;
	if (!(ar & 0x80)) {vec=11; goto fault;}
	dst=e86_get_mem16_phys(c,gate+2); target_ip=e86_get_mem16_phys(c,gate);
	if ((ar&0x1f)==5) {
		unsigned events=c->int_cnt;
		c->ip=ip;e86_task_switch_pm(c,dst,3);
		if (!(c->state&E86_STATE_SHUTDOWN) && c->tr_sel==dst && c->int_cnt==events && has_error) {
			if(!e286_current_stack(c,(e86_get_sp(c)-2)&0xffff,2)) {vec=12;error=0;goto fault;}
			e86_push(c,fault_error);
		}
		return;
	}
	error=dst & 0xfffcU;
	if (!e286_desc(c,dst,&code) || (code.ar&0x18)!=0x18 || ((code.ar>>5)&3)>c->cpl) goto fault;
	if (!(code.ar&0x80)) {vec=11; goto fault;}
	if (target_ip>code.limit) {error=0; goto fault;}
	level=(code.ar&4) ? c->cpl : (code.ar>>5)&3;
	if (level<c->cpl) {
		if (!e286_ring_stack(c,level,&ss,&sp,&stack,&vec,&error)) goto fault;
		if (!e286_stack_range(stack.ar,stack.limit,(sp-10-extra)&0xffff,10+extra)) {vec=12; error=ss&0xfffcU; goto fault;}
		e286_cache(c,E86_REG_SS,ss,&stack); e86_set_sp(c,sp);
		e86_push(c,old_ss); e86_push(c,old_sp);
	} else if (!e286_current_stack(c,(old_sp-6-extra)&0xffff,6+extra)) {vec=12; error=0; goto fault;}
	e86_push(c,flags); e86_push(c,old_cs); e86_push(c,ip);
	if(has_error)e86_push(c,fault_error);
	e286_cache(c,E86_REG_CS,(dst&0xfffcU)|level,&code); e86_set_ip(c,target_ip);
	c->flg &= ~(E86_FLG_T | 0x4000);
	if (!(ar&1)) c->flg &= ~E86_FLG_I;
	return;
fault:
	e286_transfer_fault(c,vec,error);
}

/*
 * e86_load_seg_pm: load segment descriptor from GDT/LDT into hidden cache.
 *
 * 80286 segment descriptor layout (8 bytes at table base + (sel & ~7)):
 *   [0..1] limit (16-bit)
 *   [2..4] base  (24-bit, little-endian)
 *   [5]    access rights byte
 *   [6..7] reserved (zero on 286)
 *
 * Called after any segment register is loaded in protected mode.
 * No-op if MSW.PE=0 (real mode) or sel=0 (null descriptor).
 */
void e86_load_seg_pm (e8086_t *c, unsigned reg, unsigned short sel)
{
	unsigned long desc_addr;
	unsigned short limit;
	unsigned long  base;
	unsigned char  ar;

	if ((c->msw & E286_MSW_PE) == 0) {
		return;
	}

	if (reg >= 4) {
		return;
	}

	/* Null descriptor — clear cache to all-zeros */
	if ((sel & ~3U) == 0) {
		c->seg_base[reg]  = 0;
		c->seg_limit[reg] = 0;
		c->seg_ar[reg]    = 0;
		return;
	}

	/* TI chooses the descriptor table; RPL is not part of the index.
	 * In particular, 0047 and 0043 must not share a descriptor cache. */
	desc_addr = (((sel & 4) ? c->ldtr_base : c->gdtr_base)
		+ (unsigned long)(sel & 0xfff8)) & 0xffffff;

	/* Read 6 meaningful bytes directly from physical RAM / mem callbacks */
	limit = e86_get_mem16_phys (c, desc_addr);
	base  = (unsigned long)e86_get_mem8_phys (c, desc_addr + 2)
	      | ((unsigned long)e86_get_mem8_phys (c, desc_addr + 3) << 8)
	      | ((unsigned long)e86_get_mem8_phys (c, desc_addr + 4) << 16);
	ar    = e86_get_mem8_phys (c, desc_addr + 5);

	c->seg_base[reg]  = base;
	c->seg_limit[reg] = limit;
	c->seg_ar[reg]    = ar;
	if (reg == E86_REG_CS) c->cpl = sel & 3;
}

void e86_set_80286 (e8086_t *c)
{
	unsigned i;

	e86_set_80186 (c);

	c->cpu |= (E86_CPU_INT6 | E86_CPU_FLAGS286 | E86_CPU_PUSH_FIRST);

	/* 80286 has a 24-bit address bus */
	c->addr_mask = 0xffffff;
	c->reset_addr_mask = 0xffffff;

	/* Initialise segment descriptor caches to real-mode defaults */
	for (i = 0; i < 4; i++) {
		c->seg_base[i]  = (unsigned long)c->sreg[i] << 4;
		c->seg_limit[i] = 0xffff;
		c->seg_ar[i]    = 0x93;
	}

	c->op[0x0f] = &op_0f;
	c->op[0x63] = &op_arpl;   /* 80286: ARPL (replaces 8080-CALLN on NEC) */
}
