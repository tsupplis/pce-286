/*
 * tests/test_286_flags.c
 *
 * Unit tests for 80286 real-mode flag behaviour:
 *
 *   PUSHF on 286: bits 12-15 (IOPL/NT) pushed as 0
 *   PUSHF on 8086: bits 12-15 pushed as 1
 *   POPF  on 286: bits 12-15 (IOPL/NT) not writable — stay 0
 *   POPF  on 8086: bits 12-15 stay 1 after pop
 *   IRET  on 286: restored flags have bits 12-15 = 0
 *   IRET  on 8086: restored flags keep bits 12-15 from stack
 *
 * Also checks 286 SS-load single-step suppression, exercised by CheckIt
 * "CPU Interrupt Bug" (POP SS followed by DEC CX).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

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

static e8086_t *make_cpu (void (*model_fn)(e8086_t *))
{
	e8086_t *c = e86_new ();
	model_fn (c);
	e86_reset (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	e86_set_ss (c, 0);
	e86_set_sp (c, 0x0200);
	e86_set_cs (c, 0);
	e86_set_ip (c, 0x0100);
	return c;
}

/* Run one instruction */
static void run1 (e8086_t *c)
{
	unsigned short ip = e86_get_ip (c);
	while (e86_get_ip (c) == ip && !(c->state & E86_STATE_SHUTDOWN)) e86_clock (c, 1);
}

static int pass = 1;

#define CHECK(label, cond) \
	do { if (cond) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s\n", label); \
		pass = 0; \
	} } while (0)

#define CHECK_VAL(label, got, want) \
	do { if ((unsigned)(got) == (unsigned)(want)) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s  got=0x%04X  want=0x%04X\n", \
			label, (unsigned)(got), (unsigned)(want)); \
		pass = 0; \
	} } while (0)

int main (void)
{
	e8086_t *c;
	unsigned short stk;

	/* ----------------------------------------------------------------
	 * Test 1: PUSHF on 8086 — bits 12-15 are 1 on stack
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_cpu (e86_set_8086);
	ram[0x0100] = 0x9c;          /* PUSHF */
	run1 (c);
	stk = r16 (NULL, (unsigned long)(e86_get_sp (c)));
	CHECK ("8086 PUSHF bits12-15=1", (stk & 0xf000) == 0xf000);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 2: PUSHF on 80286 — bits 12-15 are 0 on stack
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_cpu (e86_set_80286);
	ram[0x0100] = 0x9c;          /* PUSHF */
	run1 (c);
	stk = r16 (NULL, (unsigned long)(e86_get_sp (c)));
	CHECK ("80286 PUSHF bits12-15=0", (stk & 0xf000) == 0x0000);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 3: POPF on 8086 — bits 12-15 written from stack value
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_cpu (e86_set_8086);
	/* Push 0xF002 (all upper bits 1), then POPF */
	w16 (NULL, 0x01fe, 0xf202);
	e86_set_sp (c, 0x01fe);
	ram[0x0100] = 0x9d;          /* POPF */
	run1 (c);
	/* On 8086, bits 12-15 forced to 1 regardless, bit 1 forced to 1 */
	CHECK_VAL ("8086 POPF flg bits12-15=1", e86_get_flags (c) & 0xf000, 0xf000);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 4: POPF on 80286 — bits 12-15 NOT written (stay 0)
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_cpu (e86_set_80286);
	/* Push 0xF202 (IOPL=3, IF=1), then POPF */
	w16 (NULL, 0x01fe, 0xf202);
	e86_set_sp (c, 0x01fe);
	ram[0x0100] = 0x9d;          /* POPF */
	run1 (c);
	/* On 286, bits 12-15 must stay 0 */
	CHECK_VAL ("80286 POPF flg bits12-15=0", e86_get_flags (c) & 0xf000, 0x0000);
	/* IF (bit 9) must be writable */
	CHECK ("80286 POPF IF bit writable", (e86_get_flags (c) & 0x0200) != 0);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 5: IRET on 8086 — restored flags keep bits 12-15
	 * IRET pops: [SP]=IP, [SP+2]=CS, [SP+4]=FLAGS
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_cpu (e86_set_8086);
	/* Stack layout at SP=0x01fa: IP at 01fa, CS at 01fc, FLAGS at 01fe */
	w16 (NULL, 0x01fa, 0x0110);  /* IP  → land at CS:0x0110 */
	w16 (NULL, 0x01fc, 0x0000);  /* CS */
	w16 (NULL, 0x01fe, 0xf202);  /* FLAGS with IOPL=3, IF=1 */
	e86_set_sp (c, 0x01fa);
	ram[0x0100] = 0xcf;          /* IRET */
	run1 (c);
	/* 8086: bits 12-15 restored as-is from stack */
	CHECK_VAL ("8086 IRET flags bits12-15=1", e86_get_flags (c) & 0xf000, 0xf000);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 6: IRET on 80286 — restored flags bits 12-15 = 0
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_cpu (e86_set_80286);
	/* Stack layout at SP=0x01fa: IP at 01fa, CS at 01fc, FLAGS at 01fe */
	w16 (NULL, 0x01fa, 0x0110);  /* IP */
	w16 (NULL, 0x01fc, 0x0000);  /* CS */
	w16 (NULL, 0x01fe, 0xf202);  /* FLAGS with IOPL=3, IF=1 */
	e86_set_sp (c, 0x01fa);
	ram[0x0100] = 0xcf;          /* IRET */
	run1 (c);
	/* 286: bits 12-15 cleared, IF (bit 9) restored */
	CHECK_VAL ("80286 IRET flags bits12-15=0", e86_get_flags (c) & 0xf000, 0x0000);
	CHECK ("80286 IRET IF bit restored", (e86_get_flags (c) & 0x0200) != 0);
	e86_del (c);

	/* SS loads suppress the debug trap until the following instruction.
	 * CheckIt uses POP SS; cover MOV SS and a non-SS control too. */
	for (unsigned kind = 0; kind < 3; kind++) {
		unsigned next = kind ? 0x0102 : 0x0101;
		memset (ram, 0, sizeof ram);
		c = make_cpu (e86_set_80286);
		w16 (NULL, 4, 0x0300);
		w16 (NULL, 6, 0);
		e86_set_ax (c, 0);
		e86_set_cx (c, 1);
		e86_set_flags (c, 0x0102);
		if (kind == 0) {
			ram[0x0100] = 0x17; /* POP SS */
			w16 (NULL, 0x0200, 0);
		}
		else {
			ram[0x0100] = 0x8e;
			ram[0x0101] = kind == 1 ? 0xd0 : 0xd8; /* SS/DS, AX */
		}
		ram[next] = 0x49; /* DEC CX */
		run1 (c);
		if (kind < 2) {
			CHECK_VAL ("286 SS load defers single step", e86_get_ip (c), next);
			run1 (c);
			CHECK_VAL ("286 instruction after SS load executes", e86_get_cx (c), 0);
			CHECK_VAL ("286 deferred trap delivered", e86_get_ip (c), 0x0300);
			CHECK_VAL ("286 deferred trap return IP", r16 (NULL, e86_get_sp (c)), next + 1);
		}
		else {
			CHECK_VAL ("286 DS load does not suppress trap", e86_get_ip (c), 0x0300);
			CHECK_VAL ("286 DS trap precedes next instruction", e86_get_cx (c), 1);
		}
		e86_del (c);
	}

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
