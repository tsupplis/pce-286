/*
 * tests/test_286_idt.c
 *
 * Unit tests for T3-A item 4: 80286 protected-mode interrupt delivery via IDT.
 *
 * Tests:
 *   1. Real-mode INT still reads from IVT (regression check).
 *   2. PM interrupt gate (ar=0x86): reads gate from IDT, loads CS:IP, clears IF.
 *   3. PM trap gate (ar=0x87): reads gate from IDT, loads CS:IP, preserves IF.
 *   4. Stack frame: FLAGS, CS, IP are pushed in correct order.
 *   5. PM INT with interrupt gate clears IF (IF was set before).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

#define RAM_SIZE 0x100000
static unsigned char ram[RAM_SIZE];

static unsigned char  r8 (void *e, unsigned long a)
	{ (void)e; return (a < RAM_SIZE) ? ram[a] : 0xff; }
static unsigned short r16(void *e, unsigned long a)
	{ (void)e; if (a+1 < RAM_SIZE)
	    return (unsigned short)ram[a] | ((unsigned short)ram[a+1]<<8);
	  return 0xffff; }
static void w8 (void *e, unsigned long a, unsigned char  v)
	{ (void)e; if (a < RAM_SIZE) ram[a] = v; }
static void w16(void *e, unsigned long a, unsigned short v)
	{ (void)e; if (a+1 < RAM_SIZE) { ram[a]=v&0xff; ram[a+1]=v>>8; } }

static e8086_t *make_286 (void)
{
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_reset (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	c->ram     = ram;
	c->ram_cnt = RAM_SIZE;
	e86_set_cs (c, 0x0000);
	e86_set_ip (c, 0x0100);
	e86_set_ss (c, 0x0000);
	e86_set_sp (c, 0x0400);
	return c;
}

static int pass = 1;

#define CHECK(label, cond) \
	do { if (cond) { printf ("PASS %s\n", label); } \
	     else      { printf ("FAIL %s\n", label); pass = 0; } } while (0)

#define CHECK_VAL(label, got, want) \
	do { if ((unsigned long)(got) == (unsigned long)(want)) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s  got=0x%lX  want=0x%lX\n", \
			label, (unsigned long)(got), (unsigned long)(want)); \
		pass = 0; \
	} } while (0)

/* Write an IDT interrupt/trap gate at byte offset `off` in ram[]:
 *   [0..1] = IP (offset low)
 *   [2..3] = CS selector
 *   [4]    = 0 (reserved)
 *   [5]    = ar  (0x86=int gate, 0x87=trap gate)
 *   [6..7] = 0 (offset high, always 0 on 286)
 */
static void write_idt_gate (unsigned long off, unsigned short ip,
	unsigned short cs, unsigned char ar)
{
	ram[off + 0] = ip & 0xff;
	ram[off + 1] = (ip >> 8) & 0xff;
	ram[off + 2] = cs & 0xff;
	ram[off + 3] = (cs >> 8) & 0xff;
	ram[off + 4] = 0;
	ram[off + 5] = ar;
	ram[off + 6] = 0;
	ram[off + 7] = 0;
}

/* Write a GDT descriptor at `off` */
static void write_desc (unsigned long off, unsigned short limit,
	unsigned long base, unsigned char ar)
{
	ram[off + 0] = limit & 0xff;
	ram[off + 1] = (limit >> 8) & 0xff;
	ram[off + 2] = base & 0xff;
	ram[off + 3] = (base >> 8) & 0xff;
	ram[off + 4] = (base >> 16) & 0xff;
	ram[off + 5] = ar;
	ram[off + 6] = 0;
	ram[off + 7] = 0;
}

int main (void)
{
	e8086_t *c;
	unsigned short sp_before, flags_on_stack, cs_on_stack, ip_on_stack;

	/* ----------------------------------------------------------------
	 * Test 1: real-mode INT still reads IVT (regression)
	 *   Vector 0x20 → IVT at 0x0000:0x0080 = 0x00000080
	 *   IVT[0x80..0x81] = IP=0x1234, IVT[0x82..0x83] = CS=0x0000
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	/* IVT entry for vector 0x20 at phys 0x80 (0x20*4=0x80) */
	ram[0x0080] = 0x34; ram[0x0081] = 0x12;   /* IP = 0x1234 */
	ram[0x0082] = 0x00; ram[0x0083] = 0x00;   /* CS = 0x0000 */
	/* real mode: msw=0 */
	e86_trap (c, 0x20);
	CHECK_VAL ("real-mode INT20 CS", e86_get_cs(c), 0x0000);
	CHECK_VAL ("real-mode INT20 IP", e86_get_ip(c), 0x1234);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 2: PM interrupt gate — CS:IP from IDT, IF cleared
	 *   GDT at 0x002000: null + code seg (sel=0x08, base=0x030000)
	 *   IDT at 0x003000: vector 0x0D → gate{IP=0x0050, CS=0x0008, ar=0x86}
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	write_desc (0x002000, 0, 0, 0);                 /* GDT null */
	write_desc (0x002008, 0xffff, 0x030000, 0x9b);  /* GDT sel 0x08 */
	c->gdtr_base  = 0x002000;
	c->gdtr_limit = 0x0f;
	write_idt_gate (0x003068, 0x0050, 0x0008, 0x86); /* IDT vec 0x0D (0x0D*8=0x68) */
	c->idtr_base  = 0x003000;
	c->idtr_limit = 0xff;
	c->msw = E286_MSW_PE;
	/* Set IF=1 so we can verify it gets cleared */
	c->flg |= E86_FLG_I;

	e86_trap (c, 0x0d);

	CHECK_VAL ("PM int gate CS sel",   e86_get_cs(c), 0x0008);
	CHECK_VAL ("PM int gate IP",       e86_get_ip(c), 0x0050);
	CHECK_VAL ("PM int gate seg_base", c->seg_base[E86_REG_CS], 0x030000UL);
	CHECK     ("PM int gate IF clear", (c->flg & E86_FLG_I) == 0);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 3: PM trap gate — IF preserved
	 *   Same setup but ar=0x87 (trap gate)
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	write_desc (0x002000, 0, 0, 0);
	write_desc (0x002008, 0xffff, 0x030000, 0x9b);
	c->gdtr_base = 0x002000;
	c->gdtr_limit = 0x1f;
	write_idt_gate (0x003068, 0x0060, 0x0008, 0x87); /* trap gate */
	c->idtr_base = 0x003000;
	c->idtr_limit = 0xff;
	c->msw = E286_MSW_PE;
	c->flg |= E86_FLG_I;    /* IF=1 before trap */

	e86_trap (c, 0x0d);

	CHECK_VAL ("PM trap gate IP", e86_get_ip(c), 0x0060);
	CHECK     ("PM trap gate IF preserved", (c->flg & E86_FLG_I) != 0);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 4: Stack frame — FLAGS, CS, IP pushed correctly
	 *   SS:SP at 0x0000:0x0400; after INT 3 pushes should be at 0x03FA
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	write_desc (0x002000, 0, 0, 0);
	write_desc (0x002008, 0xffff, 0x030000, 0x9b);
	c->gdtr_base = 0x002000;
	c->gdtr_limit = 0x1f;
	/* SS descriptor: sel=0x10, base=0x000000 (identity map for simplicity) */
	write_desc (0x002010, 0xffff, 0x000000, 0x93);
	e86_set_ss (c, 0x10);
	c->seg_base[E86_REG_SS] = 0x000000;
	e86_set_sp (c, 0x0400);
	/* CS: use sel 0x08 */
	e86_set_cs (c, 0x08);
	c->seg_base[E86_REG_CS] = 0x030000;
	e86_set_ip (c, 0x0200);
	/* Set known flags */
	c->flg = 0x0246;   /* ZF|PF|IF */
	write_idt_gate (0x003018, 0x0070, 0x0008, 0x86); /* vec 3: 0x03*8=0x18 */
	c->idtr_base = 0x003000;
	c->idtr_limit = 0xff;
	c->msw = E286_MSW_PE;

	sp_before = e86_get_sp (c);
	e86_trap (c, 3);

	/* SP must have moved down by 6 (3 x 16-bit pushes) */
	CHECK_VAL ("PM INT stack SP-6",
		e86_get_sp(c), (unsigned short)(sp_before - 6));

	/* Stack frame at new SS:SP (SS base=0, SP=0x03FA):
	 *   [0x03FA..0x03FB] = IP = 0x0200
	 *   [0x03FC..0x03FD] = CS = 0x0008
	 *   [0x03FE..0x03FF] = FLAGS = 0x0246 */
	ip_on_stack    = (unsigned short)ram[0x03fa] | ((unsigned short)ram[0x03fb] << 8);
	cs_on_stack    = (unsigned short)ram[0x03fc] | ((unsigned short)ram[0x03fd] << 8);
	flags_on_stack = (unsigned short)ram[0x03fe] | ((unsigned short)ram[0x03ff] << 8);
	CHECK_VAL ("PM INT stack IP",    ip_on_stack,    0x0200);
	CHECK_VAL ("PM INT stack CS",    cs_on_stack,    0x0008);
	CHECK_VAL ("PM INT stack FLAGS", flags_on_stack, 0x0246);
	e86_del (c);

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
