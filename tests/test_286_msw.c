/*
 * Smoke-test for T1-C: proper MSW/GDTR/IDTR storage in the 80286 CPU.
 *
 * Compile:
 *   gcc -Isrc -o test_286_msw \
 *     test_286_msw.c \
 *     src/cpu/e8086/e8086.o \
 *     src/cpu/e8086/e80286r.o \
 *     src/cpu/e8086/e80186.o \
 *     src/cpu/e8086/opcodes.o \
 *     src/cpu/e8086/flags.o \
 *     src/cpu/e8086/ea.o \
 *     src/cpu/e8086/pqueue.o
 * Run:
 *   ./test_286_msw
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

/* ---------- minimal flat RAM for the test ---------- */

#define RAM_SIZE 0x10000

static unsigned char ram[RAM_SIZE];

static unsigned char ram_get8 (void *ext, unsigned long addr)
{
	(void) ext;
	return ram[addr & (RAM_SIZE - 1)];
}

static unsigned short ram_get16 (void *ext, unsigned long addr)
{
	addr &= (RAM_SIZE - 1);
	return (unsigned short) ram[addr] | ((unsigned short) ram[(addr + 1) & (RAM_SIZE - 1)] << 8);
}

static void ram_set8 (void *ext, unsigned long addr, unsigned char val)
{
	(void) ext;
	ram[addr & (RAM_SIZE - 1)] = val;
}

static void ram_set16 (void *ext, unsigned long addr, unsigned short val)
{
	addr &= (RAM_SIZE - 1);
	ram[addr]                      = val & 0xff;
	ram[(addr + 1) & (RAM_SIZE - 1)] = (val >> 8) & 0xff;
}

/* ---------- helper: write raw bytes at a flat address ---------- */
static void poke (unsigned short addr, const unsigned char *b, unsigned len)
{
	unsigned i;
	for (i = 0; i < len; i++) {
		ram[(addr + i) & (RAM_SIZE - 1)] = b[i];
	}
}

/* ---------- helper: read byte from flat address ---------- */
static unsigned char peek (unsigned short addr)
{
	return ram[addr & (RAM_SIZE - 1)];
}

/* ---------- set up a minimal 80286 CPU pointed at flat RAM ---------- */
static e8086_t *make_cpu (void)
{
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_reset (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f)  ram_get8,
		(e86_set_uint8_f)  ram_set8,
		(e86_get_uint16_f) ram_get16,
		(e86_set_uint16_f) ram_set16);
	/* set CS=0, IP=0x0100 — instructions will be placed at 0x0100 */
	e86_set_cs (c, 0x0000);
	e86_set_ip (c, 0x0100);
	return c;
}

/* ---------- run a single instruction from IP ---------- */
static void run1 (e8086_t *c)
{
	unsigned short ip_before = e86_get_ip (c);
	while (e86_get_ip (c) == ip_before) {
		e86_clock (c, 1);
	}
}

/* ---------- tests ---------- */

static int pass = 1;

#define CHECK(label, cond) \
	do { \
		if (cond) { \
			printf ("PASS %s\n", label); \
		} else { \
			printf ("FAIL %s\n", label); \
			pass = 0; \
		} \
	} while (0)

int main (void)
{
	e8086_t *c;

	/* ------------------------------------------------------------------ */
	/* Test 1: SMSW returns initial MSW = FFF0h */
	/* ------------------------------------------------------------------ */
	memset (ram, 0, sizeof ram);
	c = make_cpu ();

	/*
	 * SMSW AX  →  0F 01 E0   (ModRM E0 = reg AX, /4 in bits 5-3)
	 * AX should read back FFF0h (Intel reset MSW).
	 */
	{
		unsigned char insn[] = { 0x0f, 0x01, 0xe0 };
		poke (0x0100, insn, 3);
		e86_set_ax (c, 0xDEAD);
		run1 (c);
		CHECK ("SMSW initial=FFF0", e86_get_ax (c) == 0xfff0);
	}

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 2: LMSW AX then SMSW AX — round-trip */
	/* ------------------------------------------------------------------ */
	memset (ram, 0, sizeof ram);
	c = make_cpu ();

	/*
	 * LMSW AX  →  0F 01 F0   (ModRM F0 = reg AX, /6 in bits 5-3)
	 * SMSW BX  →  0F 01 D8   (ModRM D8 = reg BX, /3... wait: /4=100b → C0|reg)
	 * /6 = 110b << 3 = 0x30, reg AX=000b → ModRM = 0xF0
	 * /4 = 100b << 3 = 0x20, reg BX=011b → ModRM = 0xE3... 
	 * Actually for SMSW BX: /4 bits 5-3 = 100b, BX = reg 3 → 0x20|0x03 = 0xC0|0x23 ... 
	 * Let's just use AX again for simplicity.
	 */
	{
		/* LMSW AX with AX=0x0006 (EM|MP bits) */
		unsigned char lmsw[] = { 0x0f, 0x01, 0xf0 }; /* LMSW AX */
		unsigned char smsw[] = { 0x0f, 0x01, 0xe0 }; /* SMSW AX */
		poke (0x0100, lmsw, 3);
		poke (0x0103, smsw, 3);

		e86_set_ax (c, 0x0006);   /* MP | EM */
		run1 (c);                  /* execute LMSW */
		CHECK ("LMSW stores MSW", c->msw == 0xfff6);

		e86_set_ax (c, 0xDEAD);
		run1 (c);                  /* execute SMSW */
		CHECK ("SMSW reads back MSW", e86_get_ax (c) == 0xfff6);
	}

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 3: PE bit is sticky — cannot be cleared by LMSW once set */
	/* ------------------------------------------------------------------ */
	memset (ram, 0, sizeof ram);
	c = make_cpu ();

	{
		unsigned char lmsw_set[] = { 0x0f, 0x01, 0xf0 }; /* LMSW AX — set PE */
		unsigned char lmsw_clr[] = { 0x0f, 0x01, 0xf0 }; /* LMSW AX — try to clear PE */
		unsigned char smsw[]     = { 0x0f, 0x01, 0xe0 }; /* SMSW AX */
		poke (0x0100, lmsw_set, 3);
		poke (0x0103, lmsw_clr, 3);
		poke (0x0106, smsw,     3);

		e86_set_ax (c, 0x0001);   /* PE=1 */
		run1 (c);
		CHECK ("LMSW sets PE", (c->msw & 0x0001) != 0);

		e86_set_ax (c, 0x0000);   /* try to clear PE */
		run1 (c);
		CHECK ("PE sticky after LMSW clear", (c->msw & 0x0001) != 0);

		e86_set_ax (c, 0xDEAD);
		run1 (c);
		CHECK ("SMSW reflects sticky PE", (e86_get_ax (c) & 0x0001) != 0);
	}

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 4: EM bit → E86_CPU_INT7 flag side effect */
	/* ------------------------------------------------------------------ */
	memset (ram, 0, sizeof ram);
	c = make_cpu ();

	{
		unsigned char lmsw_em[]  = { 0x0f, 0x01, 0xf0 }; /* LMSW AX, AX=0x0004 */
		unsigned char lmsw_noem[]= { 0x0f, 0x01, 0xf0 }; /* LMSW AX, AX=0x0000 */
		poke (0x0100, lmsw_em,   3);
		poke (0x0103, lmsw_noem, 3);

		e86_set_ax (c, 0x0004);   /* EM=1 */
		run1 (c);
		CHECK ("EM=1 sets INT7 flag", (c->cpu & E86_CPU_INT7) != 0);

		e86_set_ax (c, 0x0000);   /* EM=0 */
		run1 (c);
		CHECK ("EM=0 clears INT7 flag", (c->cpu & E86_CPU_INT7) == 0);
	}

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 5: LGDT / SGDT round-trip via memory */
	/* ------------------------------------------------------------------ */
	memset (ram, 0, sizeof ram);
	c = make_cpu ();

	{
		/*
		 * Place a 6-byte DTR record at 0x0200:
		 *   limit = 0x07FF, base = 0x0B8000 (typical video BIOS range)
		 *
		 * LGDT [0x0200]  →  0F 01 16 00 02  (ModRM 0x16 = /2, direct [disp16])
		 * SGDT [0x0210]  →  0F 01 06 10 02  (ModRM 0x06 = /0, direct [disp16])
		 */
		unsigned char lgdt[] = { 0x0f, 0x01, 0x16, 0x00, 0x02 };
		unsigned char sgdt[] = { 0x0f, 0x01, 0x06, 0x10, 0x02 };
		poke (0x0100, lgdt, 5);
		poke (0x0105, sgdt, 5);

		/* Write the source DTR record at 0x0200 */
		ram_set16 (NULL, 0x0200, 0x07FF);         /* limit */
		ram_set8  (NULL, 0x0202, 0x00);            /* base  low  */
		ram_set8  (NULL, 0x0203, 0x80);            /* base  mid  */
		ram_set8  (NULL, 0x0204, 0x0B);            /* base  high */

		run1 (c);   /* LGDT */
		CHECK ("LGDT loads limit",    c->gdtr_limit == 0x07FF);
		CHECK ("LGDT loads base",     c->gdtr_base  == 0x0B8000UL);

		run1 (c);   /* SGDT */
		CHECK ("SGDT stores limit",   ram_get16 (NULL, 0x0210) == 0x07FF);
		CHECK ("SGDT stores base lo", peek (0x0212) == 0x00);
		CHECK ("SGDT stores base mid",peek (0x0213) == 0x80);
		CHECK ("SGDT stores base hi", peek (0x0214) == 0x0B);
	}

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 6: LIDT / SIDT round-trip via memory */
	/* ------------------------------------------------------------------ */
	memset (ram, 0, sizeof ram);
	c = make_cpu ();

	{
		/*
		 * LIDT [0x0200]  →  0F 01 1E 00 02  (ModRM 0x1E = /3, direct [disp16])
		 * SIDT [0x0210]  →  0F 01 0E 10 02  (ModRM 0x0E = /1, direct [disp16])
		 */
		unsigned char lidt[] = { 0x0f, 0x01, 0x1e, 0x00, 0x02 };
		unsigned char sidt[] = { 0x0f, 0x01, 0x0e, 0x10, 0x02 };
		poke (0x0100, lidt, 5);
		poke (0x0105, sidt, 5);

		ram_set16 (NULL, 0x0200, 0x03FF);
		ram_set8  (NULL, 0x0202, 0x00);
		ram_set8  (NULL, 0x0203, 0x00);
		ram_set8  (NULL, 0x0204, 0x00);

		run1 (c);   /* LIDT */
		CHECK ("LIDT loads limit",    c->idtr_limit == 0x03FF);
		CHECK ("LIDT loads base",     c->idtr_base  == 0x000000UL);

		run1 (c);   /* SIDT */
		CHECK ("SIDT stores limit",   ram_get16 (NULL, 0x0210) == 0x03FF);
	}

	e86_del (c);

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return pass ? 0 : 1;
}
