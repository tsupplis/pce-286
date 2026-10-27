/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/ibmpc/xms.h                                         *
 * Created:     2003-09-01 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2003-2010 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/


#ifndef PCE_XMS_H
#define PCE_XMS_H 1


#include <cpu/e8086/e8086.h>
#include <devices/memory.h>
#include <libini/libini.h>


#define PCE_XMS_UMB_MAX 256


typedef struct {
	unsigned long size;
	unsigned long phys_base; /* physical address in pc->mem (0 = malloc-backed) */
	unsigned      lock;
	unsigned char *data;
} xms_emb_t;


typedef struct xms_umb_t {
	unsigned short segm;
	unsigned short size;
	unsigned char  alloc;
} xms_umb_t;


typedef struct {
	unsigned       emb_cnt;
	xms_emb_t      **emb;
	unsigned long  emb_used;
	unsigned long  emb_max;

	/* Physical pool (80286 + [ram] above 1 MB).
	 * When phys_pool != NULL, EMB allocations are carved from
	 * phys_pool->data at byte offset emb_phys_base - phys_pool->addr.
	 * emb_next_ofs tracks the next free byte within that pool. */
	mem_blk_t      *phys_pool;    /* ptr to [ram] block above 0x100000, or NULL */
	unsigned long  emb_phys_base; /* physical address of EMB area start */

	unsigned       umb_cnt;
	xms_umb_t      *umb;
	unsigned short umb_segm;
	unsigned short umb_used;
	unsigned short umb_size;

	mem_blk_t      *umbmem;

	mem_blk_t      *hma;
	int            hma_alloc;
} xms_t;


xms_t *xms_new (ini_sct_t *sct);

void xms_del (xms_t *xms);

/* Bind one writable physical RAM block, reserving 64 KB for HMA when the
 * block starts at 1 MB. A higher pool keeps any separately allocated HMA.
 * Returns nonzero without changing ownership if the layout is unsuitable. */
int xms_set_phys_pool (xms_t *xms, mem_blk_t *blk, int hma);

/* Disable UMB before its memory block is registered on the bus. */
void xms_disable_umb (xms_t *xms);

mem_blk_t *xms_get_umb_mem (xms_t *xms);
mem_blk_t *xms_get_hma_mem (xms_t *xms);

void xms_reset (xms_t *xms);

void xms_prt_state (xms_t *xms);

void xms_info (xms_t *xms, e8086_t *cpu);

void xms_handler (xms_t *xms, e8086_t *cpu);


#endif
