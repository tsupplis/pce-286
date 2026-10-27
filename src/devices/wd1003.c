/* WD1003-compatible fixed-disk controller with optional CHS media.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register/IRQ contract: IBM AT Fixed Disk and Diskette Drive Adapter,
 * August 31, 1984, pp. 2-10, 15. Normal 512-byte CHS READ/WRITE,
 * VERIFY, restore, seek, diagnostics and drive parameters are supported.
 * Format-track and long/ECC commands are not implemented and abort.
 * Command latency is approximate (1 ms), not physical drive timing.
 */
#include <stdlib.h>
#include <string.h>
#include "wd1003.h"

#define WD_BUSY 0x80
#define WD_ERROR 0x01
#define WD_ABORT 0x04
#define WD_READY 0x50
#define WD_DRQ 0x08
#define WD_IDNF 0x10
#define WD_READ 1
#define WD_WRITE 2
#define WD_COMMIT 3

static
void wd1003_irq (wd1003_t *wd)
{
	unsigned char val;

	val = wd->pending && ((wd->ctl & 6) == 0);
	if (val != wd->irq) {
		wd->irq = val;
		if (wd->set_irq != NULL) {
			wd->set_irq (wd->irq_ext, val);
		}
	}
}

void wd1003_set_irq_fct (wd1003_t *wd, void *ext,
	void (*fct) (void *, unsigned char))
{
	wd->irq_ext = ext;
	wd->set_irq = fct;
	if (fct != NULL) {
		fct (ext, wd->irq);
	}
}

static
unsigned wd1003_ready (wd1003_t *wd)
{
	return wd->disk[(wd->reg[6] >> 4) & 1] ? WD_READY : 0;
}

int wd1003_set_disk (wd1003_t *wd, unsigned drive, disk_t *disk)
{
	if (drive > 1 || (wd->reg[7] & (WD_BUSY | WD_DRQ))) return 1;
	if (disk && (!disk->visible_c || disk->visible_c > 1024 ||
		!disk->visible_h || disk->visible_h > 16 ||
		!disk->visible_s || disk->visible_s > 255 ||
		(uint64_t) disk->visible_c * disk->visible_h * disk->visible_s > disk->blocks)) return 1;
	wd->disk[drive] = disk;
	wd->reg[7] = wd1003_ready (wd);
	return 0;
}

static
void wd1003_fail (wd1003_t *wd, unsigned error)
{
	wd->phase = 0;
	wd->reg[1] = error;
	wd->reg[7] = wd1003_ready (wd) | WD_ERROR;
	wd->pending = 1;
	wd1003_irq (wd);
}

static
int wd1003_address (wd1003_t *wd)
{
	unsigned d = (wd->reg[6] >> 4) & 1;
	unsigned c = wd->reg[4] | (wd->reg[5] << 8);
	unsigned h = wd->reg[6] & ((wd->ctl & 8) ? 15 : 7);
	unsigned s = wd->reg[3];
	disk_t *disk = wd->disk[d];

	if (!disk) { wd1003_fail (wd, WD_ABORT); return 1; }
	if (c >= disk->visible_c || h >= disk->visible_h || s == 0 || s > disk->visible_s) {
		wd1003_fail (wd, WD_IDNF); return 1;
	}
	wd->lba = (c * disk->visible_h + h) * disk->visible_s + s - 1;
	return 0;
}

static
void wd1003_advance (wd1003_t *wd)
{
	unsigned d = (wd->reg[6] >> 4) & 1;
	unsigned h, c, s;

	wd->remaining--;
	wd->reg[2] = wd->remaining & 255;
	if (!wd->remaining) return;
	s = wd->reg[3] + 1;
	wd->reg[3] = s & 255;
	if (s > wd->sectors[d]) {
		wd->reg[3] = 1;
		h = (wd->reg[6] & 15) + 1;
		if (h >= wd->heads[d]) {
			h = 0;
			c = (wd->reg[4] | (wd->reg[5] << 8)) + 1;
			wd->reg[4] = c & 255;
			wd->reg[5] = c >> 8;
		}
		wd->reg[6] = (wd->reg[6] & 0xf0) | h;
	}
}

static
void wd1003_sector (wd1003_t *wd, int interrupt)
{
	disk_t *disk;

	if (wd1003_address (wd)) return;
	disk = wd->disk[(wd->reg[6] >> 4) & 1];
	wd->pos = 0;
	if (wd->phase == WD_READ && dsk_read_lba (disk, wd->data, wd->lba, 1)) {
		wd1003_fail (wd, 0x40); return;
	}
	if (wd->phase == WD_WRITE && disk->readonly) {
		wd1003_fail (wd, WD_ABORT); return;
	}
	wd->reg[7] = WD_READY | WD_DRQ;
	wd->pending = interrupt;
	wd1003_irq (wd);
}

static
void wd1003_signature (wd1003_t *wd)
{
	memset (wd->reg, 0, sizeof (wd->reg));
	wd->reg[1] = 1; /* Controller diagnostics succeeded, not drive readiness. */
	wd->reg[2] = 1;
	wd->reg[3] = 1;
	wd->reg[6] = 0xa0;
	wd->reg[7] = wd1003_ready (wd);
	wd->pos = wd->remaining = wd->phase = 0;
	memset (wd->heads, 0, sizeof (wd->heads));
	memset (wd->sectors, 0, sizeof (wd->sectors));
}

void wd1003_reset (wd1003_t *wd)
{
	wd->ctl = 0;
	wd->command = 0;
	wd->delay = 0;
	wd->pending = 0;
	wd1003_signature (wd);
	wd1003_irq (wd);
}

void wd1003_clock (wd1003_t *wd, unsigned long clocks)
{
	unsigned drive, op;
	disk_t *disk;

	if (wd->delay == 0 || (wd->ctl & 4)) return;
	if (clocks < wd->delay) { wd->delay -= clocks; return; }
	wd->delay = 0;
	drive = (wd->reg[6] >> 4) & 1;
	disk = wd->disk[drive];
	wd->reg[7] = wd1003_ready (wd);
	if (wd->phase == WD_READ) { wd1003_sector (wd, 1); return; }
	if (wd->phase == WD_COMMIT) {
		if (!disk || dsk_write_lba (disk, wd->data, wd->lba, 1)) {
			wd1003_fail (wd, WD_ABORT); return;
		}
		wd1003_advance (wd);
		if (wd->remaining) {
			wd->phase = WD_WRITE;
			wd1003_sector (wd, 1); return;
		}
		wd->phase = 0;
	}
	else if (wd->command == 0x100 || wd->command == 0x90) {
		wd1003_signature (wd);
	}
	else if (wd->command == 0x91) {
		wd->heads[drive] = (wd->reg[6] & 15) + 1;
		wd->sectors[drive] = wd->reg[2];
		if (!wd->sectors[drive]) { wd1003_fail (wd, WD_ABORT); return; }
	}
	else {
		op = wd->command;
		if (!disk) { wd1003_fail (wd, WD_ABORT); return; }
		if ((op & 0xf0) == 0x10) {
			wd->reg[4] = wd->reg[5] = 0;
		}
		else if ((op & 0xf0) == 0x70) {
			if ((wd->reg[4] | (wd->reg[5] << 8)) >= disk->visible_c) {
				wd1003_fail (wd, WD_IDNF); return;
			}
		}
		else if ((op & 0xfe) == 0x20 || (op & 0xfe) == 0x30 || (op & 0xfe) == 0x40) {
			wd->remaining = wd->reg[2] ? wd->reg[2] : 256;
			if (wd->remaining > 1 && (!wd->heads[drive] || !wd->sectors[drive])) {
				wd1003_fail (wd, WD_ABORT); return;
			}
			if ((op & 0xfe) == 0x40) {
				do {
					if (wd1003_address (wd)) return;
					if (dsk_read_lba (disk, wd->data, wd->lba, 1)) {
						wd1003_fail (wd, 0x40); return;
					}
					wd1003_advance (wd);
				} while (wd->remaining);
			}
			else {
				wd->phase = ((op & 0xfe) == 0x20) ? WD_READ : WD_WRITE;
				wd1003_sector (wd, wd->phase == WD_READ); return;
			}
		}
		else { wd1003_fail (wd, WD_ABORT); return; }
	}
	wd->pending = (wd->command != 0x100);
	wd1003_irq (wd);
}

static
unsigned char wd1003_get (wd1003_t *wd, unsigned long addr)
{
	if (addr == 7) {
		wd->pending = 0;
		wd1003_irq (wd);
	}
	if (wd->reg[7] & WD_BUSY) {
		return (WD_BUSY);
	}
	/* The data port requires 16-bit accesses. */
	return ((addr == 0) ? 0xff : wd->reg[addr & 7]);
}

static
void wd1003_start (wd1003_t *wd, unsigned command)
{
	wd->command = command;
	wd->phase = wd->pos = wd->remaining = 0;
	wd->reg[1] = 0;
	wd->reg[7] = WD_BUSY;
	wd->delay = wd->input_clock / 1000 + 1;
	wd->pending = 0;
	wd1003_irq (wd);
}

static
void wd1003_set (wd1003_t *wd, unsigned long addr, unsigned char val)
{
	if (wd->reg[7] & WD_BUSY) {
		return;
	}
	if (addr == 7) {
		wd1003_start (wd, val);
	}
	else if ((addr >= 2) && (addr <= 6) && !wd->phase) {
		wd->reg[addr] = val;
		if (addr == 6) wd->reg[7] = (wd->reg[7] & ~WD_READY) | wd1003_ready (wd);
	}
	/* Write-precompensation and byte writes to the data port are ignored. */
}

static
void wd1003_control (wd1003_t *wd, unsigned long addr, unsigned char val)
{
	unsigned char old;

	(void) addr;
	old = wd->ctl;
	wd->ctl = val & 14;
	if (val & 4) {
		wd->reg[7] = WD_BUSY;
		wd->delay = 0;
		wd->phase = wd->remaining = wd->pos = 0;
		wd->pending = 0;
	}
	else if (old & 4) {
		wd1003_start (wd, 0x100);
	}
	wd1003_irq (wd);
}

static
unsigned char wd1003_control_get (wd1003_t *wd, unsigned long addr)
{
	(void) wd;
	(void) addr;
	return (0xff);
}

static
unsigned short wd1003_data_get (wd1003_t *wd, unsigned long addr)
{
	unsigned val;

	if (addr || wd->phase != WD_READ || !(wd->reg[7] & WD_DRQ)) return 0xffff;
	val = wd->data[wd->pos] | (wd->data[wd->pos + 1] << 8);
	wd->pos += 2;
	if (wd->pos == 512) {
		wd1003_advance (wd);
		if (wd->remaining) {
			wd->reg[7] = WD_BUSY;
			wd->delay = wd->input_clock / 1000 + 1;
		}
		else {
			wd->phase = 0;
			wd->reg[7] = WD_READY;
		}
	}
	return val;
}

static
void wd1003_data_set (wd1003_t *wd, unsigned long addr, unsigned short val)
{
	if (addr || wd->phase != WD_WRITE || !(wd->reg[7] & WD_DRQ)) return;
	wd->data[wd->pos++] = val & 255;
	wd->data[wd->pos++] = val >> 8;
	if (wd->pos == 512) {
		wd->phase = WD_COMMIT;
		wd->reg[7] = WD_BUSY;
		wd->delay = wd->input_clock / 1000 + 1;
	}
}

wd1003_t *wd1003_new (unsigned long task, unsigned long control,
	unsigned long input_clock)
{
	wd1003_t *wd;

	wd = calloc (1, sizeof (*wd));
	if (wd == NULL) {
		return (NULL);
	}
	wd->input_clock = input_clock;
	mem_blk_init (&wd->task, task, 8, 0);
	mem_blk_set_fct (&wd->task, wd, wd1003_get, wd1003_data_get, NULL,
		wd1003_set, wd1003_data_set, NULL);
	/* IBM's 3F6 is write-only, not an ATA alternate-status register. */
	mem_blk_init (&wd->control, control, 1, 0);
	mem_blk_set_fct (&wd->control, wd, wd1003_control_get, NULL, NULL,
		wd1003_control, NULL, NULL);
	wd1003_reset (wd);
	return (wd);
}

void wd1003_del (wd1003_t *wd)
{
	if (wd != NULL) {
		mem_blk_free (&wd->task);
		mem_blk_free (&wd->control);
		free (wd);
	}
}
