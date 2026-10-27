/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8080/internal.h                                     *
 * Created:     2012-11-28 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2012-2025 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General *
 * Public License for more details.                                          *
 *****************************************************************************/


#ifndef PCE_E8080_INTERNAL_H
#define PCE_E8080_INTERNAL_H 1


#include "e8080.h"


/* An INTA instruction consumes bus bytes, not bytes at PC. */
#define e8080_next_pc(c, n) (((c)->pc + ((c)->inta_active ? 0 : (n))) & 0xffff)
static inline unsigned char e8080_inst_byte (e8080_t *c, unsigned n)
{
	if (c->inta_active) {
		return c->get_inta ? c->get_inta (c->inta_ext, n) : 0xff;
	}
	if (c->get_fetch8) return c->get_fetch8 (c->fetch_ext, (c->pc + n) & 0xffff);
	return e8080_get_mem8 (c, (c->pc + n) & 0xffff);
}

#define e8080_set_clk(cpu, cnt, clk) do { \
	(cpu)->pc = e8080_next_pc ((cpu), (cnt)); \
	(cpu)->delay += (clk); \
	} while (0)

#define e8080_get_inst1(c) do { \
	(c)->inst[1] = e8080_inst_byte (c, 1); \
	} while (0)

#define e8080_get_inst2(c) do { \
	(c)->inst[2] = e8080_inst_byte (c, 2); \
	} while (0)

#define e8080_get_inst12(c) do { \
	(c)->inst[1] = e8080_inst_byte (c, 1); \
	(c)->inst[2] = e8080_inst_byte (c, 2); \
	} while (0)

#define e8080_get_inst23(c) do { \
	(c)->inst[2] = e8080_inst_byte (c, 2); \
	(c)->inst[3] = e8080_inst_byte (c, 3); \
	} while (0)

#define e8080_uint16(lo, hi) \
	((((hi) & 0xff) << 8) | ((lo) & 0xff))

#define e8080_sext8(v) (((v) & 0x80) ? ((v) | 0xff00) : ((v) & 0x007f))

#define e8080_get_ixd(c, i) (((c)->ix + e8080_sext8 ((c)->inst[(i)])) & 0xffff)
#define e8080_get_iyd(c, i) (((c)->iy + e8080_sext8 ((c)->inst[(i)])) & 0xffff)

#define e8080_inc_r(c) do { \
	(c)->r = ((c)->r & 0x80) | (((c)->r + 1) & 0x7f); \
	} while (0)


enum {
	ARG_NONE,
	ARG_STR,
	ARG_OPC8,
	ARG_IMM8,
	ARG_IM16,
	ARG_AD16,
	ARG_JMP8,
	ARG_JP16,
	ARG_IXD,
	ARG_IYD,
	ARG_BIT
};

typedef struct {
	unsigned char  opcode;
	const char     *mnemonic;
	const char     *arg1s;
	const char     *arg2s;
	unsigned char  arg1;
	unsigned char  arg2;
	unsigned char  size;
	unsigned char  cycles;
	unsigned short flags;
} e8080_dop_t;


int e8080_hook_exec (e8080_t *c);
int e8080_hook_undefined (e8080_t *c);
int e8080_hook_rst (e8080_t *c);


void e8080_set_psw_szp (e8080_t *c, unsigned char val, unsigned set, unsigned reset);
void e8080_set_psw_log (e8080_t *c, unsigned char val);
void e8080_set_psw_inc (e8080_t *c, unsigned char val);
void e8080_set_psw_dec (e8080_t *c, unsigned char val);
void e8080_set_psw_add (e8080_t *c, unsigned char s1, unsigned char s2);
void e8080_set_psw_sub (e8080_t *c, unsigned char s1, unsigned char s2);
void e8080_set_psw_adc (e8080_t *c, unsigned char s1, unsigned char s2, unsigned char s3);
void e8080_set_psw_sbb (e8080_t *c, unsigned char s1, unsigned char s2, unsigned char s3);

void z80_set_psw_rot (e8080_t *c, unsigned char val, int cf);
void z80_set_psw_inc (e8080_t *c, unsigned char val);
void z80_set_psw_dec (e8080_t *c, unsigned char val);
void z80_set_psw_add (e8080_t *c, unsigned d, unsigned char s1, unsigned char s2);
void z80_set_psw_add16 (e8080_t *c, unsigned long d, unsigned s1, unsigned s2);
void z80_set_psw_add16_2 (e8080_t *c, unsigned long d, unsigned s1, unsigned s2);
void z80_set_psw_sub (e8080_t *c, unsigned d, unsigned char s1, unsigned char s2);
void z80_set_psw_sub16_2 (e8080_t *c, unsigned long d, unsigned s1, unsigned s2);


void z80_op_cb (e8080_t *c);
void z80_op_dd (e8080_t *c);
void z80_op_ed (e8080_t *c);
void z80_op_fd (e8080_t *c);
void z80_op_dd_cb (e8080_t *c);
void z80_op_fd_cb (e8080_t *c);


void e8080_set_opcodes (e8080_t *c);
void z80_set_opcodes (e8080_t *c);


#endif
