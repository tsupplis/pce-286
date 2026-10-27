/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/ibmpc/ibmpc.h                                       *
 * Created:     2001-05-01 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2001-2024 Hampa Hug <hampa@hampa.ch>                     *
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


#ifndef PCE_IBMPC_H
#define PCE_IBMPC_H 1


#include "at-keyboard.h"
#include "covox.h"
#include "ems.h"
#include "keyboard.h"
#include "speaker.h"
#include "xms.h"

#include <chipset/clock/mc146818a.h>

#include <chipset/82xx/e8237.h>
#include <chipset/82xx/e8250.h>
#include <chipset/82xx/e8253.h>
#include <chipset/82xx/e8255.h>
#include <chipset/82xx/e8259.h>
#include <chipset/82xx/e8272.h>

#include <cpu/e8086/e8086.h>

#include <devices/cassette.h>
#include <devices/fdc.h>
#include <devices/hdc.h>
#include <devices/wd1003.h>
#include <devices/memory.h>
#include <devices/nvram.h>
#include <devices/parport.h>
#include <devices/serport.h>
#include <devices/video/video.h>

#include <drivers/block/block.h>

#include <drivers/video/terminal.h>

#include <lib/brkpt.h>

#include <libini/libini.h>


/*
 * Minimal 8042 keyboard controller state for the AT (5170) model.
 * Full keyboard protocol and original 5170 firmware behavior remain incomplete.
 *
 * State machine:
 *   AT_KBD_IDLE      — waiting for a command on port 0x64
 *   AT_KBD_SELFTEST  — received 0xAA; self-test completion is pending
 *   AT_KBD_CMD_D1    — received 0xD1 (write output port); next 0x60 write sets A20
 *   AT_KBD_CMD_D2    — received 0xD2 (write output buffer); next 0x60 write is data
 *   AT_KBD_CMD_60    — received 0x60 (write config byte); next 0x60 write is param
 *   AT_KBD_CMD_AE    — received 0xAE/0xAD (enable/disable kbd); immediate, no param
 */
#define AT_KBD_IDLE      0
#define AT_KBD_SELFTEST  1
#define AT_KBD_CMD_D1    2
#define AT_KBD_CMD_D2    3
#define AT_KBD_CMD_60    4

typedef struct {
	unsigned long selftest_clock; /* remaining PIT-rate ticks */
	unsigned long reply_clock; /* host command/reply delay, PIT-rate ticks */
	unsigned char state;     /* AT_KBD_* state */
	unsigned char obuf;      /* output buffer byte (read via port 0x60) */
	unsigned char obuf_full; /* 1 = output buffer has data (OBF status bit) */
	unsigned char obuf_irq;  /* 1 = output byte can request keyboard IRQ1 */
	unsigned char sys;       /* SYS flag: latched 1 after successful self-test */
	unsigned char cfg;       /* 8042 configuration byte (cmd 0x60/0x61) */
} at_kbd_t;

typedef struct ibmpc_t {
	e8086_t            *cpu;
	video_t            *video;
	disks_t            *dsk;
	disk_t             *dsk0;

	memory_t           *mem;
	mem_blk_t          *ram;

	memory_t           *prt;

	nvram_t            *nvr;

	e8237_t            dma;
	e8237_t            dma2;       /* AT: second DMA controller (channels 4-7) */
	e8253_t            pit;
	e8255_t            ppi;
	e8259_t            pic;
	e8259_t            pic2;       /* AT: second PIC (slave, IRQ8-15) */
	pc_kbd_t           kbd;
	cassette_t         *cas;
	pc_speaker_t       spk;
	pc_covox_t         *cov;

	mc146818a_t        *at_rtc;    /* AT: CMOS/RTC chip (mc146818a) */
	unsigned char      at_rtc_idx; /* AT: currently addressed CMOS register */
	at_keyboard_t      at_keyboard; /* AT keyboard command device */
	at_kbd_t           at_kbd;     /* AT: minimal 8042 keyboard controller state */

	unsigned           model;

	unsigned char      ppi_port_a[2];
	unsigned char      ppi_port_b;
	unsigned char      ppi_port_c[2];
	unsigned char      switches1_val;
	unsigned char      switches1_msk;
	unsigned char      m24_config[2];

	dev_fdc_t          *fdc;
	hdc_t              *hdc;
	wd1003_t           *wd1003;
	ems_t              *ems;
	xms_t              *xms;

	terminal_t         *trm;

	serport_t          *serport[4];
	parport_t          *parport[4];

	ini_sct_t          *cfg;

	bp_set_t           bps;

	unsigned           bootdrive;
	unsigned           disk_id;

	unsigned long      dma_page[4];
	unsigned char      dma_page_reg[16]; /* raw byte scratch for ports 0x80-0x8F */

	unsigned char      timer1_out;
	unsigned char      dack0;

	char               patch_bios_init;
	char               patch_bios_int19;

	char               memtest;

	/* allow int 1a to get real time */
	int                support_rtc;

	char               blink;
	char               cga40;
	unsigned char      fdd40;
	char               force_keyboard_enable;

	unsigned           fd_cnt;
	unsigned           hd_cnt;

	unsigned long      sync_clock2_sim;
	unsigned long      sync_clock2_real;
	unsigned long      sync_interval;

	/* cpu speed factor */
	unsigned           speed_current;
	unsigned           speed_saved;
	unsigned long      speed_clock_extra;

	unsigned           current_int;

	unsigned           mouse_button;

	unsigned long      clk_div[4];

	unsigned long      clock1;
	unsigned long      clock2;

	unsigned           brk;
	unsigned           brk_result;    /* BX value from PCE_HOOK_ABORT (0=pass, !=0=fail) */

	char               pause;
	char               trace;

	unsigned           atari_pc_turbo;
	unsigned char      atari_pc_port34;
	unsigned char      atari_pc_switches;
	unsigned char      atari_pc_rtc_port;
	mc146818a_t        *atari_pc_rtc;
} ibmpc_t;


extern ibmpc_t *par_pc;


ibmpc_t *pc_new (ini_sct_t *ini);

void pc_del (ibmpc_t *pc);

int pc_set_serport_driver (ibmpc_t *pc, unsigned port, const char *driver);
int pc_set_serport_file (ibmpc_t *pc, unsigned port, const char *fname);

int pc_set_parport_driver (ibmpc_t *pc, unsigned port, const char *driver);
int pc_set_parport_file (ibmpc_t *pc, unsigned port, const char *fname);

const char *pc_intlog_get (ibmpc_t *pc, unsigned n);
void pc_intlog_set (ibmpc_t *pc, unsigned n, const char *expr);
int pc_intlog_check (ibmpc_t *pc, unsigned n);

unsigned pc_get_pcex_seg (ibmpc_t *pc);

/*!***************************************************************************
 * @short Reset the PC
 *****************************************************************************/
void pc_reset (ibmpc_t *pc);

unsigned long pc_get_ext_mem_size (ibmpc_t *pc);

unsigned long pc_get_clock2 (ibmpc_t *pc);

/*!***************************************************************************
 * @short Reset the clock counters
 *****************************************************************************/
void pc_clock_reset (ibmpc_t *pc);

/*!***************************************************************************
 * @short Synchronize the clocks after a discontinuity
 *****************************************************************************/
void pc_clock_discontinuity (ibmpc_t *pc);

/*!***************************************************************************
 * @short Clock the pc
 *****************************************************************************/
void pc_clock (ibmpc_t *pc, unsigned long cnt);

/*!***************************************************************************
 * @short Set the specific CPU model to be emulated
 *****************************************************************************/
int pc_set_cpu_model (ibmpc_t *pc, const char *str);

/*!***************************************************************************
 * @short Set the emulated cpu clock frequency as a multiple of 4.77 MHz
 *****************************************************************************/
void pc_set_speed (ibmpc_t *pc, unsigned factor);

void pc_set_bootdrive (ibmpc_t *pc, unsigned drv);
unsigned pc_get_bootdrive (ibmpc_t *pc);



#endif
