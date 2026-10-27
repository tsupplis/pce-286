/*
 * tests/test_286_tss.c
 *
 * Unit tests for T3-A item 6: 80286 hardware task switching.
 *
 * Exercises e86_task_switch_pm() indirectly via the four entry points:
 *   op_ea  (JMP far imm)
 *   op_9a  (CALL far imm)
 *   op_ff_03 (CALL m32 — indirect)
 *   op_ff_05 (JMP m32 — indirect)
 *
 * Each test:
 *   1. Builds a minimal GDT with a code descriptor and a TSS descriptor.
 *   2. Prepopulates the target TSS with known register values.
 *   3. Sets the current (old) TSS via tr_base / tr_limit / tr_sel.
 *   4. Executes the far JMP/CALL instruction whose "CS" operand is the
 *      TSS selector.
 *   5. Checks that the new register state was loaded from the TSS and that
 *      the old register state was saved into the old TSS.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

#define RAM_SIZE  0x100000
static unsigned char ram[RAM_SIZE];

/* Memory callbacks */
static unsigned char  r8 (void *e, unsigned long a)
	{ (void)e; return (a < RAM_SIZE) ? ram[a] : 0xff; }
static unsigned short r16(void *e, unsigned long a)
	{ (void)e; if (a+1<RAM_SIZE)
	    return (unsigned short)ram[a] | ((unsigned short)ram[a+1]<<8);
	  return 0xffff; }
static void w8 (void *e, unsigned long a, unsigned char  v)
	{ (void)e; if (a < RAM_SIZE) ram[a] = v; }
static void w16(void *e, unsigned long a, unsigned short v)
	{ (void)e; if (a+1<RAM_SIZE) { ram[a]=v&0xff; ram[a+1]=v>>8; } }

/* Write an 8-byte 286 GDT descriptor */
static void write_desc (unsigned long off, unsigned short limit,
	unsigned long base, unsigned char ar)
{
	ram[off+0]=limit&0xff; ram[off+1]=(limit>>8)&0xff;
	ram[off+2]=base&0xff;  ram[off+3]=(base>>8)&0xff;
	ram[off+4]=(base>>16)&0xff;
	ram[off+5]=ar; ram[off+6]=0; ram[off+7]=0;
}

/* Write a 16-bit value at a physical address */
static void wr16 (unsigned long addr, unsigned short val)
{
	if (addr + 1 < RAM_SIZE) {
		ram[addr]   = val & 0xff;
		ram[addr+1] = (val >> 8) & 0xff;
	}
}

/* 80286 TSS field offsets */
#define TSS_BACKLINK 0x00
#define TSS_IP       0x0E
#define TSS_FLAGS    0x10
#define TSS_AX       0x12
#define TSS_CX       0x14
#define TSS_DX       0x16
#define TSS_BX       0x18
#define TSS_SP       0x1A
#define TSS_BP       0x1C
#define TSS_SI       0x1E
#define TSS_DI       0x20
#define TSS_ES       0x22
#define TSS_CS       0x24
#define TSS_SS       0x26
#define TSS_DS       0x28
#define TSS_LDTR     0x2A

static e8086_t *make_pm (void)
{
	unsigned i;
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f)  r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f) r16, (e86_set_uint16_f) w16);
	c->ram     = ram;
	c->ram_cnt = RAM_SIZE;
	/* Start in protected mode at CS=0, IP=0x0100 */
	e86_set_cs (c, 0);  e86_set_ip (c, 0x0100);
	e86_set_ss (c, 0);  e86_set_sp (c, 0x0400);
	e86_set_ds (c, 0);
	e86_set_es (c, 0);
	/* Sync descriptor cache with the just-set segment registers */
	for (i = 0; i < 4; i++) {
		c->seg_base[i] = (unsigned long)c->sreg[i] << 4;
	}
	c->msw = 0x0001;   /* PE=1 */
	return c;
}

/* Run exactly one instruction (clock until IP changes) */
static void run1 (e8086_t *c)
{
	unsigned short ip = e86_get_ip (c);
	int limit = 1000;
	while (e86_get_ip (c) == ip && --limit > 0) e86_clock (c, 1);
}

static int pass = 1;

#define CHECK_VAL(label, got, want) \
	do { if ((unsigned long)(got)==(unsigned long)(want)) { \
		printf("PASS %s\n",label); \
	} else { \
		printf("FAIL %s  got=0x%lX  want=0x%lX\n", \
			label,(unsigned long)(got),(unsigned long)(want)); \
		pass=0; \
	} } while(0)

#define CHECK(label, cond) \
	do { if (cond) { printf("PASS %s\n",label); } \
	     else      { printf("FAIL %s\n",label); pass=0; } } while(0)

/*
 * GDT layout used by all tests:
 *   0x2000: null descriptor
 *   0x2008: code descriptor  (sel=0x08, base=0, limit=0xFFFF, ar=0x9B)
 *   0x2010: TSS  descriptor  (sel=0x10, base=TSS_BASE, limit=0x2B, ar=0x89)
 *
 * Old TSS base: OLD_TSS_BASE = 0x4000
 * New TSS base: NEW_TSS_BASE = 0x5000
 */
#define GDT_BASE     0x2000UL
#define SEL_CODE     0x0008
#define SEL_TSS_NEW  0x0010
#define OLD_TSS_BASE 0x4000UL
#define NEW_TSS_BASE 0x5000UL

static void setup_gdt_and_tsses (void)
{
	/* GDT */
	write_desc (GDT_BASE + 0x00, 0,      0,            0x00); /* null */
	write_desc (GDT_BASE + 0x08, 0xffff, 0,            0x9b); /* code */
	write_desc (GDT_BASE + 0x10, 0x002b, NEW_TSS_BASE, 0x81); /* new TSS */

	write_desc (GDT_BASE + 0x18, 0xffff, 0, 0x93);
	write_desc (GDT_BASE + 0x20, 0x2b, OLD_TSS_BASE, 0x83);
	write_desc (GDT_BASE + 0x30, 0x2b, OLD_TSS_BASE, 0x83);

	/* Old TSS: all zeros (state will be saved here) */
	memset (ram + OLD_TSS_BASE, 0, 0x2c);

	/* New TSS: preload known register values */
	wr16 (NEW_TSS_BASE + TSS_IP,    0x0200); /* new IP */
	wr16 (NEW_TSS_BASE + TSS_FLAGS, 0x0002); /* minimal flags */
	wr16 (NEW_TSS_BASE + TSS_AX,    0x1111);
	wr16 (NEW_TSS_BASE + TSS_CX,    0x2222);
	wr16 (NEW_TSS_BASE + TSS_DX,    0x3333);
	wr16 (NEW_TSS_BASE + TSS_BX,    0x4444);
	wr16 (NEW_TSS_BASE + TSS_SP,    0x5555);
	wr16 (NEW_TSS_BASE + TSS_BP,    0x6666);
	wr16 (NEW_TSS_BASE + TSS_SI,    0x7777);
	wr16 (NEW_TSS_BASE + TSS_DI,    0x8888);
	wr16 (NEW_TSS_BASE + TSS_ES,    0x0000);
	wr16 (NEW_TSS_BASE + TSS_CS,    SEL_CODE);
	wr16 (NEW_TSS_BASE + TSS_SS,    0x0018);
	wr16 (NEW_TSS_BASE + TSS_DS,    0x0000);
	wr16 (NEW_TSS_BASE + TSS_LDTR,  0x0000);
}

/* Helper: read a 16-bit value from physical RAM */
static unsigned short rd16 (unsigned long addr)
{
	if (addr + 1 < RAM_SIZE)
		return (unsigned short)ram[addr] | ((unsigned short)ram[addr+1] << 8);
	return 0;
}

static void check_task_gates(void)
{
 unsigned kind;
 for(kind=0;kind<3;kind++) {
  e8086_t *c;
  memset(ram,0,sizeof ram);setup_gdt_and_tsses();c=make_pm();
  c->gdtr_base=GDT_BASE;c->gdtr_limit=0x37;
  c->tr_sel=0x20;c->tr_base=OLD_TSS_BASE;c->tr_limit=0x2b;
  e86_set_cs(c,SEL_CODE);e86_load_seg_pm(c,E86_REG_CS,SEL_CODE);
  e86_set_ss(c,0x18);e86_load_seg_pm(c,E86_REG_SS,0x18);
  if(kind==2) {
   c->idtr_base=0x6000;c->idtr_limit=0x1ff;
   wr16(0x6182,SEL_TSS_NEW);ram[0x6185]=0x85;
   ram[0x100]=0xcd;ram[0x101]=0x30;
  } else {
   wr16(GDT_BASE+0x2a,SEL_TSS_NEW);ram[GDT_BASE+0x2d]=0x85;
   ram[0x100]=kind?0x9a:0xea;wr16(0x103,0x28);
  }
  e86_execute(c);
  CHECK_VAL("task gate loads target",c->tr_sel,SEL_TSS_NEW);
  CHECK_VAL("task gate loads IP",e86_get_ip(c),0x200);
  CHECK_VAL("task gate saves resume",rd16(OLD_TSS_BASE+TSS_IP),kind==2?0x102:0x105);
  CHECK_VAL("task gate nested bit",c->flg&0x4000,kind?0x4000:0);
  CHECK_VAL("task gate backlink",rd16(NEW_TSS_BASE+TSS_BACKLINK),kind?0x20:0);
  e86_del(c);
 }
}

static void check_incoming_ldt(void)
{
 unsigned absent;
 for(absent=0;absent<2;absent++) {
  e8086_t *c;
  memset(ram,0,sizeof ram);setup_gdt_and_tsses();c=make_pm();
  c->gdtr_base=GDT_BASE;c->gdtr_limit=0x37;
  c->tr_sel=0x20;c->tr_base=OLD_TSS_BASE;c->tr_limit=0x2b;
  write_desc(GDT_BASE+0x28,0x17,0x7000,0x82);
  write_desc(0x7008,0xffff,0,0x9b);
  write_desc(0x7010,0xffff,0,absent?0x13:0x93);
  wr16(NEW_TSS_BASE+TSS_LDTR,0x28);
  wr16(NEW_TSS_BASE+TSS_CS,0x0c);
  wr16(NEW_TSS_BASE+TSS_DS,0x14);
  e86_set_cs(c,SEL_CODE);e86_load_seg_pm(c,E86_REG_CS,SEL_CODE);
  e86_set_ss(c,0x18);e86_load_seg_pm(c,E86_REG_SS,0x18);
  c->idtr_base=0x6000;c->idtr_limit=0x1ff;
  wr16(0x6058,0x600);wr16(0x605a,SEL_CODE);ram[0x605d]=0x86;
  ram[0x100]=0xea;wr16(0x103,SEL_TSS_NEW);e86_execute(c);
  CHECK_VAL("incoming LDT cache loaded",c->ldtr_base,0x7000);
  CHECK_VAL("task remains committed",c->tr_sel,SEL_TSS_NEW);
  CHECK_VAL("outgoing resume IP retained",rd16(OLD_TSS_BASE+TSS_IP),0x105);
  if(absent) {
   CHECK_VAL("incoming missing DS raises NP",c->int_vec,11);
   CHECK_VAL("incoming fault saves new IP",rd16(0x554d+2),0x200);
   CHECK_VAL("incoming fault reports DS selector",rd16(0x554d),0x14);
  } else {
   CHECK_VAL("incoming CS resolved through new LDT",e86_get_cs(c),0x0c);
   CHECK_VAL("incoming DS resolved through new LDT",e86_get_ds(c),0x14);
   CHECK_VAL("incoming task runs",e86_get_ip(c),0x200);
  }
  e86_del(c);
 }
}

int main (void)
{
	e8086_t *c;

	/* ----------------------------------------------------------------
	 * Test 1: JMP far to TSS (op_ea) — JMP imm32 where CS selector = SEL_TSS_NEW
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	setup_gdt_and_tsses ();

	c = make_pm ();
	c->gdtr_base  = GDT_BASE;
	c->gdtr_limit = 0x0037;   /* covers 3 entries */
	c->tr_sel     = 0x0020;   /* old TSS selector (fake, not in GDT) */
	c->tr_base    = OLD_TSS_BASE;
	c->tr_limit   = 0x002b;

	/* Seed old-task registers so we can verify they were saved */
	e86_set_ax (c, 0xAAAA);
	e86_set_bx (c, 0xBBBB);
	e86_set_ip (c, 0x0100);

	/* EA: JMP 0x0050:SEL_TSS_NEW  (imm IP=0x0050 is irrelevant for TSS jump) */
	/* opcode: EA lo_ip hi_ip lo_cs hi_cs */
	ram[0x0100] = 0xEA;
	ram[0x0101] = 0x50; ram[0x0102] = 0x00;   /* new IP in pq (ignored) */
	ram[0x0103] = (unsigned char)(SEL_TSS_NEW & 0xff);
	ram[0x0104] = (unsigned char)(SEL_TSS_NEW >> 8);

	run1 (c);

	/* New registers loaded from new TSS */
	CHECK_VAL("JMP-TSS: new AX", e86_get_ax(c), 0x1111);
	CHECK_VAL("JMP-TSS: new BX", e86_get_bx(c), 0x4444);
	CHECK_VAL("JMP-TSS: new IP", e86_get_ip(c), 0x0200);
	CHECK_VAL("JMP-TSS: new CS", e86_get_cs(c), SEL_CODE);
	/* TR updated */
	CHECK_VAL("JMP-TSS: TR sel", c->tr_sel, SEL_TSS_NEW);
	/* TS flag set */
	CHECK("JMP-TSS: TS set", (c->msw & 0x0008) != 0);
	/* Old state saved in old TSS */
	CHECK_VAL("JMP-TSS: old AX saved", rd16(OLD_TSS_BASE + TSS_AX), 0xAAAA);
	CHECK_VAL("JMP-TSS: old BX saved", rd16(OLD_TSS_BASE + TSS_BX), 0xBBBB);
	CHECK_VAL("JMP saves next IP", rd16(OLD_TSS_BASE + TSS_IP), 0x105);
	CHECK_VAL("JMP clears old busy", ram[GDT_BASE+0x25], 0x81);
	CHECK_VAL("JMP sets new busy", ram[GDT_BASE+0x15], 0x83);
	/* No back-link for JMP */
	CHECK_VAL("JMP-TSS: no backlink", rd16(NEW_TSS_BASE + TSS_BACKLINK), 0x0000);

	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 2: CALL far to TSS (op_9a) — back-link should be set
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	setup_gdt_and_tsses ();

	c = make_pm ();
	c->gdtr_base  = GDT_BASE;
	c->gdtr_limit = 0x0037;
	c->tr_sel     = 0x0030;   /* old TSS selector */
	c->tr_base    = OLD_TSS_BASE;
	c->tr_limit   = 0x002b;

	e86_set_ax (c, 0xCCCC);
	e86_set_ip (c, 0x0100);

	/* 9A: CALL 0x0060:SEL_TSS_NEW */
	ram[0x0100] = 0x9A;
	ram[0x0101] = 0x60; ram[0x0102] = 0x00;
	ram[0x0103] = (unsigned char)(SEL_TSS_NEW & 0xff);
	ram[0x0104] = (unsigned char)(SEL_TSS_NEW >> 8);

	run1 (c);

	CHECK_VAL("CALL-TSS: new AX",    e86_get_ax(c),  0x1111);
	CHECK_VAL("CALL-TSS: new IP",    e86_get_ip(c),  0x0200);
	CHECK_VAL("CALL-TSS: TR sel",    c->tr_sel,       SEL_TSS_NEW);
	/* Back-link written into new TSS */
	CHECK_VAL("CALL-TSS: backlink",  rd16(NEW_TSS_BASE + TSS_BACKLINK), 0x0030);
	/* Old state saved */
	CHECK_VAL("CALL-TSS: old AX saved", rd16(OLD_TSS_BASE + TSS_AX), 0xCCCC);
	CHECK_VAL("CALL retains old busy",ram[GDT_BASE+0x35],0x83);
	CHECK_VAL("CALL sets new busy",ram[GDT_BASE+0x15],0x83);
	/* Return to a real saved code/stack context, preserving its NT bit. */
	wr16(OLD_TSS_BASE+TSS_CS,SEL_CODE); wr16(OLD_TSS_BASE+TSS_SS,0x18);
	wr16(OLD_TSS_BASE+TSS_SP,0x900); wr16(OLD_TSS_BASE+TSS_FLAGS,0x4002);
	ram[0x200]=0xcf; e86_pq_init(c); run1(c);
	CHECK_VAL("task IRET restores next IP",e86_get_ip(c),0x105);
	CHECK_VAL("task IRET restores target NT",c->flg&0x4000,0x4000);
	CHECK_VAL("task IRET clears outgoing busy",ram[GDT_BASE+0x15],0x81);
	CHECK_VAL("task IRET retains target busy",ram[GDT_BASE+0x35],0x83);
	CHECK_VAL("task IRET saves outgoing next IP",rd16(NEW_TSS_BASE+TSS_IP),0x201);
	CHECK_VAL("task IRET saves cleared NT",rd16(NEW_TSS_BASE+TSS_FLAGS)&0x4000,0);

	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 3: Normal far JMP (non-TSS selector) still works correctly
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	setup_gdt_and_tsses ();

	c = make_pm ();
	c->gdtr_base  = GDT_BASE;
	c->gdtr_limit = 0x0037;
	c->tr_sel     = 0;
	c->tr_base    = 0;
	c->tr_limit   = 0;

	e86_set_ip (c, 0x0100);

	/* JMP far to SEL_CODE:0x0300 — code descriptor, not TSS → normal JMP */
	ram[0x0100] = 0xEA;
	ram[0x0101] = 0x00; ram[0x0102] = 0x03;  /* IP = 0x0300 */
	ram[0x0103] = (unsigned char)(SEL_CODE & 0xff);
	ram[0x0104] = (unsigned char)(SEL_CODE >> 8);

	run1 (c);

	CHECK_VAL("JMP-normal: IP",  e86_get_ip(c), 0x0300);
	CHECK_VAL("JMP-normal: CS",  e86_get_cs(c), SEL_CODE);
	/* TR not touched */
	CHECK_VAL("JMP-normal: TR",  c->tr_sel, 0x0000);

	e86_del (c);

	check_task_gates();
	check_incoming_ldt();
	printf("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
