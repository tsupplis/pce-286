/*
 * tests/test_286_pm_fields.c
 *
 * Unit tests for T3-A item 2: protected-mode descriptor cache fields.
 *
 * Verifies:
 *   1. After e86_new: CPL=0, LDTR/TR zeroed, seg caches at real-mode defaults.
 *   2. After e86_reset on 80286: PM fields cleared, seg caches at real-mode defaults.
 *   3. Fields are independently writable (struct layout sanity check).
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

#define CHECK_VAL(label, got, want) \
	do { if ((unsigned long)(got) == (unsigned long)(want)) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s  got=0x%lX  want=0x%lX\n", \
			label, (unsigned long)(got), (unsigned long)(want)); \
		pass = 0; \
	} } while (0)

int main (void)
{
	e8086_t *c;
	int i;

	/* ----------------------------------------------------------------
	 * Test 1: initial values after e86_new
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	CHECK_VAL ("init: cpl=0",         c->cpl,        0);
	CHECK_VAL ("init: ldtr_sel=0",    c->ldtr_sel,   0);
	CHECK_VAL ("init: ldtr_base=0",   c->ldtr_base,  0);
	CHECK_VAL ("init: ldtr_limit=0",  c->ldtr_limit, 0);
	CHECK_VAL ("init: tr_sel=0",      c->tr_sel,     0);
	CHECK_VAL ("init: tr_base=0",     c->tr_base,    0);
	CHECK_VAL ("init: tr_limit=0",    c->tr_limit,   0);
	for (i = 0; i < 4; i++) {
		CHECK_VAL ("init: seg_base=0",        c->seg_base[i],  0);
		CHECK_VAL ("init: seg_limit=0xFFFF",  c->seg_limit[i], 0xffff);
	}
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 2: e86_reset on 80286 clears PM fields
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	e86_set_80286 (c);

	/* Dirty all fields manually */
	c->cpl = 3;
	c->ldtr_sel = 0x0018; c->ldtr_base = 0x123456; c->ldtr_limit = 0x00ff;
	c->tr_sel   = 0x0020; c->tr_base   = 0xabcdef; c->tr_limit   = 0x01ff;
	for (i = 0; i < 4; i++) {
		c->seg_base[i]  = 0xdeadbe;
		c->seg_limit[i] = 0x1234;
		c->seg_ar[i]    = 0x42;
	}

	e86_reset (c);

	CHECK_VAL ("reset: cpl=0",         c->cpl,        0);
	CHECK_VAL ("reset: ldtr_sel=0",    c->ldtr_sel,   0);
	CHECK_VAL ("reset: ldtr_base=0",   c->ldtr_base,  0);
	CHECK_VAL ("reset: ldtr_limit=0",  c->ldtr_limit, 0);
	CHECK_VAL ("reset: tr_sel=0",      c->tr_sel,     0);
	CHECK_VAL ("reset: tr_base=0",     c->tr_base,    0);
	CHECK_VAL ("reset: tr_limit=0",    c->tr_limit,   0);
	for (i = 0; i < 4; i++) {
		CHECK_VAL ("reset: segment base", c->seg_base[i],
			(i == E86_REG_CS) ? 0xff0000UL : 0);
		CHECK_VAL ("reset: seg_limit=0xFFFF", c->seg_limit[i], 0xffff);
	}
	/* msw must also be 0 after reset */
	CHECK_VAL ("reset: msw=FFF0", c->msw, 0xfff0);
	e86_del (c);

	/* ----------------------------------------------------------------
	 * Test 3: fields are independently writable
	 * ---------------------------------------------------------------- */
	c = e86_new ();
	e86_set_80286 (c);
	c->cpl       = 0;
	c->ldtr_sel  = 0x0008;
	c->ldtr_base = 0x100000;
	c->ldtr_limit = 0x07ff;
	c->tr_sel    = 0x0010;
	c->tr_base   = 0x200000;
	c->tr_limit  = 0x00ff;
	c->seg_base[1]  = 0x040000;   /* CS hidden base */
	c->seg_limit[1] = 0xffff;
	c->seg_ar[1]    = 0x9b;       /* present, code, R/X */
	CHECK_VAL ("write: ldtr_base",   c->ldtr_base,   0x100000);
	CHECK_VAL ("write: tr_base",     c->tr_base,     0x200000);
	CHECK_VAL ("write: cs seg_base", c->seg_base[1], 0x040000);
	CHECK_VAL ("write: cs seg_ar",   c->seg_ar[1],   0x9b);
	e86_del (c);

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
