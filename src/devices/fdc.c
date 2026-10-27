/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/devices/fdc.c                                            *
 * Created:     2007-09-06 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2007-2023 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include <stdlib.h>
#include <string.h>

#include <devices/fdc.h>

#include <drivers/block/blkpsi.h>


/*
 * This is the glue code between the 8272 FDC, block devices and
 * the rest of the emulator.
 */


static
struct {
	unsigned v1;
	unsigned v2;
} errmap[] = {
	{ PCE_BLK_PSI_NO_ID,    E8272_ERR_NO_ID },
	{ PCE_BLK_PSI_NO_DATA,  E8272_ERR_NO_DATA },
	{ PCE_BLK_PSI_CRC_ID,   E8272_ERR_CRC_ID },
	{ PCE_BLK_PSI_CRC_DATA, E8272_ERR_CRC_DATA },
	{ PCE_BLK_PSI_DEL_DAM,  E8272_ERR_DEL_DAM },
	{ PCE_BLK_PSI_DATALEN,  E8272_ERR_DATALEN },
	{ PCE_BLK_PSI_WPROT,    E8272_ERR_WPROT },
	{ 0, 0 }
};


static void dev_fdc_del_dev (device_t *dev);


/*
 * Map an error code from PCE_BLK_PSI_* to E8272_ERR_*.
 */
static
unsigned dev_fdc_map_error (unsigned err)
{
	unsigned i;
	unsigned ret;

	ret = 0;

	i = 0;

	while (errmap[i].v1 != 0) {
		if (err & errmap[i].v1) {
			err &= ~errmap[i].v1;
			ret |= errmap[i].v2;
		}

		i += 1;
	}

	if (err) {
		ret |= E8272_ERR_OTHER;
	}

	return (ret);
}

static
unsigned dev_fdc_diskop_read (dev_fdc_t *fdc, e8272_diskop_t *p)
{
	unsigned ret, err;
	disk_t   *dsk;

	dsk = dsks_get_disk (fdc->dsks, fdc->drive[p->pd & 3]);

	if (dsk == NULL) {
		p->cnt = 0;
		return (E8272_ERR_NO_DATA);
	}

	if (dsk_get_type (dsk) != PCE_DISK_PSI) {
		if (p->cnt < 512) {
			p->cnt = 0;
			return (E8272_ERR_OTHER);
		}

		if (dsk_read_chs (dsk, p->buf, p->pc, p->ph, p->ls, 1)) {
			p->cnt = 0;
			return (E8272_ERR_NO_DATA);
		}

		p->cnt = 512;

		return (0);
	}

	err = dsk_psi_read_chs (dsk->ext, p->buf, &p->cnt, p->pc, p->ph, p->ps, 1);

	ret = dev_fdc_map_error (err);

	return (ret);
}

static
unsigned dev_fdc_diskop_write (dev_fdc_t *fdc, e8272_diskop_t *p)
{
	unsigned err, ret;
	disk_t   *dsk;

	dsk = dsks_get_disk (fdc->dsks, fdc->drive[p->pd & 3]);

	if (dsk == NULL) {
		p->cnt = 0;
		return (E8272_ERR_NO_DATA);
	}

	if (dsk_get_type (dsk) != PCE_DISK_PSI) {
		if (p->cnt != 512) {
			return (E8272_ERR_OTHER);
		}

		if (dsk_write_chs (dsk, p->buf, p->pc, p->ph, p->ls, 1)) {
			p->cnt = 0;
			return (E8272_ERR_OTHER);
		}

		return (0);
	}

	err = dsk_psi_write_chs (dsk->ext, p->buf, &p->cnt, p->pc, p->ph, p->ps, 1);

	ret = dev_fdc_map_error (err);

	return (ret);
}

static
unsigned dev_fdc_diskop_format (dev_fdc_t *fdc, e8272_diskop_t *p)
{
	unsigned      cnt;
	disk_t        *dsk;
	disk_psi_t    *psi;
	psi_sct_t     *sct;
	unsigned char buf[512];

	dsk = dsks_get_disk (fdc->dsks, fdc->drive[p->pd & 3]);

	if (dsk == NULL) {
		return (E8272_ERR_NO_DATA);
	}

	cnt = 128 << p->ln;

	if (dsk_get_type (dsk) != PCE_DISK_PSI) {
		if (cnt != 512) {
			return (E8272_ERR_OTHER);
		}

		memset (buf, p->fill, 512);

		if (dsk_write_chs (dsk, buf, p->pc, p->ph, p->ps + 1, 1)) {
			return (E8272_ERR_OTHER);
		}

		return (0);
	}

	if (p->ps == 0) {
		dsk_psi_erase_track (dsk->ext, p->pc, p->ph);
	}

	if (dsk_psi_format_sector (dsk->ext, p->pc, p->ph, p->lc, p->lh, p->ls, cnt, p->fill)) {
		return (E8272_ERR_OTHER);
	}

	/* The 8272 currently accepts only MFM FORMAT commands. Record the
	 * selected density on the new sector, not the image-wide default:
	 * other tracks may retain a different density. Legacy adapters keep
	 * their existing image-default behavior. */
	if (fdc->extended) {
		psi = dsk->ext;
		sct = psi_img_get_sector (psi->img, p->pc, p->ph, p->ps, 1);
		if (sct == NULL) {
			return (E8272_ERR_OTHER);
		}
		psi_sct_set_encoding (sct, (fdc->e8272.data_rate == 500000)
			? PSI_ENC_MFM_HD : PSI_ENC_MFM_DD);
	}

	return (0);
}

/* Sector images have density metadata, but not a physical drive/spindle
 * model. Reject incompatible MFM densities on the rate-selectable adapter.
 * An explicit nominal spindle speed distinguishes DD at 250/300 kbit/s.
 * This does not model physical rotation or step timing.
 * Keep legacy adapters and unknown image encodings permissive. */
static
int dev_fdc_rate_matches (dev_fdc_t *fdc, disk_t *dsk, e8272_diskop_t *p)
{
	disk_psi_t *psi;
	psi_sct_t *sct;
	unsigned encoding;

	if (!fdc->extended) {
		return (1);
	}
	encoding = PSI_ENC_UNKNOWN;
	if (dsk_get_type (dsk) == PCE_DISK_PSI) {
		psi = dsk->ext;
		sct = psi_img_get_sector (psi->img, p->pc, p->ph, p->ps, 1);
		if (sct != NULL) {
			encoding = sct->encoding;
		}
	}
	else if ((dsk->c == 80) && (dsk->h == 2) &&
		((dsk->s == 15) || (dsk->s == 18))) {
		encoding = PSI_ENC_MFM_HD;
	}
	else if (((dsk->c == 40) || (dsk->c == 80)) &&
		((dsk->h == 1) || (dsk->h == 2)) &&
		((dsk->s == 8) || (dsk->s == 9))) {
		encoding = PSI_ENC_MFM_DD;
	}
	if (encoding == PSI_ENC_MFM_HD) {
		return (fdc->e8272.data_rate == 500000);
	}
	if (encoding == PSI_ENC_MFM_DD) {
		if (fdc->rpm[p->pd & 3] == 300) {
			return (fdc->e8272.data_rate == 250000);
		}
		if (fdc->rpm[p->pd & 3] == 360) {
			return (fdc->e8272.data_rate == 300000);
		}
		return ((fdc->e8272.data_rate == 250000) ||
			(fdc->e8272.data_rate == 300000));
	}
	return (1);
}

static
unsigned dev_fdc_diskop_readid (dev_fdc_t *fdc, e8272_diskop_t *p)
{
	disk_t        *dsk;
	unsigned char buf[512];
	unsigned      lc, lh, ls, cnt, cnt_id;
	unsigned long pos;

	dsk = dsks_get_disk (fdc->dsks, fdc->drive[p->pd & 3]);

	if (dsk == NULL) {
		return (E8272_ERR_NO_ID);
	}
	if (!dev_fdc_rate_matches (fdc, dsk, p)) {
		return (E8272_ERR_NO_ID);
	}

	if (dsk_get_type (dsk) != PCE_DISK_PSI) {
		if (dsk_read_chs (dsk, buf, p->pc, p->ph, p->ps + 1, 1)) {
			return (E8272_ERR_NO_ID);
		}

		p->lc = p->pc;
		p->lh = p->ph;
		p->ls = p->ps + 1;
		p->ln = 2;

		return (0);
	}

	if (dsk_psi_read_id (dsk->ext, p->pc, p->ph, p->ps, &lc, &lh, &ls, &cnt, &cnt_id, &pos)) {
		return (E8272_ERR_NO_ID);
	}

	p->lc = lc;
	p->lh = lh;
	p->ls = ls;
	p->ln = 0;
	p->pos = pos;

	while (cnt_id > 128) {
		cnt_id >>= 1;
		p->ln += 1;
	}

	return (0);
}
static
unsigned dev_fdc_diskop (dev_fdc_t *fdc, unsigned op, e8272_diskop_t *p)
{
	switch (op) {
	case E8272_DISKOP_READ:
		return (dev_fdc_diskop_read (fdc, p));

	case E8272_DISKOP_WRITE:
		return (dev_fdc_diskop_write (fdc, p));

	case E8272_DISKOP_FORMAT:
		return (dev_fdc_diskop_format (fdc, p));

	case E8272_DISKOP_READID:
		return (dev_fdc_diskop_readid (fdc, p));
	}

	return (E8272_ERR_OTHER);
}


/* The adapter owns the drive-change latch, not the 8272 command engine. */
void dev_fdc_media_changed (dev_fdc_t *fdc, unsigned drive)
{
	unsigned i;

	for (i = 0; i < 4; i++) {
		if (fdc->drive[i] == drive) {
			fdc->changed[i] = 1;
			fdc->e8272.drv[i].ok = 0;
		}
	}
}

static
disk_t *dev_fdc_media (dev_fdc_t *fdc, unsigned drive)
{
	disk_t *disk;

	disk = (fdc->dsks == NULL) ? NULL : dsks_get_disk (fdc->dsks, fdc->drive[drive]);
	if (disk != fdc->media[drive]) {
		fdc->media[drive] = disk;
		fdc->changed[drive] = 1;
		fdc->e8272.drv[drive].ok = 0;
	}
	return (disk);
}

static
void dev_fdc_step (void *ext, unsigned drive)
{
	dev_fdc_t *fdc;

	fdc = ext;
	if ((dev_fdc_media (fdc, drive) != NULL) &&
		((fdc->e8272.dor & 3) == drive) &&
		(fdc->e8272.dor & (0x10 << drive))) {
		fdc->changed[drive] = 0;
	}
}

static
unsigned char dev_fdc_dir (dev_fdc_t *fdc, unsigned long addr)
{
	unsigned drive;

	(void) addr;
	drive = fdc->e8272.dor & 3;
	/* Reading DIR never clears the latch; only a step with media does. */
	if (dev_fdc_media (fdc, drive) == NULL) {
		return (0x80);
	}
	return (fdc->changed[drive] ? 0x80 : 0);
}

static
void dev_fdc_ccr (dev_fdc_t *fdc, unsigned long addr, unsigned char val)
{
	static const unsigned long rates[] = { 500000, 300000, 250000 };

	(void) addr;
	fdc->ccr = val & 3;
	/* 11 is reserved on the original three-rate adapter. */
	if (fdc->ccr < 3) {
		e8272_set_data_rate (&fdc->e8272, rates[fdc->ccr]);
		if (fdc->e8272.verbose >= 1) {
			fprintf (stderr, "FDC: CCR=%u rate=%lu\n", fdc->ccr,
				fdc->e8272.data_rate);
		}
	}
}

void dev_fdc_enable_extended (dev_fdc_t *fdc)
{
	fdc->extended = 1;
	mem_blk_set_size (&fdc->blk, 6);
	e8272_set_step_fct (&fdc->e8272, fdc, dev_fdc_step);
	dev_fdc_ccr (fdc, 0, 0);
}

static
void dev_fdc_clock (dev_fdc_t *fdc, unsigned n)
{
	e8272_clock (&fdc->e8272, n);
}

dev_fdc_t *dev_fdc_new (unsigned long addr)
{
	unsigned  i;
	dev_fdc_t *fdc;

	fdc = malloc (sizeof (dev_fdc_t));

	if (fdc == NULL) {
		return (NULL);
	}

	dev_init (&fdc->dev, fdc, "fdc");

	fdc->dev.del = dev_fdc_del_dev;
	fdc->dev.clock = (void *) dev_fdc_clock;

	e8272_init (&fdc->e8272);
	e8272_set_diskop_fct (&fdc->e8272, fdc, dev_fdc_diskop);

	fdc->dsks = NULL;
	fdc->extended = 0;
	fdc->ccr = 0;
	mem_blk_init (&fdc->extension, addr + 7, 1, 0);
	mem_blk_set_fct (&fdc->extension, fdc, dev_fdc_dir, NULL, NULL,
		dev_fdc_ccr, NULL, NULL);
	mem_blk_init (&fdc->blk, addr, 8, 0);

	mem_blk_set_fct (&fdc->blk, &fdc->e8272,
		e8272_get_uint8, NULL, NULL,
		e8272_set_uint8, NULL, NULL
	);

	for (i = 0; i < 4; i++) {
		fdc->drive[i] = 0xffff;
		fdc->rpm[i] = 0;
		fdc->changed[i] = 1;
		fdc->media[i] = NULL;
	}

	return (fdc);
}

void dev_fdc_del (dev_fdc_t *fdc)
{
	if (fdc != NULL) {
		mem_blk_free (&fdc->blk);
		mem_blk_free (&fdc->extension);
		e8272_free (&fdc->e8272);

		free (fdc);
	}
}

static
void dev_fdc_del_dev (device_t *dev)
{
	dev_fdc_del (dev->ext);
}

void dev_fdc_mem_add_io (dev_fdc_t *fdc, memory_t *io)
{
	mem_add_blk (io, &fdc->blk, 0);
	if (fdc->extended) {
		mem_add_blk (io, &fdc->extension, 0);
	}
}

void dev_fdc_mem_rmv_io (dev_fdc_t *fdc, memory_t *io)
{
	mem_rmv_blk (io, &fdc->blk);
	if (fdc->extended) {
		mem_rmv_blk (io, &fdc->extension);
	}
}


void dev_fdc_reset (dev_fdc_t *fdc)
{
	if (fdc->extended) {
		dev_fdc_ccr (fdc, 0, 0);
	}
	e8272_reset (&fdc->e8272);
}

void dev_fdc_set_disks (dev_fdc_t *fdc, disks_t *dsks)
{
	fdc->dsks = dsks;
}

void dev_fdc_set_drive (dev_fdc_t *fdc, unsigned fdcdrv, unsigned drive)
{
	if (fdcdrv < 4) {
		if (fdc->drive[fdcdrv] != drive) {
			fdc->changed[fdcdrv] = 1;
			fdc->e8272.drv[fdcdrv].ok = 0;
		}
		fdc->drive[fdcdrv] = drive;
	}
}

int dev_fdc_set_rpm (dev_fdc_t *fdc, unsigned fdcdrv, unsigned rpm)
{
	if ((fdcdrv >= 4) || ((rpm != 0) && (rpm != 300) && (rpm != 360))) {
		return (1);
	}
	if (fdc->rpm[fdcdrv] != rpm) {
		fdc->rpm[fdcdrv] = rpm;
		fdc->e8272.drv[fdcdrv].ok = 0;
	}
	return (0);
}

unsigned dev_fdc_get_drive (dev_fdc_t *fdc, unsigned fdcdrv)
{
	if (fdcdrv < 4) {
		return (fdc->drive[fdcdrv]);
	}

	return (0xffff);
}
