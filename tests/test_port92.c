/*
 * CPU bus regressions for A20 and address-width boundaries. Port 0x92 itself
 * is exercised through the installed machine bus in test_at_io_audit.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu/e8086/e8086.h"
#include "cpu/e8086/internal.h"

static unsigned char ram[0x400000];
static unsigned failures;
static unsigned checks;

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
void write8 (void *ext, unsigned long addr, unsigned char val)
{
	(void) ext;
	if (addr < sizeof (ram)) {
		ram[addr] = val;
	}
}

static
void write16 (void *ext, unsigned long addr, unsigned short val)
{
	write8 (ext, addr, val);
	write8 (ext, addr + 1, val >> 8);
}

static
void check (const char *name, unsigned long got, unsigned long want)
{
	checks += 1;
	if (got != want) {
		printf ("FAIL %s: got=%lx want=%lx\n", name, got, want);
		failures += 1;
	}
}

static
void check_bus (int direct)
{
	e8086_t *c;
	unsigned i;

	memset (ram, 0, sizeof (ram));
	c = e86_new ();
	if (c == NULL) {
		check ("CPU allocation", 0, 1);
		return;
	}
	e86_set_80286 (c);
	e86_set_mem (c, NULL, read8, write8, read16, write16);
	c->ram = ram;
	c->ram_cnt = direct ? sizeof (ram) : 0;
	e86_set_addr_mask (c, 0xefffffUL);
	c->msw = E286_MSW_PE;
	c->sreg[E86_REG_DS] = 0x18;
	c->sreg[E86_REG_CS] = 0x08;
	c->seg_base[E86_REG_DS] = 0x1fffff;
	ram[0x0fffff] = 0x34;
	ram[0x200000] = 0x12;
	ram[0x100000] = 0x99;
	check ("word across A20 rising boundary", e86_get_mem16 (c, 0x18, 0), 0x1234);
	e86_set_mem16 (c, 0x18, 0, 0x5678);
	check ("word write low byte", ram[0xfffff], 0x78);
	check ("word write high byte", ram[0x200000], 0x56);
	check ("masked memory not written", ram[0x100000], 0x99);

	c->seg_base[E86_REG_DS] = 0xffffff;
	ram[0] = 0x5a;
	check ("24-bit wrap high byte", e86_get_mem16 (c, 0x18, 0) >> 8, 0x5a);

	/* Queue crosses from 1fffff to 200000: preserve bit 21. */
	c->seg_base[E86_REG_CS] = 0x1ffffd;
	e86_set_ip (c, 0);
	for (i = 0; i < c->pq_fill; i++) {
		ram[(0x1ffffd + i) & 0xefffff] = 0x40 + i;
	}
	c->pq_cnt = 0;
	e86_pq_fill (c);
	for (i = 0; i < c->pq_fill; i++) {
		check ("prefetch across A20 boundary", c->pq[i], 0x40 + i);
	}

	/* A descriptor spanning that same boundary must use masked bus reads. */
	c->gdtr_base = 0x1ffff5;
	ram[0xffffd] = 0xff;
	ram[0xffffe] = 0x7f;
	ram[0xfffff] = 0x56;
	ram[0x200000] = 0x34;
	ram[0x200001] = 0x12;
	ram[0x200002] = 0x93;
	e86_load_seg_pm (c, E86_REG_DS, 8);
	check ("descriptor limit on masked bus", c->seg_limit[E86_REG_DS], 0x7fff);
	check ("descriptor base across gate boundary", c->seg_base[E86_REG_DS], 0x123456);
	check ("descriptor access on masked bus", c->seg_ar[E86_REG_DS], 0x93);
	check ("TSS word across gate boundary", e86_tss_read16 (c, 0x1fffff, 0), 0x3456);

	/* Reset only the CPU: the external gate remains asserted. */
	e86_set_a20 (c, 0);
	e86_reset (c);
	check ("CPU reset retains A20 mask", e86_get_addr_mask (c), 0xefffff);
	e86_set_a20 (c, 1);
	check ("enabled gate retains 24-bit width", e86_get_addr_mask (c), 0xffffff);

	/* Legacy 20-bit bus wraps the second byte too. */
	e86_set_8086 (c);
	c->msw = 0;
	e86_set_addr_mask (c, 0xfffff);
	ram[0xfffff] = 0x78;
	ram[0] = 0x56;
	check ("8086 word wraps at 1 MB", e86_get_mem16 (c, 0xffff, 15), 0x5678);
	e86_del (c);
}

int main (void)
{
	check_bus (0);
	check_bus (1);
	printf ("%u CPU address-bus checks, %u failures\n", checks, failures);
	return (failures != 0);
}
