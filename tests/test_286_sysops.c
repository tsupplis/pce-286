/*
 * tests/test_286_sysops.c
 *
 * Unit tests for T3-A item 5: 80286 system opcodes.
 *
 *   SLDT/STR     — store LDTR/TR selector
 *   LLDT/LTR     — load LDTR/TR from GDT
 *   VERR/VERW    — descriptor type, bounds and privilege checks
 *   LAR r16,r/m  — load access rights byte
 *   LSL r16,r/m  — load segment limit
 *   CLTS         — clear TS bit in MSW
 *   ARPL         — adjust RPL field
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

#define RAM_SIZE 0x100000
static unsigned char ram[RAM_SIZE];

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

static e8086_t *make_286 (void)
{
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	c->ram     = ram;
	c->ram_cnt = RAM_SIZE;
	e86_reset (c);
	e86_set_cs (c, 0); e86_set_ip (c, 0x0100);
	e86_set_ss (c, 0); e86_set_sp (c, 0x0400);
	e86_set_ds (c, 0);
	e86_set_es (c, 0);
	return c;
}

/* Run one instruction */
static void run1 (e8086_t *c)
{
	unsigned short ip = e86_get_ip (c);
	while (e86_get_ip (c) == ip) e86_clock (c, 1);
}

static int pass = 1;

#define CHECK(label, cond) \
	do { if (cond) { printf("PASS %s\n",label); } \
	     else      { printf("FAIL %s\n",label); pass=0; } } while(0)

#define CHECK_VAL(label, got, want) \
	do { if ((unsigned long)(got)==(unsigned long)(want)) { \
		printf("PASS %s\n",label); \
	} else { \
		printf("FAIL %s  got=0x%lX  want=0x%lX\n", \
			label,(unsigned long)(got),(unsigned long)(want)); \
		pass=0; \
	} } while(0)

/* Write GDT descriptor */
static void write_desc(unsigned long off, unsigned short limit,
	unsigned long base, unsigned char ar)
{
	ram[off+0]=limit&0xff; ram[off+1]=(limit>>8)&0xff;
	ram[off+2]=base&0xff;  ram[off+3]=(base>>8)&0xff;
	ram[off+4]=(base>>16)&0xff;
	ram[off+5]=ar; ram[off+6]=0; ram[off+7]=0;
}

static void check_verify (void)
{
	e8086_t *c;
	unsigned i, wr;
	static const struct {
		unsigned sel, ar, cpl, limit, ldt, readable, writable;
	} cases[] = {
		{8, 0x93, 0, 15, 0, 1, 1},
		{8, 0x91, 0, 15, 0, 1, 0},
		{8, 0x9b, 0, 15, 0, 1, 0},
		{8, 0x99, 0, 15, 0, 0, 0},
		{8, 0x83, 0, 15, 0, 0, 0},
		{0, 0x93, 0, 15, 0, 0, 0},
		{8, 0x93, 0, 14, 0, 0, 0},
		{8, 0x93, 3, 15, 0, 0, 0},
		{11, 0x93, 0, 15, 0, 0, 0},
		{11, 0xf3, 3, 15, 0, 1, 1},
		{11, 0x9f, 3, 15, 0, 1, 0},
		{8, 0x13, 0, 15, 0, 1, 1},
		{12, 0x93, 0, 15, 1, 1, 1},
		{12, 0x93, 0, 14, 1, 0, 0},
		{12, 0x93, 0, 15, 0, 0, 0}
	};

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		for (wr = 0; wr < 2; wr++) {
			memset (ram, 0, sizeof ram);
			c = make_286 ();
			c->msw = E286_MSW_PE;
			c->cpl = cases[i].cpl;
			c->gdtr_base = 0x2000;
			c->gdtr_limit = cases[i].limit;
			c->ldtr_base = 0x3000;
			c->ldtr_limit = cases[i].limit;
			c->ldtr_sel = cases[i].ldt ? 0x18 : 0;
			write_desc (cases[i].sel & 4 ? 0x3008 : 0x2008,
				0xffff, 0x40000, cases[i].ar);
			ram[0x100] = 0x0f; ram[0x101] = 0;
			ram[0x102] = wr ? 0xe8 : 0xe0;
			e86_set_ax (c, cases[i].sel);
			c->flg = 0x897; /* preserve arithmetic flags except ZF */
			e86_execute (c);
			CHECK_VAL ("verify descriptor result", e86_get_zf (c),
				wr ? cases[i].writable : cases[i].readable);
			CHECK_VAL ("verify preserves other flags", c->flg & ~E86_FLG_Z, 0x897);
			CHECK_VAL ("verify preserves selector", e86_get_ax (c), cases[i].sel);
			CHECK_VAL ("invalid selector does not fault", c->int_cnt, 0);
			e86_del (c);
		}
	}
}

static void check_queries (void)
{
	static const struct {
		unsigned sel, ar, cpl, bound, ldt, lar, lsl;
	} cases[] = {
		{11, 0xf3, 3, 15, 0, 1, 1},
		{11, 0x73, 3, 15, 0, 1, 1}, /* nonresident data */
		{11, 0xf3, 3, 14, 0, 0, 0}, /* truncated descriptor */
		{0, 0xf3, 3, 15, 0, 0, 0},
		{11, 0x93, 3, 15, 0, 0, 0},
		{11, 0x93, 0, 15, 0, 0, 0}, /* RPL restriction */
		{8, 0x93, 3, 15, 0, 0, 0}, /* CPL restriction */
		{11, 0x9f, 3, 15, 0, 1, 1}, /* conforming code */
		{15, 0xf3, 3, 15, 1, 1, 1},
		{15, 0xf3, 3, 15, 0, 0, 0}, /* absent LDT */
		{15, 0xf3, 3, 14, 1, 0, 0},
		{11, 0xe1, 3, 15, 0, 1, 1},
		{11, 0xe2, 3, 15, 0, 1, 1},
		{11, 0xe3, 3, 15, 0, 1, 1},
		{11, 0xe4, 3, 15, 0, 1, 0},
		{11, 0xe5, 3, 15, 0, 1, 0},
		{11, 0, 3, 15, 0, 0, 0}
	};
	unsigned i, op;
	e8086_t *c;
	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		for (op = 0; op < 2; op++) {
			unsigned valid = op ? cases[i].lsl : cases[i].lar;
			memset (ram, 0, sizeof ram);
			c = make_286 ();
			c->msw = E286_MSW_PE;
			c->cpl = cases[i].cpl;
			c->gdtr_base = 0x2000; c->gdtr_limit = cases[i].bound;
			c->ldtr_base = 0x3000; c->ldtr_limit = cases[i].bound;
			c->ldtr_sel = cases[i].ldt ? 0x18 : 0;
			write_desc (cases[i].sel & 4 ? 0x3008 : 0x2008,
				0x5678, 0x40000, cases[i].ar);
			ram[0x100] = 0x0f; ram[0x101] = 2 + op; ram[0x102] = 0xc3;
			e86_set_bx (c, cases[i].sel); e86_set_ax (c, 0xa55a);
			c->flg = 0x897;
			e86_execute (c);
			CHECK_VAL ("descriptor query ZF", e86_get_zf (c), valid);
			CHECK_VAL ("descriptor query destination", e86_get_ax (c),
				valid ? (op ? 0x5678 : cases[i].ar << 8) : 0xa55a);
			CHECK_VAL ("descriptor query other flags", c->flg & ~0x40,
				0x897 & ~0x40);
			e86_del (c);
		}
	}
}

static void check_system_privileges (void)
{
 static const unsigned char ops[][3] = {{0x0f,1,0x16},{0x0f,1,0x1e},
  {0x0f,1,0xf0},{0x0f,6,0},{0x0f,0,0xd0},{0x0f,0,0xd8}};
 unsigned i;
 for(i=0;i<sizeof ops/sizeof ops[0];i++) {
  e8086_t *c;
  memset(ram,0,sizeof ram); c=make_286();
  c->gdtr_base=0x2000; c->gdtr_limit=0x17;
  write_desc(0x2008,0xffff,0,0xfb); write_desc(0x2010,0xffff,0,0xf3);
  c->msw=E286_MSW_PE|E286_MSW_TS; c->cpl=3;
  e86_set_cs(c,0xb); e86_load_seg_pm(c,E86_REG_CS,0xb);
  e86_set_ss(c,0x13); e86_load_seg_pm(c,E86_REG_SS,0x13);
  c->idtr_base=0x3000; c->idtr_limit=0x7ff;
  w16(NULL,0x3068,0x500); w16(NULL,0x306a,0xb); ram[0x306d]=0xe6;
  memcpy(ram+0x100,ops[i],3); ram[0x103]=0; ram[0x104]=6;
  e86_set_ax(c,0x18); e86_execute(c);
  CHECK_VAL("privileged system instruction faults",c->int_vec,13);
  CHECK_VAL("privileged instruction restart",r16(NULL,0x3fa),0x100);
  CHECK_VAL("privileged instruction error zero",r16(NULL,0x3f8),0);
  CHECK_VAL("privilege fault preserves GDTR",c->gdtr_base,0x2000);
  CHECK_VAL("privilege fault preserves IDTR",c->idtr_base,0x3000);
  CHECK_VAL("privilege fault preserves MSW",c->msw,E286_MSW_PE|E286_MSW_TS);
  CHECK_VAL("privilege fault preserves LDTR",c->ldtr_sel,0);
  CHECK_VAL("privilege fault preserves TR",c->tr_sel,0);
  e86_del(c);
 }
}

static void check_real_mode_system_ops (void)
{
 unsigned i;
 for(i=0;i<7;i++) {
  e8086_t *c;
  memset(ram,0,sizeof ram); c=make_286();
  w16(NULL,24,0x500);
  if(i==6) { ram[0x100]=0x63; ram[0x101]=0xc0; }
  else { ram[0x100]=0x0f; ram[0x101]=0; ram[0x102]=0xc0+8*i; }
  e86_execute(c);
  CHECK_VAL("protected-only system opcode rejects real mode",c->int_vec,6);
  CHECK_VAL("real mode UD restarts instruction",r16(NULL,0x3fa),0x100);
  e86_del(c);
 }
}

static void check_system_descriptors (void)
{
 static const struct { unsigned sel, ar, bound, vec; } cases[] = {
  {0x18,0,0x1f,0}, {0x1c,0,0x1f,13}, {0x18,0,0x1e,13},
  {0x18,0x93,0x1f,13}, {0x18,0x09,0x1f,13},
  {0x18,0x03,0x1f,13}, {0x18,0x100,0x1f,11}
 };
 unsigned i, tr;
 for(tr=0;tr<2;tr++) for(i=0;i<sizeof cases/sizeof cases[0];i++) {
  e8086_t *c; unsigned ar=cases[i].ar;
  memset(ram,0,sizeof ram); c=make_286();
  c->msw=E286_MSW_PE; c->gdtr_base=0x2000; c->gdtr_limit=cases[i].bound;
  write_desc(0x2008,0xffff,0,0x9b); write_desc(0x2010,0xffff,0,0x93);
  if(ar==0 || ar==0x100) ar=(tr?1:2)|(ar==0?0x80:0);
  write_desc(0x2018,0x7f,0x50000,ar);
  e86_set_cs(c,8); e86_load_seg_pm(c,E86_REG_CS,8);
  e86_set_ss(c,0x10); e86_load_seg_pm(c,E86_REG_SS,0x10);
  c->idtr_base=0x3000; c->idtr_limit=0x7ff;
  w16(NULL,0x3058,0x500); w16(NULL,0x305a,8); ram[0x305d]=0x86;
  w16(NULL,0x3068,0x500); w16(NULL,0x306a,8); ram[0x306d]=0x86;
  c->ldtr_sel=0x28; c->ldtr_base=0x60000; c->ldtr_limit=0x99;
  c->tr_sel=0x30; c->tr_base=0x70000; c->tr_limit=0xaa;
  ram[0x100]=0x0f; ram[0x101]=0; ram[0x102]=tr?0xd8:0xd0;
  e86_set_ax(c,cases[i].sel); e86_execute(c);
  if(cases[i].vec) {
   CHECK_VAL("system descriptor fault",c->int_vec,cases[i].vec);
   CHECK_VAL("system descriptor selector error",r16(NULL,0x3f8),cases[i].sel&~3U);
   CHECK_VAL("descriptor fault preserves LDTR",c->ldtr_base,0x60000);
   CHECK_VAL("descriptor fault preserves TR",c->tr_base,0x70000);
   CHECK_VAL("descriptor fault preserves access byte",ram[0x201d],ar);
  } else {
   CHECK_VAL("valid system descriptor loads",tr?c->tr_base:c->ldtr_base,0x50000);
   CHECK_VAL("LTR marks TSS busy",ram[0x201d],tr?0x83:0x82);
  }
  e86_del(c);
 }
}

int main (void)
{
	e8086_t *c;

	/* ----------------------------------------------------------------
	 * SLDT: store LDTR selector to register
	 *   0F 00 /0 with ModRM=C0 (reg AX)
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->ldtr_sel = 0x0018;
	c->msw = E286_MSW_PE;
	/* 0F 00 C0 = SLDT AX (ModRM C0: mod=11, /0, rm=AX) */
	ram[0x100]=0x0f; ram[0x101]=0x00; ram[0x102]=0xc0;
	run1(c);
	CHECK_VAL("SLDT AX", e86_get_ax(c), 0x0018);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * STR: store TR selector to register
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->tr_sel = 0x0020;
	c->msw = E286_MSW_PE;
	/* 0F 00 C8 = STR AX (ModRM C8: mod=11, /1, rm=AX) */
	ram[0x100]=0x0f; ram[0x101]=0x00; ram[0x102]=0xc8;
	run1(c);
	CHECK_VAL("STR AX", e86_get_ax(c), 0x0020);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * LLDT: load LDTR — GDT at 0x2000, entry sel=0x08 base=0x050000
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	write_desc(0x2000, 0, 0, 0);
	write_desc(0x2008, 0x07ff, 0x050000, 0x82); /* LDT descriptor */
	c->gdtr_base = 0x2000; c->gdtr_limit = 0x1f;
	/* 0F 00 D0 = LLDT AX  (ModRM D0: mod=11, /2, rm=AX) */
	ram[0x100]=0x0f; ram[0x101]=0x00; ram[0x102]=0xd0;
	e86_set_ax(c, 0x0008);
	run1(c);
	CHECK_VAL("LLDT sel",   c->ldtr_sel,   0x0008);
	CHECK_VAL("LLDT base",  c->ldtr_base,  0x050000UL);
	CHECK_VAL("LLDT limit", c->ldtr_limit, 0x07ff);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * LTR: load TR — GDT entry sel=0x10 base=0x060000
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	write_desc(0x2000, 0, 0, 0);
	write_desc(0x2010, 0x00ff, 0x060000, 0x81); /* TSS descriptor */
	c->gdtr_base = 0x2000; c->gdtr_limit = 0x1f;
	/* 0F 00 D8 = LTR AX  (ModRM D8: mod=11, /3, rm=AX) */
	ram[0x100]=0x0f; ram[0x101]=0x00; ram[0x102]=0xd8;
	e86_set_ax(c, 0x0010);
	run1(c);
	CHECK_VAL("LTR sel",   c->tr_sel,   0x0010);
	CHECK_VAL("LTR base",  c->tr_base,  0x060000UL);
	CHECK_VAL("LTR limit", c->tr_limit, 0x00ff);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * VERR: readable data descriptor
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	c->flg &= ~E86_FLG_Z;   /* clear ZF first */
	write_desc(0x2008, 0xffff, 0x30000, 0x93);
	c->gdtr_base = 0x2000; c->gdtr_limit = 0x1f;
	/* 0F 00 E0 = VERR AX  (ModRM E0: mod=11, /4, rm=AX) */
	ram[0x100]=0x0f; ram[0x101]=0x00; ram[0x102]=0xe0;
	e86_set_ax(c, 0x0008);
	run1(c);
	CHECK("VERR ZF=1", e86_get_zf(c) == 1);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * VERW: ZF=1
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	c->flg &= ~E86_FLG_Z;
	write_desc(0x2010, 0xffff, 0x30000, 0x93);
	c->gdtr_base = 0x2000; c->gdtr_limit = 0x1f;
	/* 0F 00 E8 = VERW AX  (ModRM E8: mod=11, /5, rm=AX) */
	ram[0x100]=0x0f; ram[0x101]=0x00; ram[0x102]=0xe8;
	e86_set_ax(c, 0x0010);
	run1(c);
	CHECK("VERW ZF=1", e86_get_zf(c) == 1);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * LAR AX, BX  — BX=0x08, GDT[0x08].ar=0x9B → AX = 0x9B00
	 * 0F 02 C3  (ModRM C3: mod=11, reg=AX /0, rm=BX)
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	write_desc(0x2000, 0, 0, 0);
	write_desc(0x2008, 0xffff, 0x030000, 0x9b);
	c->gdtr_base = 0x2000;
	c->gdtr_limit = 15;
	ram[0x100]=0x0f; ram[0x101]=0x02; ram[0x102]=0xc3;
	e86_set_bx(c, 0x0008);
	run1(c);
	CHECK_VAL("LAR AX", e86_get_ax(c), 0x9b00);
	CHECK("LAR ZF=1", e86_get_zf(c) == 1);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * LSL AX, BX  — BX=0x08, GDT[0x08].limit=0x7FFF → AX = 0x7FFF
	 * 0F 03 C3  (ModRM C3: mod=11, reg=AX, rm=BX)
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	write_desc(0x2000, 0, 0, 0);
	write_desc(0x2008, 0x7fff, 0x030000, 0x9b);
	c->gdtr_base = 0x2000;
	c->gdtr_limit = 15;
	ram[0x100]=0x0f; ram[0x101]=0x03; ram[0x102]=0xc3;
	e86_set_bx(c, 0x0008);
	run1(c);
	CHECK_VAL("LSL AX", e86_get_ax(c), 0x7fff);
	CHECK("LSL ZF=1", e86_get_zf(c) == 1);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * CLTS — clear TS bit (bit 3) in MSW
	 * 0F 06
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE | E286_MSW_TS;  /* PE=1, TS=1 */
	ram[0x100]=0x0f; ram[0x101]=0x06;
	run1(c);
	CHECK("CLTS PE preserved", (c->msw & E286_MSW_PE) != 0);
	CHECK("CLTS TS cleared",   (c->msw & E286_MSW_TS) == 0);
	e86_del(c);

	/* ----------------------------------------------------------------
	 * ARPL — adjust RPL
	 *   AX=0x0008 (RPL=0), BX=0x0013 (RPL=3) → AX should become 0x000B (RPL=3), ZF=1
	 * 63 C3  (ModRM C3: mod=11, reg=AX, rm=BX — ARPL dst=rm, src=reg)
	 * Note: ARPL encoding: opcode 63, ModRM, dst=r/m, src=reg
	 * ---------------------------------------------------------------- */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	c->flg &= ~E86_FLG_Z;
	/* 63 C3: ARPL BX, AX  (ModRM C3: mod=11, /AX, rm=BX)
	 * dst=BX=0x000B(want), src=AX — let's set up:
	 *   AX=0x0003 (RPL=3), BX=0x0008 (RPL=0)  → BX.RPL promoted to 3 → BX=0x000B */
	ram[0x100]=0x63; ram[0x101]=0xd8;  /* ModRM D8: mod=11, reg=BX(/3), rm=AX */
	/* ARPL r/m16, r16: r/m=dst, reg=src
	 * ModRM D8 = 11 011 000 → mod=11, reg=3(BX), rm=0(AX)
	 * ARPL AX, BX: if AX.RPL < BX.RPL → AX.RPL = BX.RPL */
	e86_set_ax(c, 0x0008);   /* RPL=0 */
	e86_set_bx(c, 0x0003);   /* RPL=3 */
	run1(c);
	/* AX.RPL was 0 < BX.RPL 3 → AX gets RPL=3 → 0x000B, ZF=1 */
	CHECK_VAL("ARPL AX promoted", e86_get_ax(c), 0x000b);
	CHECK("ARPL ZF=1 when promoted", e86_get_zf(c) == 1);
	e86_del(c);

	/* ARPL when no change needed: AX RPL >= BX RPL → ZF=0, AX unchanged */
	memset(ram,0,sizeof ram);
	c = make_286();
	c->msw = E286_MSW_PE;
	c->flg |= E86_FLG_Z;   /* pre-set ZF=1 to verify it gets cleared */
	ram[0x100]=0x63; ram[0x101]=0xd8;  /* ARPL AX, BX */
	e86_set_ax(c, 0x0013);   /* RPL=3 */
	e86_set_bx(c, 0x0001);   /* RPL=1 */
	run1(c);
	CHECK_VAL("ARPL AX unchanged", e86_get_ax(c), 0x0013);
	CHECK("ARPL ZF=0 when no change", e86_get_zf(c) == 0);
	e86_del(c);

	check_verify ();
	check_queries ();
	check_system_privileges ();
	check_real_mode_system_ops ();
	check_system_descriptors ();
	printf("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
