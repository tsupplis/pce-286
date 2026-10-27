/*
 * Quick smoke-test for the 80286 0x0F disassembler.
 * Compile: gcc -Isrc -o test_disasm_286 test_disasm_286.c src/cpu/e8086/disasm.o
 * Run:     ./test_disasm_286
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"

typedef struct {
	const char     *label;
	unsigned char   bytes[8];
	unsigned        len;       /* expected dat_n */
	const char     *mnemonic;  /* expected op string */
	const char     *operand;   /* expected arg1 string (substring match) */
	int             expect_286; /* expect E86_DFLAGS_286 set */
} testcase_t;

static testcase_t tests[] = {
	/*
	 * ModRM encoding for direct memory [disp16]: mod=00 r/m=110 → base=0x06
	 * /reg field in bits 5-3:  /0=0x00 /1=0x08 /2=0x10 /3=0x18
	 * Full byte: /0 → 0x06, /1 → 0x0E, /2 → 0x16, /3 → 0x1E
	 */
	/* 0F 01 /0 (SGDT) + ModRM 0x06 + disp16 0x0100 */
	{ "SGDT mem",  { 0x0f, 0x01, 0x06, 0x00, 0x01 }, 5, "SGDT", "WORD",  1 },
	/* 0F 01 /1 (SIDT) + ModRM 0x0E + disp16 */
	{ "SIDT mem",  { 0x0f, 0x01, 0x0e, 0x00, 0x01 }, 5, "SIDT", "WORD",  1 },
	/* 0F 01 /2 (LGDT) + ModRM 0x16 + disp16 */
	{ "LGDT mem",  { 0x0f, 0x01, 0x16, 0x00, 0x01 }, 5, "LGDT", "WORD",  1 },
	/* 0F 01 /3 (LIDT) + ModRM 0x1E + disp16 */
	{ "LIDT mem",  { 0x0f, 0x01, 0x1e, 0x00, 0x01 }, 5, "LIDT", "WORD",  1 },
	/* 0F 01 /4 + ModRM 0xE0 (register AX) */
	{ "SMSW AX",   { 0x0f, 0x01, 0xe0 },              3, "SMSW", "AX",    1 },
	/* 0F 01 /6 + ModRM 0xF0 (register AX) */
	{ "LMSW AX",   { 0x0f, 0x01, 0xf0 },              3, "LMSW", "AX",    1 },
	/* 0F xx unknown → DB */
	{ "0F unknown",{ 0x0f, 0x05 },                    1, "DB",   NULL,    0 },
	/* 8086 context: plain POP AX (58) — unrelated, sanity check */
	{ "POP AX",    { 0x58 },                          1, "POP",  "AX",    0 },
};

#define NTEST (sizeof(tests) / sizeof(tests[0]))

int main (void)
{
	unsigned     i;
	int          pass = 1;
	e86_disasm_t op;

	for (i = 0; i < NTEST; i++) {
		testcase_t *t = &tests[i];

		memset (&op, 0, sizeof (op));
		e86_disasm (&op, t->bytes, 0x0000);

		int ok = 1;

		/* check mnemonic */
		if (strcmp (op.op, t->mnemonic) != 0) {
			printf ("FAIL %-14s  mnemonic: got '%s', want '%s'\n",
				t->label, op.op, t->mnemonic);
			ok = 0;
		}

		/* check operand substring if specified */
		if (t->operand != NULL && strstr (op.arg1, t->operand) == NULL) {
			printf ("FAIL %-14s  arg1: got '%s', want substring '%s'\n",
				t->label, op.arg1, t->operand);
			ok = 0;
		}

		/* check dat_n (instruction length) */
		if (op.dat_n != t->len) {
			printf ("FAIL %-14s  dat_n: got %u, want %u\n",
				t->label, op.dat_n, t->len);
			ok = 0;
		}

		/* check E86_DFLAGS_286 flag */
		int has_286 = (op.flags & E86_DFLAGS_286) != 0;
		if (has_286 != t->expect_286) {
			printf ("FAIL %-14s  E86_DFLAGS_286: got %d, want %d\n",
				t->label, has_286, t->expect_286);
			ok = 0;
		}

		if (ok) {
			printf ("PASS %-14s  %s %s\n", t->label, op.op, op.arg1);
		}
		else {
			pass = 0;
		}
	}

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return pass ? 0 : 1;
}
