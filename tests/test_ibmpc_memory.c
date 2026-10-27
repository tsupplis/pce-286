/* Memory ownership regressions through machine setup and the XMS interface. */
#include "arch/ibmpc/main.h"
#include "arch/ibmpc/ibmpc.h"
#include "arch/ibmpc/hook.h"
#include "arch/ibmpc/xms.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

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
ibmpc_t *machine (const char *model, const char *cpu, unsigned long base,
	unsigned long size, int helper)
{
	ini_sct_t *cfg, *ram;
	ibmpc_t *pc;

	cfg = ini_sct_new (NULL);
	if (cfg == NULL) {
		return (NULL);
	}
	ini_set_str (cfg, "system.model", model);
	ini_set_uint32 (cfg, "system.patch_bios_init", 0);
	ini_set_uint32 (cfg, "system.patch_bios_int19", 0);
	ini_set_str (cfg, "cpu.model", cpu);
	ini_set_uint32 (cfg, "ram.address", 0);
	ini_set_uint32 (cfg, "ram.size", 640 * 1024UL);
	ini_set_uint32 (cfg, "rom.address", 0xf0000);
	ini_set_uint32 (cfg, "rom.size", 0x10000);
	ini_set_uint32 (cfg, "rom.default", 0x5a);
	ini_set_str (cfg, "terminal.driver", "null");
	ini_set_str (cfg, "video.device", "cga");
	if (size != 0) {
		ram = ini_get_sct (cfg, "ram[+]", 1);
		ini_set_uint32 (ram, "address", base);
		ini_set_uint32 (ram, "size", size);
	}
	if (helper) {
		ini_set_uint32 (cfg, "xms.xms_size", 128 * 1024UL);
		ini_set_uint32 (cfg, "xms.hma", 1);
	}
	pc = pc_new (cfg);
	if (pc == NULL) {
		ini_sct_del (cfg);
	}
	return (pc);
}

static
void xcall (ibmpc_t *pc, unsigned fn, unsigned dx)
{
	e86_set_ax (pc->cpu, fn << 8);
	e86_set_dx (pc->cpu, dx);
	pc_hook_old (pc, PCEH_XMS & 0xff, PCEH_XMS >> 8);
}

static
unsigned allocate (ibmpc_t *pc, unsigned kb)
{
	xcall (pc, 9, kb);
	check ("XMS allocation succeeds", e86_get_ax (pc->cpu), 1);
	return (e86_get_ax (pc->cpu) ? e86_get_dx (pc->cpu) : 0);
}

static
void reporting (const char *model, const char *cpu, int helper)
{
	ibmpc_t *pc;
	unsigned kb;

	pc = machine (model, cpu, 0x100000, 0x40000, helper);
	if (pc == NULL) {
		check ("machine allocation", 0, 1);
		return;
	}
	kb = strcmp (cpu, "80286") == 0 ? 256 : 128;
	e86_set_ax (pc->cpu, 0x8800);
	pc_hook_old (pc, PCEH_INT & 0xff, 0x15);
	check ("extended memory independent of XMS and ROM aliases", e86_get_ax (pc->cpu), kb);
	if (pc->at_rtc != NULL) {
		check ("CMOS reports installed RAM", pc->at_rtc->data[0x17] |
			(pc->at_rtc->data[0x18] << 8), 256);
	}
	pc_del (pc);
}

static
void ownership (unsigned long base, unsigned long size)
{
	ibmpc_t *pc;
	mem_blk_t *rom;

	pc = machine ("5170", "80286", base, size, 1);
	if (pc == NULL) {
		check ("machine allocation", 0, 1);
		return;
	}
	rom = mem_get_blk (pc->mem, 0xfffff0);
	check ("XMS never selects ROM", pc->xms->phys_pool == rom, 0);
	if (size == 0) {
		check ("no physical pool without extended RAM", pc->xms->phys_pool != NULL, 0);
	}
	check ("HMA exists", pc->xms->hma != NULL, 1);
	if (pc->xms->hma != NULL) {
		check ("HMA always starts at 1 MB", pc->xms->hma->addr1, 0x100000);
	}
	check ("HMA accessible through physical map", mem_get_blk (pc->mem, 0x100000) != NULL, 1);
	pc_reset (pc);
	check ("reset cannot clear BIOS through XMS HMA", mem_get_uint8 (pc->mem, 0xff0100), 0x5a);
	if (size != 0) {
		check ("EMB starts after HMA or at configured pool",
			pc->xms->emb_phys_base, base == 0x100000 ? 0x110000 : base);
	}
	pc_del (pc);
}

static
void moves_and_allocations (void)
{
	ibmpc_t *pc;
	unsigned a, b, again;
	unsigned long addr;
	xms_emb_t *src, *dst;

	pc = machine ("5160", "80286", 0x100000, 0x40000, 1);
	if (pc == NULL) {
		check ("machine allocation", 0, 1);
		return;
	}
	a = allocate (pc, 64);
	b = allocate (pc, 64);
	if ((a == 0) || (b == 0)) {
		pc_del (pc);
		return;
	}
	src = pc->xms->emb[a - 1];
	dst = pc->xms->emb[b - 1];
	memset (src->data, 0x11, src->size);
	memset (dst->data, 0x22, dst->size);
	src->data[10] = 0x5a;
	src->data[11] = 0xa5;
	/* Move two bytes from source+10 to destination+20. */
	mem_set_uint32_le (pc->mem, 0x100, 2);
	mem_set_uint16_le (pc->mem, 0x104, a);
	mem_set_uint32_le (pc->mem, 0x106, 10);
	mem_set_uint16_le (pc->mem, 0x10a, b);
	mem_set_uint32_le (pc->mem, 0x10c, 20);
	e86_set_ds (pc->cpu, 0);
	e86_set_si (pc->cpu, 0x100);
	xcall (pc, 0x0b, 0);
	check ("move honors source and destination offsets", dst->data[20], 0x5a);
	check ("move preserves destination prefix", dst->data[0], 0x22);

	/* Declared block bounds, with backing RAM beyond the block for safe regression. */
	mem_set_uint32_le (pc->mem, 0x106, src->size - 1);
	mem_set_uint32_le (pc->mem, 0x10c, 20);
	xcall (pc, 0x0b, 0);
	check ("out-of-range source move rejected", e86_get_ax (pc->cpu), 0);
	check ("out-of-range source error", e86_get_bl (pc->cpu), 0xa7);

	xcall (pc, 0x0c, b);
	addr = ((unsigned long)e86_get_dx (pc->cpu) << 16) | e86_get_bx (pc->cpu);
	check ("lock returns actual physical address", addr, dst->phys_base);
	xcall (pc, 0x0d, b);
	xcall (pc, 0x0a, a);
	again = allocate (pc, 64);
	check ("freed physical range reused", pc->xms->emb[again - 1]->phys_base, 0x110000);
	check ("reused allocation stays inside RAM", pc->xms->emb[again - 1]->phys_base +
		pc->xms->emb[again - 1]->size <= 0x140000, 1);
	pc_del (pc);
}

static
void resize_and_fragmentation (void)
{
	ibmpc_t *pc;
	unsigned a, b, c, h, i;
	unsigned long base;
	xms_emb_t *blk;

	pc = machine ("5170", "80286", 0x100000, 0x40000, 1);
	if (pc == NULL) {
		check ("machine allocation", 0, 1);
		return;
	}
	a = allocate (pc, 48);
	b = allocate (pc, 48);
	c = allocate (pc, 48);
	if (!a || !b || !c) {
		pc_del (pc);
		return;
	}
	xcall (pc, 0x0a, b);
	xcall (pc, 8, 0);
	check ("largest physical free block reflects fragmentation", e86_get_ax (pc->cpu), 48);
	check ("total free space includes holes", e86_get_dx (pc->cpu), 96);
	blk = pc->xms->emb[c - 1];
	memset (blk->data, 0x67, blk->size);
	/* A physical resize may move within the pool, never host-realloc its data. */
	e86_set_bx (pc->cpu, 80);
	xcall (pc, 0x0f, c);
	check ("physical resize succeeds", e86_get_ax (pc->cpu), 1);
	check ("physical resize preserves bytes", blk->data[0], 0x67);
	check ("physical resize fits backing RAM", blk->phys_base + blk->size <= 0x140000, 1);
	xcall (pc, 0x0c, c);
	base = blk->phys_base;
	e86_set_bx (pc->cpu, 32);
	xcall (pc, 0x0f, c);
	check ("locked block cannot resize", e86_get_ax (pc->cpu), 0);
	check ("locked physical address retained", blk->phys_base, base);
	xcall (pc, 0x0a, c);
	check ("locked block cannot be freed", e86_get_ax (pc->cpu), 0);
	xcall (pc, 0x0d, c);
	xcall (pc, 0x0a, c);
	xcall (pc, 0x0a, a);
	for (i = 0; i < 16; i++) {
		h = allocate (pc, 192);
		if (h == 0) {
			break;
		}
		check ("repeated full-pool allocation bounded", pc->xms->emb[h - 1]->phys_base, 0x110000);
		xcall (pc, 0x0a, h);
	}
	pc_del (pc);
}

static
void windows (const char *model, unsigned ems_seg, unsigned umb_seg,
	unsigned umb_kb, int want_ems, int want_umb)
{
	ini_sct_t *cfg;
	ibmpc_t *pc;

	cfg = ini_sct_new (NULL);
	ini_set_str (cfg, "system.model", model);
	ini_set_str (cfg, "cpu.model", "80286");
	ini_set_uint32 (cfg, "ram.size", 640 * 1024UL);
	ini_set_uint32 (cfg, "rom.address", 0xf0000);
	ini_set_uint32 (cfg, "rom.size", 65536);
	ini_set_uint32 (cfg, "rom.default", 0x5a);
	ini_set_str (cfg, "terminal.driver", "null");
	ini_set_str (cfg, "video.device", "cga");
	ini_set_uint32 (cfg, "ems.size", 1024 * 1024UL);
	ini_set_uint32 (cfg, "ems.segment", ems_seg);
	ini_set_uint32 (cfg, "xms.umb_segm", umb_seg);
	ini_set_uint32 (cfg, "xms.umb_size", umb_kb * 1024UL);
	ini_set_uint32 (cfg, "xms.xms_size", 128 * 1024UL);
	pc = pc_new (cfg);
	check ("window machine created", pc != NULL, 1);
	if (pc == NULL) {
		ini_sct_del (cfg);
		return;
	}
	check ("EMS collision policy", pc->ems != NULL, want_ems);
	check ("XMS remains available", pc->xms != NULL, 1);
	if (pc->xms != NULL) {
		check ("UMB collision policy", xms_get_umb_mem (pc->xms) != NULL, want_umb);
		check ("UMB allocator matches mapping", pc->xms->umb_size != 0, want_umb);
		xcall (pc, 0x10, 16);
		check ("guest UMB request matches availability", e86_get_ax (pc->cpu), want_umb);
		xcall (pc, 9, 1);
		check ("XMS allocation survives rejected UMB", e86_get_ax (pc->cpu), 1);
		pc_reset (pc);
		check ("reset preserves BIOS with rejected windows", mem_get_uint8 (pc->mem, 0xf0100), 0x5a);
	}
	pc_del (pc);
}

int main (void)
{
	par_terminal = "null";
	reporting ("5170", "80286", 0);
	reporting ("5170", "80286", 1);
	reporting ("5160", "80286", 0);
	reporting ("5160", "8086", 1);
	ownership (0, 0);
	ownership (0x100000, 0x40000);
	ownership (0x110000, 0x40000);
	moves_and_allocations ();
	resize_and_fragmentation ();
	windows ("5160", 0xe000, 0xca00, 88, 1, 1);
	windows ("5170", 0xe000, 0xca00, 88, 1, 1);
	windows ("atari-pc", 0xe000, 0xca00, 88, 1, 1);
	windows ("m24", 0xe000, 0xca00, 88, 1, 1);
	windows ("5160", 0xf000, 0xca00, 88, 0, 1);
	windows ("5170", 0xb800, 0xca00, 88, 0, 1);
	windows ("5160", 0x8000, 0xca00, 88, 0, 1);
	windows ("5170", 0xe000, 0xdf00, 8, 1, 0);
	windows ("5160", 0xe000, 0xf000, 16, 1, 0);
	windows ("5170", 0xe000, 0xb800, 16, 1, 0);
	printf ("%u memory ownership checks, %u failures\n", checks, failures);
	return (failures != 0);
}
