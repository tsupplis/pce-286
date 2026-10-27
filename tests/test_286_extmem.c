/*
 * tests/test_286_extmem.c
 *
 * Unit test for T5-B: 80286 extended memory above 1 MB.
 *
 * Verifies:
 *   1. A mem_blk_t registered at 0x100000 is reachable by an 80286 CPU
 *      (addr_mask = 0xFFFFFF) — sentinel write/read round-trip.
 *   2. The 80286 addr_mask = 0xFFFFFF does not truncate the physical address
 *      (i.e. the write lands in the above-1MB block, not at 0x00000).
 *   3. With addr_mask = 0xFFFFF (A20 off / sub-286 behaviour), the same
 *      logical address wraps below 1 MB and the above-1MB block is untouched.
 *   4. pc_mem_set_above1mb_active(active=0) hides the block; active=1
 *      restores it — consistent with A20 gate toggling.
 *
 * This test exercises only the CPU address mask + memory subsystem layer.
 * It does not instantiate a full ibmpc_t; the active-flag helper is tested
 * by directly manipulating mem_blk_t.active (the same field the real helper
 * writes).
 */

#include <stdio.h>
#include <string.h>
#include "cpu/e8086/e8086.h"
#include "devices/memory.h"

static int pass = 1;

#define CHECK(label, cond) \
	do { if (cond) { \
		printf ("PASS %s\n", label); \
	} else { \
		printf ("FAIL %s\n", label); \
		pass = 0; \
	} } while (0)

/* Write a byte through the CPU memory interface at a raw physical address,
 * masked by addr_mask (exactly as the CPU does for every access). */
static void cpu_write8 (e8086_t *c, memory_t *mem,
                        unsigned long phys, unsigned char val)
{
	mem_set_uint8 (mem, phys & c->addr_mask, val);
}

/* Read a byte through the CPU memory interface at a raw physical address,
 * masked by addr_mask. */
static unsigned char cpu_read8 (e8086_t *c, memory_t *mem,
                                unsigned long phys)
{
	return mem_get_uint8 (mem, phys & c->addr_mask);
}

int main (void)
{
	e8086_t   *c;
	memory_t  *mem;
	mem_blk_t *blk_low;   /* conventional RAM: 0x00000–0x9FFFF */
	mem_blk_t *blk_ext;   /* extended RAM:     0x100000–0x1FFFFF */

	/* Build a minimal memory map: 640 KB conventional + 1 MB extended. */
	mem     = mem_new ();
	blk_low = mem_blk_new (0x000000UL, 640UL * 1024, 1);
	blk_ext = mem_blk_new (0x100000UL, 1UL * 1024 * 1024, 1);
	mem_blk_clear (blk_low, 0x00);
	mem_blk_clear (blk_ext, 0x00);
	mem_add_blk (mem, blk_low, 1);
	mem_add_blk (mem, blk_ext, 1);

	/* ------------------------------------------------------------------ */
	/* Test 1: 80286 (addr_mask=0xFFFFFF) can reach 0x100000 without wrap */
	/* ------------------------------------------------------------------ */
	c = e86_new ();
	e86_set_80286 (c);

	/* Pre-load blk_low[0] with a known canary so we can detect a wrap.
	 * If the write below lands at 0x00000 instead of 0x100000 it will
	 * overwrite this canary and the check will catch it. */
	mem_blk_set_uint8 (blk_low, 0, 0x11);  /* canary at physical 0x00000 */
	mem_blk_clear (blk_ext, 0x00);         /* extended block starts clean */

	/* physical 0x100000 — only reachable on 286 (addr_mask = 0xFFFFFF) */
	cpu_write8 (c, mem, 0x100000UL, 0xA5);

	/* Prove the write reached blk_ext (address was NOT truncated) */
	CHECK ("286 write at 0x100000 stored in blk_ext[0]",
		mem_blk_get_uint8 (blk_ext, 0) == 0xA5);

	/* Prove the canary at 0x00000 is intact (address did NOT wrap) */
	CHECK ("286 write at 0x100000 did not wrap to 0x00000",
		mem_blk_get_uint8 (blk_low, 0) == 0x11);

	/* Round-trip read through the same mask path */
	CHECK ("286 read from 0x100000 returns written value",
		cpu_read8 (c, mem, 0x100000UL) == 0xA5);

	/* Restore blk_low[0] for subsequent tests */
	mem_blk_set_uint8 (blk_low, 0, 0x00);

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 2: addr_mask=0xFFFFF (A20 off) wraps 0x100000 → 0x00000       */
	/* ------------------------------------------------------------------ */
	c = e86_new ();
	e86_set_80286 (c);
	e86_set_addr_mask (c, 0xfffffUL);   /* simulate A20 disabled */

	/* Clear sentinel from test 1 */
	mem_blk_clear (blk_ext, 0x00);
	mem_blk_clear (blk_low, 0x00);

	/* With A20 off: physical 0x100000 masked to 0x00000 */
	cpu_write8 (c, mem, 0x100000UL, 0xB6);

	CHECK ("A20 off: write wraps to 0x00000 in blk_low",
		mem_blk_get_uint8 (blk_low, 0) == 0xB6);

	CHECK ("A20 off: blk_ext[0] untouched",
		mem_blk_get_uint8 (blk_ext, 0) == 0x00);

	e86_del (c);

	/* ------------------------------------------------------------------ */
	/* Test 3: active=0 hides blk_ext; active=1 restores it               */
	/* (mirrors pc_mem_set_above1mb_active behaviour)                      */
	/* ------------------------------------------------------------------ */
	c = e86_new ();
	e86_set_80286 (c);

	mem_blk_clear (blk_ext, 0x00);
	mem_blk_clear (blk_low, 0x00);

	/* Hide the extended block */
	mem_blk_set_active (blk_ext, 0);

	/* Write to 0x100000 — block is hidden, should go nowhere (defval) */
	cpu_write8 (c, mem, 0x100000UL, 0xC7);
	CHECK ("active=0: write to 0x100000 not stored in blk_ext",
		mem_blk_get_uint8 (blk_ext, 0) == 0x00);

	/* Restore the block */
	mem_blk_set_active (blk_ext, 1);

	cpu_write8 (c, mem, 0x100000UL, 0xD4);
	CHECK ("active=1: write to 0x100000 stored in blk_ext",
		mem_blk_get_uint8 (blk_ext, 0) == 0xD4);

	e86_del (c);

	mem_del (mem);

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return (pass ? 0 : 1);
}
