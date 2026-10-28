/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/ibmpc/ibmpc.c                                       *
 * Created:     1999-04-16 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 1999-2025 Hampa Hug <hampa@hampa.ch>                     *
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


#include "main.h"
#include "hook.h"
#include "ibmpc.h"
#include "atari-pc.h"
#include "m24.h"
#include "msg.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include <lib/brkpt.h>
#include <lib/inidsk.h>
#include <lib/iniram.h>
#include <lib/initerm.h>
#include <lib/load.h>
#include <lib/log.h>
#include <lib/string.h>
#include <lib/sysdep.h>

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
#include <devices/memory.h>
#include <devices/nvram.h>
#include <devices/parport.h>
#include <devices/serport.h>
#include <devices/video/mda.h>
#include <devices/video/hgc.h>
#include <devices/video/cga.h>
#include <devices/video/ega.h>
#include <devices/video/vga.h>
#include <devices/video/olivetti.h>
#include <devices/video/plantronics.h>
#include <devices/video/wy700.h>

#include <drivers/block/block.h>

#include <drivers/pti/pti-io.h>

#include <drivers/video/terminal.h>

#include <libini/libini.h>


#define PCE_IBMPC_SLEEP 10000


static char *par_intlog[256];

static
void pc_at_kbd_update_irq (ibmpc_t *pc)
{
	e8259_set_irq1 (&pc->pic, pc->at_kbd.obuf_full &&
		pc->at_kbd.obuf_irq && (pc->at_kbd.cfg & 1));
}

/* Called from pc_setup_ppi and pc_reset for the AT model. */
static
void pc_at_kbd_init (ibmpc_t *pc)
{
	pc->at_kbd.selftest_clock = 0;
	pc->at_kbd.reply_clock = 0;
	pc->at_kbd.state     = AT_KBD_IDLE;
	pc->at_kbd.obuf      = 0;
	pc->at_kbd.obuf_full = 0;
	pc->at_kbd.obuf_irq  = 0;
	/* memtest=0 requests a simulated warm start. AMI checks 8042 SYS
	 * before trusting the BIOS RAM marker; both must describe that start.
	 * Cold starts and resets after the guest clears the marker still need
	 * the normal controller self-test. No reply is fabricated here. */
	pc->at_kbd.sys = !pc->memtest &&
		(mem_get_uint16_le (pc->mem, 0x0472) == 0x1234);
	pc->at_kbd.cfg       = 0x05; /* kbd clock/data enabled, IRQ1 enabled */
	pc_at_kbd_update_irq (pc);
}

/*
 * Place a byte into the 8042 output buffer and mark OBF.
 * Controller replies retain their existing polling-only behavior.
 */
static
void pc_at_kbd_set_obuf (ibmpc_t *pc, unsigned char val)
{
	pc->at_kbd.obuf      = val;
	pc->at_kbd.obuf_irq  = 0;
	pc->at_kbd.obuf_full = 1;   /* BIOS polls OBF before reading port 0x60 */
	pc_at_kbd_update_irq (pc);
}

static
void pc_at_kbd_set_data (ibmpc_t *pc, unsigned char val)
{
	pc_at_kbd_set_obuf (pc, val);
	pc->at_kbd.obuf_irq = 1;
	pc_at_kbd_update_irq (pc);
}

/* The AT controller consumes transport bytes; only it drives the PIC. */
static
void pc_at_kbd_receive (ibmpc_t *pc, unsigned char val)
{
	if (!val || !pc->kbd.key_valid || pc->at_kbd.obuf_full) {
		return;
	}
	at_keyboard_sent (&pc->at_keyboard, pc_kbd_get_key (&pc->kbd));
	pc_at_kbd_set_data (pc, pc_kbd_get_key (&pc->kbd));
	pc->kbd.key_valid = 0;
}

static
void pc_at_kbd_clock (ibmpc_t *pc, unsigned long cnt)
{
	unsigned char val;

	if (pc->at_kbd.state == AT_KBD_SELFTEST) {
		if (cnt < pc->at_kbd.selftest_clock) {
			pc->at_kbd.selftest_clock -= cnt;
			return;
		}
		pc->at_kbd.selftest_clock = 0;
		if (pc->at_kbd.obuf_full) {
			return;
		}
		pc->at_kbd.state = AT_KBD_IDLE;
		pc->at_kbd.sys = 1;
		pc_at_kbd_set_obuf (pc, 0x55);
		pc_kbd_set_enable (&pc->kbd, 1);
		return;
	}

	if (cnt < pc->at_kbd.reply_clock) {
		pc->at_kbd.reply_clock -= cnt;
		return;
	}
	pc->at_kbd.reply_clock = 0;

	if (pc->at_kbd.obuf_full || (pc->at_kbd.cfg & 0x10) ||
		(pc->at_kbd.state != AT_KBD_IDLE))
	{
		return;
	}
	if (at_keyboard_read (&pc->at_keyboard, &val) == 0) {
		pc_at_kbd_set_data (pc, val);
		return;
	}
	if (!pc->at_keyboard.scanning) {
		return;
	}
	if (pc->kbd.key_valid) {
		pc_at_kbd_receive (pc, 1);
	}
	else {
		pc_kbd_clock (&pc->kbd, cnt);
	}
}

static
unsigned char pc_get_port8 (ibmpc_t *pc, unsigned long addr)
{
	unsigned char val;

	if (m24_get_port8 (pc, addr, &val) == 0) {
		return (val);
	}

	if (atari_pc_get_port8 (pc, addr, &val) == 0) {
		return (val);
	}

	val = 0xff;

	switch (addr) {
	case 0x0060:
		/* 8042 data port — AT only (5170).
		 * On XT/5160 this address is covered by the PPI block. */
		if (PC_HAS_8042 (pc)) {
			if (pc->at_kbd.obuf_full) {
				val = pc->at_kbd.obuf;
				/* OBF backpressure holds the next keyboard byte.
				 * Approximate its transfer after release by 1 ms. */
				if (pc->at_kbd.obuf_irq && pc->at_keyboard.count &&
					(pc->at_kbd.reply_clock < PCE_IBMPC_CLK2 / 1000))
				{
					pc->at_kbd.reply_clock = PCE_IBMPC_CLK2 / 1000;
				}
				pc->at_kbd.obuf_full = 0;
				pc->at_kbd.obuf_irq = 0;
				pc_at_kbd_update_irq (pc);
			}
		}
		break;

	case 0x0061:
		/* AT system control port B (speaker / timer gate) — AT only.
		 * Mirrors the function of PPI port B on the XT. */
		if (pc->model & PCE_IBMPC_5170) {
			val = pc->ppi_port_b;
		}
		break;

	case 0x0064:
		/* 8042 status port — present on AT (5170).
		 * Bit 1 (IBF): input buffer full  — always 0 (we accept instantly).
		 * Bit 0 (OBF): output buffer full — 1 when data is waiting on 0x60.
		 * Bit 2 (SYS): system flag        — set after successful self-test.
		 * Bit 4 (INH): keyboard inhibit   — 0 = enabled.
		 */
		if (PC_HAS_8042 (pc)) {
			/* Status reads do not consume or manufacture keyboard data. */
			val = 0x10; /* bit 4 (INH) always set = keyboard not locked */
			if (pc->at_kbd.obuf_full) {
				val |= 0x01; /* OBF */
			}
			if (pc->at_kbd.sys) {
				val |= 0x04; /* SYS — latched after self-test passed */
			}
		}
		break;

	case 0x0070:
		/* AT CMOS: index register read-back (write-only on real HW,
		 * but some software reads it; return the index with NMI bit) */
		if (pc->model & PCE_IBMPC_5170) {
			val = pc->at_rtc_idx;
		}
		break;

	case 0x0071:
		/* AT CMOS: data read */
		if ((pc->model & PCE_IBMPC_5170) && (pc->at_rtc != NULL)) {
			val = mc146818a_get_uint8 (pc->at_rtc, pc->at_rtc_idx & 0x3f);
		}
		break;

	case 0x0092:
		/* Fast A20 gate (PS/2 / AT-compatible port).
		 * Only present when CPU is 80286 (FLAGS286+INT6 combination). */
		if ((pc->cpu->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) ==
		    (E86_CPU_FLAGS286 | E86_CPU_INT6))
		{
			val = (e86_get_addr_mask (pc->cpu) & 0x100000UL) ? 0x02 : 0x00;
		}
		break;

	case 0x0080: case 0x0081: case 0x0082: case 0x0083:
	case 0x0084: case 0x0085: case 0x0086: case 0x0087:
	case 0x0088: case 0x0089: case 0x008a: case 0x008b:
	case 0x008c: case 0x008d: case 0x008e: case 0x008f:
		/* DMA page registers — return raw byte written */
		val = pc->dma_page_reg[addr & 0x0f];
		break;
	}

#ifdef DEBUG_PORTS
	pc_log_deb ("get port 8 %04lX <- %02X\n", addr, val);
#endif

	return (val);
}

static
unsigned short pc_get_port16 (ibmpc_t *pc, unsigned long addr)
{
#ifdef DEBUG_PORTS
	pc_log_deb ("get port 16 %04lX\n", addr);
#endif

	/* AT DMA page latches are 8-bit devices on adjacent I/O addresses. */
	if ((pc->model & PCE_IBMPC_5170) && (addr >= 0x80) && (addr < 0x8f)) {
		unsigned short val;

		val = pc_get_port8 (pc, addr);
		val |= (unsigned short) pc_get_port8 (pc, addr + 1) << 8;
		return (val);
	}

	return (0xffff);
}

static
void pc_set_port8 (ibmpc_t *pc, unsigned long addr, unsigned char val)
{
#ifdef DEBUG_PORTS
	pc_log_deb ("set port 8 %04lX <- %02X\n", addr, val);
#endif

	if (m24_set_port8 (pc, addr, val) == 0) {
		return;
	}

	if (atari_pc_set_port8 (pc, addr, val) == 0) {
		return;
	}

	switch (addr) {
	case 0x0060:
		/* 8042 data port write — AT only (5170).
		 * On 5160 this is covered by the PPI block. */
		if (PC_HAS_8042 (pc)) {
			switch (pc->at_kbd.state) {
			case AT_KBD_CMD_D1:
				/* Write output port: bit 1 = A20, bit 0 = reset (active low).
				 * We honour A20 only; reset pulse is handled by cmd 0xFE. */
				e86_set_a20 (pc->cpu, val & 2);
				pc->at_kbd.state = AT_KBD_IDLE;
				break;

			case AT_KBD_CMD_D2:
				/* Write output buffer: put byte in OBF, raise IRQ1 */
				pc_at_kbd_set_data (pc, val);
				pc->at_kbd.state = AT_KBD_IDLE;
				break;

			case AT_KBD_CMD_60:
				/* Write config byte */
				pc->at_kbd.cfg = val;
				/* AT inhibit release must not invoke the XT reset protocol. */
				pc->kbd.clk = (val & 0x10) ? 0 : 1;
				pc_at_kbd_update_irq (pc);
				pc->at_kbd.state = AT_KBD_IDLE;
				break;

			default:
				{
					int result;

					result = at_keyboard_write (&pc->at_keyboard, val);
					if (result < 0) {
						pce_log (MSG_ERR, "*** AT keyboard reply queue full\n");
					}
					else {
						/* IBM 8042 host writes clear command-byte inhibit. */
						pc->at_kbd.cfg &= ~0x10;
						pc->kbd.clk = 1;
						/* Approximate command/response transport by 2 ms.
						 * Status reads are passive; serial pins and IBF
						 * execution timing are not yet modeled. */
						pc->at_kbd.reply_clock = PCE_IBMPC_CLK2 / 500;
						if (result & AT_KEYBOARD_CLEAR_SCAN) {
							pc->kbd.key_i = pc->kbd.key_j;
							pc->kbd.key_valid = 0;
						}
					}
				}
				pc->at_kbd.state = AT_KBD_IDLE;
				break;
			}
		}
		break;

	case 0x0061:
		/* AT system control port B (speaker / timer gate) — AT only (5170).
		 * Same semantics as PPI port B: bit 1 = speaker gate,
		 * bit 0 = PIT channel 2 gate. */
		if (pc->model & PCE_IBMPC_5170) {
			unsigned char old = pc->ppi_port_b;
			/* Upper bits are hardware status, not writable latches. */
			pc->ppi_port_b = (old & 0xf0) | (val & 0x0f);

			e8253_set_gate (&pc->pit, 2, val & 0x01);

			if ((old ^ val) & 0x02) {
				pc_speaker_set_msk (&pc->spk, val & 0x02);
			}
		}
		break;

	case 0x0064:
		/* 8042 command port write. */
		if (PC_HAS_8042 (pc)) {
			switch (val) {
			case 0x20:
				/* Read config byte → put in OBF */
				pc_at_kbd_set_obuf (pc, pc->at_kbd.cfg);
				break;

			case 0x60:
				/* Write config byte — next byte on port 0x60 is param */
				pc->at_kbd.state = AT_KBD_CMD_60;
				break;

			case 0xA8:
				/* Enable mouse (aux device) — acknowledge, no effect */
				break;

			case 0xC0:
				/* Read input port — respond with 0xFF (all inputs high) */
				pc_at_kbd_set_obuf (pc, 0xff);
				break;

			case 0xD0:
				/* Read output port — respond with current output port state.
				 * Bit 1 = A20, bit 0 = reset (always 1). */
				pc_at_kbd_set_obuf (pc,
					0xcd |
					((e86_get_addr_mask (pc->cpu) & 0x100000UL) ? 0x02 : 0x00));
				break;

			case 0xAA:
				/* IBM 1503033 firmware entry 0700 writes CF to P2,
				 * restoring A20 before testing. Warm POST relies on it. */
				e86_set_a20 (pc->cpu, 1);
				/* Accept now; complete asynchronously in the controller clock.
				 * Approximate execution time: 1 ms, not firmware cycle timing.
				 * Leave old output available for the BIOS to drain. */
				pc->at_kbd.state = AT_KBD_SELFTEST;
				pc->at_kbd.selftest_clock = PCE_IBMPC_CLK2 / 1000;
				break;

			case 0xAB:
				/* Interface test — respond with 0x00 (no error) */
				pc_at_kbd_set_obuf (pc, 0x00);
				break;

			case 0xAD:
				/* Inhibit the interface without discarding keyboard data. */
				pc->at_kbd.cfg |= 0x10;
				pc->kbd.clk = 0;
				break;

			case 0xAE:
				/* Release inhibit; this is not a keyboard reset. */
				pc->at_kbd.cfg &= ~0x10;
				pc->kbd.clk = 1;
				break;

			case 0xD1:
				/* Write output port — next byte on port 0x60 */
				pc->at_kbd.state = AT_KBD_CMD_D1;
				break;

			case 0xD2:
				/* Write output buffer — next byte on port 0x60 */
				pc->at_kbd.state = AT_KBD_CMD_D2;
				break;

			case 0xDD:
				/* Disable A20 gate directly */
				e86_set_a20 (pc->cpu, 0);
				break;

			case 0xDF:
				/* Enable A20 gate directly */
				e86_set_a20 (pc->cpu, 1);
				break;

			case 0xE0:
				/* Read test inputs: bit 0 = T0/clock, bit 1 = T1/data.
				 * The byte transport has no serial pins: data is idle high,
				 * and AD/AE or command-byte inhibit controls logical clock.
				 * This does not model clock/data transitions within a byte. */
				pc_at_kbd_set_obuf (pc, (pc->at_kbd.cfg & 0x10) ? 2 : 3);
				break;

			case 0xFE:
				/* Pulse reset line */
				pc->cpu->state |= E86_STATE_RESET;
				break;

			default:
				/* Unknown command — ignore */
				break;
			}
		}
		break;

	case 0x0070:
		/* AT CMOS: set index register (bit 7 = NMI disable) */
		if (pc->model & PCE_IBMPC_5170) {
			pc->at_rtc_idx = val;
		}
		break;

	case 0x0071:
		/* AT CMOS: write data register */
		if ((pc->model & PCE_IBMPC_5170) && (pc->at_rtc != NULL)) {
			mc146818a_set_uint8 (pc->at_rtc, pc->at_rtc_idx & 0x3f, val);
		}
		break;

	case 0x0092:
		/* Fast A20 gate (PS/2 / AT-compatible port).
		 * Bit 1: A20 enable. Bit 0: system reset pulse. */
		if ((pc->cpu->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) ==
		    (E86_CPU_FLAGS286 | E86_CPU_INT6))
		{
			if (val & 0x01) {
				/* Pulse RESET line */
				pc->cpu->state |= E86_STATE_RESET;
			}
			e86_set_a20 (pc->cpu, val & 2);
		}
		break;

	case 0x0080: case 0x0081: case 0x0082: case 0x0083:
	case 0x0084: case 0x0085: case 0x0086: case 0x0087:
	case 0x0088: case 0x0089: case 0x008a: case 0x008b:
	case 0x008c: case 0x008d: case 0x008e: case 0x008f:
		/* DMA page registers — store raw for read-back */
		pc->dma_page_reg[addr & 0x0f] = val;
		/* DMA1 pages: AT supplies A16..A23; PC/XT supplies A16..A19. */
		switch (addr) {
		case 0x0081: pc->dma_page[2] = (unsigned long)(val & ((pc->model & PCE_IBMPC_5170) ? 0xff : 0x0f)) << 16; break;
		case 0x0082: pc->dma_page[3] = (unsigned long)(val & ((pc->model & PCE_IBMPC_5170) ? 0xff : 0x0f)) << 16; break;
		case 0x0083: pc->dma_page[1] = (unsigned long)(val & ((pc->model & PCE_IBMPC_5170) ? 0xff : 0x0f)) << 16; break;
		case 0x0087: pc->dma_page[0] = (unsigned long)(val & ((pc->model & PCE_IBMPC_5170) ? 0xff : 0x0f)) << 16; break;
		/* AT DMAC2 page registers */
		case 0x0089: if (pc->model & PCE_IBMPC_5170) { pc->dma_page[2] = (unsigned long)(val & 0x0f) << 16; } break;
		case 0x008a: if (pc->model & PCE_IBMPC_5170) { pc->dma_page[3] = (unsigned long)(val & 0x0f) << 16; } break;
		case 0x008b: if (pc->model & PCE_IBMPC_5170) { pc->dma_page[1] = (unsigned long)(val & 0x0f) << 16; } break;
		case 0x008f: if (pc->model & PCE_IBMPC_5170) { pc->dma_page[0] = (unsigned long)(val & 0x0f) << 16; } break;
		}
		break;
	}
}

static
void pc_set_port16 (ibmpc_t *pc, unsigned long addr, unsigned short val)
{
#ifdef DEBUG_PORTS
	pc_log_deb ("set port 16 %04lX <- %04X\n", addr, val);
#endif
	/* Preserve byte-register side effects for AT word I/O cycles. */
	if ((pc->model & PCE_IBMPC_5170) && (addr >= 0x80) && (addr < 0x8f)) {
		pc_set_port8 (pc, addr, val & 0xff);
		pc_set_port8 (pc, addr + 1, val >> 8);
	}

}


static
void pc_dma2_set_mem8 (ibmpc_t *pc, unsigned long addr, unsigned char val)
{
	mem_set_uint8 (pc->mem, pc->dma_page[2] + addr, val);
}

static
unsigned char pc_dma2_get_mem8 (ibmpc_t *pc, unsigned long addr)
{
	return (mem_get_uint8 (pc->mem, pc->dma_page[2] + addr));
}

static
void pc_dma3_set_mem8 (ibmpc_t *pc, unsigned long addr, unsigned char val)
{
	mem_set_uint8 (pc->mem, pc->dma_page[3] + addr, val);
}

static
unsigned char pc_dma3_get_mem8 (ibmpc_t *pc, unsigned long addr)
{
	return (mem_get_uint8 (pc->mem, pc->dma_page[3] + addr));
}


static
unsigned char pc_ppi_get_port_a (ibmpc_t *pc)
{
	unsigned char val;

	if (pc->ppi_port_b & 0x80) {
		val = pc->ppi_port_a[0] & ~pc->switches1_msk;
		val |= pc->switches1_val & pc->switches1_msk;
	}
	else {
		pc->ppi_port_a[1] = pc_kbd_get_key (&pc->kbd);
		val = pc->ppi_port_a[1];
	}

	return (val);
}

static
unsigned char pc_ppi_get_port_c (ibmpc_t *pc)
{
	if (pc->model & PCE_IBMPC_5160) {
		if (pc->ppi_port_b & 0x08) {
			return (pc->ppi_port_c[1]);
		}
		else {
			return (pc->ppi_port_c[0]);
		}
	}
	else {
		if (pc->cas != NULL) {
			if (cas_get_inp (pc->cas)) {
				pc->ppi_port_c[0] &= ~0x10;
				pc->ppi_port_c[1] &= ~0x10;
			}
			else {
				pc->ppi_port_c[0] |= 0x10;
				pc->ppi_port_c[1] |= 0x10;
			}
		}

		if (pc->ppi_port_b & 0x04) {
			return (pc->ppi_port_c[0]);
		}
		else {
			return (pc->ppi_port_c[1]);
		}
	}
}

static
void pc_ppi_set_port_b (ibmpc_t *pc, unsigned char val)
{
	unsigned char old;

	old = pc->ppi_port_b;
	pc->ppi_port_b = val;

	if (pc->force_keyboard_enable) {
		val |= 0x40;
	}

	pc_kbd_set_clk (&pc->kbd, val & 0x40);
	pc_kbd_set_enable (&pc->kbd, (val & 0x80) == 0);

	e8253_set_gate (&pc->pit, 2, val & 0x01);

	if ((old ^ val) & 0x02) {
		pc_speaker_set_msk (&pc->spk, val & 0x02);
	}

	if (pc->model & PCE_IBMPC_5150) {
		if ((old ^ val) & 0x08) {
			/* cassette motor change */

			if (pc->cas != NULL) {
				cas_set_motor (pc->cas, (val & 0x08) == 0);

				if (val & 0x08) {
					/* motor off: restore clock */
					pc->speed_current = pc->speed_saved;
					pc->speed_clock_extra = 0;
				}
				else {
					/* motor on: set clock to 4.77 MHz */
					pc->speed_saved = pc->speed_current;
					pc->speed_current = 1;
					pc->speed_clock_extra = 0;
				}
			}
		}
	}
}

static
void pc_set_dack0 (ibmpc_t *pc, unsigned char val)
{
	pc->dack0 = (val != 0);

	if (pc->dack0) {
		e8237_set_dreq0 (&pc->dma, 0);
	}
}

static
void pc_set_timer1_out (ibmpc_t *pc, unsigned char val)
{
	if (pc->dack0 == 0) {
		if ((pc->timer1_out == 0) && (val != 0)) {
			e8237_set_dreq0 (&pc->dma, 1);
		}
	}

	/* AT refresh status toggles once per request (the rising edge). */
	if ((pc->model & PCE_IBMPC_5170) && !pc->timer1_out && val) {
		pc->ppi_port_b ^= 0x10;
	}

	pc->timer1_out = (val != 0);
}

static
void pc_set_timer2_out (ibmpc_t *pc, unsigned char val)
{
	if (pc->model & PCE_IBMPC_5170) {
		pc->ppi_port_b = (pc->ppi_port_b & ~0x20) | (val ? 0x20 : 0);
	}
	else if (val) {
		pc->ppi_port_c[0] |= 0x20;
		pc->ppi_port_c[1] |= 0x20;
	}
	else {
		pc->ppi_port_c[0] &= ~0x20;
		pc->ppi_port_c[1] &= ~0x20;
	}


	if (pc->model & PCE_IBMPC_5150) {
		if (pc->cas != NULL) {
			cas_set_out (pc->cas, !val);
		}
	}

	pc_speaker_set_out (&pc->spk, val);
}

static
void pc_set_key (ibmpc_t *pc, unsigned event, unsigned key)
{
	if (event == PCE_KEY_EVENT_MAGIC) {
		if (key == PCE_KEY_B) {
			pc->blink = !pc->blink;
			pc_set_msg (pc, "emu.video.blink", pc->blink ? "on" : "off");
		}
		else if (key == PCE_KEY_O) {
			pc_set_msg (pc, "emu.video.composite.cycle", "");
		}
		else if (key == PCE_KEY_9) {
			pc_set_speed (pc, 2 * pc->speed_current);
		}
		else if (key == PCE_KEY_F9) {
			pc_set_msg (pc, "emu.cas.play", "");
		}
		else if (key == PCE_KEY_F10) {
			pc_set_msg (pc, "emu.cas.record", "");
		}
		else if (key == PCE_KEY_F11) {
			pc_set_msg (pc, "emu.cas.load", "0");
		}
		else if (key == PCE_KEY_F12) {
			pc_set_msg (pc, "emu.cas.stop", "");
		}
		else {
			pce_log (MSG_INF, "unhandled magic key (%u)\n",
				(unsigned) key
			);
		}

		return;
	}

	if (!PC_HAS_8042 (pc) || pc->at_keyboard.scanning) {
		pc_kbd_set_key (&pc->kbd, event, key);
	}
}

static
void pc_set_mouse (void *ext, int dx, int dy, unsigned button)
{
	ibmpc_t *pc = ext;

	if ((pc->mouse_button ^ button) & ~button & 4) {
		pc_set_msg (pc, "term.release", "1");
	}

	pc->mouse_button = button;

	chr_mouse_set (dx, dy, button);
}

static
int pc_trap (ibmpc_t *pc, unsigned n)
{
	return (1);
}

static
void pc_set_video_mode (ibmpc_t *pc, unsigned mode)
{
	if (pc->model & PCE_IBMPC_5150) {
		pc->ppi_port_a[0] &= ~0x30;
		pc->ppi_port_a[0] |= (mode & 0x03) << 4;
	}
	else if (pc->model & PCE_IBMPC_5160) {
		pc->ppi_port_c[1] &= ~0x03;
		pc->ppi_port_c[1] |= mode & 0x03;
	}

	m24_set_video_mode (pc, mode);
}

static
void pc_set_floppy_count (ibmpc_t *pc, unsigned cnt)
{
	pc->fd_cnt = cnt;

	if (pc->model & PCE_IBMPC_5150) {
		pc->ppi_port_a[0] &= ~0xc1;

		if (cnt > 0) {
			pc->ppi_port_a[0] |= 0x01;
			pc->ppi_port_a[0] |= ((cnt - 1) & 3) << 6;
		}
	}
	else if (pc->model & PCE_IBMPC_5160) {
		pc->ppi_port_c[1] &= ~0x0c;

		if (cnt > 0) {
			pc->ppi_port_c[1] |= ((cnt - 1) & 3) << 2;
		}
	}
}

static
void pc_set_ram_size (ibmpc_t *pc, unsigned long cnt)
{
	if (pc->model & PCE_IBMPC_5150) {
		pc->ppi_port_a[0] &= 0xf3;
		pc->ppi_port_c[0] &= 0xf0;
		pc->ppi_port_c[1] &= 0xfe;

		if (cnt <= 65536) {
			cnt = (cnt <= 16384) ? 0 : ((cnt - 16384) / 16384);

			pc->ppi_port_a[0] |= (cnt & 3) << 2;
		}
		else {
			cnt = (cnt - 65536) / 32768;

			pc->ppi_port_a[0] |= 0x0c;
			pc->ppi_port_c[0] |= cnt & 0x0f;
			pc->ppi_port_c[1] |= (cnt >> 4) & 0x01;
		}
	}
	else if (pc->model & PCE_IBMPC_5160) {
		cnt = cnt >> 16;

		if (cnt > 0) {
			cnt -= 1;

			if (cnt > 3) {
				cnt = 3;
			}
		}

		pc->ppi_port_c[0] &= 0xf3;
		pc->ppi_port_c[0] |= (cnt << 2) & 0x0c;
	}
}


static
void pc_setup_system (ibmpc_t *pc, ini_sct_t *ini)
{
	unsigned   fdcnt, sw1val, sw1msk, fdd40;
	int        patch_init, patch_int19, memtest, kbden, cga40;
	const char *model;
	ini_sct_t  *sct;

	pc->switches1_val = 0;
	pc->switches1_msk = 0;

	pc->blink = 0;
	pc->cga40 = 0;
	pc->force_keyboard_enable = 0;

	pc->fd_cnt = 0;
	pc->hd_cnt = 0;

	pc->current_int = 0;

	pc->brk = 0;
	pc->brk_result = 0;
	pc->pause = 0;
	pc->trace = 0;

	/* AT-specific fields — safe NULL/0 for all non-AT models */
	pc->at_rtc = NULL;
	pc->at_rtc_idx = 0;

	sct = ini_next_sct (ini, NULL, "system");

	if (sct == NULL) {
		sct = ini;
	}

	ini_get_string (sct, "model", &model, "5150");
	ini_get_uint16 (sct, "boot", &pc->bootdrive, 128);
	ini_get_uint16 (sct, "floppy_disk_drives", &fdcnt, 2);
	ini_get_bool (sct, "rtc", &pc->support_rtc, 1);
	ini_get_uint16 (sct, "switches_1_val", &sw1val, 0);
	ini_get_uint16 (sct, "switches_1_msk", &sw1msk, 0);
	ini_get_bool (sct, "force_keyboard_enable", &kbden, 0);
	ini_get_bool (sct, "cga40", &cga40, 0);
	ini_get_uint16 (sct, "fdd40", &fdd40, 0);
	ini_get_bool (sct, "patch_bios_init", &patch_init, 1);
	ini_get_bool (sct, "patch_bios_int19", &patch_int19, 1);
	ini_get_bool (sct, "memtest", &memtest, 1);

	pce_log_tag (MSG_INF, "SYSTEM:",
		"model=%s floppies=%u cga40=%d fdd40=%02X sw1=%02X/%02X"
		" patch-init=%d patch-int19=%d\n",
		model, fdcnt, cga40, fdd40, sw1val, sw1msk,
		patch_init, patch_int19
	);

	if (strcmp (model, "5150") == 0) {
		pc->model = PCE_IBMPC_5150;
	}
	else if (strcmp (model, "5160") == 0) {
		pc->model = PCE_IBMPC_5160;
	}
	else if (strcmp (model, "m24") == 0) {
		pc->model = PCE_IBMPC_5160 | PCE_IBMPC_M24;
	}
	else if (strcmp (model, "atari-pc") == 0) {
		pc->model = PCE_IBMPC_5150 | PCE_IBMPC_ATARI;
	}
	else if (strcmp (model, "5170") == 0) {
		/* AT inherits the XT flag set (5160) so all XT-path code
		 * continues to work; 5170 adds the AT-specific devices */
		pc->model = PCE_IBMPC_5160 | PCE_IBMPC_5170;
	}
	else {
		pce_log (MSG_ERR, "*** unknown model (%s)\n", model);
		pc->model = PCE_IBMPC_5150;
	}

	pc->patch_bios_init = patch_init != 0;
	pc->patch_bios_int19 = patch_int19 != 0;

	pc->memtest = (memtest != 0);

	pc->ppi_port_a[0] = 0x30 | 0x0c;
	pc->ppi_port_a[1] = 0;
	pc->ppi_port_b = 0x08;
	pc->ppi_port_c[0] = 0;
	pc->ppi_port_c[1] = 0;

	pc->switches1_val = sw1val & sw1msk;
	pc->switches1_msk = sw1msk;

	pc->cga40 = (cga40 != 0);
	pc->fdd40 = fdd40 & 0xff;
	pc->force_keyboard_enable = (kbden != 0);

	if (pc->model & PCE_IBMPC_5160) {
		pc->ppi_port_c[0] |= 0x01;
	}

	pc_set_floppy_count (pc, fdcnt);
}

static
void pc_setup_mem (ibmpc_t *pc, ini_sct_t *ini)
{
	unsigned long ram;

	pc->mem = mem_new();

	ini_get_ram (pc->mem, ini, &pc->ram);
	ini_get_rom (pc->mem, ini);

	if (pc->ram != NULL) {
		ram = mem_blk_get_size (pc->ram);
	}
	else {
		ram = 65536;
	}

	pc_set_ram_size (pc, ram);

	if ((pc->ram != NULL) && (pc->memtest == 0)) {
		mem_set_uint16_le (pc->mem, 0x0472, 0x1234);
	}
}

static
void pc_setup_ports (ibmpc_t *pc, ini_sct_t *ini)
{
	pc->prt = mem_new();

	mem_set_fct (pc->prt, pc,
		pc_get_port8, pc_get_port16, NULL,
		pc_set_port8, pc_set_port16, NULL
	);
}

static
void pc_setup_nvram (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t     *sct;
	const char    *fname;
	unsigned long base, size;

	pc->nvr = NULL;

	sct = ini_next_sct (ini, NULL, "nvram");
	if (sct == NULL) {
		return;
	}

	ini_get_string (sct, "file", &fname, NULL);
	ini_get_uint32 (sct, "base", &base, 0);
	ini_get_uint32 (sct, "size", &size, 65536);

	pce_log_tag (MSG_INF, "NVRAM:", "addr=0x%08lx size=%lu file=%s\n",
		base, size, (fname == NULL) ? "<>" : fname
	);

	pc->nvr = nvr_new (base, size);
	if (pc->nvr == NULL) {
		pce_log (MSG_ERR, "*** creating nvram failed\n");
		return;
	}

	nvr_set_endian (pc->nvr, 0);

	mem_add_blk (pc->mem, nvr_get_mem (pc->nvr), 0);

	if (fname != NULL) {
		if (nvr_set_fname (pc->nvr, fname)) {
			pce_log (MSG_ERR, "*** loading nvram failed (%s)\n", fname);
		}
	}
}

/*
 * Expose the low system ROM at the 286 reset window. Aliases share backing
 * storage so ROM loads and PCE BIOS patches are visible through both mappings.
 * Explicit high mappings take precedence, including on accelerated XT systems.
 */
static
void pc_setup_bios_alias (ibmpc_t *pc)
{
	unsigned i, j, cnt, before;
	unsigned long low, high, size;
	mem_blk_t *rom, *blk, *alias;

	if ((pc->cpu->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) !=
		(E86_CPU_FLAGS286 | E86_CPU_INT6))
	{
		return;
	}

	cnt = pc->mem->cnt;
	for (i = 0; i < cnt; i++) {
		rom = pc->mem->lst[i].blk;
		if (!rom->active || !rom->readonly || (rom->data == NULL) ||
			(rom->addr1 >= 0x100000UL) || (rom->addr2 < 0xe0000UL))
		{
			continue;
		}
		low = (rom->addr1 < 0xe0000UL) ? 0xe0000UL : rom->addr1;
		size = ((rom->addr2 < 0xfffffUL) ? rom->addr2 : 0xfffffUL) - low + 1;
		high = low + 0xf00000UL;

		for (j = 0; j < pc->mem->cnt; j++) {
			blk = pc->mem->lst[j].blk;
			if (blk->active && (blk->addr1 < high + size) && (blk->addr2 >= high)) {
				break;
			}
		}
		if (j < pc->mem->cnt) {
			if (!blk->readonly) {
				pce_log (MSG_ERR, "*** RAM overlaps 286 BIOS reset window at %06lx\n", high);
			}
			continue;
		}

		alias = mem_blk_new (high, size, 0);
		if (alias == NULL) {
			pce_log (MSG_ERR, "*** creating 286 BIOS alias failed\n");
			return;
		}
		mem_blk_set_data (alias, rom->data + low - rom->addr1, 0);
		mem_blk_set_readonly (alias, 1);
		before = pc->mem->cnt;
		mem_add_blk (pc->mem, alias, 1);
		if (pc->mem->cnt == before) {
			mem_blk_del (alias);
			pce_log (MSG_ERR, "*** mapping 286 BIOS alias failed\n");
			return;
		}
		pce_log_tag (MSG_INF, "ROM:", "alias=%06lx source=%06lx size=%lu\n",
			high, low, size);
	}
}

static
void pc_setup_cpu (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t     *sct;
	const char    *model;
	unsigned      speed;

	sct = ini_next_sct (ini, NULL, "cpu");

	ini_get_string (sct, "model", &model, "8088");
	ini_get_uint16 (sct, "speed", &speed, 0);

	pce_log_tag (MSG_INF, "CPU:", "model=%s speed=%uX\n",
		model, speed
	);

	pc->cpu = e86_new();

	if (pc_set_cpu_model (pc, model)) {
		pce_log (MSG_ERR, "*** unknown cpu model (%s)\n", model);
	}

	e86_set_mem (pc->cpu, pc->mem,
		(e86_get_uint8_f) &mem_get_uint8,
		(e86_set_uint8_f) &mem_set_uint8,
		(e86_get_uint16_f) &mem_get_uint16_le,
		(e86_set_uint16_f) &mem_set_uint16_le
	);

	e86_set_prt (pc->cpu, pc->prt,
		(e86_get_uint8_f) &mem_get_uint8,
		(e86_set_uint8_f) &mem_set_uint8,
		(e86_get_uint16_f) &mem_get_uint16_le,
		(e86_set_uint16_f) &mem_set_uint16_le
	);

	if (pc->ram != NULL) {
		e86_set_ram (pc->cpu, pc->ram->data, pc->ram->size);
	}
	else {
		e86_set_ram (pc->cpu, NULL, 0);
	}

	pc->cpu->op_ext = pc;
	pc->cpu->op_hook = pc_hook_old;

	e86_set_hook_fct (pc->cpu, pc, pc_hook);
	e86_set_trap_fct (pc->cpu, pc, pc_trap);

	pc_setup_bios_alias (pc);

	pc->speed_current = speed;
	pc->speed_saved = speed;
	pc->speed_clock_extra = 0;
}

static
void pc_setup_dma (ibmpc_t *pc, ini_sct_t *ini)
{
	unsigned long addr;
	ini_sct_t     *sct;
	mem_blk_t     *blk;

	sct = ini_next_sct (ini, NULL, "dmac");

	ini_get_uint32 (sct, "address", &addr, 0);

	pce_log_tag (MSG_INF, "DMAC:", "addr=0x%08x size=0x%04x\n", addr, 16);

	pc->dma_page[0] = 0;
	pc->dma_page[1] = 0;
	pc->dma_page[2] = 0;
	pc->dma_page[3] = 0;

	pc->dack0 = 0;

	e8237_init (&pc->dma);

	blk = mem_blk_new (addr, 16, 0);
	if (blk == NULL) {
		pce_log (MSG_ERR, "*** allocating DMAC failed\n");
		return;
	}

	mem_blk_set_fct (blk, &pc->dma,
		e8237_get_uint8, e8237_get_uint16, e8237_get_uint32,
		e8237_set_uint8, e8237_set_uint16, e8237_set_uint32
	);

	mem_add_blk (pc->prt, blk, 1);

	/* This is a hack. HLDA should be connected to the CPU core. Instead,
	 * this will keep it permanently at high. */
	e8237_set_hlda (&pc->dma, 1);

	e8237_set_dack_fct (&pc->dma, 0, pc, pc_set_dack0);
}

static
void pc_setup_pic (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t     *sct;
	mem_blk_t     *blk;
	unsigned long addr;

	sct = ini_next_sct (ini, NULL, "pic");

	ini_get_uint32 (sct, "address", &addr, 0x0020);

	pce_log_tag (MSG_INF, "PIC:", "addr=0x%08x size=0x%04x\n", addr, 2);

	e8259_init (&pc->pic);

	blk = mem_blk_new (addr, 2, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &pc->pic,
		e8259_get_uint8, e8259_get_uint16, e8259_get_uint32,
		e8259_set_uint8, e8259_set_uint16, e8259_set_uint32
	);

	mem_add_blk (pc->prt, blk, 1);

	e8259_set_int_fct (&pc->pic, pc->cpu, e86_irq);

	e86_set_inta_fct (pc->cpu, &pc->pic, e8259_inta);
}

static
void pc_setup_pit (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t     *sct;
	mem_blk_t     *blk;
	unsigned long addr;

	sct = ini_next_sct (ini, NULL, "pit");

	ini_get_uint32 (sct, "address", &addr, 0x0040);

	pce_log_tag (MSG_INF, "PIT:", "addr=0x%08lx size=0x%04x\n",
		addr, 4
	);

	pc->timer1_out = 0;

	e8253_init (&pc->pit);

	blk = mem_blk_new (addr, 4, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &pc->pit,
		e8253_get_uint8, e8253_get_uint16, e8253_get_uint32,
		e8253_set_uint8, e8253_set_uint16, e8253_set_uint32
	);

	mem_add_blk (pc->prt, blk, 1);

	e8253_set_gate (&pc->pit, 0, 1);
	e8253_set_gate (&pc->pit, 1, 1);
	e8253_set_gate (&pc->pit, 2, 1);

	e8253_set_out_fct (&pc->pit, 0, &pc->pic, e8259_set_irq0);
	e8253_set_out_fct (&pc->pit, 1, pc, pc_set_timer1_out);
	e8253_set_out_fct (&pc->pit, 2, pc, pc_set_timer2_out);
}

static
void pc_setup_ppi (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t     *sct;
	mem_blk_t     *blk;
	unsigned long addr;

	sct = ini_next_sct (ini, NULL, "ppi");

	ini_get_uint32 (sct, "address", &addr, 0x0060);

	e8255_init (&pc->ppi);

	/*
	 * The AT (5170) has no 8255 PPI at 0x60-0x63.  Ports 0x60 (8042 data)
	 * and 0x61 (speaker/timer control) are handled directly by pc_get/set_port8.
	 * Do not register a PPI block so the fallback handlers fire.
	 */
	if (PC_HAS_8042 (pc)) {
		pce_log_tag (MSG_INF, "PPI:", "skipped (AT model uses 8042)\n");
		pc_at_kbd_init (pc);
		return;
	}

	/* All PC/XT variants retain their PPI when the CPU is upgraded. */
	pce_log_tag (MSG_INF, "PPI:", "addr=0x%08x size=0x%04x\n", addr, 4);

	pc->ppi.port[0].read_ext = pc;
	pc->ppi.port[0].read = (void *) pc_ppi_get_port_a;
	pc->ppi.port[1].write_ext = pc;
	pc->ppi.port[1].write = (void *) pc_ppi_set_port_b;
	pc->ppi.port[2].read_ext = pc;
	pc->ppi.port[2].read = (void *) pc_ppi_get_port_c;

	blk = mem_blk_new (addr, 4, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &pc->ppi,
		e8255_get_uint8, e8255_get_uint16, e8255_get_uint32,
		e8255_set_uint8, e8255_set_uint16, e8255_set_uint32
	);

	mem_add_blk (pc->prt, blk, 1);

	e8255_set_uint8 (&pc->ppi, 3, 0x99);
}

static
void pc_setup_kbd (ibmpc_t *pc, ini_sct_t *ini)
{
	pc_kbd_init (&pc->kbd);
	at_keyboard_init (&pc->at_keyboard);

	if (PC_HAS_8042 (pc)) {
		pc_kbd_set_irq_fct (&pc->kbd, pc, pc_at_kbd_receive);
	}
	else {
		pc_kbd_set_irq_fct (&pc->kbd, &pc->pic, e8259_set_irq1);
	}
}

static
void pc_setup_cassette (ibmpc_t *pc, ini_sct_t *ini)
{
	int           enable;
	const char    *read_name, *write_name;
	unsigned long delay;
	ini_sct_t     *sct;

	pc->cas = NULL;

	if ((pc->model & PCE_IBMPC_5150) == 0) {
		return;
	}

	if ((sct = ini_next_sct (ini, NULL, "cassette")) == NULL) {
		return;
	}

	ini_get_bool (sct, "enable", &enable, 1);

	if (enable == 0) {
		return;
	}

	ini_get_string (sct, "file", &write_name, NULL);
	ini_get_string (sct, "write", &write_name, write_name);
	ini_get_string (sct, "read", &read_name, NULL);
	ini_get_uint32 (sct, "motor_delay", &delay, 0);

	pce_log_tag (MSG_INF, "CASSETTE:", "read=%s write=%s motor_delay=%lu\n",
		(read_name != NULL) ? read_name : "<none>",
		(write_name != NULL) ? write_name : "<none>",
		delay
	);

	if ((pc->cas = cas_new()) == NULL) {
		pce_log (MSG_ERR, "*** alloc failed\n");
		return;
	}

	delay = (unsigned long) (((double) delay * PCE_IBMPC_CLK2) / 1000.0);

	cas_set_clock (pc->cas, PCE_IBMPC_CLK2);
	cas_set_motor_delay (pc->cas, delay);

	cas_set_auto_play (pc->cas, 1);

	pti_set_default_clock (PCE_IBMPC_CLK2);

	if (cas_set_read_name (pc->cas, read_name)) {
		pce_log (MSG_ERR, "*** opening read file failed (%s)\n",
			read_name
		);
	}

	if (cas_set_write_name (pc->cas, write_name, 1)) {
		pce_log (MSG_ERR, "*** opening write file failed (%s)\n",
			write_name
		);
	}
}

static
void pc_setup_speaker (ibmpc_t *pc, ini_sct_t *ini)
{
	const char    *driver;
	unsigned      volume;
	unsigned long srate;
	unsigned long lowpass;
	ini_sct_t     *sct;

	pc_speaker_init (&pc->spk);
	pc_speaker_set_clk_fct (&pc->spk, pc, pc_get_clock2);

	sct = ini_next_sct (ini, NULL, "speaker");

	if (sct == NULL) {
		return;
	}

	ini_get_string (sct, "driver", &driver, NULL);
	ini_get_uint16 (sct, "volume", &volume, 500);
	ini_get_uint32 (sct, "sample_rate", &srate, 44100);
	ini_get_uint32 (sct, "lowpass", &lowpass, 0);

	pce_log_tag (MSG_INF, "SPEAKER:", "volume=%u srate=%lu lowpass=%lu driver=%s\n",
		volume, srate, lowpass,
		(driver != NULL) ? driver : "<none>"
	);

	if (driver != NULL) {
		if (pc_speaker_set_driver (&pc->spk, driver, srate)) {
			pce_log (MSG_ERR,
				"*** setting sound driver failed (%s)\n",
				driver
			);
		}
	}

	pc_speaker_set_lowpass (&pc->spk, lowpass);

	pc_speaker_set_volume (&pc->spk, volume);
}

static
void pc_setup_covox (ibmpc_t *pc, ini_sct_t *ini)
{
	const char    *driver;
	const char    *mode;
	unsigned      volume;
	unsigned long srate, lowpass;
	unsigned      port;
	ini_sct_t     *sct;

	pc->cov = NULL;

	sct = ini_next_sct (ini, NULL, "covox");

	if (sct == NULL) {
		return;
	}

	ini_get_string (sct, "driver", &driver, NULL);
	ini_get_string (sct, "mode", &mode, "covox");
	ini_get_uint16 (sct, "parport", &port, 0);
	ini_get_uint16 (sct, "volume", &volume, 500);
	ini_get_uint32 (sct, "sample_rate", &srate, 44100);
	ini_get_uint32 (sct, "lowpass", &lowpass, 0);

	if (strcmp (mode, "none") == 0) {
		return;
	}

	pce_log_tag (MSG_INF,
		"COVOX:",
		"parport=%u mode=%s volume=%u srate=%lu lowpass=%lu driver=%s\n",
		port, mode, volume, srate, lowpass,
		(driver != NULL) ? driver : "<none>"
	);

	if ((port > 3) || (pc->parport[port] == NULL)) {
		pce_log (MSG_ERR, "*** no parallel port (%u)\n", port);
		return;
	}

	pc->cov = pc_covox_new();

	if (pc->cov == NULL) {
		pce_log (MSG_ERR, "*** creating covox failed\n");
		return;
	}

	pc_covox_set_clk_fct (pc->cov, pc, pc_get_clock2);

	if (driver != NULL) {
		if (pc_covox_set_driver (pc->cov, driver, srate)) {
			pce_log (MSG_ERR,
				"*** setting sound driver failed (%s)\n",
				driver
			);
		}
	}

	if (strcmp (mode, "covox") == 0) {
		pc_covox_set_mode (pc->cov, 0);
	}
	else if (strcmp (mode, "disney") == 0) {
		pc_covox_set_mode (pc->cov, 1);
	}
	else {
		pce_log (MSG_ERR, "*** unknown mode (%s)\n", mode);
	}

	pc_covox_set_lowpass (pc->cov, lowpass);

	pc_covox_set_volume (pc->cov, volume);

	parport_set_data_fct (pc->parport[port], pc->cov, pc_covox_set_data);
	parport_set_ctrl_fct (pc->parport[port], pc->cov, pc_covox_set_ctrl);
	parport_set_status_fct (pc->parport[port], pc->cov, pc_covox_get_status);
}

static
void pc_setup_terminal (ibmpc_t *pc, ini_sct_t *ini)
{
	pc->trm = ini_get_terminal (ini, par_terminal);

	if (pc->trm == NULL) {
		return;
	}

	trm_set_key_fct (pc->trm, pc, pc_set_key);
	trm_set_mouse_fct (pc->trm, pc, pc_set_mouse);
	trm_set_msg_fct (pc->trm, pc, pc_set_msg);
}

static
int pc_setup_mda (ibmpc_t *pc, ini_sct_t *sct)
{
	pc->video = mda_new_ini (sct);
	if (pc->video == NULL) {
		return (1);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, 3);

	return (0);
}

static
int pc_setup_olivetti (ibmpc_t *pc, ini_sct_t *sct)
{
	pc->video = m24_new_ini (sct);

	if (pc->video == NULL) {
		return (1);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, pc->cga40 ? 1 : 2);

	return (0);
}

static
int pc_setup_plantronics (ibmpc_t *pc, ini_sct_t *sct)
{
	pc->video = pla_new_ini (sct);
	if (pc->video == NULL) {
		return (1);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, pc->cga40 ? 1 : 2);

	return (0);
}

static
int pc_setup_wy700 (ibmpc_t *pc, ini_sct_t *sct)
{
	pc->video = wy700_new_ini (sct);
	if (pc->video == NULL) {
		return (1);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, pc->cga40 ? 1 : 2);

	return (0);
}

static
int pc_setup_hgc (ibmpc_t *pc, ini_sct_t *sct)
{
	pc->video = hgc_new_ini (sct);
	if (pc->video == NULL) {
		return (1);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, 3);

	return (0);
}

static
int pc_setup_cga (ibmpc_t *pc, ini_sct_t *sct)
{
	pc->video = cga_new_ini (sct);
	if (pc->video == NULL) {
		return (1);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, pc->cga40 ? 1 : 2);

	return (0);
}

static
int pc_setup_ega (ibmpc_t *pc, ini_sct_t *sct)
{
	int      enable_irq;
	unsigned irq;

	pc->video = ega_new_ini (sct);

	if (pc->video == NULL) {
		return (1);
	}

	ini_get_bool (sct, "enable_irq", &enable_irq, 0);
	ini_get_uint16 (sct, "irq", &irq, 2);

	pce_log_tag (MSG_INF, "VIDEO:", "EGA irq=%u (%s)\n",
		irq, enable_irq ? "enabled" : "disabled"
	);

	if (enable_irq) {
		ega_set_irq_fct (pc->video->ext,
			&pc->pic, e8259_get_irq_fct (&pc->pic, irq)
		);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, 0);

	return (0);
}

static
int pc_setup_vga (ibmpc_t *pc, ini_sct_t *sct)
{
	int      enable_irq;
	unsigned irq;

	pc->video = vga_new_ini (sct);

	if (pc->video == NULL) {
		return (1);
	}

	ini_get_bool (sct, "enable_irq", &enable_irq, 0);
	ini_get_uint16 (sct, "irq", &irq, 2);

	pce_log_tag (MSG_INF, "VIDEO:", "VGA irq=%u (%s)\n",
		irq, enable_irq ? "enabled" : "disabled"
	);

	if (enable_irq) {
		vga_set_irq_fct (pc->video->ext,
			&pc->pic, e8259_get_irq_fct (&pc->pic, irq)
		);
	}

	mem_add_blk (pc->mem, pce_video_get_mem (pc->video), 0);
	mem_add_blk (pc->prt, pce_video_get_reg (pc->video), 0);

	pc_set_video_mode (pc, 0);

	return (0);
}

static
void pc_setup_video (ibmpc_t *pc, ini_sct_t *ini)
{
	const char *dev;
	ini_sct_t  *sct;

	pc->video = NULL;

	sct = ini_next_sct (ini, NULL, "video");
	ini_get_string (sct, "device", &dev, "cga");

	if (par_video != NULL) {
		while ((sct != NULL) && (strcmp (par_video, dev) != 0)) {
			sct = ini_next_sct (ini, sct, "video");
			ini_get_string (sct, "device", &dev, "cga");
		}

		if (sct == NULL) {
			dev = par_video;
		}
	}

	pce_log_tag (MSG_INF, "VIDEO:", "device=%s\n", dev);

	if (strcmp (dev, "vga") == 0) {
		pc_setup_vga (pc, sct);
	}
	else if (strcmp (dev, "ega") == 0) {
		pc_setup_ega (pc, sct);
	}
	else if (strcmp (dev, "cga") == 0) {
		pc_setup_cga (pc, sct);
	}
	else if (strcmp (dev, "hgc") == 0) {
		pc_setup_hgc (pc, sct);
	}
	else if (strcmp (dev, "mda") == 0) {
		pc_setup_mda (pc, sct);
	}
	else if (strcmp (dev, "olivetti") == 0) {
		pc_setup_olivetti (pc, sct);
	}
	else if (strcmp (dev, "plantronics") == 0) {
		pc_setup_plantronics (pc, sct);
	}
	else if ((strcmp (dev, "wyse") == 0) || (strcmp (dev, "wy700") == 0)) {
		pc_setup_wy700 (pc, sct);
	}
	else {
		pce_log (MSG_ERR, "*** unknown video device (%s)\n", dev);
	}

	if (pc->video == NULL) {
		pce_log (MSG_ERR, "*** setting up video device failed\n");
	}

	if (pc->video != NULL) {
		ini_get_ram (pc->mem, sct, &pc->ram);
		ini_get_rom (pc->mem, sct);
		pce_load_mem_ini (pc->mem, sct);

		if (pc->trm != NULL) {
			pce_video_set_terminal (pc->video, pc->trm);
		}
	}
}

static
void pc_setup_disks (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t *sct;
	disk_t    *dsk;

	pc->dsk0 = NULL;

	pc->dsk = dsks_new();

	sct = NULL;
	while ((sct = ini_next_sct (ini, sct, "disk")) != NULL) {
		if (ini_get_disk (sct, &dsk)) {
			pce_log (MSG_ERR, "*** loading drive failed\n");
			continue;
		}

		if (dsk == NULL) {
			continue;
		}

		dsks_add_disk (pc->dsk, dsk);

		if (dsk_get_drive (dsk) >= 0x80) {
			pc->hd_cnt += 1;
		}
	}
}

static
void pc_setup_fdc (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t     *sct;
	int           accurate, ignore_eot;
	unsigned long addr;
	unsigned      verbose;
	unsigned      irq, ss;
	unsigned      drv[4];
	unsigned      i, rpm;
	char          rpm_key[16];

	pc->fdc = NULL;

	sct = ini_next_sct (ini, NULL, "fdc");

	if (sct == NULL) {
		return;
	}

	ini_get_uint32 (sct, "address", &addr, 0x3f0);
	ini_get_uint16 (sct, "irq", &irq, 6);
	ini_get_uint16 (sct, "verbose", &verbose, 0);
	ini_get_bool (sct, "accurate", &accurate, 0);
	ini_get_bool (sct, "ignore_eot", &ignore_eot, 0);
	ini_get_uint16 (sct, "drive0", &drv[0], 0xffff);
	ini_get_uint16 (sct, "drive1", &drv[1], 0xffff);
	ini_get_uint16 (sct, "drive2", &drv[2], 0xffff);
	ini_get_uint16 (sct, "drive3", &drv[3], 0xffff);
	ini_get_uint16 (sct, "single_sided", &ss, 0);

	pce_log_tag (MSG_INF, "FDC:",
		"addr=0x%08lx irq=%u accurate=%d eot=%d verbose=%u drv=[%u %u %u %u] ss=%02X\n",
		addr, irq, accurate, !ignore_eot, verbose,
		drv[0], drv[1], drv[2], drv[3], ss
	);

	pc->fdc = dev_fdc_new (addr);

	if (pc->fdc == NULL) {
		pce_log (MSG_ERR, "*** creating fdc failed\n");
		return;
	}

	if (pc->model & PCE_IBMPC_5170) {
		dev_fdc_enable_extended (pc->fdc);
	}
	dev_fdc_mem_add_io (pc->fdc, pc->prt);
	dev_fdc_set_disks (pc->fdc, pc->dsk);

	dev_fdc_set_drive (pc->fdc, 0, drv[0]);
	dev_fdc_set_drive (pc->fdc, 1, drv[1]);
	dev_fdc_set_drive (pc->fdc, 2, drv[2]);
	dev_fdc_set_drive (pc->fdc, 3, drv[3]);
	for (i = 0; i < 4; i++) {
		sprintf (rpm_key, "drive%u_rpm", i);
		ini_get_uint16 (sct, rpm_key, &rpm, 0);
		if (dev_fdc_set_rpm (pc->fdc, i, rpm)) {
			pce_log (MSG_ERR, "FDC: invalid %s=%u (expected 0, 300 or 360)\n", rpm_key, rpm);
		}
	}

	e8272_set_input_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2);
	e8272_set_verbose (&pc->fdc->e8272, verbose);
	e8272_set_accuracy (&pc->fdc->e8272, accurate != 0);
	e8272_set_ignore_eot (&pc->fdc->e8272, ignore_eot != 0);
	e8272_set_drive_mask (&pc->fdc->e8272, (1 << pc->fd_cnt) - 1);
	e8272_set_single_sided (&pc->fdc->e8272, ss);
	e8272_set_irq_fct (&pc->fdc->e8272, &pc->pic, e8259_get_irq_fct (&pc->pic, irq));
	e8272_set_dreq_fct (&pc->fdc->e8272, &pc->dma, e8237_set_dreq2);

	e8237_set_tc_fct (&pc->dma, 2, &pc->fdc->e8272, e8272_set_tc);

	pc->dma.chn[2].iord_ext = &pc->fdc->e8272;
	pc->dma.chn[2].iord = (void *) e8272_read_data;

	pc->dma.chn[2].iowr_ext = &pc->fdc->e8272;
	pc->dma.chn[2].iowr = (void *) e8272_write_data;

	pc->dma.chn[2].memrd_ext = pc;
	pc->dma.chn[2].memrd = (void *) pc_dma2_get_mem8;

	pc->dma.chn[2].memwr_ext = pc;
	pc->dma.chn[2].memwr = (void *) pc_dma2_set_mem8;
}

/* The fixed-disk half of the AT combination adapter is present even when
 * no hard drives are fitted. Keep the XT HDC and generic ATA independent. */
static
void pc_setup_wd1003 (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t *sct;
	unsigned i, disk;
	static const char *key[] = { "drive0", "drive1" };
	pc->wd1003 = NULL;
	if (!(pc->model & PCE_IBMPC_5170) ||
		(ini_next_sct (ini, NULL, "wd1003") == NULL)) {
		return;
	}
	pc->wd1003 = wd1003_new (0x1f0, 0x3f6, PCE_IBMPC_CLK2);
	if (pc->wd1003 == NULL) {
		pce_log (MSG_ERR, "*** creating wd1003 failed\n");
		return;
	}
	/* The floppy block otherwise claims all eight ports, including 3F6.
	 * Leave 3F4/3F5 and the separate DIR/CCR at 3F7 with the FDC. */
	if ((pc->fdc != NULL) && (pc->fdc->blk.addr1 == 0x3f0)) {
		mem_blk_set_size (&pc->fdc->blk, 6);
	}
	mem_add_blk (pc->prt, &pc->wd1003->task, 0);
	mem_add_blk (pc->prt, &pc->wd1003->control, 0);
	wd1003_set_irq_fct (pc->wd1003, &pc->pic2,
		(void (*)(void *, unsigned char)) e8259_get_irq_fct (&pc->pic2, 6));
	sct = ini_next_sct (ini, NULL, "wd1003");
	for (i = 0; i < 2; i++) {
		ini_get_uint16 (sct, key[i], &disk, 0xffff);
		if (disk == 0xffff) continue;
		if (!dsks_get_disk (pc->dsk, disk) ||
			wd1003_set_disk (pc->wd1003, i, dsks_get_disk (pc->dsk, disk))) {
			pce_log (MSG_ERR, "*** WD1003 %s: missing disk or invalid CHS geometry (%u)\n", key[i], disk);
		}
	}
	pce_log_tag (MSG_INF, "WD1003:", "addr=01F0 control=03F6 irq=14 drives=%u/%u\n",
		pc->wd1003->disk[0] != NULL, pc->wd1003->disk[1] != NULL);
}

static
void pc_setup_hdc (ibmpc_t *pc, ini_sct_t *ini)
{
	unsigned      irq;
	unsigned long addr;
	unsigned      drv[2];
	unsigned      cfg;
	unsigned      sectors;
	const char    *cfg_id;
	ini_sct_t     *sct;

	pc->hdc = NULL;

	sct = ini_next_sct (ini, NULL, "hdc");

	if (sct == NULL) {
		return;
	}

	ini_get_uint32 (sct, "address", &addr, 0x320);
	ini_get_uint16 (sct, "irq", &irq, 5);
	ini_get_uint16 (sct, "drive0", &drv[0], 0xffff);
	ini_get_uint16 (sct, "drive1", &drv[1], 0xffff);
	ini_get_uint16 (sct, "sectors", &sectors, 17);
	ini_get_string (sct, "config_id", &cfg_id, "");
	ini_get_uint16 (sct, "switches", &cfg, 0);

	pce_log_tag (MSG_INF, "HDC:",
		"addr=0x%08lx irq=%u drv=[%u %u] sectors=%u switches=0x%02x id=\"%s\"\n",
		addr, irq, drv[0], drv[1], sectors, cfg, cfg_id
	);

	pc->hdc = hdc_new (addr);

	if (pc->hdc == NULL) {
		pce_log (MSG_ERR, "*** creating hdc failed\n");
		return;
	}

	hdc_mem_add_io (pc->hdc, pc->prt);
	hdc_set_disks (pc->hdc, pc->dsk);

	hdc_set_drive (pc->hdc, 0, drv[0]);
	hdc_set_drive (pc->hdc, 1, drv[1]);

	hdc_set_config_id (pc->hdc, (const unsigned char *) cfg_id, strlen (cfg_id));
	hdc_set_config (pc->hdc, cfg);

	hdc_set_sectors (pc->hdc, sectors);

	hdc_set_irq_fct (pc->hdc, &pc->pic, e8259_get_irq_fct (&pc->pic, irq));
	hdc_set_dreq_fct (pc->hdc, &pc->dma, e8237_set_dreq3);

	pc->dma.chn[3].iord_ext = pc->hdc;
	pc->dma.chn[3].iord = (void *) hdc_read_data;

	pc->dma.chn[3].iowr_ext = pc->hdc;
	pc->dma.chn[3].iowr = (void *) hdc_write_data;

	pc->dma.chn[3].memrd_ext = pc;
	pc->dma.chn[3].memrd = (void *) pc_dma3_get_mem8;

	pc->dma.chn[3].memwr_ext = pc;
	pc->dma.chn[3].memwr = (void *) pc_dma3_set_mem8;
}

static
void pc_setup_parport (ibmpc_t *pc, ini_sct_t *ini)
{
	unsigned        i;
	unsigned long   addr;
	const char      *driver;
	ini_sct_t       *sct;
	static unsigned defbase[4] = { 0x378, 0x278, 0x3bc, 0x2bc };

	for (i = 0; i < 4; i++) {
		pc->parport[i] = NULL;
	}

	i = 0;
	sct = NULL;
	while ((sct = ini_next_sct (ini, sct, "parport")) != NULL) {
		if (i >= 4) {
			break;
		}

		ini_get_uint32 (sct, "address", &addr, defbase[i]);
		ini_get_string (sct, "driver", &driver, NULL);

		pce_log_tag (MSG_INF,
			"PARPORT:", "LPT%u addr=0x%04lx driver=%s\n",
			i + 1, addr, (driver == NULL) ? "<none>" : driver
		);

		pc->parport[i] = parport_new (addr);

		if (pc->parport[i] == NULL) {
			pce_log (MSG_ERR,
				"*** parport %u setup failed\n", i + 1
			);
		}
		else {
			if (driver != NULL) {
				if (parport_set_driver (pc->parport[i], driver)) {
					pce_log (MSG_ERR,
						"*** can't open driver (%s)\n",
						driver
					);
				}

			}

			mem_add_blk (pc->prt, parport_get_reg (pc->parport[i]), 0);

			i += 1;
		}
	}
}

static
void pc_setup_serport (ibmpc_t *pc, ini_sct_t *ini)
{
	unsigned      i;
	unsigned long addr;
	unsigned      irq;
	unsigned      multi, multi_read, multi_write;
	const char    *driver;
	const char    *log;
	const char    *chip;
	ini_sct_t     *sct;

	static unsigned long defbase[4] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8 };
	static unsigned      defirq[4] = { 4, 3, 4, 3 };

	for (i = 0; i < 4; i++) {
		pc->serport[i] = NULL;
	}

	i = 0;
	sct = NULL;
	while ((sct = ini_next_sct (ini, sct, "serial")) != NULL) {
		if (i >= 4) {
			break;
		}

		if (ini_get_uint32 (sct, "address", &addr, defbase[i])) {
			ini_get_uint32 (sct, "io", &addr, defbase[i]);
		}

		ini_get_uint16 (sct, "irq", &irq, defirq[i]);
		ini_get_string (sct, "uart", &chip, "8250");
		ini_get_uint16 (sct, "multichar", &multi, 1);
		ini_get_uint16 (sct, "multichar_read", &multi_read, multi);
		ini_get_uint16 (sct, "multichar_write", &multi_write, multi);
		ini_get_string (sct, "driver", &driver, NULL);
		ini_get_string (sct, "log", &log, NULL);

		pce_log_tag (MSG_INF,
			"SERPORT:", "COM%u addr=0x%04lx irq=%u multichar=r=%u/w=%u uart=%s\n",
			i + 1, addr, irq, multi_read, multi_write, chip
		);

		if (driver != NULL) {
			pce_log_tag (MSG_INF,
				"SERPORT:", "COM%u driver=%s\n",
				i + 1, driver
			);
		}

		pc->serport[i] = ser_new (addr, 0);

		if (pc->serport[i] == NULL) {
			pce_log (MSG_ERR, "*** serial port setup failed [%04X/%u -> %s]\n",
				addr, irq, (driver == NULL) ? "<none>" : driver
			);
		}
		else {
			if (driver != NULL) {
				if (ser_set_driver (pc->serport[i], driver)) {
					pce_log (MSG_ERR,
						"*** can't open driver (%s)\n",
						driver
					);
				}

			}

			if (log != NULL) {
				if (ser_set_log (pc->serport[i], log)) {
					pce_log (MSG_ERR,
						"*** can't open log file (%s)\n",
						log
					);
				}
			}

			if (e8250_set_chip_str (&pc->serport[i]->uart, chip)) {
				pce_log (MSG_ERR, "*** unknown UART chip (%s)\n", chip);
			}

			e8250_set_multichar (&pc->serport[i]->uart, multi_read, multi_write);

			e8250_set_irq_fct (&pc->serport[i]->uart,
				&pc->pic, e8259_get_irq_fct (&pc->pic, irq)
			);

			mem_add_blk (pc->prt, ser_get_reg (pc->serport[i]), 0);

			i += 1;
		}
	}
}

/* DOS helper windows must not shadow installed RAM, ROM or devices. */
static
int pc_check_helper_mem (ibmpc_t *pc, mem_blk_t *mem, const char *name)
{
	unsigned i;
	mem_blk_t *blk;

	for (i = 0; i < pc->mem->cnt; i++) {
		blk = pc->mem->lst[i].blk;
		if (blk->active && (blk->addr1 <= mem->addr2) &&
			(blk->addr2 >= mem->addr1))
		{
			pce_log (MSG_ERR,
				"*** %s: window %06lx-%06lx overlaps %06lx-%06lx; disabled\n",
				name, mem->addr1, mem->addr2, blk->addr1, blk->addr2);
			return (1);
		}
	}
	return (0);
}

static
void pc_setup_ems (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t *sct;
	mem_blk_t *mem;

	pc->ems = NULL;

	sct = ini_next_sct (ini, NULL, "ems");
	if (sct == NULL) {
		return;
	}

	pc->ems = ems_new (sct);
	if (pc->ems == NULL) {
		return;
	}

	mem = ems_get_mem (pc->ems);
	if (mem != NULL) {
		if (pc_check_helper_mem (pc, mem, "EMS")) {
			ems_del (pc->ems);
			pc->ems = NULL;
		}
		else {
			mem_add_blk (pc->mem, mem, 0);
		}
	}
}

static
void pc_setup_xms (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t *sct;
	mem_blk_t *mem, *pool, *blk;
	unsigned i;
	int hma, is_286, overlap;

	pc->xms = NULL;
	sct = ini_next_sct (ini, NULL, "xms");
	if (sct == NULL) {
		return;
	}
	pc->xms = xms_new (sct);
	if (pc->xms == NULL) {
		return;
	}

	is_286 = (pc->cpu->cpu & (E86_CPU_FLAGS286 | E86_CPU_INT6)) ==
		(E86_CPU_FLAGS286 | E86_CPU_INT6);
	if (is_286) {
		pool = NULL;
		for (i = 0; i < pc->mem->cnt; i++) {
			blk = pc->mem->lst[i].blk;
			if (blk->active && !blk->readonly && (blk->data != NULL) &&
				(blk->addr1 >= 0x100000UL) && (blk->addr2 < 0xfe0000UL))
			{
				if ((pool == NULL) || (blk->addr1 < pool->addr1)) {
					pool = blk;
				}
			}
		}
		ini_get_bool (sct, "hma", &hma, 0);
		if ((pool != NULL) && xms_set_phys_pool (pc->xms, pool, hma)) {
			pce_log (MSG_ERR, "*** xms: physical pool cannot satisfy HMA layout\n");
		}
	}

	mem = xms_get_umb_mem (pc->xms);
	if (mem != NULL) {
		if (pc_check_helper_mem (pc, mem, "UMB")) {
			xms_disable_umb (pc->xms);
		}
		else {
			mem_add_blk (pc->mem, mem, 0);
		}
	}

	/* Shared HMA views are already mapped through their RAM owner. */
	mem = xms_get_hma_mem (pc->xms);
	if ((mem != NULL) && mem->data_del) {
		overlap = 0;
		for (i = 0; i < pc->mem->cnt; i++) {
			blk = pc->mem->lst[i].blk;
			if (blk->active && (blk->addr1 <= mem->addr2) && (blk->addr2 >= mem->addr1)) {
				overlap = 1;
				break;
			}
		}
		if (overlap) {
			pce_log (MSG_ERR, "*** xms: standalone HMA overlaps configured memory; disabled\n");
			mem_blk_del (mem);
			pc->xms->hma = NULL;
		}
		else {
			mem_add_blk (pc->mem, mem, 0);
		}
	}
}

/* Contiguous, accessible extended RAM, independent of a DOS helper's pool. */
unsigned long pc_get_ext_mem_size (ibmpc_t *pc)
{
	unsigned long addr, end, next;
	mem_blk_t *blk;

	addr = 0x100000UL;
	end = (pc->model & PCE_IBMPC_5170) ? 0xfe0000UL : 0x1000000UL;
	while (addr < end) {
		blk = mem_get_blk (pc->mem, addr);
		if ((blk == NULL) || blk->readonly || (blk->data == NULL)) {
			break;
		}
		next = (blk->addr2 >= end - 1) ? end : blk->addr2 + 1;
		if (next <= addr) {
			break;
		}
		addr = next;
	}
	return (addr - 0x100000UL);
}

/* -----------------------------------------------------------------------
 * AT-specific setup functions (5170 only)
 * ----------------------------------------------------------------------- */

/*
 * Second PIC (slave, base vector 0x70, wired to master IRQ2).
 * AT I/O: 0xA0-0xA1
 */
/* Acknowledge both controllers when the AT master selects cascade IRQ2. */
static
unsigned char pc_at_inta (ibmpc_t *pc)
{
	unsigned char vec = e8259_inta (&pc->pic);

	if (vec == pc->pic.base + 2) {
		return e8259_inta (&pc->pic2);
	}

	return vec;
}

static
void pc_setup_pic2 (ibmpc_t *pc)
{
	mem_blk_t *blk;

	if ((pc->model & PCE_IBMPC_5170) == 0) {
		return;
	}

	pce_log_tag (MSG_INF, "PIC2:", "addr=0x%08x size=0x%04x (AT slave)\n",
		0xa0, 2);

	e8259_init (&pc->pic2);

	blk = mem_blk_new (0xa0, 2, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &pc->pic2,
		e8259_get_uint8, e8259_get_uint16, e8259_get_uint32,
		e8259_set_uint8, e8259_set_uint16, e8259_set_uint32
	);

	mem_add_blk (pc->prt, blk, 1);

	/* Slave output wired to master IRQ2 */
	e8259_set_int_fct (&pc->pic2, &pc->pic, e8259_set_irq2);
	e86_set_inta_fct (pc->cpu, pc, pc_at_inta);
}

/*
 * Second DMA (16-bit channels 4-7, cascade channel 4 connects to first DMA).
 * AT I/O: 0xC0-0xDE (word-wide, step 2).
 *
 * AT DMAC2 port wrappers:
 *
 * On the AT the second 8237 (channels 4-7) is mapped at 0xC0-0xDF with
 * word-spaced registers: port 0xC0=offset 0, 0xC2=offset 2, 0xC4=offset 4 …
 * The 8237 logic uses offsets 0-15 (one per byte). We halve the incoming
 * address so the doubled AT port spacing maps back to the 8237 register set.
 */
static unsigned char
pc_dma2_get_u8 (e8237_t *dma, unsigned long addr)
{
	return (e8237_get_uint8 (dma, addr >> 1));
}

static void
pc_dma2_set_u8 (e8237_t *dma, unsigned long addr, unsigned char val)
{
	e8237_set_uint8 (dma, addr >> 1, val);
}

static unsigned short
pc_dma2_get_u16 (e8237_t *dma, unsigned long addr)
{
	return (e8237_get_uint16 (dma, addr >> 1));
}

static void
pc_dma2_set_u16 (e8237_t *dma, unsigned long addr, unsigned short val)
{
	e8237_set_uint16 (dma, addr >> 1, val);
}

static unsigned long
pc_dma2_get_u32 (e8237_t *dma, unsigned long addr)
{
	return (e8237_get_uint32 (dma, addr >> 1));
}

static void
pc_dma2_set_u32 (e8237_t *dma, unsigned long addr, unsigned long val)
{
	e8237_set_uint32 (dma, addr >> 1, val);
}

void pc_setup_dma2 (ibmpc_t *pc)
{
	mem_blk_t *blk;

	if ((pc->model & PCE_IBMPC_5170) == 0) {
		return;
	}

	pce_log_tag (MSG_INF, "DMAC2:", "addr=0x%08x size=0x%04x (AT ch4-7)\n",
		0xc0, 32);

	e8237_init (&pc->dma2);

	blk = mem_blk_new (0xc0, 32, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &pc->dma2,
		pc_dma2_get_u8, pc_dma2_get_u16, pc_dma2_get_u32,
		pc_dma2_set_u8, pc_dma2_set_u16, pc_dma2_set_u32
	);

	mem_add_blk (pc->prt, blk, 1);

	e8237_set_hlda (&pc->dma2, 1);
}

/*
 * AT CMOS / RTC (mc146818a).
 * I/O via port 0x70 (index) / 0x71 (data) handled in pc_get/set_port8.
 * Optional: config key "cmos_file" names the persistent NVRAM image.
 */

/*
 * Derive CMOS equipment bytes from the live emulator configuration and write
 * them into the MC146818A data array.  Called after mc146818a_init() and
 * before mc146818a_load() so that:
 *   - a clean first boot always gets correct values,
 *   - a saved .nvr file from a previous run takes full precedence (loaded
 *     afterwards and overwrites these defaults).
 *
 * Gaps 5 + 9 from T6-A analysis.
 */
static
void pc_cmos_populate_from_config (ibmpc_t *pc)
{
	unsigned char *d = pc->at_rtc->data;
	unsigned long  base_kb, ext_kb;
	unsigned       i;
	unsigned short cksum;

	/* --- Reg B: 24-hour BCD mode (IBM AT default) --- */
	d[0x0b] = 0x02;   /* 24h=1, DM=0 (BCD), all IRQs disabled */

	/* --- Base memory (conventional RAM, max 640 KB) --- */
	base_kb = (pc->ram != NULL) ? (mem_blk_get_size (pc->ram) / 1024UL) : 640UL;
	if (base_kb > 640UL) { base_kb = 640UL; }
	d[0x15] = (unsigned char)(base_kb & 0xff);
	d[0x16] = (unsigned char)((base_kb >> 8) & 0xff);

	/* CMOS describes accessible physical RAM, not host-only XMS storage. */
	ext_kb = pc_get_ext_mem_size (pc) / 1024UL;
	d[0x17] = (unsigned char)(ext_kb & 0xff);
	d[0x18] = (unsigned char)((ext_kb >> 8) & 0xff);

	/* CMOS describes installed drives, not the currently inserted media.
	 * Preserve the legacy fdd40 inference when no explicit type is supplied.
	 * Types: 1=360K, 2=1.2M, 3=720K, 4=1.44M, 5=2.88M. */
	{
		ini_sct_t *sct;
		unsigned type[2], fallback, n;
		static const char *key[2] = { "floppy0_type", "floppy1_type" };

		sct = ini_next_sct (pc->cfg, NULL, "cmos");
		for (n = 0; n < 2; n++) {
			type[n] = 0;
			if (n >= pc->fd_cnt) {
				continue;
			}
			fallback = (pc->fdd40 & (1 << n)) ? 1 : 4;
			ini_get_uint16 (sct, key[n], &type[n], fallback);
			if ((type[n] < 1) || (type[n] > 5)) {
				pce_log (MSG_ERR, "*** invalid CMOS %s=%u, using %u\n",
					key[n], type[n], fallback);
				type[n] = fallback;
			}
		}
		d[0x10] = (unsigned char) ((type[0] << 4) | type[1]);
	}

	/* Original AT types 1-14. Explicit geometry/type selection keeps the
	 * existing assisted-disk configurations diskless in CMOS. */
	d[0x12] = 0;
	if (pc->wd1003 != NULL) {
		ini_sct_t *sct = ini_next_sct (pc->cfg, NULL, "cmos");
		static const char *key[] = { "hard_disk0_type", "hard_disk1_type" };
		unsigned type, n;
		for (n = 0; n < 2; n++) {
			ini_get_uint16 (sct, key[n], &type, 0);
			if (type > 14 || (type && !pc->wd1003->disk[n])) {
				pce_log (MSG_ERR, "*** invalid CMOS %s=%u or absent WD1003 drive\n", key[n], type);
				type = 0;
			}
			d[0x12] |= type << (n ? 0 : 4);
		}
	}

	/* --- Equipment byte (0x14) ---
	 * Bits 7:6 = number of floppies - 1 (0 = 1 floppy, 1 = 2 floppies)
	 * Bits 5:4 = display (0=EGA/VGA, 1=CGA40, 2=CGA80, 3=mono)
	 * Bit  1   = math coprocessor (0 — not emulated)
	 * Bit  0   = floppy drive(s) present
	 *
	 * Determine display type from config (same logic as pc_setup_video).
	 * Default to CGA80 (0x02); option-ROM EGA/VGA uses zero.          */
	{
		unsigned char disp = 0x02;   /* CGA80 */
		const char   *dev  = NULL;
		ini_sct_t    *vsct = ini_next_sct (pc->cfg, NULL, "video");
		ini_get_string (vsct, "device", &dev, "cga");
		if (par_video != NULL) {
			dev = par_video;
		}
		if (dev != NULL) {
			if ((strcmp (dev, "vga") == 0) || (strcmp (dev, "ega") == 0)) {
				disp = 0;
			}
			else if ((strcmp (dev, "mda") == 0) ||
			    (strcmp (dev, "hgc") == 0)) {
				disp = 0x03;  /* mono */
			}
			else if (strcmp (dev, "cga") == 0 && pc->cga40) {
				disp = 0x01;  /* CGA 40-column */
			}
		}

		d[0x14] = (unsigned char)(
			((pc->fd_cnt > 1 ? pc->fd_cnt - 1 : 0) << 6) |
			(disp << 4) |
			(pc->fd_cnt > 0 ? 0x01 : 0x00));
	}

	/* --- POST diagnostic byte (0x0E): 0x00 — no errors, valid CMOS --- */
	d[0x0e] = 0x00;

	/* --- Checksum over bytes 0x10–0x2D (big-endian 16-bit at 0x2E/0x2F) --- */
	cksum = 0;
	for (i = 0x10; i <= 0x2d; i++) {
		cksum += d[i];
	}
	d[0x2e] = (unsigned char)((cksum >> 8) & 0xff);
	d[0x2f] = (unsigned char)(cksum & 0xff);

}

void pc_setup_at_rtc (ibmpc_t *pc, ini_sct_t *ini)
{
	ini_sct_t  *sct;
	const char *fname;
	const char *start;
	int         sync_rtc;

	if ((pc->model & PCE_IBMPC_5170) == 0) {
		return;
	}

	sct = ini_next_sct (ini, NULL, "cmos");

	ini_get_string (sct, "file",      &fname,    NULL);
	ini_get_string (sct, "start",     &start,    NULL);
	ini_get_bool   (sct, "sync_rtc_on_start", &sync_rtc, 1);

	pce_log_tag (MSG_INF, "CMOS:",
		"AT RTC addr=0x70/0x71 file=%s start=%s sync=%d\n",
		(fname != NULL) ? fname : "<none>",
		(start != NULL) ? start : "now",
		sync_rtc
	);

	pc->at_rtc = mc146818a_new();
	if (pc->at_rtc == NULL) {
		pce_log (MSG_ERR, "*** creating AT RTC failed\n");
		return;
	}

	mc146818a_init (pc->at_rtc);
	mc146818a_set_input_clock (pc->at_rtc, PCE_IBMPC_CLK2);
	mc146818a_set_century_offset (pc->at_rtc, 0x32);

	/* Step 1: populate equipment bytes from config (Gap 5 + 9) */
	pc_cmos_populate_from_config (pc);

	/* Step 2: load .nvr file — takes full precedence over auto-populated defaults */
	if (fname != NULL) {
		mc146818a_set_fname (pc->at_rtc, fname);
		mc146818a_load (pc->at_rtc);
	}

	/* Step 3: stamp time (unless sync_rtc_on_start=0 and .nvr was loaded) */
	if (sync_rtc || fname == NULL) {
		mc146818a_set_time_string (pc->at_rtc, start);
	}

	/* Log the final CMOS state (after load + time stamp) */
	{
		unsigned char *d = pc->at_rtc->data;
		unsigned short cksum = ((unsigned short)d[0x2e] << 8) | d[0x2f];
		unsigned long  base_kb = (unsigned long)d[0x15] | ((unsigned long)d[0x16] << 8);
		unsigned long  ext_kb  = (unsigned long)d[0x17] | ((unsigned long)d[0x18] << 8);
		pce_log_tag (MSG_INF, "CMOS:",
			"final: base=%luK ext=%luK fd=%u t0/t1=%02X equip=%02X cksum=%04X%s\n",
			base_kb, ext_kb, pc->fd_cnt, d[0x10], d[0x14], cksum,
			(fname != NULL) ? " (nvr loaded)" : " (auto)"
		);
	}

	/* RTC alarm/update IRQ → slave PIC IRQ0 (= system IRQ8) */
	mc146818a_set_irq_fct (pc->at_rtc, &pc->pic2, e8259_set_irq0);
}

ibmpc_t *pc_new (ini_sct_t *ini)
{
	ibmpc_t *pc;

	pc = malloc (sizeof (ibmpc_t));
	if (pc == NULL) {
		return (NULL);
	}

	pc->cfg = ini;

	pc->disk_id = 0;

	pc->mouse_button = 0;

	bps_init (&pc->bps);

	pc_setup_system (pc, ini);
	pc_setup_m24 (pc, ini);
	pc_setup_atari_pc (pc, ini);

	pc_setup_mem (pc, ini);
	pc_setup_ports (pc, ini);

	pc_setup_nvram (pc, ini);
	pc_setup_cpu (pc, ini);
	pc_setup_dma (pc, ini);
	pc_setup_pic (pc, ini);
	pc_setup_pic2 (pc);          /* AT: slave PIC — no-op for 5150/5160 */
	pc_setup_dma2 (pc);          /* AT: second DMA — no-op for 5150/5160 */
	pc_setup_pit (pc, ini);
	pc_setup_ppi (pc, ini);
	pc_setup_kbd (pc, ini);
	pc_setup_cassette (pc, ini);
	pc_setup_speaker (pc, ini);

	pc_setup_terminal (pc, ini);

	pc_setup_video (pc, ini);

	if (pc->trm != NULL) {
		trm_open (pc->trm, 640, 480);
		trm_set_msg_trm (pc->trm, "term.title", "pce-ibmpc");
	}

	pc_setup_disks (pc, ini);
	pc_setup_fdc (pc, ini);
	pc_setup_hdc (pc, ini);
	pc_setup_wd1003 (pc, ini);
	pc_setup_serport (pc, ini);
	pc_setup_parport (pc, ini);
	pc_setup_ems (pc, ini);
	pc_setup_xms (pc, ini);
	pc_setup_at_rtc (pc, ini);   /* AT: CMOS/RTC — no-op for 5150/5160 */
	pc_setup_covox (pc, ini);

	pce_load_mem_ini (pc->mem, ini);

	mem_move_to_front (pc->mem, 0xfe000);

	pc_clock_reset (pc);

	return (pc);
}

void pc_del_xms (ibmpc_t *pc)
{
	xms_del (pc->xms);
	pc->xms = NULL;
}

void pc_del_ems (ibmpc_t *pc)
{
	ems_del (pc->ems);
	pc->ems = NULL;
}

void pc_del_parport (ibmpc_t *pc)
{
	unsigned i;

	for (i = 0; i < 4; i++) {
		if (pc->parport[i] != NULL) {
			parport_del (pc->parport[i]);
		}
	}
}

void pc_del_serport (ibmpc_t *pc)
{
	unsigned i;

	for (i = 0; i < 4; i++) {
		if (pc->serport[i] != NULL) {
			ser_del (pc->serport[i]);
		}
	}
}

void pc_del (ibmpc_t *pc)
{
	if (pc == NULL) {
		return;
	}

	bps_free (&pc->bps);

	atari_pc_del (pc);

	pc_del_xms (pc);
	pc_del_ems (pc);
	pc_del_parport (pc);
	pc_del_serport (pc);
	hdc_del (pc->hdc);
	wd1003_del (pc->wd1003);
	dev_fdc_del (pc->fdc);
	dsks_del (pc->dsk);

	pce_video_del (pc->video);

	trm_del (pc->trm);

	pc_covox_del (pc->cov);
	pc_speaker_free (&pc->spk);
	cas_del (pc->cas);
	/* AT: save CMOS and free RTC */
	if (pc->at_rtc != NULL) {
		mc146818a_save (pc->at_rtc);
		mc146818a_del (pc->at_rtc);
		pc->at_rtc = NULL;
	}

	e8237_free (&pc->dma);
	e8237_free (&pc->dma2);
	e8255_free (&pc->ppi);
	e8253_free (&pc->pit);
	e8259_free (&pc->pic);
	e8259_free (&pc->pic2);
	e86_del (pc->cpu);

	nvr_del (pc->nvr);
	mem_del (pc->mem);
	mem_del (pc->prt);

	ini_sct_del (pc->cfg);

	free (pc);
}

int pc_set_serport_driver (ibmpc_t *pc, unsigned port, const char *driver)
{
	if ((port >= 4) || (pc->serport[port] == NULL)) {
		return (1);
	}

	if (ser_set_driver (pc->serport[port], driver)) {
		return (1);
	}

	return (0);
}

int pc_set_serport_file (ibmpc_t *pc, unsigned port, const char *fname)
{
	int  r;
	char *driver;

	driver = str_cat_alloc ("stdio:file=", fname);

	r = pc_set_serport_driver (pc, port, driver);

	free (driver);

	return (r);
}

int pc_set_parport_driver (ibmpc_t *pc, unsigned port, const char *driver)
{
	if ((port >= 4) || (pc->parport[port] == NULL)) {
		return (1);
	}

	if (parport_set_driver (pc->parport[port], driver)) {
		return (1);
	}

	return (0);
}

int pc_set_parport_file (ibmpc_t *pc, unsigned port, const char *fname)
{
	int  r;
	char *driver;

	driver = str_cat_alloc ("stdio:file=", fname);

	r = pc_set_parport_driver (pc, port, driver);

	free (driver);

	return (r);
}

const char *pc_intlog_get (ibmpc_t *pc, unsigned n)
{
	return (par_intlog[n & 0xff]);
}

void pc_intlog_set (ibmpc_t *pc, unsigned n, const char *expr)
{
	char **str;

	str = &par_intlog[n & 0xff];

	free (*str);

	if ((expr == NULL) || (*expr == 0)) {
		*str = NULL;
		return;
	}

	*str = str_copy_alloc (expr);
}

int pc_intlog_check (ibmpc_t *pc, unsigned n)
{
	unsigned long val;
	const char    *str;
	cmd_t         cmd;

	str = par_intlog[n & 0xff];

	if (str == NULL) {
		return (0);
	}

	cmd_set_str (&cmd, str);

	if (cmd_match_uint32 (&cmd, &val)) {
		if (val) {
			return (1);
		}
	}

	return (0);
}

/*
 * Get the segment address of the PCE ROM extension
 */
unsigned pc_get_pcex_seg (ibmpc_t *pc)
{
	unsigned i;
	unsigned seg;
	unsigned v1, v2;

	for (i = 0; i < 32; i++) {
		seg = ((i < 16) ? 0xf000 : 0xc000) + (i & 15) * 0x0100;

		v1 = e86_get_mem16 (pc->cpu, seg, 0x0004);
		v2 = e86_get_mem16 (pc->cpu, seg, 0x0006);

		if ((v1 == 0x4350) && (v2 == 0x5845)) {
			return (seg);
		}
	}

	return (0);
}

void pc_patch_bios (ibmpc_t *pc)
{
	unsigned seg;
	unsigned i, sum, off;
	unsigned char jump[6];
	static const unsigned char xt86_entry[6] = { 0xfa, 0xb4, 0xd5, 0x9e, 0x73, 0x4a };
	static const char xt86_date[] = "05/09/86";

	if (pc->patch_bios_init == 0) {
		return;
	}

	if (e86_get_mem8 (pc->cpu, 0xf000, 0xfff0) != 0xea) {
		return;
	}

	if (e86_get_mem16 (pc->cpu, 0xf000, 0xfff3) != 0xf000) {
		return;
	}

	seg = pc_get_pcex_seg (pc);

	if (seg == 0) {
		return;
	}

	pc_log_deb ("patching the bios (0x%04x)\n", seg);

	/* The IBM 1986 XT keyboard handler jumps straight to E05B on warm
	 * boot. Patching only FFFF0 mixes PCEX cold initialization with native
	 * warm POST, and breaks the ROM checksum. For this identified BIOS,
	 * redirect the common entry instead. The sixth byte is unreachable
	 * after the far jump and preserves the sum of the replaced six bytes.
	 * Keep other BIOSes and native AT reset/resume on their existing paths. */
	off = e86_get_mem16 (pc->cpu, 0xf000, 0xfff1);
	if ((pc->model == PCE_IBMPC_5160) && (off == 0xe05b)) {
		for (i = 0; i < 8; i++) {
			if (e86_get_mem8 (pc->cpu, 0xf000, 0xfff5 + i) != xt86_date[i]) break;
		}
		if (i == 8) {
			for (i = 0; i < 6; i++) {
				if (e86_get_mem8 (pc->cpu, 0xf000, off + i) != xt86_entry[i]) break;
			}
			if ((i == 6) || ((e86_get_mem8 (pc->cpu, 0xf000, off) == 0xea) &&
				(e86_get_mem16 (pc->cpu, 0xf000, off + 1) == 0x000c) &&
				(e86_get_mem16 (pc->cpu, 0xf000, off + 3) == seg))) {
				sum = 0;
				for (i = 0; i < 6; i++) sum += e86_get_mem8 (pc->cpu, 0xf000, off + i);
				jump[0] = 0xea; jump[1] = 0x0c; jump[2] = 0;
				jump[3] = seg & 255; jump[4] = seg >> 8;
				for (i = 0; i < 5; i++) sum -= jump[i];
				jump[5] = sum & 255;
				for (i = 0; i < 6; i++) mem_set_uint8_rw (pc->mem, 0xf0000 + off + i, jump[i]);
				return;
			}
		}
	}

	mem_set_uint8_rw (pc->mem, 0xffff1, 0x0c);
	mem_set_uint8_rw (pc->mem, 0xffff2, 0x00);
	mem_set_uint8_rw (pc->mem, 0xffff3, seg & 0xff);
	mem_set_uint8_rw (pc->mem, 0xffff4, (seg >> 8) & 0xff);
}

void pc_reset (ibmpc_t *pc)
{
	unsigned i;

	pc_log_deb ("reset pc\n");

	pc_patch_bios (pc);

	/* A full machine reset restores the existing power-on gate state. */
	e86_set_a20 (pc->cpu, 1);
	e86_reset (pc->cpu);

	e8237_reset (&pc->dma);
	e8253_reset (&pc->pit);
	e8259_reset (&pc->pic);

	/* AT: reset slave PIC and second DMA on warm reset (Gap 8) */
	if (pc->model & PCE_IBMPC_5170) {
		e8259_reset (&pc->pic2);
		e8237_reset (&pc->dma2);
	}

	/* Reset the keyboard controller only on an AT motherboard. */
	if (PC_HAS_8042 (pc)) {
		pc_at_kbd_init (pc);
		at_keyboard_init (&pc->at_keyboard);
	}

	pc_kbd_reset (&pc->kbd);

	/* AT reset enables the interface in the 8042 command byte. Keep the
	 * transport clock consistent, including when assisted BIOS startup
	 * bypasses native keyboard initialization. Do not invoke XT reset. */
	if (pc->model & PCE_IBMPC_5170) {
		pc->kbd.clk = (pc->at_kbd.cfg & 0x10) ? 0 : 1;
		pc_kbd_set_enable (&pc->kbd, 1);
	}

	for (i = 0; i < 4; i++) {
		if (pc->serport[i] != NULL) {
			ser_reset (pc->serport[i]);
		}
	}

	if (pc->fdc != NULL) {
		dev_fdc_reset (pc->fdc);
	}

	if (pc->wd1003 != NULL) {
		wd1003_reset (pc->wd1003);
	}

	if (pc->hdc != NULL) {
		hdc_reset (pc->hdc);
	}

	if (pc->xms != NULL) {
		xms_reset (pc->xms);
	}

	if (pc->ems != NULL) {
		ems_reset (pc->ems);
	}
}

/*
 * Get the 1.19 MHz system clock
 */
unsigned long pc_get_clock2 (ibmpc_t *pc)
{
	return (pc->clock2);
}


void pc_clock_reset (ibmpc_t *pc)
{
	unsigned i;

	for (i = 0; i < 4; i++) {
		pc->clk_div[i] = 0;
	}

	pc->sync_clock2_sim = 0;
	pc->sync_clock2_real = 0;

	pce_get_interval_us (&pc->sync_interval);

	pc->speed_clock_extra = 0;

	pc->clock1 = 0;
	pc->clock2 = 0;
}

void pc_clock_discontinuity (ibmpc_t *pc)
{
	pc->sync_clock2_real = pc->sync_clock2_sim;
	pce_get_interval_us (&pc->sync_interval);

	pc->speed_clock_extra = 0;
}

/*
 * Synchronize the system clock with real time
 */
static
void pc_clock_delay (ibmpc_t *pc)
{
	unsigned long vclk;
	unsigned long rclk;
	unsigned long us;

	vclk = pc->sync_clock2_sim;

	rclk = pce_get_interval_us (&pc->sync_interval);
	rclk = (PCE_IBMPC_CLK2 * (unsigned long long) rclk) / 1000000;
	rclk += pc->sync_clock2_real;

	if (vclk < rclk) {
		pc->sync_clock2_sim = 0;
		pc->sync_clock2_real = rclk - vclk;

		if (pc->speed_clock_extra > 0) {
			pc->speed_clock_extra -= 1;
		}

		if (pc->sync_clock2_real >= (2 * PCE_IBMPC_CLK2)) {
			pc->sync_clock2_real -= PCE_IBMPC_CLK2;
			pce_log (MSG_INF, "host system too slow, skipping 1 second.\n");
		}

		return;
	}
	else {
		pc->speed_clock_extra += 1;
	}

	vclk -= rclk;

	pc->sync_clock2_sim = vclk;
	pc->sync_clock2_real = 0;

	us = (1000000 * (unsigned long long) vclk) / PCE_IBMPC_CLK2;

	if (us > PCE_IBMPC_SLEEP) {
		pce_usleep (us);
	}
}

void pc_clock (ibmpc_t *pc, unsigned long cnt)
{
	unsigned      i;
	unsigned long spd;
	unsigned long clk;

	/* AT shutdown decoding resets only the CPU, preserving RAM and the
	 * CMOS shutdown byte used by the BIOS to resume real-mode execution. */
	if (PC_HAS_8042 (pc) && (pc->cpu->state & E86_STATE_SHUTDOWN)) {
		pc->cpu->state = E86_STATE_RESET;
	}

	if (cnt == 0) {
		cnt = 4;
	}

	if (pc->speed_current == 0) {
		e86_clock (pc->cpu, cnt + pc->speed_clock_extra);
		spd = 4;
	}
	else {
		e86_clock (pc->cpu, cnt);
		spd = 4 * pc->speed_current;
	}

	pc->clock1 += cnt;

	if (pc->clock1 < spd) {
		return;
	}

	pc->clock1 -= spd;

	pc->sync_clock2_sim += 1;

	pc->clock2 += 1;

	pce_video_clock0 (pc->video, 1, 1);

	e8253_clock (&pc->pit, 1);

	if (pc->cas != NULL) {
		cas_clock (pc->cas);
	}

	pc->clk_div[0] += 1;

	if (pc->clk_div[0] >= 8) {
		clk = pc->clk_div[0] & ~7UL;
		pc->clk_div[1] += clk;
		pc->clk_div[0] &= 7;

		if (PC_HAS_8042 (pc)) {
			pc_at_kbd_clock (pc, clk);
		}
		else {
			pc_kbd_clock (&pc->kbd, clk);
		}

		e8237_clock (&pc->dma, clk);

		pce_video_clock1 (pc->video, 0);

		for (i = 0; i < 4; i++) {
			if (pc->serport[i] != NULL) {
				e8250_clock (&pc->serport[i]->uart, clk);
			}
		}

		if (pc->wd1003 != NULL) {
			wd1003_clock (pc->wd1003, clk);
		}

		if (pc->fdc != NULL) {
			e8272_clock (&pc->fdc->e8272, clk);
		}

		if (pc->clk_div[1] >= 1024) {
			clk = pc->clk_div[1] & ~1023UL;
			pc->clk_div[1] &= 1023;
			pc->clk_div[2] += clk;

			if (pc->trm != NULL) {
				trm_check (pc->trm);
			}

			if (pc->atari_pc_rtc != NULL) {
				mc146818a_clock (pc->atari_pc_rtc, clk);
			}

			/* AT: clock the CMOS/RTC */
			if (pc->at_rtc != NULL) {
				mc146818a_clock (pc->at_rtc, clk);
			}

			/* AT: clock the second DMA */
			if (pc->model & PCE_IBMPC_5170) {
				e8237_clock (&pc->dma2, clk);
			}

			if (pc->hdc != NULL) {
				hdc_clock (pc->hdc, clk);
			}

			pc_speaker_clock (&pc->spk, clk);

			if (pc->cov != NULL) {
				pc_covox_clock (pc->cov, clk);
			}

			for (i = 0; i < 4; i++) {
				if (pc->serport[i] != NULL) {
					ser_clock (pc->serport[i], clk);
				}
			}

			if (pc->clk_div[2] >= 16384) {
				pc->clk_div[2] &= 16383;
				pc_clock_delay (pc);
			}
		}
	}
}

int pc_set_cpu_model (ibmpc_t *pc, const char *str)
{
	if (strcmp (str, "8086") == 0) {
		e86_set_8086 (pc->cpu);
	}
	else if (strcmp (str, "8088") == 0) {
		e86_set_8088 (pc->cpu);
	}
	else if (strcmp (str, "80186") == 0) {
		e86_set_80186 (pc->cpu);
	}
	else if (strcmp (str, "80188") == 0) {
		e86_set_80188 (pc->cpu);
	}
	else if (strcmp (str, "v30") == 0) {
		e86_set_v30 (pc->cpu);
	}
	else if (strcmp (str, "v20") == 0) {
		e86_set_v20 (pc->cpu);
	}
	else if (strcmp (str, "80286") == 0) {
		e86_set_80286 (pc->cpu);
	}
	else {
		return (1);
	}

	return (0);
}

void pc_set_speed (ibmpc_t *pc, unsigned factor)
{
	pc->speed_current = factor;
	pc->speed_clock_extra = 0;

	pce_log_tag (MSG_INF, "CPU:", "setting speed to %uX\n", pc->speed_current);
}

void pc_set_bootdrive (ibmpc_t *pc, unsigned drv)
{
	pc->bootdrive = drv;
}

unsigned pc_get_bootdrive (ibmpc_t *pc)
{
	return (pc->bootdrive);
}
