/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/cpu/e8086/internal.h                                     *
 * Created:     2003-04-10 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2003-2017 Hampa Hug <hampa@hampa.ch>                     *
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


#ifndef PCE_E8086_INTERNAL_H
#define PCE_E8086_INTERNAL_H 1


#include "e8086.h"


#define e86_mk_uint16(lo, hi) \
	(((hi) << 8) | (lo))

#define e86_mk_sint16(lo) \
	(((lo) & 0x80) ? ((lo) | 0xff00) : ((lo) & 0xff))

#define e86_add_sint8(w, b) \
	((((b) & 0x80) ? ((w) + ((b) | 0xff00)) : ((w) + ((b) & 0xff))) & 0xffff)

#define e86_add_uint16(s1, s2) \
	(((s1) + (s2)) & 0xffff)

#define e86_add_uint4(s1, s2) \
	(((s1) & 0x0f) + ((s2) & 0x0f))

#define e86_sub_uint4(s1, s2) \
	(((s1) & 0x0f) - ((s2) & 0x0f))


#define e86_get_sego(cpu, seg) \
	(((cpu)->prefix & E86_PREFIX_SEG) ? (cpu)->seg_override : (seg));

#define e86_get_seg(cpu, seg) \
	(((cpu)->prefix & E86_PREFIX_SEG) ? (cpu)->seg_override : (cpu)->sreg[(seg)])


#define e86_get_seg_reg(cpu, reg) \
	(((cpu)->prefix & E86_PREFIX_SEG) ? (cpu)->seg_override_reg : (reg))

unsigned e86_ea_seg_reg (e8086_t *c);

#define e86_set_clk(cpu, clk) do { (cpu)->delay += (clk); } while (0)
#define e86_set_clk_ea(cpu, clkreg, clkmem) \
	do { \
		(c)->delay += ((c)->ea.is_mem) ? (clkmem) : (clkreg); \
	} while (0)


typedef void (*e86_ea_f) (e8086_t *c);


extern e86_opcode_f e86_opcodes[256];
extern e86_ea_f e86_ea[32];


int e86_hook (e8086_t *c);

/*
 * e86_tss_read16: read a 16-bit word from a physical TSS at the given offset.
 * Used by op_cf (IRET with NT=1) to read the back-link selector.
 */
unsigned short e86_tss_read16 (e8086_t *c, unsigned long base, unsigned short ofs);

/* Protected code/gate transfers return nonzero when handled or faulted. */
int e86_far_code_pm (e8086_t *c, unsigned sel, unsigned ip, unsigned ret_ip, int call);
int e86_return_pm (e8086_t *c, unsigned adjust, int iret);
void e86_interrupt_pm (e8086_t *c, unsigned n, unsigned ip);

int e86_call_gate_pm (e8086_t *c, unsigned short sel, unsigned short ret_ip);

/*
 * e86_task_switch_pm: perform an 80286 hardware task switch.
 *
 * new_sel  — selector of the incoming TSS in the GDT.
 * is_call  — non-zero for CALL (sets back-link in new TSS); zero for JMP.
 *
 * Returns non-zero if the switch was performed (selector named a TSS
 * descriptor); returns 0 if the selector is a normal code segment, so
 * the caller can proceed with a regular far JMP/CALL.
 * No-op (returns 0) when MSW.PE = 0.
 */
int e86_task_switch_pm (e8086_t *c, unsigned short new_sel, int is_call);


int e86_check_seg_access (e8086_t *c, unsigned reg, unsigned ofs, unsigned size, int write);
int e86_check_near_target (e8086_t *c, unsigned short target);
int e86_check_instruction (e8086_t *c, int decode);
int e86_check_privilege (e8086_t *c, int privileged);
int e86_check_stack_access (e8086_t *c, unsigned size, int push);
int e86_check_ea_access (e8086_t *c, unsigned size, int write);
void e86_get_ea_ptr (e8086_t *c, unsigned char *ea);
unsigned char e86_get_ea8 (e8086_t *c);
unsigned short e86_get_ea16 (e8086_t *c);
void e86_set_ea8 (e8086_t *c, unsigned char val);
void e86_set_ea16 (e8086_t *c, unsigned short val);


void e86_pq_adjust (e8086_t *c, unsigned cnt);


void e86_set_flg_szp_8 (e8086_t *c, unsigned char val);
void e86_set_flg_szp_16 (e8086_t *c, unsigned short val);
void e86_set_flg_log_8 (e8086_t *c, unsigned char val);
void e86_set_flg_log_16 (e8086_t *c, unsigned short val);
void e86_set_flg_adc_8 (e8086_t *c, unsigned char s1, unsigned char s2, unsigned char s3);
void e86_set_flg_adc_16 (e8086_t *c, unsigned short s1, unsigned short s2, unsigned short s3);
void e86_set_flg_add_8 (e8086_t *c, unsigned char s1, unsigned char s2);
void e86_set_flg_add_16 (e8086_t *c, unsigned short s1, unsigned short s2);
void e86_set_flg_sbb_8 (e8086_t *c, unsigned char s1, unsigned char s2, unsigned char s3);
void e86_set_flg_sbb_16 (e8086_t *c, unsigned short s1, unsigned short s2, unsigned short s3);
void e86_set_flg_sub_8 (e8086_t *c, unsigned char s1, unsigned char s2);
void e86_set_flg_sub_16 (e8086_t *c, unsigned short s1, unsigned short s2);


int e86_load_data_seg (e8086_t *c, unsigned reg, unsigned sel);

#endif
