/*
 * tests/test_loadall.c
 *
 * Unit tests for T7-A: LOADALL (0F 05) instruction.
 *
 * The LOADALL table sits at physical 0x000800 (102 bytes).
 * We write the table directly into ram[], execute the opcode,
 * then verify all CPU fields were loaded correctly.
 *
 * Tests (T1–T14) mirror the unit-test plan from the T7-A Findings section
 * of cpu-enhancement-plan.md.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

#define RAM_SIZE  0x110000   /* 1 MB + a bit, covers 0x000800 table */
#define TBL_BASE  0x000800   /* fixed physical address of LOADALL table */
#define CODE_SEG  0x0000
#define CODE_IP   0x0200     /* place test opcodes here */

static unsigned char ram[RAM_SIZE];

/* Memory callbacks */
static unsigned char  r8 (void *e, unsigned long a)
	{ (void)e; return (a < RAM_SIZE) ? ram[a] : 0xff; }
static unsigned short r16(void *e, unsigned long a)
	{ (void)e; if (a + 1 < RAM_SIZE)
	    return (unsigned short)ram[a] | ((unsigned short)ram[a + 1] << 8);
	  return 0xffff; }
static void w8 (void *e, unsigned long a, unsigned char v)
	{ (void)e; if (a < RAM_SIZE) ram[a] = v; }
static void w16(void *e, unsigned long a, unsigned short v)
	{ (void)e; if (a + 1 < RAM_SIZE) { ram[a] = v & 0xff; ram[a + 1] = v >> 8; } }

/* Write a 16-bit little-endian value into the LOADALL table */
static void tbl_w16 (unsigned short ofs, unsigned short val)
{
	unsigned long addr = TBL_BASE + ofs;
	ram[addr]     = val & 0xff;
	ram[addr + 1] = (val >> 8) & 0xff;
}

/* Write an 8-bit value into the LOADALL table */
static void tbl_w8 (unsigned short ofs, unsigned char val)
{
	ram[TBL_BASE + ofs] = val;
}

/* Write a 24-bit base (3 bytes) into the LOADALL table */
static void tbl_wbase (unsigned short ofs, unsigned long base)
{
	ram[TBL_BASE + ofs]     = base & 0xff;
	ram[TBL_BASE + ofs + 1] = (base >>  8) & 0xff;
	ram[TBL_BASE + ofs + 2] = (base >> 16) & 0xff;
}

static e8086_t *make_286 (void)
{
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	c->ram     = ram;
	c->ram_cnt = RAM_SIZE;
	/* Place execution at CODE_SEG:CODE_IP */
	e86_set_cs (c, CODE_SEG);
	e86_set_ip (c, CODE_IP);
	e86_set_ss (c, 0);
	e86_set_sp (c, 0x0400);
	e86_set_ds (c, 0);
	e86_set_es (c, 0);
	return (c);
}

/* Write the LOADALL opcode (0F 05) at the current CS:IP */
static void place_loadall (e8086_t *c)
{
	unsigned long phys = ((unsigned long)e86_get_cs (c) << 4) + e86_get_ip (c);
	ram[phys]     = 0x0F;
	ram[phys + 1] = 0x05;
}

/* Execute exactly one instruction */
static void run1 (e8086_t *c)
{
	unsigned short ip = e86_get_ip (c);
	while (e86_get_ip (c) == ip) {
		e86_clock (c, 1);
	}
}

static int pass = 1;

#define CHECK(label, cond) \
	do { if (cond) { printf ("PASS %s\n", label); } \
	     else      { printf ("FAIL %s\n", label); pass = 0; } } while (0)

/* -----------------------------------------------------------------------
 * Helper: reset ram and cpu, write LOADALL at CODE_IP, return fresh cpu.
 * ----------------------------------------------------------------------- */
static e8086_t *fresh (void)
{
	memset (ram, 0, sizeof (ram));
	e8086_t *c = make_286 ();
	place_loadall (c);
	return (c);
}

int main (void)
{
	e8086_t *c;

	/* ------------------------------------------------------------------ */
	/* T1: all-zeroes table → all regs zero, MSW=0, IP=0, CS=0            */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	/* Table is all-zeroes; set known non-zero state first */
	e86_set_ax (c, 0x1234);
	e86_set_cs (c, CODE_SEG);
	e86_set_ip (c, CODE_IP);
	c->msw = 0x0001;  /* PE remains set through LOADALL */
	/* Place HLT at the destination IP=0 so execution stops there */
	ram[0x0000] = 0xF4;

	run1 (c);

	CHECK ("T1 LOADALL retains PE", c->msw == 0x0001);
	CHECK ("T1 AX=0",  e86_get_ax (c) == 0x0000);
	CHECK ("T1 IP=0",  e86_get_ip (c) == 0x0000);
	CHECK ("T1 CS=0",  e86_get_cs (c) == 0x0000);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T2: MSW = 0x0001 → LOADALL can *set* PE                            */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16 (0x06, 0x0001);  /* MSW = PE */
	run1 (c);
	CHECK ("T2 MSW PE set by LOADALL", (c->msw & 0x0001) != 0);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T3: clear PE — start with PE=1, table MSW=0 → PE must remain 1         */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	c->msw = 0x0001;  /* force PE on */
	tbl_w16 (0x06, 0x0000);
	run1 (c);
	CHECK ("T3 LOADALL cannot clear PE", (c->msw & 0x0001) != 0);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T4: FLAGS loaded from +18                                           */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16 (0x18, 0x0246);  /* ZF | PF | IF — typical real-mode FLAGS */
	run1 (c);
	CHECK ("T4 FLAGS loaded", c->flg == 0x0246);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T5: IP loaded from +1A                                              */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16 (0x1A, 0xBEEF);
	/* Place HLT at destination CS:IP so execution stops exactly there */
	ram[0xBEEF] = 0xF4;
	run1 (c);
	CHECK ("T5 IP loaded", e86_get_ip (c) == 0xBEEF);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T6: CS selector and CS descriptor cache loaded                      */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16 (0x22, 0x0020);          /* CS selector */
	/* CS cache at +40: limit=0xFFFF, base=0x200000, AR=0x9B */
	tbl_w16  (0x40, 0xFFFF);         /* limit */
	tbl_wbase(0x3C, 0x200000UL);     /* base 24-bit */
	tbl_w8   (0x3F, 0x9B);           /* AR: present, code, R/X */
	run1 (c);
	CHECK ("T6 CS selector",      e86_get_cs (c) == 0x0020);
	CHECK ("T6 CS seg_limit",     c->seg_limit[E86_REG_CS] == 0xFFFF);
	CHECK ("T6 CS seg_base",      c->seg_base[E86_REG_CS]  == 0x200000UL);
	CHECK ("T6 CS seg_ar",        c->seg_ar[E86_REG_CS]    == 0x9B);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T7: all GP registers loaded from correct offsets                    */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16 (0x26, 0x0001);  /* DI */
	tbl_w16 (0x28, 0x0002);  /* SI */
	tbl_w16 (0x2A, 0x0003);  /* BP */
	tbl_w16 (0x2C, 0x0004);  /* SP */
	tbl_w16 (0x2E, 0x0005);  /* BX */
	tbl_w16 (0x30, 0x0006);  /* DX */
	tbl_w16 (0x32, 0x0007);  /* CX */
	tbl_w16 (0x34, 0x0008);  /* AX */
	run1 (c);
	CHECK ("T7 DI", e86_get_di (c) == 0x0001);
	CHECK ("T7 SI", e86_get_si (c) == 0x0002);
	CHECK ("T7 BP", e86_get_bp (c) == 0x0003);
	CHECK ("T7 SP", e86_get_sp (c) == 0x0004);
	CHECK ("T7 BX", e86_get_bx (c) == 0x0005);
	CHECK ("T7 DX", e86_get_dx (c) == 0x0006);
	CHECK ("T7 CX", e86_get_cx (c) == 0x0007);
	CHECK ("T7 AX", e86_get_ax (c) == 0x0008);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T8: GDT limit and base loaded from +52                             */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16  (0x52, 0x00FF);         /* GDT limit */
	tbl_wbase(0x4E, 0x123456UL);     /* GDT base  */
	run1 (c);
	CHECK ("T8 GDTR limit", c->gdtr_limit == 0x00FF);
	CHECK ("T8 GDTR base",  c->gdtr_base  == 0x123456UL);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T9: IDT limit and base loaded from +5E                             */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16  (0x5E, 0x03FF);         /* IDT limit = 256 vectors */
	tbl_wbase(0x5A, 0x000000UL);     /* IDT base  */
	run1 (c);
	CHECK ("T9 IDTR limit", c->idtr_limit == 0x03FF);
	CHECK ("T9 IDTR base",  c->idtr_base  == 0x000000UL);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T10: LDT selector and cache loaded                                  */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16  (0x1C, 0x0018);         /* LDT selector */
	tbl_w16  (0x58, 0x00FF);         /* LDT cache limit */
	tbl_wbase(0x54, 0x0A0000UL);     /* LDT cache base  */
	run1 (c);
	CHECK ("T10 LDT selector",    c->ldtr_sel   == 0x0018);
	CHECK ("T10 LDT cache limit", c->ldtr_limit == 0x00FF);
	CHECK ("T10 LDT cache base",  c->ldtr_base  == 0x0A0000UL);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T11: TR selector and cache loaded                                   */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16  (0x16, 0x0028);         /* TR selector */
	tbl_w16  (0x64, 0x002B);         /* TR cache limit */
	tbl_wbase(0x60, 0x0B0000UL);     /* TR cache base  */
	run1 (c);
	CHECK ("T11 TR selector",    c->tr_sel   == 0x0028);
	CHECK ("T11 TR cache limit", c->tr_limit == 0x002B);
	CHECK ("T11 TR cache base",  c->tr_base  == 0x0B0000UL);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T12: prefetch queue flushed (pq_cnt == 0) after LOADALL             */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	/* Prefill queue with junk to make the test meaningful */
	c->pq_cnt = 4;
	run1 (c);
	CHECK ("T12 pq_cnt=0 after LOADALL", c->pq_cnt == 0);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T13: EM bit (MSW bit 2) → E86_CPU_INT7 flag set                    */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	tbl_w16 (0x06, 0x0004);  /* MSW: EM=1 */
	run1 (c);
	CHECK ("T13 EM=1 sets E86_CPU_INT7", (c->cpu & E86_CPU_INT7) != 0);
	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* T14: EM=0 → E86_CPU_INT7 cleared even if it was set before          */
	/* ------------------------------------------------------------------ */
	c = fresh ();
	c->cpu |= E86_CPU_INT7;   /* pre-set flag */
	tbl_w16 (0x06, 0x0000);   /* MSW: EM=0 */
	run1 (c);
	CHECK ("T14 EM=0 clears E86_CPU_INT7", (c->cpu & E86_CPU_INT7) == 0);
	e86_del (c);

	puts ("");
	puts (pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
