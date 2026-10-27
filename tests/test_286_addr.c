/*
 * tests/test_286_addr.c
 *
 * Unit test for T3-A item 1: 80286 24-bit address bus.
 *
 * Verifies:
 *   1. e86_set_80286 sets addr_mask to 0xFFFFFF.
 *   2. e86_reset on an 80286 CPU preserves addr_mask = 0xFFFFFF (not reverted
 *      to the 8086 default of 0xFFFFF).
 *   3. Default (8086) CPU keeps addr_mask = 0xFFFFF after reset.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

static int pass = 1;

#define CHECK(label, cond) \
	do { if (cond) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s\n", label); \
		pass = 0; \
	} } while (0)

static unsigned char ram[0x1000000];

static
unsigned char read8 (void *ext, unsigned long addr)
{
	(void) ext;
	return (addr < sizeof (ram) ? ram[addr] : 0xff);
}

static
unsigned short read16 (void *ext, unsigned long addr)
{
	return (read8 (ext, addr) | (read8 (ext, addr + 1) << 8));
}

static
void check_reset (int direct)
{
	e8086_t *c;
	unsigned i;

	memset (ram, 0x90, sizeof (ram));
	c = e86_new ();
	if (c == NULL) {
		CHECK ("CPU allocation", 0);
		return;
	}
	e86_set_80286 (c);
	e86_set_mem (c, NULL, read8, NULL, read16, NULL);
	e86_set_ram (c, ram, direct ? sizeof (ram) : 0);
	c->idtr_base = 0x123400; c->idtr_limit = 7;
	c->cpu |= E86_CPU_INT7;
	e86_reset (c);
	CHECK ("286 reset MSW", c->msw == 0xfff0);
	CHECK ("286 reset IDTR", c->idtr_base == 0 && c->idtr_limit == 0x3ff);
	CHECK ("286 reset clears EM execution mode", !(c->cpu & E86_CPU_INT7));
	CHECK ("286 reset CS visible", e86_get_cs (c) == 0xf000);
	CHECK ("286 reset IP", e86_get_ip (c) == 0xfff0);
	CHECK ("286 reset hidden CS base", c->seg_base[E86_REG_CS] == 0xff0000);

	/* The low and high reset windows deliberately contain different code. */
	ram[0xfffff0] = 0xeb;
	ram[0xfffff1] = 2;
	ram[0xfffff4] = 0xea;
	ram[0xfffff5] = 0x00;
	ram[0xfffff6] = 0x01;
	ram[0xfffff7] = 0x00;
	ram[0xfffff8] = 0xf0;
	ram[0xf0100] = 0xb8;
	ram[0xf0101] = 0x34;
	ram[0xf0102] = 0x12;
	e86_execute (c);
	CHECK ("reset instruction fetched above 15 MB", e86_get_ip (c) == 0xfff4);
	CHECK ("near jump retains reset CS base", c->seg_base[E86_REG_CS] == 0xff0000);
	e86_execute (c);
	CHECK ("far jump loads ordinary real-mode CS base", c->seg_base[E86_REG_CS] == 0xf0000);
	CHECK ("far jump reaches low BIOS", e86_get_ip (c) == 0x0100);
	e86_execute (c);
	CHECK ("low BIOS instruction executes", e86_get_ax (c) == 0x1234);

	/* Prefetch at the end of the segment must use the same hidden base. */
	e86_reset (c);
	e86_set_ip (c, 0xfffd);
	for (i = 0; i < c->pq_fill; i++) {
		ram[0xff0000 + ((0xfffd + i) & 0xffff)] = 0x40 + i;
	}
	e86_pq_fill (c);
	for (i = 0; i < c->pq_fill; i++) {
		CHECK ("reset-base prefetch at segment boundary", c->pq[i] == (i < 3 ? 0x40 + i : 0));
	}

	/* LOADALL can install a non-paragraph CS base while PE is clear. */
	c->seg_base[E86_REG_CS] = 0x200000;
	e86_set_ip (c, 0x0100);
	c->pq_cnt = 0;
	ram[0x200100] = 0xb8;
	ram[0x200101] = 0x78;
	ram[0x200102] = 0x56;
	e86_execute (c);
	CHECK ("real-mode prefetch retains loaded CS cache", e86_get_ax (c) == 0x5678);
	e86_set_sreg (c, E86_REG_CS, 0x1234);
	CHECK ("explicit real-mode CS load refreshes cache", c->seg_base[E86_REG_CS] == 0x12340);
	e86_del (c);
}

/* Equal visible selectors may retain different caches after descriptor edits
	* and reloads. LOADALL also permits non-paragraph bases with PE clear. */
static void write8 (void *ext, unsigned long addr, unsigned char value)
{
	(void) ext;
	if (addr < sizeof ram) ram[addr] = value;
}

static void write16 (void *ext, unsigned long addr, unsigned short value)
{
	write8 (ext, addr, value & 255);
	write8 (ext, addr + 1, value >> 8);
}

static void check_operand_caches (int pm, int direct)
{
	e8086_t *c = e86_new ();
	unsigned i;
	static const unsigned char code[] = {
		0x8b,0x07,             /* MOV AX,[BX] -- DS */
		0x8b,0x56,0x00,        /* MOV DX,[BP] -- SS */
		0x26,0x8b,0x0f,        /* MOV CX,ES:[BX] */
		0x89,0x07,             /* MOV [BX],AX */
		0xa4,                 /* MOVSB DS:SI -> ES:DI */
		0x50,0x5b,            /* PUSH AX; POP BX */
		0xa1,0x00,0x01,      /* MOV AX,DS:[0100] */
		0xc8,0x00,0x00,0x02  /* ENTER 0,2: read old frame via SS */
	};
	memset (ram, 0, sizeof ram);
	e86_set_80286 (c);
	e86_set_mem (c, NULL, read8, write8, read16, write16);
	e86_set_ram (c, ram, direct ? sizeof ram : 0);
	e86_reset (c);
	e86_set_cs (c, 0x1000);
	e86_set_ip (c, 0x100);
	e86_set_ds (c, 0x20); e86_set_ss (c, 0x20); e86_set_es (c, 0x20);
	c->msw = pm ? E286_MSW_PE : 0;
	for (i = 0; i < 4; i++) { c->seg_limit[i] = 0xffff; c->seg_ar[i] = 0x93; }
	c->seg_ar[E86_REG_CS] = 0x9b;
	c->seg_base[E86_REG_DS] = 0x20000;
	c->seg_base[E86_REG_ES] = 0x30000;
	c->seg_base[E86_REG_SS] = 0x40000;
	memcpy (ram + 0x10100, code, sizeof code);
	ram[0x20100] = 0x34; ram[0x20101] = 0x12;
	ram[0x30100] = 0x78; ram[0x30101] = 0x56;
	ram[0x40100] = 0xbc; ram[0x40101] = 0x9a;
	e86_set_bx (c, 0x100); e86_set_bp (c, 0x100);
	e86_set_si (c, 0x100); e86_set_di (c, 0x110); e86_set_sp (c, 0x200);
	e86_execute (c);
	CHECK ("MOV uses DS cache despite equal ES selector", e86_get_ax (c) == 0x1234);
	e86_execute (c);
	CHECK ("BP operand uses SS cache", e86_get_dx (c) == 0x9abc);
	e86_execute (c);
	CHECK ("ES override uses ES cache", e86_get_cx (c) == 0x5678);
	e86_set_ax (c, 0x4321); e86_execute (c);
	CHECK ("MOV writes through DS cache", ram[0x20100] == 0x21 && ram[0x30100] == 0x78);
	e86_execute (c);
	CHECK ("MOVSB keeps source/destination caches distinct", ram[0x30110] == 0x21);
	e86_execute (c); e86_execute (c);
	CHECK ("stack uses SS cache", ram[0x401fe] == 0x21 && e86_get_bx (c) == 0x4321);
	e86_execute (c);
	CHECK ("absolute operand uses DS cache", e86_get_ax (c) == 0x4321);
	ram[0x400fe] = 0xf0; ram[0x400ff] = 0xde;
	e86_execute (c);
	CHECK ("ENTER copies frame through SS", ram[0x401fc] == 0xf0 && ram[0x401fd] == 0xde);
	CHECK ("nested ENTER retains all frame words", e86_get_sp (c) == 0x1fa);
	e86_del (c);
}

int main (void)
{
	e8086_t *c;

	check_operand_caches (0, 0);
	check_operand_caches (0, 1);
	check_operand_caches (1, 0);
	check_operand_caches (1, 1);
	check_reset (0);
	check_reset (1);

	/* ----------------------------------------------------------------
	 * Test 1: default (8086) addr_mask = 0xFFFFF
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	CHECK ("8086 addr_mask = 0xFFFFF",
		e86_get_addr_mask (c) == 0xffffful);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 2: e86_set_80286 sets addr_mask = 0xFFFFFF
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	e86_set_80286 (c);
	CHECK ("80286 addr_mask = 0xFFFFFF after set",
		e86_get_addr_mask (c) == 0xfffffful);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 3: e86_reset on 80286 preserves 24-bit mask
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	e86_set_80286 (c);
	e86_reset (c);
	CHECK ("80286 addr_mask = 0xFFFFFF after reset",
		e86_get_addr_mask (c) == 0xfffffful);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 4: e86_reset on default CPU keeps 20-bit mask
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	e86_reset (c);
	CHECK ("8086 addr_mask = 0xFFFFF after reset",
		e86_get_addr_mask (c) == 0xffffful);
	e86_del (c);

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
