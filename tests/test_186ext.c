/*
 * tests/test_186ext.c
 *
 * C-level unit test mirroring tests/asm/t186.asm.
 * Covers 80186 extended instructions at the CPU core level:
 *   - PUSH imm8 (sign-extended), PUSH imm16   (0x6A, 0x68)
 *   - PUSHA / POPA round-trip                  (0x60, 0x61)
 *   - ENTER / LEAVE                             (0xC8, 0xC9)
 *   - IMUL reg, r/m, imm16 / imm8              (0x69, 0x6B)
 *   - SHL/SHR/SAR r/m16, imm8                  (0xC1)
 *
 * Same byte sequences as the NASM test so both stay in sync.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

/* ---------- minimal flat RAM ---------- */
#define RAM_SIZE 0x10000
static unsigned char ram[RAM_SIZE];

static unsigned char  r8 (void *e, unsigned long a)
	{ (void)e; return ram[a & 0xffff]; }
static unsigned short r16(void *e, unsigned long a)
	{ (void)e; a &= 0xffff;
	  return (unsigned short)ram[a] | ((unsigned short)ram[(a+1)&0xffff]<<8); }
static void w8 (void *e, unsigned long a, unsigned char  v)
	{ (void)e; ram[a & 0xffff] = v; }
static void w16(void *e, unsigned long a, unsigned short v)
	{ (void)e; a &= 0xffff; ram[a]=v&0xff; ram[(a+1)&0xffff]=v>>8; }

static e8086_t *make_186 (void)
{
	e8086_t *c = e86_new();
	e86_set_80186 (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	/* CS=0, IP=0x0100, SS=0, SP=0x0300 */
	e86_set_cs (c, 0);
	e86_set_ip (c, 0x0100);
	e86_set_ss (c, 0);
	e86_set_sp (c, 0x0300);
	return c;
}

/* Run until IP advances past 0x0100 start */
static void run1 (e8086_t *c)
{
	unsigned short ip = e86_get_ip (c);
	while (e86_get_ip (c) == ip) e86_clock (c, 1);
}

/* Run N instructions from current IP */
static void runN (e8086_t *c, int n)
{
	int i;
	for (i = 0; i < n; i++) run1 (c);
}

static int pass = 1;
#define CHECK(label, cond) \
	do { if (cond) { printf ("PASS %s\n", label); } \
	     else { printf ("FAIL %s\n", label); pass = 0; } } while (0)

#define CHECK_VAL(label, got, want) \
	do { if ((unsigned long)(got) == (unsigned long)(want)) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s  got=0x%04lX  want=0x%04lX\n", \
			label, (unsigned long)(got), (unsigned long)(want)); \
		pass = 0; \
	} } while (0)

int main (void)
{
	e8086_t *c;

	/* ==================================================================
	 * Test 1: PUSH imm8 (sign-extended)  — opcode 0x6A imm8
	 * push 0x42 → AX should be 0x0042 after pop
	 * ================================================================== */
	memset (ram, 0, sizeof ram);
	c = make_186 ();
	{
		/* 6A 42  — PUSH byte 0x42 (sign-extended to 0x0042) */
		/* 58     — POP AX */
		unsigned char insn[] = { 0x6a, 0x42, 0x58 };
		memcpy (ram + 0x100, insn, sizeof insn);
		run1 (c);   /* PUSH imm8 */
		run1 (c);   /* POP AX   */
		CHECK_VAL ("PUSH imm8 sign-extend", e86_get_ax (c), 0x0042);
	}
	e86_del (c);

	/* ==================================================================
	 * Test 2: PUSH imm16  — opcode 0x68 imm16
	 * push 0x1234 → AX should be 0x1234 after pop
	 * ================================================================== */
	memset (ram, 0, sizeof ram);
	c = make_186 ();
	{
		/* 68 34 12  — PUSH word 0x1234 */
		/* 58        — POP AX           */
		unsigned char insn[] = { 0x68, 0x34, 0x12, 0x58 };
		memcpy (ram + 0x100, insn, sizeof insn);
		run1 (c);
		run1 (c);
		CHECK_VAL ("PUSH imm16", e86_get_ax (c), 0x1234);
	}
	e86_del (c);

	/* ==================================================================
	 * Test 3: PUSHA / POPA round-trip
	 * Set AX=1111 CX=2222 DX=3333 BX=4444 BP=5555 SI=6666 DI=7777,
	 * PUSHA, zero all, POPA — verify all restored.
	 * ================================================================== */
	memset (ram, 0, sizeof ram);
	c = make_186 ();
	{
		/* 60 = PUSHA, 61 = POPA */
		unsigned char insn[] = { 0x60, 0x61 };
		memcpy (ram + 0x100, insn, sizeof insn);

		e86_set_ax (c, 0x1111);
		e86_set_cx (c, 0x2222);
		e86_set_dx (c, 0x3333);
		e86_set_bx (c, 0x4444);
		e86_set_bp (c, 0x5555);
		e86_set_si (c, 0x6666);
		e86_set_di (c, 0x7777);

		run1 (c);   /* PUSHA */

		/* Zero everything */
		e86_set_ax (c, 0); e86_set_cx (c, 0); e86_set_dx (c, 0);
		e86_set_bx (c, 0); e86_set_bp (c, 0); e86_set_si (c, 0);
		e86_set_di (c, 0);

		run1 (c);   /* POPA */

		CHECK_VAL ("PUSHA/POPA AX", e86_get_ax (c), 0x1111);
		CHECK_VAL ("PUSHA/POPA CX", e86_get_cx (c), 0x2222);
		CHECK_VAL ("PUSHA/POPA DX", e86_get_dx (c), 0x3333);
		CHECK_VAL ("PUSHA/POPA BX", e86_get_bx (c), 0x4444);
		CHECK_VAL ("PUSHA/POPA BP", e86_get_bp (c), 0x5555);
		CHECK_VAL ("PUSHA/POPA SI", e86_get_si (c), 0x6666);
		CHECK_VAL ("PUSHA/POPA DI", e86_get_di (c), 0x7777);
	}
	e86_del (c);

	/* ==================================================================
	 * Test 4: ENTER 8, 0 / LEAVE
	 * ENTER: push BP, BP=SP, SP=SP-8
	 * LEAVE: SP=BP, pop BP
	 * ================================================================== */
	memset (ram, 0, sizeof ram);
	c = make_186 ();
	{
		/* C8 08 00 00  — ENTER 8, 0  (imm16=8, level=0) */
		/* C9           — LEAVE */
		unsigned char insn[] = { 0xc8, 0x08, 0x00, 0x00, 0xc9 };
		memcpy (ram + 0x100, insn, sizeof insn);

		e86_set_bp (c, 0xBEEF);
		e86_set_sp (c, 0x0300);

		run1 (c);   /* ENTER */

		/* After ENTER 8,0:
		 *   push BP (old=0xBEEF) → SP = 0x02FE
		 *   BP = SP = 0x02FE
		 *   SP = BP - 8 = 0x02F6 */
		unsigned short bp_after = e86_get_bp (c);
		unsigned short sp_after = e86_get_sp (c);
		CHECK_VAL ("ENTER BP = old_SP-2", bp_after, 0x02FE);
		CHECK_VAL ("ENTER SP = BP-8",     sp_after, 0x02F6);

		run1 (c);   /* LEAVE */

		/* After LEAVE: SP=BP=0x02FE, pop → BP=0xBEEF, SP=0x0300 */
		CHECK_VAL ("LEAVE BP restored", e86_get_bp (c), 0xBEEF);
		CHECK_VAL ("LEAVE SP restored", e86_get_sp (c), 0x0300);
	}
	e86_del (c);

	/* ==================================================================
	 * Test 5: IMUL reg16, r/m16, imm8  — opcode 0x6B /r imm8
	 * CX = AX * 7   (10 * 7 = 70)
	 * CX = AX * 4   (-3 * 4 = -12 = 0xFFF4)
	 * ================================================================== */
	memset (ram, 0, sizeof ram);
	c = make_186 ();
	{
		/* 6B C8 07  — IMUL CX, AX, 7   (ModRM C8: reg=CX /r=1, rm=AX /r=0) */
		/* 6B C8 04  — IMUL CX, AX, 4 */
		unsigned char insn[] = { 0x6b, 0xc8, 0x07, 0x6b, 0xc8, 0x04 };
		memcpy (ram + 0x100, insn, sizeof insn);

		e86_set_ax (c, 10);
		run1 (c);
		CHECK_VAL ("IMUL CX=10*7=70",  e86_get_cx (c), 70);

		e86_set_ax (c, (unsigned short)-3);
		run1 (c);
		CHECK_VAL ("IMUL CX=-3*4=-12", e86_get_cx (c), (unsigned short)-12);
	}
	e86_del (c);

	/* ==================================================================
	 * Test 6: Shift by immediate  — opcode 0xC1
	 * SHL AX, 3   : 0x0001 << 3 = 0x0008   (C1 E0 03)
	 * SHR AX, 3   : 0x0080 >> 3 = 0x0010   (C1 E8 03)
	 * SAR AX, 4   : 0xFF00 >> 4 = 0xFFF0   (C1 F8 04)
	 * ================================================================== */
	memset (ram, 0, sizeof ram);
	c = make_186 ();
	{
		/* C1 E0 03  — SHL AX, 3  (/4 = SHL, reg AX=0) */
		/* C1 E8 03  — SHR AX, 3  (/5 = SHR, reg AX=0) */
		/* C1 F8 04  — SAR AX, 4  (/7 = SAR, reg AX=0) */
		unsigned char insn[] = {
			0xc1, 0xe0, 0x03,
			0xc1, 0xe8, 0x03,
			0xc1, 0xf8, 0x04
		};
		memcpy (ram + 0x100, insn, sizeof insn);

		e86_set_ax (c, 0x0001);
		run1 (c);
		CHECK_VAL ("SHL AX,3", e86_get_ax (c), 0x0008);

		e86_set_ax (c, 0x0080);
		run1 (c);
		CHECK_VAL ("SHR AX,3", e86_get_ax (c), 0x0010);

		e86_set_ax (c, 0xFF00);
		run1 (c);
		CHECK_VAL ("SAR AX,4", e86_get_ax (c), 0xFFF0);
	}
	e86_del (c);

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
