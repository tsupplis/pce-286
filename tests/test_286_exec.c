/*
 * tests/test_286_exec.c
 *
 * Unit tests for 80286 protected-mode instruction *execution* via
 * e86_execute().  These tests directly run encoded opcodes in RAM and verify
 * correct behaviour, catching bugs that pure-struct tests miss:
 *
 *   1. Pqueue fetch address in PM: when CS holds a selector (not a paragraph),
 *      e86_pq_fill must use seg_base[CS]+IP, not (CS<<4)+IP.
 *   2. After IRET from a PM interrupt, the CS descriptor cache is reloaded so
 *      subsequent code fetches go to the right physical address.
 *   3. MOV r/m8, imm8 in PM writes to seg_base[DS]+offset (not DS<<4+offset).
 *   4. Full mini-test: enter PM, write sentinel via DS, IRET from INT,
 *      verify sentinel and that execution continued past the IRET.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

#define RAM_SIZE 0x100000   /* 1 MB */
static unsigned char ram[RAM_SIZE];

/* Memory callbacks */
static unsigned char  r8 (void *e, unsigned long a)
	{ (void)e; if(a>=0xfffff0 && a<=0xffffff)return 0x90; return (a < RAM_SIZE) ? ram[a] : 0xff; }
static unsigned short r16(void *e, unsigned long a)
	{ (void)e;
	  if (a+1 < RAM_SIZE) return (unsigned short)ram[a] | ((unsigned short)ram[a+1]<<8);
	  if(a>=0xfffff0 && a<0xffffff)return 0x9090;
	  return 0xffff; }
static void w8 (void *e, unsigned long a, unsigned char  v)
	{ (void)e; if (a < RAM_SIZE) ram[a] = v; }
static void w16(void *e, unsigned long a, unsigned short v)
	{ (void)e; if (a+1 < RAM_SIZE) { ram[a]=v&0xff; ram[a+1]=v>>8; } }

/* Create a fresh 80286 CPU with 1 MB RAM */
static e8086_t *make_286 (void)
{
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	c->ram     = ram;
	c->ram_cnt = RAM_SIZE;
	return c;
}

/* Write an 8-byte GDT descriptor at ram[off] */
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

/* Write a 286 IDT interrupt gate at ram[off] */
static void write_idt_gate (unsigned long off, unsigned short ip,
	unsigned short cs_sel, unsigned char ar)
{
	ram[off + 0] = ip & 0xff;
	ram[off + 1] = (ip >> 8) & 0xff;
	ram[off + 2] = cs_sel & 0xff;
	ram[off + 3] = (cs_sel >> 8) & 0xff;
	ram[off + 4] = 0;
	ram[off + 5] = ar;
	ram[off + 6] = 0;
	ram[off + 7] = 0;
}

/* Run at most `max_steps` instructions; stop when IP == stop_ip (or steps exhausted) */
static void run_until (e8086_t *c, unsigned short stop_ip, unsigned max_steps)
{
	while (max_steps-- && e86_get_ip(c) != stop_ip) {
		e86_execute (c);
	}
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

/* -------------------------------------------------------------------------
 * Layout used by tests 1–4:
 *
 *  Physical 0x010000: code segment base (SEL_CODE = 0x08)
 *    +0x0000: code under test
 *  Physical 0x020000: data segment base (SEL_DATA = 0x10)
 *    +0x0050: sentinel byte (should become 0xA5 after test 3/4)
 *  Physical 0x001000: GDT
 *    +0x00: null
 *    +0x08: code  (base=0x010000, limit=0xFFFF, ar=0x9B)
 *    +0x10: data  (base=0x020000, limit=0xFFFF, ar=0x93)
 *    +0x18: stack (base=0x000000, limit=0xFFFF, ar=0x93)  SS selector=0x18
 *  Physical 0x002000: IDT (32 entries)
 *  Physical 0x000400: stack (SS base=0, SP=0x0400)
 * ------------------------------------------------------------------------- */

#define SEL_NULL  0x00
#define SEL_CODE  0x08
#define SEL_DATA  0x10
#define SEL_STACK 0x18

#define GDT_BASE  0x001000UL
#define IDT_BASE  0x002000UL
#define CODE_BASE 0x010000UL   /* physical base of code segment */
#define DATA_BASE 0x020000UL   /* physical base of data segment */
#define STACK_SP  0x0400       /* initial SP (SS base = 0x000000) */

/*
 * Encode `count` bytes starting at physical `CODE_BASE + ip` in RAM.
 */
static void code_at (unsigned long ip, const unsigned char *bytes, unsigned count)
{
	unsigned i;
	for (i = 0; i < count; i++) {
		ram[CODE_BASE + ip + i] = bytes[i];
	}
}

/* Setup: GDT, IDT, CPU registers for tests 1–4 */
static e8086_t *pm_setup (void)
{
	e8086_t *c;

	memset (ram, 0, sizeof ram);
	c = make_286 ();

	/* ---- GDT ---- */
	write_desc (GDT_BASE + 0x00, 0,      0,         0);     /* null */
	write_desc (GDT_BASE + 0x08, 0xffff, CODE_BASE, 0x9b);  /* code */
	write_desc (GDT_BASE + 0x10, 0xffff, DATA_BASE, 0x93);  /* data */
	write_desc (GDT_BASE + 0x18, 0xffff, 0x000000,  0x93);  /* expand-up stack */
	c->gdtr_base  = GDT_BASE;
	c->gdtr_limit = 0x1f;

	/* ---- IDT: all 32 gates point to ip=0x0080, SEL_CODE ---- */
	{
		unsigned i;
		for (i = 0; i < 32; i++) {
			write_idt_gate (IDT_BASE + i * 8, 0x0080, SEL_CODE, 0x86);
		}
	}
	c->idtr_base  = IDT_BASE;
	c->idtr_limit = 0xff;

	/* ---- Segment registers + caches ---- */
	c->msw = E286_MSW_PE;

	e86_set_cs (c, SEL_CODE);
	e86_load_seg_pm (c, E86_REG_CS, SEL_CODE);

	e86_set_ds (c, SEL_DATA);
	e86_load_seg_pm (c, E86_REG_DS, SEL_DATA);

	e86_set_es (c, SEL_DATA);
	e86_load_seg_pm (c, E86_REG_ES, SEL_DATA);

	e86_set_ss (c, SEL_STACK);
	e86_load_seg_pm (c, E86_REG_SS, SEL_STACK);

	e86_set_sp (c, STACK_SP);
	e86_set_ip (c, 0x0000);   /* tests set their own start IP */

	return c;
}

int main (void)
{
	e8086_t *c;

	/* ================================================================
	 * Test 1: Pqueue fetch uses seg_base[CS]+IP in PM, not (CS<<4)+IP.
	 *
	 * CS selector = SEL_CODE = 0x08.  If the bug existed, e86_pq_fill
	 * would fetch from 0x0008<<4 + IP = 0x80 + IP instead of
	 * CODE_BASE + IP = 0x010000 + IP.
	 *
	 * We place  "MOV AX, 0x1234" (B8 34 12) at physical CODE_BASE+0x0010
	 * and "HLT" (F4) at 0x80+0x0010 = 0x0090.
	 * With the fix: AX = 0x1234.
	 * Without fix:  the CPU fetches 0x00 bytes (zeroed RAM at 0x0090)
	 *               and adds NOP/garbage, never setting AX correctly.
	 * ================================================================ */
	c = pm_setup ();
	/* MOV AX, 0x1234 at CODE_BASE+0x0010 */
	{ const unsigned char insn[] = { 0xB8, 0x34, 0x12 };
	  code_at (0x0010, insn, 3); }
	/* NOP then HLT at CODE_BASE+0x0013 (we stop after 10 steps) */
	{ const unsigned char insn[] = { 0x90, 0xF4 };
	  code_at (0x0013, insn, 2); }
	/* Also write something at the wrong location so a misdetect fails */
	ram[0x0090] = 0xB8; ram[0x0091] = 0xCD; ram[0x0092] = 0xAB; /* MOV AX, 0xABCD */

	e86_set_ip (c, 0x0010);
	e86_set_ax (c, 0x0000);
	run_until (c, 0x0013, 20);   /* stop after MOV AX executes */

	CHECK_VAL ("pqueue PM: AX set from CODE_BASE", e86_get_ax(c), 0x1234);
	CHECK_VAL ("pqueue PM: CS still SEL_CODE",     e86_get_cs(c), SEL_CODE);
	e86_del (c);

	/* ================================================================
	 * Test 2: MOV byte [DS:0x0050], 0xA5 writes to DATA_BASE+0x0050
	 *         not to (DS<<4)+0x0050 = 0x00100+0x50.
	 *
	 * Opcode: C6 06 50 00 A5  (MOV byte [0x0050], 0xA5)
	 * ================================================================ */
	c = pm_setup ();
	{ const unsigned char insn[] = { 0xC6, 0x06, 0x50, 0x00, 0xA5 };
	  code_at (0x0020, insn, 5); }
	e86_set_ip (c, 0x0020);
	run_until (c, 0x0025, 10);

	CHECK_VAL ("PM MOV [DS:0050] writes to DATA_BASE+0050",
		ram[DATA_BASE + 0x0050], 0xA5);
	CHECK_VAL ("PM MOV did not touch wrong real-mode addr",
		ram[(SEL_DATA << 4) + 0x0050], 0x00);
	e86_del (c);

	/* ================================================================
	 * Test 3: PM INT dispatch then IRET returns to correct location.
	 *
	 * Code at CODE_BASE+0x0030:
	 *   INT 0x10          (CD 10)       — fires IDT gate → handler at +0x0080
	 *   MOV AX, 0xBEEF    (B8 EF BE)   — only reached if IRET returns here
	 *   HLT               (F4)
	 *
	 * Handler at CODE_BASE+0x0080:
	 *   MOV BX, 0xCAFE    (BB FE CA)   — mark "handler was called"
	 *   IRET              (CF)
	 *
	 * After IRET, execution resumes at CODE_BASE+0x0032.
	 * Expected: BX=0xCAFE (handler ran), AX=0xBEEF (post-IRET ran).
	 * ================================================================ */
	c = pm_setup ();
	/* Code at +0x0030 */
	{ const unsigned char insn[] = {
		0xCD, 0x10,              /* INT 0x10 */
		0xB8, 0xEF, 0xBE,        /* MOV AX, 0xBEEF */
		0xF4                     /* HLT */
	  };
	  code_at (0x0030, insn, sizeof insn); }
	/* Handler at +0x0080 */
	{ const unsigned char insn[] = {
		0xBB, 0xFE, 0xCA,        /* MOV BX, 0xCAFE */
		0xCF                     /* IRET */
	  };
	  code_at (0x0080, insn, sizeof insn); }

	e86_set_ip (c, 0x0030);
	e86_set_ax (c, 0x0000);
	e86_set_bx (c, 0x0000);
	c->flg |= E86_FLG_I;   /* IF=1 so hardware IRQs don't interfere */
	run_until (c, 0x0036, 50);  /* stop at HLT (ip after MOV AX) */

	CHECK_VAL ("PM INT+IRET: handler called (BX=CAFE)", e86_get_bx(c), 0xCAFE);
	CHECK_VAL ("PM INT+IRET: post-IRET code ran (AX=BEEF)", e86_get_ax(c), 0xBEEF);
	e86_del (c);

	/* ================================================================
	 * Test 4: IRET reloads CS descriptor cache.
	 *
	 * Same as test 3 but we corrupt seg_base[CS] after the INT dispatch
	 * (simulating a stale cache) and verify the IRET fixes it by
	 * checking that AX gets the right value from a MOV at the correct
	 * physical location.
	 *
	 * We cannot directly "corrupt then check" through execute() since
	 * we cannot pause mid-execution, so instead we verify that after the
	 * full INT+IRET round-trip the fetch came from CODE_BASE (not from
	 * some wrong address) by placing a distinguishing value in the right
	 * spot and 0x00 in the wrong spot.
	 * ================================================================ */
	c = pm_setup ();
	/* Code at +0x0040: INT then MOV DX, 0x5A5A then HLT */
	{ const unsigned char insn[] = {
		0xCD, 0x10,              /* INT 0x10 */
		0xBA, 0x5A, 0x5A,        /* MOV DX, 0x5A5A */
		0xF4                     /* HLT */
	  };
	  code_at (0x0040, insn, sizeof insn); }
	/* Reuse handler from test 3 at +0x0080 (already written above, but
	 * this is a fresh pm_setup so we must write it again) */
	{ const unsigned char insn[] = {
		0xBB, 0x01, 0x00,        /* MOV BX, 0x0001  (handler ran) */
		0xCF                     /* IRET */
	  };
	  code_at (0x0080, insn, sizeof insn); }

	e86_set_ip (c, 0x0040);
	e86_set_dx (c, 0x0000);
	e86_set_bx (c, 0x0000);
	c->flg |= E86_FLG_I;
	run_until (c, 0x0046, 50);

	CHECK_VAL ("PM IRET cache: handler ran",       e86_get_bx(c), 0x0001);
	CHECK_VAL ("PM IRET cache: post-IRET MOV ran", e86_get_dx(c), 0x5A5A);
	e86_del (c);

	/* ================================================================
	 * Test 5: MOV [DS:SI], AL in PM reaches DATA_BASE+SI (not DS<<4+SI).
	 *
	 * Opcode: 88 04  (MOV [SI], AL — DS override implicit)
	 * SI = 0x0100, AL = 0x77
	 * ================================================================ */
	c = pm_setup ();
	{ const unsigned char insn[] = { 0x88, 0x04 };   /* MOV [SI], AL */
	  code_at (0x0050, insn, 2); }
	e86_set_ip (c, 0x0050);
	e86_set_si (c, 0x0100);
	e86_set_al (c, 0x77);
	run_until (c, 0x0052, 10);

	CHECK_VAL ("PM MOV [SI], AL → DATA_BASE+0100",
		ram[DATA_BASE + 0x0100], 0x77);
	CHECK_VAL ("PM MOV [SI] did not hit real-mode addr",
		ram[(SEL_DATA << 4) + 0x0100], 0x00);
	e86_del (c);

	/* ================================================================
	 * Test 6: PUSH/POP in PM uses SS base=0 (not SS<<4).
	 *
	 * SS=SEL_STACK=0x18, seg_base[SS]=0x000000, SP=0x0400.
	 * PUSH AX (0x1234): should appear at physical 0x0000:0x03FE.
	 * ================================================================ */
	c = pm_setup ();
	{ const unsigned char insn[] = { 0x50 };   /* PUSH AX */
	  code_at (0x0060, insn, 1); }
	e86_set_ip (c, 0x0060);
	e86_set_ax (c, 0x1234);
	run_until (c, 0x0061, 10);

	CHECK_VAL ("PM PUSH: SP decremented",
		e86_get_sp(c), (unsigned short)(STACK_SP - 2));
	CHECK_VAL ("PM PUSH: low byte at physical 0x03FE",
		ram[0x03fe], 0x34);
	CHECK_VAL ("PM PUSH: high byte at physical 0x03FF",
		ram[0x03ff], 0x12);
	e86_del (c);

	/* ================================================================
	 * Test 7: Pqueue wraps correctly when IP is near 0xFFFF in PM.
	 *
	 * Place a MOV AX, 0xABCD at CODE_BASE+0x0070 and confirm it
	 * executes correctly (paranoia check that wrap-around path is OK).
	 * ================================================================ */
	c = pm_setup ();
	{ const unsigned char insn[] = { 0xB8, 0xCD, 0xAB };
	  code_at (0x0070, insn, 3); }
	e86_set_ip (c, 0x0070);
	e86_set_ax (c, 0x0000);
	run_until (c, 0x0073, 10);

	CHECK_VAL ("PM pqueue normal fetch AX=ABCD", e86_get_ax(c), 0xABCD);
	e86_del (c);

	/* ================================================================
	 * Test 8: tprot-style scenario — CS stays as real-mode paragraph
	 * through LMSW PE=1; IDT gate uses SEL_CODE; IRET restores real-mode CS.
	 *
	 * Physical layout:
	 *   0x01000: code (DOS .COM loads here: CS=0x0100, so CS<<4 = 0x01000)
	 *     +0x100: .COM entry point (org 0x100)
	 *       [0x1100]: MOV AX, 0x0001 (B8 01 00)  ← LMSW value
	 *       [0x1103]: LMSW AX (0F 01 F0)  ← enter PM with CS still 0x0100
	 *       [0x1106]: INT 0x10 (CD 10)
	 *       [0x1108]: MOV CX, 0xBEEF (B9 EF BE)  ← post-IRET sentinel
	 *       [0x110B]: HLT (F4)
	 *     +0x180: IDT handler
	 *       [0x1180]: MOV DX, 0xCAFE (BA FE CA)
	 *       [0x1183]: IRET (CF)
	 *
	 *   GDT at 0x00800: null + code(base=0x01000, SEL_CODE=0x08)
	 *                         + data(base=0x01000, SEL_DATA=0x10)
	 *                         + stack(base=0x00000, SEL_STACK=0x18)
	 *   IDT at 0x00C00: all gates → {IP=0x0180, CS=SEL_CODE=0x08, ar=0x86}
	 *   Stack: SS=0x0000 (real-mode, base=0), SP=0x0400
	 *
	 * Key: CS = 0x0100 (real-mode paragraph) throughout — never reloaded.
	 * seg_base[CS] is synced by real-mode pqueue fill to 0x01000.
	 * After LMSW PE=1: pqueue uses seg_base[CS]=0x01000 → correct.
	 * IRET must validate the saved CS selector: GDT[0x100] explicitly
	 *   aliases the original code base so this synthetic return is legal.
	 * ================================================================ */
	{
		/*
		 * Exact tprot sequence:
		 *  1. Real-mode pqueue fills sync seg_base[CS].
		 *  2. LMSW PE=1.
		 *  3. MOV DS, SEL_DATA  → e86_load_seg_pm updates seg_base[DS].
		 *  4. MOV ES, SEL_DATA.
		 *  5. MOV SS, SEL_STACK → seg_base[SS]=0, SP stays at ~0x0400.
		 *  6. INT 0x10 → IDT gate (SEL_CODE:handler), pushes on SS:SP.
		 *  7. handler: IRET → returns to step 6's next instruction.
		 *  8. MOV CX, 0xBEEF (post-IRET sentinel).
		 */
		unsigned long COM_BASE = 0x01000UL;   /* CS=0x0100 → base */
		unsigned long GDT      = 0x00800UL;
		unsigned long IDT_ADDR = 0x00C00UL;
		unsigned short CS_PARA = 0x0100;

		memset (ram, 0, sizeof ram);
		c = make_286 ();

		/* GDT */
		write_desc (GDT + 0x00, 0,      0,        0);
		write_desc (GDT + 0x08, 0xffff, COM_BASE, 0x9b);  /* SEL_CODE=0x08 */
		write_desc (GDT + 0x10, 0xffff, COM_BASE, 0x93);  /* SEL_DATA=0x10 */
		write_desc (GDT + 0x18, 0xffff, 0x000000, 0x93);  /* SEL_STACK=0x18 */
		write_desc (GDT + CS_PARA, 0xffff, COM_BASE, 0x9b);
		c->gdtr_base  = GDT;
		c->gdtr_limit = CS_PARA + 7;

		/* IDT: all gates → IP=0x0180, SEL_CODE */
		{
			unsigned i;
			for (i = 0; i < 32; i++)
				write_idt_gate (IDT_ADDR + i * 8, 0x0180, 0x08, 0x86);
		}
		c->idtr_base  = IDT_ADDR;
		c->idtr_limit = 0xff;

		/* CPU in real mode, CS = paragraph, SS = 0, SP = 0x0400 */
		e86_set_cs (c, CS_PARA);
		c->seg_base[E86_REG_CS] = (unsigned long)CS_PARA << 4;
		e86_set_ss (c, 0x0000);
		c->seg_base[E86_REG_SS] = 0x000000;
		e86_set_ds (c, CS_PARA);
		c->seg_base[E86_REG_DS] = (unsigned long)CS_PARA << 4;
		e86_set_es (c, CS_PARA);
		c->seg_base[E86_REG_ES] = (unsigned long)CS_PARA << 4;
		e86_set_sp (c, 0x0400);

		/*
		 * Code at COM_BASE + 0x100:
		 *   B8 01 00        MOV AX, 0x0001
		 *   0F 01 F0        LMSW AX          ← PE=1
		 *   B8 10 00        MOV AX, 0x0010   (SEL_DATA)
		 *   8E D8           MOV DS, AX
		 *   8E C0           MOV ES, AX
		 *   B8 18 00        MOV AX, 0x0018   (SEL_STACK)
		 *   8E D0           MOV SS, AX
		 *   CD 10           INT 0x10
		 *   B9 EF BE        MOV CX, 0xBEEF
		 *   F4              HLT
		 */
		{
			const unsigned char code[] = {
				0xB8, 0x01, 0x00,   /* MOV AX, 0x0001 */
				0x0F, 0x01, 0xF0,   /* LMSW AX */
				0xB8, 0x10, 0x00,   /* MOV AX, SEL_DATA */
				0x8E, 0xD8,         /* MOV DS, AX */
				0x8E, 0xC0,         /* MOV ES, AX */
				0xB8, 0x18, 0x00,   /* MOV AX, SEL_STACK */
				0x8E, 0xD0,         /* MOV SS, AX */
				0xCD, 0x10,         /* INT 0x10 */
				0xB9, 0xEF, 0xBE,   /* MOV CX, 0xBEEF */
				0xF4                /* HLT */
			};
			unsigned i;
			for (i = 0; i < sizeof code; i++)
				ram[COM_BASE + 0x100 + i] = code[i];
		}
		/* Handler at COM_BASE + 0x180 */
		{
			const unsigned char hnd[] = {
				0xBA, 0xFE, 0xCA,   /* MOV DX, 0xCAFE */
				0xCF                /* IRET */
			};
			unsigned i;
			for (i = 0; i < sizeof hnd; i++)
				ram[COM_BASE + 0x180 + i] = hnd[i];
		}

		e86_set_ip (c, 0x0100);
		e86_set_cx (c, 0x0000);
		e86_set_dx (c, 0x0000);
		c->flg = 0x0002;   /* real-mode flags */

		/* HLT is at 0x0100 + 3+3+3+2+2+3+2+2+3+1 = 0x0118 */
		run_until (c, 0x0118, 200);

		CHECK_VAL ("tprot+segs: handler ran (DX=CAFE)",       e86_get_dx(c), 0xCAFE);
		CHECK_VAL ("tprot+segs: post-IRET code ran (CX=BEEF)", e86_get_cx(c), 0xBEEF);
		e86_del (c);
	}

	/* ================================================================
		* Test 9: T5+T11 preconditions — msw=0x0007 (EM=1 from LMSW 0x000f
		* then CLTS) must not break INT 0x10 delivery or IRET.
		*
		* Exact tprot.asm sequence reproduced in machine code:
		*   1. LMSW 0x0001   → PE=1
		*   2. MOV DS/ES/SS, reload selectors (SP=0x0E00)
		*   3. LMSW 0x000f   → msw=0x000f (sets EM, MP, TS)
		*   4. CLTS          → msw=0x0007 (clears TS; EM=1 stays)
		*   5. INT 0x10      → handler sets DI=0xBEEF, IRET
		*   6. MOV CX, 0xBEEF  (post-IRET sentinel)
		*   7. HLT
		*
		* Same physical layout as test 8.
		* ================================================================ */
	{
		unsigned long COM_BASE = 0x01000UL;
		unsigned long GDT      = 0x00800UL;
		unsigned long IDT_ADDR = 0x00C00UL;
		unsigned short CS_PARA = 0x0100;

		memset (ram, 0, sizeof ram);
		c = make_286 ();

		write_desc (GDT + 0x00, 0,      0,        0);
		write_desc (GDT + 0x08, 0xffff, COM_BASE, 0x9b);
		write_desc (GDT + 0x10, 0xffff, COM_BASE, 0x93);
		write_desc (GDT + 0x18, 0xffff, 0x000000, 0x93);
		write_desc (GDT + CS_PARA, 0xffff, COM_BASE, 0x9b);
		c->gdtr_base  = GDT;
		c->gdtr_limit = CS_PARA + 7;

		{
			unsigned i;
			for (i = 0; i < 32; i++)
				write_idt_gate (IDT_ADDR + i * 8, 0x0180, 0x08, 0x86);
		}
		c->idtr_base  = IDT_ADDR;
		c->idtr_limit = 0xff;

		e86_set_cs (c, CS_PARA);
		c->seg_base[E86_REG_CS] = (unsigned long)CS_PARA << 4;
		e86_set_ss (c, 0x0000);
		c->seg_base[E86_REG_SS] = 0x000000;
		e86_set_ds (c, CS_PARA);
		c->seg_base[E86_REG_DS] = (unsigned long)CS_PARA << 4;
		e86_set_es (c, CS_PARA);
		c->seg_base[E86_REG_ES] = (unsigned long)CS_PARA << 4;
		e86_set_sp (c, 0x0E00);

		/*
		 * Code at COM_BASE + 0x100:
		 *   B8 01 00       MOV AX, 0x0001
		 *   0F 01 F0       LMSW AX           ← PE=1
		 *   B8 10 00       MOV AX, SEL_DATA
		 *   8E D8          MOV DS, AX
		 *   8E C0          MOV ES, AX
		 *   B8 18 00       MOV AX, SEL_STACK
		 *   8E D0          MOV SS, AX
		 *   B8 0F 00       MOV AX, 0x000f    ← T5: set EM/MP/TS
		 *   0F 01 F0       LMSW AX
		 *   0F 06          CLTS              ← clear TS → msw=0x0007
		 *   CD 10          INT 0x10          ← T11
		 *   B9 EF BE       MOV CX, 0xBEEF   ← post-IRET sentinel
		 *   F4             HLT
		 */
		{
			const unsigned char code[] = {
				0xB8, 0x01, 0x00,   /* MOV AX, 0x0001 */
				0x0F, 0x01, 0xF0,   /* LMSW AX */
				0xB8, 0x10, 0x00,   /* MOV AX, SEL_DATA */
				0x8E, 0xD8,         /* MOV DS, AX */
				0x8E, 0xC0,         /* MOV ES, AX */
				0xB8, 0x18, 0x00,   /* MOV AX, SEL_STACK */
				0x8E, 0xD0,         /* MOV SS, AX */
				0xB8, 0x0F, 0x00,   /* MOV AX, 0x000f (T5 LMSW val) */
				0x0F, 0x01, 0xF0,   /* LMSW AX → msw=0x000f */
				0x0F, 0x06,         /* CLTS    → msw=0x0007 */
				0xCD, 0x10,         /* INT 0x10 (T11) */
				0xB9, 0xEF, 0xBE,   /* MOV CX, 0xBEEF */
				0xF4                /* HLT */
			};
			unsigned i;
			for (i = 0; i < sizeof code; i++)
				ram[COM_BASE + 0x100 + i] = code[i];
		}
		/* Handler at COM_BASE + 0x180: set DI=0xBEEF, IRET */
		{
			const unsigned char hnd[] = {
				0xBF, 0xEF, 0xBE,   /* MOV DI, 0xBEEF */
				0xCF                /* IRET */
			};
			unsigned i;
			for (i = 0; i < sizeof hnd; i++)
				ram[COM_BASE + 0x180 + i] = hnd[i];
		}

		e86_set_ip (c, 0x0100);
		e86_set_cx (c, 0x0000);
		e86_set_di (c, 0x0000);
		c->flg = 0x0002;

		/* HLT offset from start:
		 * 3+3+3+2+2+3+2+3+3+2+2+3+1 = 32 = 0x20 bytes after 0x100 → 0x120 */
		run_until (c, 0x0120, 200);

		CHECK_VAL ("T5+T11: msw after CLTS is 0x0007",
			(unsigned long)c->msw, 0x0007UL);
		CHECK_VAL ("T5+T11: handler ran (DI=BEEF)",
			e86_get_di(c), 0xBEEF);
		CHECK_VAL ("T5+T11: post-IRET code ran (CX=BEEF)",
			e86_get_cx(c), 0xBEEF);
		e86_del (c);
	}

	/* Far transfers must reload CS, including both forms of RETF.
	 * A same-base COM test cannot detect a stale code descriptor cache. */
	{
		unsigned mode;
		for (mode = 0; mode < 6; mode++) {
			static const unsigned char jump[] = { 0xea, 0, 1, 0x20, 0 };
			static const unsigned char call[] = { 0x9a, 0, 1, 0x20, 0 };
			static const unsigned char icall[] = { 0xff, 0x1e, 0x50, 0 };
			static const unsigned char ijump[] = { 0xff, 0x2e, 0x50, 0 };
			static const unsigned char ret[] = { 0xcb };
			static const unsigned char ret_imm[] = { 0xca, 4, 0 };
			unsigned stop = mode == 1 ? 5 : mode == 2 ? 4 : 0x103;
			c = pm_setup ();
			write_desc (GDT_BASE + 0x20, 0xffff, 0x30000, 0x9a);
			c->gdtr_limit = 0x27;
			ram[0x30100] = 0xbf; ram[0x30101] = 0xef; ram[0x30102] = 0xbe;
			ram[0x30103] = 0xcb;
			w16(NULL, DATA_BASE + 0x50, 0x100);
			w16(NULL, DATA_BASE + 0x52, 0x20);
			if (mode == 0) code_at (0, jump, sizeof jump);
			if (mode == 1) code_at (0, call, sizeof call);
			if (mode == 2) code_at (0, icall, sizeof icall);
			if (mode == 3) code_at (0, ijump, sizeof ijump);
			if (mode >= 4) {
				code_at (0, mode == 4 ? ret : ret_imm, mode == 4 ? sizeof ret : sizeof ret_imm);
				w16(NULL, STACK_SP, 0x100);
				w16(NULL, STACK_SP + 2, 0x20);
			}
			run_until (c, stop, 200);
			CHECK_VAL ("PM far transfer executed destination bytes", e86_get_di(c), 0xbeef);
			CHECK_VAL ("PM far transfer reached expected IP", e86_get_ip(c), stop);
			CHECK_VAL ("PM far transfer refreshed CS base", c->seg_base[E86_REG_CS],
				(mode == 1 || mode == 2) ? CODE_BASE : 0x30000);
			CHECK_VAL ("PM far transfer stack balance", e86_get_sp(c),
				STACK_SP + (mode == 4 ? 4 : mode == 5 ? 8 : 0));
			e86_del (c);
		}
	}

	/* OS/2 startup: INT 24h on an empty IDT entry must fault, not jump
	 * into physical zero. Check both empty and out-of-limit entries. */
	{
		unsigned mode;
		for (mode = 0; mode < 2; mode++) {
			static const unsigned char insn[] = { 0x2e, 0xcd, 0x24 };
			c = pm_setup ();
			c->idtr_limit = mode ? 0xff : 0x127;
			code_at (0, insn, sizeof insn);
			run_until (c, 0x80, 8);
			CHECK_VAL ("INT empty/out-of-range gate raises GP", c->int_vec, 13);
			CHECK_VAL ("INT GP handler reached", e86_get_ip(c), 0x80);
			CHECK_VAL ("INT GP has one error-code frame", e86_get_sp(c), STACK_SP - 8);
			CHECK_VAL ("INT GP IDT error code", r16(NULL, STACK_SP - 8), 0x122);
			CHECK_VAL ("INT GP restarts at first prefix", r16(NULL, STACK_SP - 6), 0);
			CHECK_VAL ("INT GP saved code selector", r16(NULL, STACK_SP - 4), SEL_CODE);
			e86_del (c);
		}
	}

	/* Empty-IDT INT3 must shut down, not fetch an IVT entry as a gate. */
	{
		static const unsigned char insn[] = { 0xcc, 0x40 };
		unsigned long count;
		c = pm_setup ();
		c->idtr_limit = 0;
		code_at (0, insn, sizeof insn);
		e86_execute (c);
		CHECK_VAL ("Empty IDT shuts down", c->state, E86_STATE_SHUTDOWN);
		CHECK_VAL ("Shutdown leaves stack intact", e86_get_sp(c), STACK_SP);
		count = c->opcnt;
		c->flg |= E86_FLG_I;
		c->irq = 1;
		e86_execute (c);
		CHECK_VAL ("Shutdown stops instruction execution", c->opcnt, count);
		CHECK_VAL ("IRQ cannot wake shutdown", e86_interrupt(c, 0x20), 1);
		c->state |= E86_STATE_RESET;
		e86_execute (c);
		CHECK_VAL ("Reset leaves protected mode", c->msw & 1, 0);
		CHECK_VAL ("Reset clears shutdown", c->state & E86_STATE_SHUTDOWN, 0);
		e86_del (c);
	}

	{
		c = pm_setup ();
		e86_set_es(c, e86_get_ss(c));
		c->seg_base[E86_REG_ES] = 0x50000;
		e86_push(c, 0xbeef);
		CHECK_VAL("Stack write uses SS cache despite ES alias", r16(NULL, STACK_SP-2), 0xbeef);
		CHECK_VAL("Stack read uses SS cache despite ES alias", e86_pop(c), 0xbeef);
		e86_del(c);
	}

	{
		unsigned mode;
		for (mode=0; mode<2; mode++) {
			unsigned char insn[] = { 0xc4, 0x3e, 0x50, 0 };
			unsigned reg = mode ? E86_REG_DS : E86_REG_ES;
			c = pm_setup ();
			write_desc(GDT_BASE+0x20, 0x1234, 0x50000, 0x92);
			c->gdtr_limit=0x27;
			w16(NULL, DATA_BASE+0x50, 0xbeef);
			w16(NULL, DATA_BASE+0x52, 0x20);
			if (mode) insn[0]=0xc5;
			code_at(0, insn, sizeof insn);
			e86_execute(c);
			CHECK_VAL("LDS/LES loads offset", e86_get_di(c), 0xbeef);
			CHECK_VAL("LDS/LES refreshes base", c->seg_base[reg], 0x50000);
			CHECK_VAL("LDS/LES refreshes limit", c->seg_limit[reg], 0x1234);
			e86_del(c);
		}
	}

	/* Same-level call gates ignore the operand offset and use the gate's
	 * target. Gate RPL is not the new CPL; parameters stay on this stack. */
	{
		unsigned mode;
		for (mode=0; mode<4; mode++) {
			unsigned char direct[] = {0x9a, 0xff, 0xff, 0x23, 0};
			unsigned char indirect[] = {0xff, 0x1e, 0x50, 0};
			c = pm_setup();
			c->gdtr_limit = 0x27;
			write_idt_gate(GDT_BASE+0x20, 0x90, SEL_CODE, 0xe4);
			ram[GDT_BASE+0x24]=2;
			w16(NULL, DATA_BASE+0x50, 0xffff);
			w16(NULL, DATA_BASE+0x52, 0x23);
			if (mode & 2) { c->seg_ar[E86_REG_SS]=0x96; c->seg_limit[E86_REG_SS]=0x100; }
			code_at(0, (mode & 1) ? indirect : direct, (mode & 1) ? 4 : 5);
			ram[CODE_BASE+0x90]=0xcb;
			e86_execute(c);
			CHECK_VAL("Call gate target selector", e86_get_cs(c), SEL_CODE);
			CHECK_VAL("Call gate ignores operand offset", e86_get_ip(c), 0x90);
			CHECK_VAL("Call gate keeps CPL", c->cpl, 0);
			CHECK_VAL("Call gate keeps same-level stack", e86_get_sp(c), STACK_SP-4);
			e86_execute(c);
			CHECK_VAL("Call gate RETF continuation", e86_get_ip(c), (mode & 1) ? 4 : 5);
			CHECK_VAL("Call gate RETF restores stack", e86_get_sp(c), STACK_SP);
			e86_del(c);
		}
	}

	/* Gate faults must restart CALL and must not push a return frame. */
	{
		unsigned mode;
		for (mode=0; mode<3; mode++) {
			unsigned char insn[] = {0x9a, 0, 0, 0x23, 0};
			c=pm_setup();
			c->gdtr_limit=0x27;
			write_idt_gate(GDT_BASE+0x20, 0x90, SEL_CODE,
			              mode==0 ? 0x64 : mode==1 ? 0x84 : 0xe4);
			if (mode==2) w16(NULL, GDT_BASE+SEL_CODE, 0x80);
			code_at(0, insn, sizeof insn);
			e86_execute(c);
			CHECK_VAL("Invalid call gate faults", c->int_vec, mode==0 ? 11 : 13);
			CHECK_VAL("Gate fault has only exception frame", e86_get_sp(c), STACK_SP-8);
			CHECK_VAL("Gate fault error code", r16(NULL, STACK_SP-8), mode==2 ? 0 : 0x20);
			CHECK_VAL("Gate fault restarts CALL", r16(NULL, STACK_SP-6), 0);
			e86_del(c);
		}
	}

	/* OS/2: ES=0047 selects LDT[8], not GDT[8] (the BIOS data area).
	 * A MOVSB through this selector must not overwrite floppy motor state. */
	{
		unsigned mode;
		for (mode=0; mode<2; mode++) {
			unsigned char insn[] = {0xb8, 0x47, 0, 0x8e, 0xc0, 0xa4};
			c=pm_setup();
			c->gdtr_limit=0x47;
			c->ldtr_sel=0x20; c->ldtr_base=0x3000; c->ldtr_limit=0x47;
			write_desc(GDT_BASE+0x40, 0xffff, 0x400, 0xf2); /* RPL3 control must be accessible. */
			write_desc(0x3040, 0x1234, 0x60000, 0xf2);
			ram[0x43f]=3; ram[0x6003f]=0;
			ram[DATA_BASE+0x70]=0x6f;
			e86_set_si(c,0x70); e86_set_di(c,0x3f);
			if (mode) insn[1]=0x43;
			code_at(0, insn, sizeof insn);
			run_until(c,sizeof insn,10);
			CHECK_VAL("TI selects descriptor table base",c->seg_base[E86_REG_ES],mode ? 0x400 : 0x60000);
			CHECK_VAL("TI selects descriptor table limit",c->seg_limit[E86_REG_ES],mode ? 0xffff : 0x1234);
			CHECK_VAL("LDT copy preserves BDA motor state",ram[0x43f],mode ? 0x6f : 3);
			CHECK_VAL("LDT copy writes its own buffer",ram[0x6003f],mode ? 0 : 0x6f);
			e86_del(c);
		}
	}

	/* OS/2 buffer inspection must use the same LDT as segment loads. */
	{
		unsigned op, local;
		for (op=2; op<=3; op++) for (local=0; local<2; local++) {
			unsigned char insn[] = {0x0f, 0, 0xc3}; /* LAR/LSL AX,BX */
			c=pm_setup();
			c->gdtr_limit=0xa7;
			c->ldtr_sel=0x20; c->ldtr_base=0x3000; c->ldtr_limit=0xbf;
			write_desc(GDT_BASE+0xa0, 0x1234, 0x400, 0x72);
			write_desc(0x30a0, 0x601b, 0x60000, 0xf2);
			e86_set_bx(c, local ? 0xa7 : 0xa3);
			insn[1]=op;
			code_at(0, insn, sizeof insn);
			e86_execute(c);
			CHECK_VAL("LAR/LSL select GDT or LDT", e86_get_ax(c),
			          op==2 ? (local ? 0xf200 : 0x7200) : (local ? 0x601b : 0x1234));
			CHECK_VAL("LAR/LSL valid selector succeeds",e86_get_zf(c),1);
			e86_del(c);
		}
	}

	/* Privilege transitions: explicit stack frames, not just reaching a target. */
	{
		unsigned mode;
		for (mode=0; mode<3; mode++) {
			unsigned char call[] = {0x9a,0xff,0xff,0x33,0};
			unsigned char ret[] = {0xca,4,0};
			c=pm_setup();
			c->gdtr_limit=0x3f;
			write_desc(GDT_BASE+0x20,0xffff,0x30000,0xfa);
			write_desc(GDT_BASE+0x28,0xffff,0x40000,0xf2);
			write_desc(GDT_BASE+0x38,0x2b,0x50000,0x83);
			write_idt_gate(GDT_BASE+0x30,0x90,SEL_CODE,0xe4);
			ram[GDT_BASE+0x34]=2;
			c->tr_sel=0x38; c->tr_base=0x50000; c->tr_limit=0x2b;
			w16(NULL,0x50002,STACK_SP); w16(NULL,0x50004,SEL_STACK);
			e86_set_cs(c,0x23); e86_load_seg_pm(c,E86_REG_CS,0x23);
			e86_set_ss(c,0x2b); e86_load_seg_pm(c,E86_REG_SS,0x2b);
			e86_set_sp(c,0x800); e86_set_ip(c,0);
			w16(NULL,0x40800,0x1234); w16(NULL,0x40802,0xabcd);
			memcpy(ram+0x30000,call,sizeof call);
			memcpy(ram+CODE_BASE+0x90,ret,sizeof ret);
			CHECK_VAL("CS load tracks user CPL",c->cpl,3);
			if (mode==0) {
				e86_execute(c);
				CHECK_VAL("Gate enters ring zero",c->cpl,0);
				CHECK_VAL("Gate uses TSS stack",e86_get_ss(c),SEL_STACK);
				CHECK_VAL("Gate frame size with two parameters",e86_get_sp(c),STACK_SP-12);
				CHECK_VAL("Gate return IP",r16(NULL,STACK_SP-12),5);
				CHECK_VAL("Gate return CS",r16(NULL,STACK_SP-10),0x23);
				CHECK_VAL("Gate first parameter",r16(NULL,STACK_SP-8),0x1234);
				CHECK_VAL("Gate second parameter",r16(NULL,STACK_SP-6),0xabcd);
				CHECK_VAL("Gate saved outer SP",r16(NULL,STACK_SP-4),0x800);
				CHECK_VAL("Gate saved outer SS",r16(NULL,STACK_SP-2),0x2b);
				e86_execute(c);
				CHECK_VAL("RETF restores user CPL",c->cpl,3);
				CHECK_VAL("RETF restores user IP",e86_get_ip(c),5);
				CHECK_VAL("RETF releases outer parameters",e86_get_sp(c),0x804);
			}
			else {
				unsigned flags=0x3202;
				write_idt_gate(IDT_BASE+0x20*8,0x90,SEL_CODE,mode==1 ? 0x86 : 0x87);
				c->idtr_limit=0x107; c->flg=flags;
				ram[CODE_BASE+0x90]=0xcf;
				e86_trap(c,0x20);
				CHECK_VAL("Interrupt enters ring zero",c->cpl,0);
				CHECK_VAL("Interrupt TSS frame size",e86_get_sp(c),STACK_SP-10);
				CHECK_VAL("Interrupt saved flags",r16(NULL,STACK_SP-6),flags);
				CHECK_VAL("Interrupt gate IF behavior",e86_get_if(c),mode==2);
				e86_execute(c);
				CHECK_VAL("IRET restores user CPL",c->cpl,3);
				CHECK_VAL("IRET restores user SP",e86_get_sp(c),0x800);
				CHECK_VAL("IRET restores flags",c->flg,flags);
			}
			CHECK_VAL("Return restores outer SS",e86_get_ss(c),0x2b);
			CHECK_VAL("Return restores outer stack base",c->seg_base[E86_REG_SS],0x40000);
			e86_del(c);
		}
	}
	{
		unsigned char call[] = {0x9a,0,0,0x30,0};
		c=pm_setup(); c->gdtr_limit=0x37;
		write_desc(GDT_BASE+0x20,0xffff,CODE_BASE,0xde); /* conforming DPL2 */
		write_desc(GDT_BASE+0x28,0xffff,0x30000,0xda);
		write_idt_gate(GDT_BASE+0x30,0x90,0x2a,0xe4);
		e86_set_cs(c,0x22); e86_load_seg_pm(c,E86_REG_CS,0x22);
		code_at(0,call,sizeof call); ram[0x30090]=0xcb;
		e86_execute(c);
		CHECK_VAL("OS/2 ring-two gate keeps CPL",c->cpl,2);
		CHECK_VAL("OS/2 ring-two gate target",e86_get_cs(c),0x2a);
		e86_execute(c);
		CHECK_VAL("Ring-two return selector",e86_get_cs(c),0x22);
		CHECK_VAL("Ring-two return CPL",c->cpl,2);
		e86_del(c);
	}
	/* Bad outer returns must fault on the old stack, without installing SS. */
	{
		unsigned mode;
		for (mode=0;mode<3;mode++) {
			unsigned char ret[]={0xcb};
			c=pm_setup(); c->gdtr_limit=0x2f;
			write_desc(GDT_BASE+0x20,0xffff,0x30000,mode==0 ? 0x7a : 0xfa);
			write_desc(GDT_BASE+0x28,0xffff,0x40000,mode==2 ? 0x72 : 0xf2);
			w16(NULL,STACK_SP,0x90); w16(NULL,STACK_SP+2,0x23);
			w16(NULL,STACK_SP+4,0x800); w16(NULL,STACK_SP+6,mode==1 ? 0 : 0x2b);
			code_at(0,ret,sizeof ret); e86_execute(c);
			CHECK_VAL("Invalid outer return exception",c->int_vec,mode==0 ? 11 : mode==1 ? 13 : 12);
			CHECK_VAL("Invalid return preserves kernel CPL",c->cpl,0);
			CHECK_VAL("Invalid return preserves SS",e86_get_ss(c),SEL_STACK);
			CHECK_VAL("Invalid return faults before popping",e86_get_sp(c),STACK_SP-8);
			CHECK_VAL("Invalid return restart IP",r16(NULL,STACK_SP-6),0);
			e86_del(c);
		}
	}
	/* Software INT cannot use a kernel-only gate from ring 3. */
	{
		c=pm_setup(); c->gdtr_limit=0x3f;
		write_desc(GDT_BASE+0x20,0xffff,0x30000,0xfa);
		write_desc(GDT_BASE+0x28,0xffff,0x40000,0xf2);
		c->tr_sel=0x38; c->tr_base=0x50000; c->tr_limit=0x2b;
		w16(NULL,0x50002,STACK_SP); w16(NULL,0x50004,SEL_STACK);
		e86_set_cs(c,0x23); e86_load_seg_pm(c,E86_REG_CS,0x23);
		e86_set_ss(c,0x2b); e86_load_seg_pm(c,E86_REG_SS,0x2b);
		e86_set_sp(c,0x800); e86_set_ip(c,0);
		ram[0x30000]=0xcd; ram[0x30001]=0x10;
		e86_execute(c);
		CHECK_VAL("Ring-three INT DPL fault",c->int_vec,13);
		CHECK_VAL("INT DPL error identifies gate",r16(NULL,STACK_SP-12),0x82);
		CHECK_VAL("INT DPL restart before INT",r16(NULL,STACK_SP-10),0);
		CHECK_VAL("INT DPL saves user selector",r16(NULL,STACK_SP-8),0x23);
		e86_del(c);
	}

	/* OS/2 conforming helper at DPL2 called from CPL3 keeps CPL3. */
	{
		unsigned char call[]={0x9a,0x90,0,0x22,0};
		c=pm_setup(); c->gdtr_limit=0x27;
		write_desc(GDT_BASE+8,0xffff,CODE_BASE,0xfa);
		write_desc(GDT_BASE+0x18,0xffff,0,0xf2);
		write_desc(GDT_BASE+0x20,0xffff,0x30000,0xde);
		e86_set_cs(c,0xb); e86_load_seg_pm(c,E86_REG_CS,0xb);
		e86_set_ss(c,0x1b); e86_load_seg_pm(c,E86_REG_SS,0x1b);
		code_at(0,call,sizeof call); ram[0x30090]=0xcb;
		e86_execute(c);
		CHECK_VAL("Conforming call preserves CPL3",c->cpl,3);
		CHECK_VAL("Conforming CS RPL reflects CPL",e86_get_cs(c),0x23);
		e86_execute(c);
		CHECK_VAL("Conforming return uses same stack",e86_get_sp(c),STACK_SP);
		CHECK_VAL("Conforming return keeps SS",e86_get_ss(c),0x1b);
		CHECK_VAL("Conforming return keeps CPL3",c->cpl,3);
		e86_del(c);
	}

	/* A data-segment load must fault before changing any destination. */
	{
		unsigned char movds[]={0x8e,0xd8};
		c=pm_setup(); c->gdtr_limit=0x27;
		write_desc(GDT_BASE+0x20,0xffff,0x30000,0x12);
		e86_set_ax(c,0x20); code_at(0,movds,sizeof movds);
		e86_execute(c);
		CHECK_VAL("Nonpresent MOV DS raises NP",c->int_vec,11);
		CHECK_VAL("Nonpresent MOV preserves DS",e86_get_ds(c),SEL_DATA);
		CHECK_VAL("Nonpresent MOV preserves hidden base",c->seg_base[E86_REG_DS],DATA_BASE);
		CHECK_VAL("NP selector error",r16(NULL,STACK_SP-8),0x20);
		CHECK_VAL("NP restarts MOV",r16(NULL,STACK_SP-6),0);
		e86_del(c);
	}

	{
		unsigned mode;
		for (mode=0;mode<3;mode++) {
			unsigned char popds[]={0x1f}, lds[]={0xc5,0x1e,0x00,0x01};
			c=pm_setup(); c->gdtr_limit=0x27;
			write_desc(GDT_BASE+0x20,0xffff,0x30000,mode==2 ? 0x92 : 0x12);
			w16(NULL,STACK_SP,0x20); w16(NULL,DATA_BASE+0x100,0x5678);
			w16(NULL,DATA_BASE+0x102,0x20); e86_set_bx(c,0x1234);
			code_at(0,mode ? lds : popds,mode ? sizeof lds : sizeof popds);
			e86_execute(c);
			if (mode<2) {
				CHECK_VAL("Faulting POP/LDS preserves DS",e86_get_ds(c),SEL_DATA);
				CHECK_VAL("Faulting POP leaves operand on stack",e86_get_sp(c),STACK_SP-8);
				CHECK_VAL("Faulting LDS preserves general register",e86_get_bx(c),0x1234);
				CHECK_VAL("POP/LDS reports nonpresent",c->int_vec,11);
			} else {
				CHECK_VAL("Present LDS loads general register",e86_get_bx(c),0x5678);
				CHECK_VAL("Present LDS loads hidden base",c->seg_base[E86_REG_DS],0x30000);
				CHECK_VAL("Data load sets accessed bit",ram[GDT_BASE+0x25],0x93);
			}
			e86_del(c);
		}
	}

	/* An LDT far target must not be mistaken for the same-index GDT TSS. */
	{
		unsigned char jump[]={0xea,0x90,0,0x2c,0};
		c=pm_setup(); c->gdtr_limit=0x2f;
		c->ldtr_sel=0x20; c->ldtr_base=0x3000; c->ldtr_limit=0x2f;
		write_desc(GDT_BASE+0x28,0x2b,0x50000,0x81);
		write_desc(0x3028,0xffff,0x30000,0x9a);
		code_at(0,jump,sizeof jump); e86_execute(c);
		CHECK_VAL("LDT JMP uses LDT code",e86_get_cs(c),0x2c);
		CHECK_VAL("LDT JMP reaches requested IP",e86_get_ip(c),0x90);
		CHECK_VAL("LDT JMP leaves TR alone",c->tr_sel,0);
		e86_del(c);
	}

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
