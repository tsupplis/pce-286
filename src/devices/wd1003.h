/* WD1003-compatible fixed-disk controller with optional CHS media.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef PCE_DEVICES_WD1003_H
#define PCE_DEVICES_WD1003_H 1

#include <devices/memory.h>
#include <drivers/block/block.h>

typedef struct {
	mem_blk_t task, control;
	unsigned char reg[8];
	unsigned char ctl, pending, irq;
	unsigned command;
	disk_t *disk[2]; /* borrowed from the machine disk set */
	unsigned char data[512];
	unsigned pos, remaining, phase;
	uint32_t lba;
	unsigned char heads[2], sectors[2];
	unsigned long delay, input_clock;
	void *irq_ext;
	void (*set_irq) (void *, unsigned char);
} wd1003_t;

wd1003_t *wd1003_new (unsigned long task, unsigned long control,
	unsigned long input_clock);
int wd1003_set_disk (wd1003_t *wd, unsigned drive, disk_t *disk);
void wd1003_del (wd1003_t *wd);
void wd1003_reset (wd1003_t *wd);
void wd1003_clock (wd1003_t *wd, unsigned long clocks);
void wd1003_set_irq_fct (wd1003_t *wd, void *ext,
	void (*fct) (void *, unsigned char));

#endif
