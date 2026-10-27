/*
 * tests/test_286_prot.c
 *
 * Unit tests for T3-A item 3: 80286 protected-mode segment translation.
 *
 * Sequence:
 *   1. Set up a minimal GDT in RAM:
 *        entry 0: null descriptor
 *        entry 1 (sel=0x08): code segment, base=0x010000, limit=0xFFFF
 *        entry 2 (sel=0x10): data segment, base=0x020000, limit=0xFFFF
 *   2. Load GDTR via LGDT.
 *   3. Load CS/DS selectors (via MOV SR or direct seg_base[] manipulation).
 *   4. Set LMSW PE=1.
 *   5. Write a known value to DS:0x0100 → should appear at physical 0x020100.
 *   6. Read back via CS:0x0100 with CS pointing to code seg → physical 0x010100.
 *   7. Verify translations are seg_base + offset, not seg<<4 + offset.
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

#define RAM_SIZE 0x100000   /* 1 MB — enough for 24-bit addresses in tests */
static unsigned char ram[RAM_SIZE];

static unsigned char  r8 (void *e, unsigned long a)
	{ (void)e; if (a < RAM_SIZE) return ram[a]; return 0xff; }
static unsigned short r16(void *e, unsigned long a)
	{ (void)e;
	  if (a+1 < RAM_SIZE) return (unsigned short)ram[a] | ((unsigned short)ram[a+1]<<8);
	  return 0xffff; }
static void w8 (void *e, unsigned long a, unsigned char  v)
	{ (void)e; if (a < RAM_SIZE) ram[a] = v; }
static void w16(void *e, unsigned long a, unsigned short v)
	{ (void)e; if (a+1 < RAM_SIZE) { ram[a]=v&0xff; ram[a+1]=v>>8; } }

static e8086_t *make_286 (void)
{
	e8086_t *c = e86_new ();
	e86_set_80286 (c);
	e86_set_mem (c, NULL,
		(e86_get_uint8_f) r8,  (e86_set_uint8_f)  w8,
		(e86_get_uint16_f)r16, (e86_set_uint16_f) w16);
	/* set RAM pointer so fast-path is used */
	c->ram     = ram;
	c->ram_cnt = RAM_SIZE;
	e86_reset (c);
	e86_set_cs (c, 0);
	e86_set_ip (c, 0x0100);
	e86_set_ss (c, 0);
	e86_set_sp (c, 0x0200);
	return c;
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
	do { if ((unsigned long)(got) == (unsigned long)(want)) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s  got=0x%lX  want=0x%lX\n", \
			label, (unsigned long)(got), (unsigned long)(want)); \
		pass = 0; \
	} } while (0)

/*
 * Write a 286 GDT descriptor at byte offset `off` in ram[]:
 *   bytes 0-1: limit (16-bit)
 *   bytes 2-4: base (24-bit, little-endian)
 *   byte 5:    access rights
 *   bytes 6-7: zero (286 reserved)
 */
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

/* Execute real MOV/ALU encodings against a bounded cached segment. */
static void check_operand_limits (void)
{
	e8086_t *c;
	unsigned i, vec, start;
	static const struct {
		unsigned char prefix;
		unsigned short opcode;
		unsigned char modrm;
		unsigned short offset, limit;
		unsigned char ar;
		unsigned fault;
	} cases[] = {
		{0x26, 0x8b, 0x05, 0, 0, 0x93, 13},
		{0x26, 0x8a, 0x05, 0, 0, 0x93, 0},
		{0x26, 0x8b, 0x05, 0, 1, 0x93, 0},
		{0x26, 0x89, 0x05, 0, 0, 0x93, 13},
		{0x26, 0xc7, 0x05, 0, 0, 0x93, 13},
		{0x26, 0x8b, 0x05, 0xffff, 0xffff, 0x93, 13},
		{0x26, 0x8b, 0x05, 0x100, 0xff, 0x97, 0},
		{0x26, 0x8b, 0x05, 0xff, 0xff, 0x97, 13},
		{0x36, 0x8b, 0x05, 0x1000, 0xfff, 0x93, 12},
		{0x26, 0x8b, 0xc0, 0, 0, 0x93, 0},
		{0, 0x8b, 0x02, 0x1000, 0xfff, 0x93, 12},
		{0, 0x8b, 0x05, 0, 0, 0x93, 13},
		{0x26, 0xc7, 0x05, 0, 0xffff, 0x91, 13},
		{0x26, 0x8b, 0x05, 0, 0xffff, 0x99, 13},
		{0x26, 0x8b, 0x05, 0, 0xffff, 0x9b, 0},
		{0x26, 0x01, 0x05, 0, 0, 0x93, 13}, /* ADD word past limit */
		{0x26, 0x03, 0x05, 0, 0, 0x93, 13}, /* ADD register destination */
		{0x26, 0x00, 0x05, 0, 0xffff, 0x91, 13}, /* read-only byte */
		{0x26, 0x11, 0x05, 0, 0xffff, 0x91, 13}, /* ADC */
		{0x26, 0x19, 0x05, 0, 0xffff, 0x91, 13}, /* SBB */
		{0x26, 0x21, 0x05, 0, 0xffff, 0x91, 13}, /* AND */
		{0x26, 0x29, 0x05, 0, 0xffff, 0x91, 13}, /* SUB */
		{0x26, 0x31, 0x05, 0, 0xffff, 0x91, 13}, /* XOR */
		{0x36, 0x09, 0x05, 0x1000, 0xfff, 0x93, 12}, /* SS OR */
		{0x26, 0x80, 0x05, 0, 0xffff, 0x91, 13},
		{0x26, 0x81, 0x05, 0, 0, 0x93, 13},
		{0x26, 0x83, 0x05, 0xffff, 0xffff, 0x93, 13},
		{0x26, 0x85, 0x05, 0, 0, 0x93, 13}, /* TEST */
		{0x26, 0x87, 0x05, 0, 0xffff, 0x91, 13}, /* XCHG */
		{0x26, 0x39, 0x05, 0, 0xffff, 0x91, 0}, /* CMP is read-only */
		{0x26, 0x85, 0x05, 0, 0xffff, 0x91, 0} /* TEST is read-only */,
		{0x26, 0xd0, 0x25, 0, 0xffff, 0x91, 13}, /* SHL read-only */
		{0x26, 0xd1, 0x25, 0, 0, 0x93, 13},
		{0x26, 0xd2, 0x25, 0, 0xffff, 0x91, 13},
		{0x26, 0xd3, 0x25, 0, 0, 0x93, 13},
		{0x26, 0xc0, 0x25, 0, 0xffff, 0x91, 13},
		{0x26, 0xc1, 0x25, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x15, 0, 0xffff, 0x91, 13}, /* NOT */
		{0x26, 0xf7, 0x15, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x1d, 0, 0xffff, 0x91, 13}, /* NEG */
		{0x26, 0xf7, 0x1d, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x25, 1, 0, 0x93, 13}, /* MUL */
		{0x26, 0xf7, 0x25, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x2d, 1, 0, 0x93, 13}, /* IMUL */
		{0x26, 0xf7, 0x2d, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x35, 1, 0, 0x93, 13}, /* DIV */
		{0x26, 0xf7, 0x35, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x3d, 1, 0, 0x93, 13}, /* IDIV */
		{0x26, 0xf7, 0x3d, 0, 0, 0x93, 13},
		{0x26, 0xf6, 0x05, 1, 0, 0x93, 13}, /* immediate TEST */
		{0x26, 0xf7, 0x05, 0, 0, 0x93, 13},
		{0x26, 0xfe, 0x05, 0, 0xffff, 0x91, 13}, /* INC */
		{0x26, 0xff, 0x05, 0, 0, 0x93, 13},
		{0x26, 0xfe, 0x0d, 0, 0xffff, 0x91, 13}, /* DEC */
		{0x36, 0xff, 0x0d, 0x1000, 0xfff, 0x93, 12},
		{0x26, 0x69, 0x05, 0, 0, 0x93, 13}, /* immediate IMUL */
		{0x26, 0x6b, 0x05, 0, 0, 0x93, 13},
		{0x26, 0xc4, 0x05, 0, 2, 0x93, 13}, /* LES pointer tail */
		{0x26, 0xc5, 0x05, 0, 2, 0x93, 13}, /* LDS */
		{0x26, 0x62, 0x05, 0, 2, 0x93, 13}, /* BOUND pair */
		{0x26, 0xff, 0x15, 0, 0, 0x93, 13}, /* near CALL */
		{0x26, 0xff, 0x1d, 0, 2, 0x93, 13}, /* far CALL */
		{0x26, 0xff, 0x25, 0, 0, 0x93, 13}, /* near JMP */
		{0x26, 0xff, 0x2d, 0, 2, 0x93, 13}, /* far JMP */
		{0x26, 0xff, 0x35, 0, 0, 0x93, 13}, /* PUSH memory */
		{0x26, 0x8f, 0x05, 0, 0xffff, 0x91, 13}, /* POP memory */
		{0x26, 0x8c, 0x05, 0, 0xffff, 0x91, 13},
		{0x26, 0x8e, 0x05, 0, 0, 0x93, 13},
		{0x26, 0x0f01, 0x05, 0, 4, 0x93, 13}, /* SGDT */
		{0x26, 0x0f01, 0x0d, 0, 4, 0x93, 13}, /* SIDT */
		{0x26, 0x0f01, 0x15, 0, 4, 0x93, 13}, /* LGDT */
		{0x26, 0x0f01, 0x1d, 0, 4, 0x93, 13}, /* LIDT */
		{0x26, 0x0f01, 0x25, 0, 0, 0x93, 13}, /* SMSW */
		{0x26, 0x0f01, 0x35, 0, 0, 0x93, 13}, /* LMSW */
		{0x26, 0x0f00, 0x05, 0, 0, 0x93, 13}, /* SLDT */
		{0x26, 0x0f00, 0x0d, 0, 0, 0x93, 13}, /* STR */
		{0x26, 0x0f00, 0x15, 0, 0, 0x93, 13}, /* LLDT */
		{0x26, 0x0f00, 0x1d, 0, 0, 0x93, 13}, /* LTR */
		{0x26, 0x0f00, 0x25, 0, 0, 0x93, 13}, /* VERR */
		{0x26, 0x0f00, 0x2d, 0, 0, 0x93, 13}, /* VERW */
		{0x26, 0x0f02, 0x05, 0, 0, 0x93, 13}, /* LAR memory */
		{0x26, 0x0f03, 0x05, 0, 0, 0x93, 13}  /* LSL memory */

	};

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		memset (ram, 0, sizeof ram);
		c = make_286 ();
		write_desc (0x1008, 0xffff, 0x10000, 0x9b);
		write_desc (0x1010, 0xffff, 0x20000, 0x93);
		write_desc (0x1018, 0xffff, 0x30000, 0x93);
		c->gdtr_base = 0x1000;
		c->gdtr_limit = 0x1f;
		c->idtr_base = 0x2000;
		c->idtr_limit = 0x7ff;
		c->msw = E286_MSW_PE;
		e86_set_cs (c, 8);
		e86_set_ss (c, 0x10);
		e86_set_es (c, 0x18);
		e86_set_ds (c, 0x18);
		e86_load_seg_pm (c, E86_REG_CS, 8);
		e86_load_seg_pm (c, E86_REG_SS, 0x10);
		e86_load_seg_pm (c, E86_REG_ES, 0x18);
		e86_load_seg_pm (c, E86_REG_DS, 0x18);
		/* Same selector, different cached limits: select by register. */
		c->seg_limit[E86_REG_DS] = 0xffff;
		vec = cases[i].prefix == 0x36 ? E86_REG_SS : E86_REG_ES;
		if (!cases[i].prefix) {
			vec = cases[i].modrm == 2 ? E86_REG_SS : E86_REG_DS;
		}
		c->seg_limit[vec] = cases[i].limit;
		c->seg_ar[vec] = cases[i].ar;
		e86_set_di (c, cases[i].offset);
		e86_set_bp (c, cases[i].offset);
		e86_set_ax (c, 0xabcd);
		e86_set_cx (c, 1);
		e86_set_dx (c, 0);
		e86_set_sp (c, 0x800);
		start = 0x10100;
		if (cases[i].prefix) {
			ram[start++] = cases[i].prefix;
		}
		if (cases[i].opcode > 255) ram[start++] = cases[i].opcode >> 8;
		ram[start++] = cases[i].opcode & 255;
		ram[start++] = cases[i].modrm;
		ram[start++] = 0x34;
		ram[start] = 0x12;
		ram[0x30000 + cases[i].offset] = 0x78;
		ram[0x30001 + cases[i].offset] = 0x56;
		for (vec = 12; vec <= 13; vec++) {
			w16 (NULL, 0x2000 + vec * 8, 0x500);
			w16 (NULL, 0x2002 + vec * 8, 8);
			ram[0x2005 + vec * 8] = 0x86;
		}
		c->flg = 0x897;
		e86_execute (c);
		if (cases[i].fault) {
			CHECK_VAL ("operand fault vector", c->int_vec, cases[i].fault);
			CHECK_VAL ("fault handler IP", e86_get_ip (c), 0x500);
			CHECK_VAL ("fault preserves GDT", c->gdtr_base, 0x1000);
			CHECK_VAL ("fault preserves IDT", c->idtr_base, 0x2000);
			CHECK_VAL ("fault preserves AX", e86_get_ax (c), 0xabcd);
			CHECK_VAL ("fault preserves DX", e86_get_dx (c), 0);
			CHECK_VAL ("fault preserves CX", e86_get_cx (c), 1);
			CHECK_VAL ("fault preserves operand", ram[0x30000 + cases[i].offset], 0x78);
			CHECK_VAL ("error-code frame SP", e86_get_sp (c), 0x7f8);
			CHECK_VAL ("limit error code zero", r16 (NULL, 0x207f8), 0);
			CHECK_VAL ("restart includes segment prefix", r16 (NULL, 0x207fa), 0x100);
			CHECK_VAL ("saved fault CS", r16 (NULL, 0x207fc), 8);
			CHECK_VAL ("fault preserves saved flags", r16 (NULL, 0x207fe), 0x897);
			if (i == 0 || i == 15) {
				/* Repair the cached limit, discard error code, IRET, retry. */
				c->seg_limit[E86_REG_ES] = 1;
				ram[0x10500] = 0x83;
				ram[0x10501] = 0xc4;
				ram[0x10502] = 2;
				ram[0x10503] = 0xcf;
				e86_execute (c);
				e86_execute (c);
				CHECK_VAL ("IRET restarts faulting operand", e86_get_ip (c), 0x100);
				e86_execute (c);
				if (i == 0) {
					CHECK_VAL ("retried MOV loads operand", e86_get_ax (c), 0x5678);
				}
				else {
					CHECK_VAL ("retried ADD updates memory once", r16 (NULL, 0x30000), 0x0245);
					CHECK_VAL ("retried ADD sets carry", e86_get_cf (c), 1);
				}
				CHECK_VAL ("retried instruction completes", e86_get_ip (c), 0x103);
			}
		}
		else {
			CHECK_VAL ("valid MOV advances IP", e86_get_ip (c), 0x103);
			CHECK_VAL ("valid MOV leaves stack", e86_get_sp (c), 0x800);
			CHECK_VAL ("valid MOV operand", e86_get_ax (c),
				(cases[i].modrm == 0xc0 || cases[i].opcode == 0x39 ||
				 cases[i].opcode == 0x85) ? 0xabcd :
				(cases[i].opcode == 0x8a ? 0xab78 : 0x5678));
		}
		e86_del (c);
	}
}

/* A REP fault must retain completed iterations and restart at the prefix. */
static void check_rep_restart (void)
{
	e8086_t *c;
	unsigned size, vec;
	for (size = 1; size <= 2; size++) {
		memset (ram, 0, sizeof ram);
		c = make_286 ();
		write_desc (0x1008, 0xffff, 0x10000, 0x9b);
		write_desc (0x1010, 0xffff, 0x20000, 0x93);
		write_desc (0x1018, 0xffff, 0x30000, 0x93);
		write_desc (0x1020, 0xffff, 0x40000, 0x93);
		c->gdtr_base = 0x1000; c->gdtr_limit = 0x27;
		c->idtr_base = 0x2000; c->idtr_limit = 0x7ff;
		c->msw = E286_MSW_PE;
		e86_set_cs (c, 8); e86_set_ss (c, 0x10);
		e86_set_ds (c, 0x18); e86_set_es (c, 0x20);
		e86_load_seg_pm (c, E86_REG_CS, 8);
		e86_load_seg_pm (c, E86_REG_SS, 0x10);
		e86_load_seg_pm (c, E86_REG_DS, 0x18);
		e86_load_seg_pm (c, E86_REG_ES, 0x20);
		c->seg_limit[E86_REG_DS] = size - 1;
		e86_set_sp (c, 0x800); e86_set_cx (c, 2);
		e86_set_si (c, 0); e86_set_di (c, 0);
		ram[0x10100] = 0xf3; ram[0x10101] = size == 1 ? 0xa4 : 0xa5;
		ram[0x30000] = 0x11; ram[0x30000 + size] = 0x22;
		for (vec = 12; vec <= 13; vec++) {
			w16 (NULL, 0x2000 + vec * 8, 0x500);
			w16 (NULL, 0x2002 + vec * 8, 8);
			ram[0x2005 + vec * 8] = 0x86;
		}
		e86_execute (c);
		CHECK_VAL ("REP first iteration copied", ram[0x40000], 0x11);
		e86_execute (c);
		CHECK_VAL ("REP source limit faults", c->int_vec, 13);
		CHECK_VAL ("REP retains unfinished count", e86_get_cx (c), 1);
		CHECK_VAL ("REP retains source position", e86_get_si (c), size);
		CHECK_VAL ("REP retains destination position", e86_get_di (c), size);
		CHECK_VAL ("REP fault leaves next destination intact", ram[0x40000 + size], 0);
		CHECK_VAL ("REP fault saves prefix IP", r16 (NULL, 0x207fa), 0x100);
		c->seg_limit[E86_REG_DS] = 2 * size - 1;
		ram[0x10500] = 0x83; ram[0x10501] = 0xc4;
		ram[0x10502] = 2; ram[0x10503] = 0xcf;
		e86_execute (c); e86_execute (c); e86_execute (c);
		CHECK_VAL ("REP retry copies second element", ram[0x40000 + size], 0x22);
		CHECK_VAL ("REP retry finishes count", e86_get_cx (c), 0);
		CHECK_VAL ("REP retry finishes instruction", e86_get_ip (c), 0x102);
		/* Zero count must not touch even an unusable segment. */
		c->seg_ar[E86_REG_DS] = 0; c->seg_ar[E86_REG_ES] = 0;
		e86_set_ip (c, 0x100); c->pq_cnt = 0;
		e86_execute (c);
		CHECK_VAL ("REP zero count does not access memory", e86_get_ip (c), 0x102);
		e86_del (c);
	}
}

static void check_bound_restart (void)
{
	e8086_t *c;
	unsigned i;
	static const unsigned values[] = {10, 11, 12, 13, 0xffff};

	for (i = 0; i < sizeof values / sizeof values[0]; i++) {
		memset (ram, 0, sizeof ram);
		c = make_286 ();
		e86_set_es (c, 0x3000);
		e86_set_ds (c, 0x4000);
		e86_set_ax (c, values[i]);
		ram[0x100] = 0x26; /* ES: BOUND AX,[DI] */
		ram[0x101] = 0x62;
		ram[0x102] = 0x05;
		w16 (NULL, 0x30000, 0);
		w16 (NULL, 0x30002, 10);
		w16 (NULL, 5 * 4, 0x500);
		ram[0x500] = 0xcf; /* handler IRET */
		e86_execute (c);
		if (i == 0 || i == 15) {
			CHECK_VAL ("BOUND includes upper endpoint", e86_get_ip (c), 0x103);
		}
		else {
			CHECK_VAL ("BOUND rejects outside interval", c->int_vec, 5);
			CHECK_VAL ("BOUND has no error code", e86_get_sp (c), 0x1fa);
			CHECK_VAL ("BOUND saves prefix address", r16 (NULL, 0x1fa), 0x100);
			e86_set_ax (c, 5);
			e86_execute (c);
			e86_execute (c);
			CHECK_VAL ("BOUND retry retains ES override", e86_get_ip (c), 0x103);
		}
		e86_del (c);
	}
}

/* A ring-zero fault stack lets us inspect an invalid ring-three stack. */
static void check_stack_faults (void)
{
 static const unsigned char ops[] = {0x06,0x0e,0x16,0x1e,0x50,0x9c,0x68,0x6a,0xe8,
  0x07,0x17,0x1f,0x58,0x9d,0xc2,0xc3,0x60,0x61,0xc8,0xc9};
 unsigned i, j, sp, limit;
 e8086_t *c;
 for (i=0; i<sizeof ops; i++) {
  memset(ram,0,sizeof ram); c=make_286();
  sp=i<9 ? 0x102 : 0x100; limit=0x100;
  if (ops[i]==0x60) sp=0x110;
  if (ops[i]==0x61) limit=0x10e;
  if (ops[i]==0xc8) sp=0x106;
  if (ops[i]==0xc9) sp=0x80;
  write_desc(0x1008,0xffff,0x10000,0x9b);
  write_desc(0x1010,0xffff,0x20000,0x93);
  write_desc(0x1018,0xffff,0x10000,0xfb);
  write_desc(0x1020,limit,0x40000,0xf3);
  c->gdtr_base=0x1000; c->gdtr_limit=0x27;
  c->msw=E286_MSW_PE; c->cpl=3;
  e86_set_cs(c,0x1b); e86_load_seg_pm(c,E86_REG_CS,0x1b);
  e86_set_ss(c,0x23); e86_load_seg_pm(c,E86_REG_SS,0x23);
  c->tr_base=0x5000; c->tr_sel=0x28; c->tr_limit=0x2b;
  w16(NULL,0x5002,0x800); w16(NULL,0x5004,0x10);
  c->idtr_base=0x2000; c->idtr_limit=0x7ff;
  w16(NULL,0x2060,0x500); w16(NULL,0x2062,8); ram[0x2065]=0x86;
  e86_set_sp(c,sp); e86_set_bp(c,0x100); e86_set_ax(c,0x1234);
  ram[0x10100]=ops[i]; ram[0x10103]=2;
  for(j=0x70;j<0x120;j++) ram[0x40000+j]=0xa5;
  e86_execute(c);
  CHECK_VAL("stack fault vector",c->int_vec,12);
  CHECK_VAL("stack fault handler",e86_get_ip(c),0x500);
  CHECK_VAL("stack fault preserves AX",e86_get_ax(c),0x1234);
  CHECK_VAL("stack fault preserves BP",e86_get_bp(c),0x100);
  CHECK_VAL("stack fault frame SP",e86_get_sp(c),0x7f4);
  CHECK_VAL("stack fault error",r16(NULL,0x207f4),0);
  CHECK_VAL("stack fault restart IP",r16(NULL,0x207f6),0x100);
  CHECK_VAL("stack fault original SP",r16(NULL,0x207fc),sp);
  for(j=0x70;j<0x120;j++) if(ram[0x40000+j]!=0xa5) break;
  CHECK("stack fault preserves memory",j==0x120);
  e86_del(c);
 }
}

static unsigned port_count;
static unsigned char port_r8(void *e, unsigned long a) { (void)e;(void)a;port_count++;return 0x55; }
static unsigned short port_r16(void *e, unsigned long a) { (void)e;(void)a;port_count++;return 0x5555; }
static void port_w8(void *e, unsigned long a,unsigned char v) { (void)e;(void)a;(void)v;port_count++; }
static void port_w16(void *e, unsigned long a,unsigned short v) { (void)e;(void)a;(void)v;port_count++; }
static e8086_t *priv_cpu(unsigned cpl,unsigned iopl)
{
 e8086_t *c;
 memset(ram,0,sizeof ram);c=make_286();c->msw=1;
 c->gdtr_base=0x1000;c->gdtr_limit=0x17;
 write_desc(0x1008,0xffff,0x10000,0x9b|(cpl<<5));
 write_desc(0x1010,0xffff,0x20000,0x93|(cpl<<5));
 e86_set_cs(c,8|cpl);e86_load_seg_pm(c,E86_REG_CS,8|cpl);
 e86_set_ss(c,0x10|cpl);e86_load_seg_pm(c,E86_REG_SS,0x10|cpl);
 e86_set_ds(c,0x10|cpl);e86_load_seg_pm(c,E86_REG_DS,0x10|cpl);
 e86_set_es(c,0x10|cpl);e86_load_seg_pm(c,E86_REG_ES,0x10|cpl);
 c->idtr_base=0x3000;c->idtr_limit=0x7ff;
 w16(NULL,0x3068,0x500);w16(NULL,0x306a,8|cpl);ram[0x306d]=0x86|(cpl<<5);
 c->flg=2|(iopl<<12);e86_set_sp(c,0x800);e86_set_ax(c,0x1234);e86_set_cx(c,1);
 e86_set_prt(c,NULL,port_r8,port_w8,port_r16,port_w16);port_count=0;
 return c;
}
static void check_io_privilege(void)
{
 static const unsigned char ops[]={0xe4,0xe5,0xe6,0xe7,0xec,0xed,0xee,0xef,0x6c,0x6d,0x6e,0x6f,0xfa,0xfb,0xf0,0xf4};
 unsigned i,cpl,iopl;
 for(cpl=0;cpl<4;cpl++)for(iopl=0;iopl<4;iopl++)for(i=0;i<sizeof ops;i++) {
  e8086_t *c=priv_cpu(cpl,iopl);unsigned fail=ops[i]==0xf4?cpl!=0:cpl>iopl;
  ram[0x10100]=ops[i];ram[0x10101]=0x90;
  e86_execute(c);
  if(fail) {
   CHECK_VAL("untrusted instruction GP",c->int_vec,13);
   CHECK_VAL("untrusted instruction error",r16(NULL,0x207f8),0);
   CHECK_VAL("untrusted instruction restart",r16(NULL,0x207fa),0x100);
   CHECK_VAL("untrusted instruction no port access",port_count,0);
   CHECK_VAL("untrusted instruction preserves AX",e86_get_ax(c),0x1234);
   CHECK("untrusted HLT does not halt",!(c->state&E86_STATE_HALT));
  } else {
   CHECK_VAL("trusted instruction no exception",c->int_cnt,0);
   if(i<12)CHECK_VAL("trusted instruction reaches port",port_count,1);
  }
  e86_del(c);
 }
 for(cpl=0;cpl<4;cpl++)for(iopl=0;iopl<4;iopl++) {
  e8086_t *c=priv_cpu(cpl,iopl);unsigned mask=0x7fd5,expected;
  if(cpl)mask&=~0x3000U;
  if(cpl>iopl)mask&=~0x200U;
  expected=(c->flg&~mask)|(0x7ed5&mask)|2;
  ram[0x10100]=0x9d;w16(NULL,0x20800,0x7ed5);e86_execute(c);
  CHECK_VAL("POPF privilege mask",c->flg,expected);
  ram[0x10101]=0x9c;e86_pq_init(c);e86_execute(c);
  CHECK_VAL("PUSHF preserves PM flags",r16(NULL,0x20800),expected);
  e86_del(c);
 }
}

static void check_fetch_bounds(void)
{
 static const unsigned char bytes[][6]={{0x90},{0xb8,0x34,0x12},
  {0x89,0x06,0,1},{0xc7,0x06,0,1,0x34,0x12},{0x0f,1,0x16,0,1},
  {0x26,0xb8,0x34,0x12}};
 static const unsigned lengths[]={1,3,4,6,5,4};
 unsigned i,n;
 for(i=0;i<sizeof lengths/sizeof lengths[0];i++)for(n=0;n<lengths[i];n++) {
  e8086_t *c=priv_cpu(0,0);
  /* Separate handler code segment remains valid beyond the operand limit. */
  write_desc(0x1018,0xffff,0x10000,0x9b);c->gdtr_limit=0x1f;
  w16(NULL,0x306a,0x18);
  c->seg_limit[E86_REG_CS]=0xff+n;
  memcpy(ram+0x10100,bytes[i],lengths[i]);
  e86_execute(c);
  CHECK_VAL("truncated instruction GP",c->int_vec,13);
  CHECK_VAL("fetch fault restart prefix",r16(NULL,0x207fa),0x100);
  CHECK_VAL("fetch fault preserves AX",e86_get_ax(c),0x1234);
  CHECK_VAL("fetch fault preserves destination",r16(NULL,0x20100),0);
  e86_del(c);
 }
 for(n=9;n<=10;n++) {
  e8086_t *c=priv_cpu(0,0);
  memset(ram+0x10100,0x26,n);ram[0x10100+n]=0x90;
  e86_execute(c);
  if(n==9) CHECK_VAL("ten-byte instruction accepted",e86_get_ip(c),0x10a);
  else {CHECK_VAL("eleven-byte instruction rejected",c->int_vec,13);
   CHECK_VAL("length fault restart prefix",r16(NULL,0x207fa),0x100);}
  e86_del(c);
 }
}

static void check_near_bounds(void)
{
 static const unsigned char ops[][3]={{0xe8,0xfd,0},{0xe9,0xfd,0},{0xeb,0x7e,0},
 {0xff,0xd0,0},{0xff,0xe0,0},{0xc3,0,0},{0xc2,4,0},{0x70,0x7e,0},
 {0xe2,0x7e,0},{0xe1,0x7e,0},{0xe0,0x7e,0},{0xe3,0x7e,0}};
 unsigned i;
 for(i=0;i<sizeof ops/sizeof ops[0];i++) {
  e8086_t *c=priv_cpu(0,0);
  write_desc(0x1018,0xffff,0x10000,0x9b);c->gdtr_limit=0x1f;w16(NULL,0x306a,0x18);
  c->seg_limit[E86_REG_CS]=0x103;memcpy(ram+0x10100,ops[i],3);
  c->flg=2|0x800|(i==9?0x40:0);e86_set_cx(c,i==11?0:2);
  w16(NULL,0x20800,0x200);w16(NULL,0x207fe,0xabcd);
  e86_execute(c);
  CHECK_VAL("near target limit fault",c->int_vec,13);
  CHECK_VAL("near fault restart",r16(NULL,0x207fa),0x100);
  CHECK_VAL("near fault stack unchanged before frame",e86_get_sp(c),0x7f8);
  CHECK_VAL("near fault retains CX",e86_get_cx(c),i==11?0:2);
  e86_del(c);
 }
}

static void check_task_rejection(void)
{
 static const struct {unsigned ar,limit,bound,vec;} cases[]={
  {0x81,0x2b,0x1e,13},{0x83,0x2b,0x1f,13},{0x89,0x2b,0x1f,13},
  {0x01,0x2b,0x1f,11},{0x81,0x2a,0x1f,10}};
 unsigned i;
 for(i=0;i<sizeof cases/sizeof cases[0];i++) {
  e8086_t *c=priv_cpu(0,0);
  c->gdtr_limit=cases[i].bound;write_desc(0x1018,cases[i].limit,0x50000,cases[i].ar);
  c->tr_sel=0x20;c->tr_base=0x60000;c->tr_limit=0x2b;
  memset(ram+0x60000,0xa5,0x2c);
  w16(NULL,0x3050,0x500);w16(NULL,0x3052,8);ram[0x3055]=0x86;
  w16(NULL,0x3058,0x500);w16(NULL,0x305a,8);ram[0x305d]=0x86;
  ram[0x10100]=0xea;w16(NULL,0x10103,0x18);
  e86_execute(c);
  CHECK_VAL("invalid task fault",c->int_vec,cases[i].vec);
  CHECK_VAL("invalid task selector error",r16(NULL,0x207f8),0x18);
  CHECK_VAL("invalid task preserves TR",c->tr_sel,0x20);
  CHECK_VAL("invalid task preserves old TSS",r16(NULL,0x6000e),0xa5a5);
  CHECK_VAL("invalid task preserves descriptor",ram[0x101d],cases[i].ar);
  e86_del(c);
 }
}

static void check_nested_faults(void)
{
 unsigned kind;
 for(kind=0;kind<4;kind++) {
  e8086_t *c=priv_cpu(0,0);
  c->cur_ip=0x100;
  if(kind<3) {
   c->gdtr_limit=0x3f;
   write_desc(0x1018,0xffff,0x10000,0x9b);
   write_desc(0x1020,0xffff,0x20000,0x93);
   write_desc(0x1030,0x2b,0x50000,0x83);
   write_desc(0x1038,0x2b,0x60000,0x81);
   c->tr_sel=0x30;c->tr_base=0x50000;c->tr_limit=0x2b;
   w16(NULL,0x6000e,0x600);w16(NULL,0x60010,2);
   w16(NULL,0x6001a,0x900);w16(NULL,0x60024,0x18);w16(NULL,0x60026,0x20);
   w16(NULL,0x3042,0x38);ram[0x3045]=kind==2?0:0x85;
   if(kind==0) {
    write_desc(0x1010,0x7f9,0x20000,0x97);e86_load_seg_pm(c,E86_REG_SS,0x10);
   } else ram[0x306d]=0; /* original #GP gate invalid */
   e86_fault_error(c,13,0x1234);
   if(kind==2) CHECK("failed DF delivery shuts down",c->state&E86_STATE_SHUTDOWN);
   else {
    CHECK_VAL("nested protection fault becomes DF",c->int_vec,8);
    CHECK_VAL("DF task starts",e86_get_ip(c),0x600);
    CHECK_VAL("DF has exactly one error word",e86_get_sp(c),0x8fe);
    CHECK_VAL("DF error is zero",r16(NULL,0x208fe),0);
    CHECK_VAL("failed frame did not change old SP",r16(NULL,0x5001a),0x800);
    CHECK_VAL("DF task backlink",r16(NULL,0x60000),0x30);
   }
  } else {
   /* #UD is benign: its bad gate causes #GP, not #DF. */
   e86_fault(c,6);
   CHECK_VAL("benign nested fault stays GP",c->int_vec,13);
   CHECK_VAL("benign nested error identifies IDT",r16(NULL,0x207f8),0x32);
   CHECK_VAL("benign nested frame size",e86_get_sp(c),0x7f8);
  }
  e86_del(c);
 }
}

static void check_real_mode_bounds(void)
{
 unsigned cpu;
 for(cpu=0;cpu<2;cpu++) {
  e8086_t *c;
  memset(ram,0,sizeof ram);c=make_286();if(!cpu)e86_set_8086(c);
  e86_set_cs(c,0);e86_set_ds(c,0x1000);e86_set_ss(c,0x2000);e86_set_sp(c,0x800);
  ram[0x100]=0xa1;ram[0x101]=0xff;ram[0x102]=0xff;
  w16(NULL,13*4,0x500);e86_set_ax(c,0x1234);e86_execute(c);
  if(cpu) {
   CHECK_VAL("286 real word wrap faults",c->int_vec,13);
   CHECK_VAL("real fault has no error word",e86_get_sp(c),0x7fa);
   CHECK_VAL("real wrap preserves destination",e86_get_ax(c),0x1234);
  } else CHECK_VAL("8086 real wrap remains allowed",c->int_cnt,0);
  e86_del(c);
 }
 {e8086_t *c=make_286();memset(ram,0,sizeof ram);ram[0x100]=0x50;
  e86_set_sp(c,1);e86_execute(c);
  CHECK("286 PUSH SP=1 shuts down",c->state&E86_STATE_SHUTDOWN);e86_del(c);}
}

static void check_invalid_and_nm(void)
{
 static const unsigned char bad[][2]={{0x8c,0xe0},{0x8e,0xc8},{0x8e,0xe0},
  {0xc4,0xc0},{0xc5,0xc0},{0x62,0xc0},{0xff,0xd8},{0xff,0xe8}};
 unsigned i,msw,op;
 for(i=0;i<sizeof bad/sizeof bad[0];i++) {
  e8086_t *c=priv_cpu(0,0);
  w16(NULL,0x3030,0x500);w16(NULL,0x3032,8);ram[0x3035]=0x86;
  ram[0x10100]=bad[i][0];ram[0x10101]=bad[i][1];e86_execute(c);
  CHECK_VAL("invalid 286 encoding raises UD",c->int_vec,6);
  CHECK_VAL("invalid encoding preserves AX",e86_get_ax(c),0x1234);
  CHECK_VAL("UD restarts instruction",r16(NULL,0x207fa),0x100);e86_del(c);
 }
 for(op=0;op<2;op++)for(msw=0;msw<16;msw+=2) {
  e8086_t *c=priv_cpu(0,0);unsigned fault=op?!!(msw&12):((msw&10)==10);
  c->msw=msw|1;w16(NULL,0x3038,0x500);w16(NULL,0x303a,8);ram[0x303d]=0x86;
  ram[0x10100]=op?0xd8:0x9b;ram[0x10101]=0xc0;e86_execute(c);
  CHECK_VAL("WAIT/ESC MSW trap matrix",c->int_cnt,fault);
  if(fault)CHECK_VAL("WAIT/ESC raises NM",c->int_vec,7);
  e86_del(c);
 }
}

int main (void)
{
	e8086_t *c;

	/* ----------------------------------------------------------------
	 * Test 1: real-mode address translation unchanged after init
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	/* In real mode: DS=0x1000 → base = 0x10000; ofs 0x0200 → phys 0x10200 */
	e86_set_ds (c, 0x1000);
	c->seg_base[3] = 0x10000;  /* DS index=3 */
	/* msw=0 → real mode, should use seg<<4 */
	CHECK_VAL ("real-mode DS:0200 phys",
		e86_seg_to_linear (c, e86_get_ds(c), 0x0200),
		0x10200UL);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 2: PM translation uses seg_base[] not seg<<4
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	/* selector 0x10 loaded into DS, seg_base[3] = 0x020000 */
	e86_set_ds (c, 0x10);
	c->seg_base[3] = 0x020000UL;
	c->msw = E286_MSW_PE;     /* enter PM */
	CHECK_VAL ("PM DS:0100 phys",
		e86_seg_to_linear (c, e86_get_ds(c), 0x0100),
		0x020100UL);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 3: PM write reaches correct physical address
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	e86_set_ds (c, 0x10);
	c->seg_base[3] = 0x020000UL;
	c->msw = E286_MSW_PE;
	/* Write 0xAB to DS:0x0050 — should land at 0x020050 */
	e86_set_mem8 (c, e86_get_ds(c), 0x0050, 0xAB);
	CHECK_VAL ("PM write DS:0050 → phys 020050", ram[0x020050], 0xAB);
	/* Real-mode address 0x10050 must be untouched */
	CHECK_VAL ("PM write did not touch real-mode addr", ram[0x10050], 0x00);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 4: PM read from correct physical address
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	e86_set_es (c, 0x08);
	c->seg_base[0] = 0x010000UL;   /* ES index=0 */
	c->msw = E286_MSW_PE;
	ram[0x010080] = 0xCD;          /* place sentinel at physical 0x010080 */
	CHECK_VAL ("PM read ES:0080 → phys 010080",
		e86_get_mem8 (c, e86_get_es(c), 0x0080), 0xCD);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 5: Switching back to real mode (msw PE=0) uses seg<<4
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	e86_set_ds (c, 0x0200);
	c->seg_base[3] = 0xDEAD00UL;   /* bogus PM base */
	c->msw = 0;                    /* real mode */
	ram[0x02050] = 0x77;           /* physical 0x0200<<4 + 0x50 = 0x02050 */
	CHECK_VAL ("real-mode read DS:0050 after PM exit",
		e86_get_mem8 (c, e86_get_ds(c), 0x0050), 0x77);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 6: e86_load_seg_pm reads GDT and populates seg_base[]
	 *   GDT at physical 0x001000:
	 *     entry 0 (null): all zeros
	 *     entry 1 (sel=0x08): base=0x030000, limit=0x7FFF, ar=0x9B
	 *     entry 2 (sel=0x10): base=0x040000, limit=0xFFFF, ar=0x93
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	c->ram = ram;
	c->ram_cnt = RAM_SIZE;
	/* Build GDT at 0x001000 */
	write_desc (0x001000, 0, 0, 0);              /* null */
	write_desc (0x001008, 0x7fff, 0x030000, 0x9b); /* sel 0x08: code */
	write_desc (0x001010, 0xffff, 0x040000, 0x93); /* sel 0x10: data */
	c->gdtr_base  = 0x001000;
	c->gdtr_limit = 0x17;
	c->msw = E286_MSW_PE;

	/* Load sel 0x08 into CS */
	e86_load_seg_pm (c, E86_REG_CS, 0x08);
	CHECK_VAL ("GDT load CS base",  c->seg_base[E86_REG_CS],  0x030000UL);
	CHECK_VAL ("GDT load CS limit", c->seg_limit[E86_REG_CS], 0x7fff);
	CHECK_VAL ("GDT load CS ar",    c->seg_ar[E86_REG_CS],    0x9b);

	/* Load sel 0x10 into DS */
	e86_load_seg_pm (c, E86_REG_DS, 0x10);
	CHECK_VAL ("GDT load DS base",  c->seg_base[E86_REG_DS],  0x040000UL);
	CHECK_VAL ("GDT load DS limit", c->seg_limit[E86_REG_DS], 0xffff);
	CHECK_VAL ("GDT load DS ar",    c->seg_ar[E86_REG_DS],    0x93);

	/* Null descriptor clears cache */
	e86_load_seg_pm (c, E86_REG_ES, 0x00);
	CHECK_VAL ("GDT null ES base",  c->seg_base[E86_REG_ES],  0UL);
	CHECK_VAL ("GDT null ES limit", c->seg_limit[E86_REG_ES], 0);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 7: PM memory access after GDT-loaded seg_base
	 *   Write to DS:0x0020 → should appear at 0x040020
	 * ---------------------------------------------------------------- */
	memset (ram, 0, sizeof ram);
	c = make_286 ();
	c->ram = ram;
	c->ram_cnt = RAM_SIZE;
	write_desc (0x001000, 0, 0, 0);
	write_desc (0x001008, 0xffff, 0x030000, 0x9b);
	write_desc (0x001010, 0xffff, 0x040000, 0x93);
	c->gdtr_base = 0x001000;
	c->msw = E286_MSW_PE;
	e86_set_cs (c, 0x08);
	e86_load_seg_pm (c, E86_REG_CS, 0x08);
	e86_set_ds (c, 0x10);
	e86_load_seg_pm (c, E86_REG_DS, 0x10);

	e86_set_mem8 (c, e86_get_ds(c), 0x0020, 0xEE);
	CHECK_VAL ("PM+GDT write DS:0020 → 040020", ram[0x040020], 0xEE);
	CHECK_VAL ("PM+GDT did not corrupt 030020", ram[0x030020], 0x00);
	e86_del (c);

	check_operand_limits ();
	check_rep_restart ();
	check_stack_faults ();
	check_io_privilege ();
	check_fetch_bounds ();
	check_near_bounds ();
	check_task_rejection ();
	check_nested_faults ();
	check_real_mode_bounds ();
	check_invalid_and_nm ();
	check_bound_restart ();

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
