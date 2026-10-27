/*
 * Machine I/O regression tests. Construct real machines through pc_new and
 * access devices through the installed I/O bus; do not copy device logic.
 */

#include "arch/ibmpc/main.h"
#include "arch/ibmpc/ibmpc.h"
#include "arch/ibmpc/hook.h"
#include "arch/ibmpc/xms.h"
#include "arch/ibmpc/int13.h"

#include <drivers/block/blkram.h>
#include <drivers/block/blkpsi.h>
#include <stdio.h>
#include <string.h>


static unsigned failures = 0;
static unsigned checks = 0;
static const char *test_system;
static const char *test_cpu;

static
void check_value (const char *name, unsigned long got, unsigned long want)
{
	checks += 1;

	if (got != want) {
		printf ("FAIL %s/%s: %s (got=%lx want=%lx)\n",
			test_system, test_cpu, name, got, want);
		failures += 1;
	}
}

static
ibmpc_t *new_machine_config (const char *system, const char *cpu, unsigned rom_flags)
{
	ini_sct_t *cfg;
	ibmpc_t   *pc;

	test_system = system;
	test_cpu = cpu;

	cfg = ini_sct_new (NULL);
	if (cfg == NULL) {
		return (NULL);
	}

	ini_set_str (cfg, "system.model", system);
	ini_set_uint32 (cfg, "system.patch_bios_init", 0);
	ini_set_uint32 (cfg, "system.patch_bios_int19", 0);
	ini_set_str (cfg, "cpu.model", cpu);
	ini_set_uint32 (cfg, "cpu.speed", 1);
	ini_set_uint32 (cfg, "ram.address", 0);
	ini_set_uint32 (cfg, "ram.size", 640UL * 1024);
	ini_set_str (cfg, "terminal.driver", "null");
	ini_set_str (cfg, "video.device", "cga");

	if (rom_flags & 8) {
		ini_get_sct (cfg, "wd1003", 1);
		ini_set_uint32 (cfg, "fdc.drive0", 0);
		if (rom_flags & 16) ini_set_uint32 (cfg, "fdc.drive0_rpm", 360);
	}

	if (rom_flags & 32) {
		ini_set_str (cfg, "disk.type", "ram");
		ini_set_str (cfg, "disk.file", "/dev/null");
		ini_set_uint32 (cfg, "disk.drive", 128);
		ini_set_uint32 (cfg, "disk.c", 16);
		ini_set_uint32 (cfg, "disk.h", 2);
		ini_set_uint32 (cfg, "disk.s", 17);
		ini_set_uint32 (cfg, "wd1003.drive0", 128);
		ini_set_uint32 (cfg, "cmos.hard_disk0_type", 2);
	}

	if (rom_flags & 7) {
		ini_sct_t *rom;

		rom = ini_get_sct (cfg, "rom", 1);
		ini_set_uint32 (rom, "address", 0xf0000);
		ini_set_uint32 (rom, "size", 0x10000);
		ini_set_uint32 (rom, "default", 0x90);
		if (rom_flags & 6) {
			rom = ini_get_sct (cfg, (rom_flags & 2) ? "rom[+]" : "ram[+]", 1);
			ini_set_uint32 (rom, "address", 0xff0000);
			ini_set_uint32 (rom, "size", 0x10000);
			ini_set_uint32 (rom, "default", 0x5a);
		}
	}

	pc = pc_new (cfg);
	if (pc == NULL) {
		ini_sct_del (cfg);
		return (NULL);
	}

	pc_reset (pc);
	return (pc);
}

static
ibmpc_t *new_machine (const char *system, const char *cpu)
{
	return (new_machine_config (system, cpu, 0));
}

static
void check_a20_support (ibmpc_t *pc, unsigned expected)
{
	e86_set_ax (pc->cpu, 0x2403);
	pc_hook_old (pc, PCEH_INT & 0xff, 0x15);

	if (expected != 0) {
		check_value ("BIOS A20 service succeeds", e86_get_cf (pc->cpu), 0);
		check_value ("BIOS advertises installed A20 methods",
			e86_get_bx (pc->cpu), expected);
	}
	else {
		check_value ("BIOS A20 service unsupported", e86_get_cf (pc->cpu), 1);
		check_value ("BIOS A20 error", e86_get_ah (pc->cpu), 0x86);
	}
}

static
void check_xt_machine (const char *system, const char *cpu)
{
	ibmpc_t  *pc;
	unsigned before;

	before = failures;
	pc = new_machine (system, cpu);
	if (pc == NULL) {
		check_value ("machine allocation", 0, 1);
		return;
	}

	check_value ("PPI mapped at 0x60", mem_get_blk (pc->prt, 0x60) != NULL, 1);
	check_value ("AT slave PIC absent", mem_get_blk (pc->prt, 0xa0) != NULL, 0);
	check_value ("AT second DMA absent", mem_get_blk (pc->prt, 0xc0) != NULL, 0);
	check_value ("AT CMOS absent", pc->at_rtc != NULL, 0);

	/* XT clock/acknowledge remains on PPI port B, including with a 286. */
	mem_set_uint8 (pc->prt, 0x61, 0x40);
	pc->kbd.key = 0x1e;
	pc->kbd.key_valid = 1;
	check_value ("PPI reads keyboard latch", mem_get_uint8 (pc->prt, 0x60), 0x1e);
	mem_set_uint8 (pc->prt, 0x61, 0xc0);
	check_value ("PPI acknowledges keyboard", pc->kbd.key_valid, 0);
	check_value ("PPI disables keyboard", pc->kbd.enable, 0);
	mem_set_uint8 (pc->prt, 0x61, 0x40);
	check_value ("PPI enables keyboard", pc->kbd.enable, 1);

	mem_set_uint8 (pc->prt, 0x64, 0xaa);
	check_value ("no 8042 response at port 0x64",
		mem_get_uint8 (pc->prt, 0x64), strcmp (system, "m24") == 0 ? 1 : 0xff);
	mem_set_uint8 (pc->prt, 0x64, 0xe0);
	check_value ("no AT test inputs on XT-family controller",
		mem_get_uint8 (pc->prt, 0x64), strcmp (system, "m24") == 0 ? 1 : 0xff);
	mem_set_uint8 (pc->prt, 0x64, 0xfe);
	check_value ("absent 8042 cannot reset CPU", pc->cpu->state & E86_STATE_RESET, 0);

	if (strcmp (cpu, "80286") == 0) {
		/* Preserve the existing emulator fast-A20 extension for XT/286. */
		mem_set_uint8 (pc->prt, 0x92, 0);
		check_value ("XT/286 fast A20 off", mem_get_uint8 (pc->prt, 0x92), 0);
		mem_set_uint8 (pc->prt, 0x92, 2);
		check_value ("XT/286 fast A20 on", mem_get_uint8 (pc->prt, 0x92), 2);
		check_a20_support (pc, 2);
	}
	else {
		check_value ("legacy CPU has no fast A20 port", mem_get_uint8 (pc->prt, 0x92), 0xff);
		check_a20_support (pc, 0);
	}

	pc_reset (pc);
	check_value ("reset retains XT controller selection",
		mem_get_uint8 (pc->prt, 0x64), strcmp (system, "m24") == 0 ? 1 : 0xff);

	printf ("%s machine %s/%s\n", failures == before ? "PASS" : "FAIL", system, cpu);
	pc_del (pc);
}

static
void keyboard_ticks (ibmpc_t *pc, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++) {
		pc_clock (pc, 4);
	}
}

static
void check_at_delivery (void)
{
	ibmpc_t *pc;
	unsigned head, i;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("AT delivery machine allocation", 0, 1);
		return;
	}
	/* Clock peripherals without executing a guest or its interrupt handler. */
	pc->cpu->state |= E86_STATE_HALT;
	/* Assisted BIOS startup does not program the 8042. Reset must leave
	 * the transport consistent with the enabled controller command byte. */
	check_value ("AT reset clock matches command byte", pc->kbd.clk,
		(pc->at_kbd.cfg & 0x10) ? 0 : 1);
	pc->kbd.key_i = pc->kbd.key_j;
	pc_kbd_set_keycode (&pc->kbd, 0x1c);
	keyboard_ticks (pc, 4096);
	check_value ("AT reset delivers key without BIOS setup",
		mem_get_uint8 (pc->prt, 0x60), 0x1c);
	pc->kbd.key_i = pc->kbd.key_j;
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 5);
	mem_set_uint8 (pc->prt, 0x64, 0xae);
	pc_kbd_set_keycode (&pc->kbd, 0x1e);
	pc_kbd_set_keycode (&pc->kbd, 0x9e);
	keyboard_ticks (pc, 512);
	check_value ("AT byte arrives without status polling", pc->at_kbd.obuf_full, 1);
	check_value ("AT pending byte asserts IRQ1", pc->pic.irq_inp & 2, 2);
	head = pc->kbd.key_i;
	keyboard_ticks (pc, 32768);
	check_value ("full OBF stops keyboard dequeue", pc->kbd.key_i, head);
	check_value ("full OBF preserves first byte", mem_get_uint8 (pc->prt, 0x60), 0x1e);
	check_value ("AT data read acknowledges IRQ", pc->pic.irq_inp & 2, 0);
	for (i = 0; i < 16; i++) {
		check_value ("status polling cannot replay a scan byte", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	}
	keyboard_ticks (pc, 512);
	check_value ("next queued byte raises IRQ", pc->pic.irq_inp & 2, 2);
	check_value ("next queued byte retains order", mem_get_uint8 (pc->prt, 0x60), 0x9e);
	keyboard_ticks (pc, 512);
	check_value ("drained keyboard stays empty", mem_get_uint8 (pc->prt, 0x64) & 1, 0);

	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 4);
	pc_kbd_set_keycode (&pc->kbd, 0xe0);
	keyboard_ticks (pc, 512);
	check_value ("IRQ-disabled byte still sets OBF", pc->at_kbd.obuf_full, 1);
	check_value ("keyboard cannot bypass controller IRQ mask", pc->pic.irq_inp & 2, 0);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 5);
	check_value ("enabling IRQ exposes pending byte", pc->pic.irq_inp & 2, 2);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 4);
	check_value ("disabling IRQ lowers pending line", pc->pic.irq_inp & 2, 0);
	check_value ("IRQ changes preserve pending data", mem_get_uint8 (pc->prt, 0x60), 0xe0);

	mem_set_uint8 (pc->prt, 0x64, 0xad);
	pc_kbd_set_keycode (&pc->kbd, 0x1d);
	head = pc->kbd.key_i;
	keyboard_ticks (pc, 512);
	check_value ("inhibit preserves queued byte", pc->kbd.key_i, head);
	check_value ("inhibit produces no output", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	mem_set_uint8 (pc->prt, 0x64, 0xae);
	keyboard_ticks (pc, 512);
	check_value ("release delivers queued byte once", mem_get_uint8 (pc->prt, 0x60), 0x1d);
	check_value ("release cannot replay byte", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	pc_del (pc);
}

static
void keyboard_reply (ibmpc_t *pc, unsigned val)
{
	keyboard_ticks (pc, 4096);
	check_value ("keyboard reply sets OBF", mem_get_uint8 (pc->prt, 0x64) & 1, 1);
	check_value ("keyboard reply byte", mem_get_uint8 (pc->prt, 0x60), val);
}

static
void check_at_protocol (void)
{
	ibmpc_t *pc;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("AT protocol machine allocation", 0, 1);
		return;
	}
	pc->cpu->state |= E86_STATE_HALT;
	pc->kbd.key_i = pc->kbd.key_j;
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 5);
	mem_set_uint8 (pc->prt, 0x60, 0xff);
	keyboard_reply (pc, 0xfa);
	keyboard_reply (pc, 0xaa);
	mem_set_uint8 (pc->prt, 0x60, 0xfe);
	keyboard_reply (pc, 0xaa);
	mem_set_uint8 (pc->prt, 0x60, 0xed);
	keyboard_reply (pc, 0xfa);
	mem_set_uint8 (pc->prt, 0x60, 7);
	keyboard_reply (pc, 0xfa);
	mem_set_uint8 (pc->prt, 0x60, 0xf3);
	keyboard_reply (pc, 0xfa);
	mem_set_uint8 (pc->prt, 0x60, 0x20);
	keyboard_reply (pc, 0xfa);
	mem_set_uint8 (pc->prt, 0x60, 0xee);
	keyboard_reply (pc, 0xee);
	mem_set_uint8 (pc->prt, 0x60, 0xf5);
	keyboard_reply (pc, 0xfa);
	pc_kbd_set_keycode (&pc->kbd, 0x1e);
	keyboard_ticks (pc, 512);
	check_value ("F5 inhibits scan delivery", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	mem_set_uint8 (pc->prt, 0x60, 0xf6);
	keyboard_reply (pc, 0xfa);
	keyboard_ticks (pc, 512);
	check_value ("F6 preserves disabled scanning", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	mem_set_uint8 (pc->prt, 0x60, 0xf4);
	keyboard_reply (pc, 0xfa);
	keyboard_ticks (pc, 512);
	check_value ("F4 clears stale queued scan data", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	pc_kbd_set_keycode (&pc->kbd, 0x30);
	keyboard_reply (pc, 0x30);
	mem_set_uint8 (pc->prt, 0x60, 0xfe);
	keyboard_reply (pc, 0x30);
	/* Controller D2 remains separate from keyboard command parsing. */
	mem_set_uint8 (pc->prt, 0x64, 0xd2);
	mem_set_uint8 (pc->prt, 0x60, 0xff);
	keyboard_reply (pc, 0xff);
	keyboard_ticks (pc, 512);
	check_value ("D2 FF is data, not keyboard reset", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	pc_del (pc);
}

static
void check_at_host_write (void)
{
	ibmpc_t *pc;
	unsigned i;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("host-write machine allocation", 0, 1);
		return;
	}
	pc->cpu->state |= E86_STATE_HALT;
	pc->kbd.key_i = pc->kbd.key_j;
	mem_set_uint8 (pc->prt, 0x64, 0xad);
	mem_set_uint8 (pc->prt, 0x60, 0xff);
	check_value ("host write releases inhibit", pc->at_kbd.cfg & 0x10, 0);
	check_value ("host write releases clock", pc->kbd.clk, 1);
	keyboard_ticks (pc, 8);
	check_value ("command ACK survives initial drain", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	for (i = 0; i < 32; i++) {
		check_value ("polling cannot accelerate ACK", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	}
	keyboard_reply (pc, 0xfa);
	keyboard_ticks (pc, 8);
	check_value ("BAT waits for its own transfer", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	keyboard_reply (pc, 0xaa);
	keyboard_ticks (pc, 4096);
	check_value ("reset replies delivered once", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	mem_set_uint8 (pc->prt, 0x64, 0xad);
	mem_set_uint8 (pc->prt, 0x64, 0xd1);
	mem_set_uint8 (pc->prt, 0x60, 3);
	check_value ("controller parameter preserves inhibit", pc->at_kbd.cfg & 0x10, 0x10);
	pc_del (pc);
}

static
void check_at_test_inputs (void)
{
	ibmpc_t *pc;
	unsigned head, tail, i;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("test-input machine allocation", 0, 1);
		return;
	}
	/* IBM POST 36 inhibits the keyboard, drains OBF, then issues E0. */
	mem_set_uint8 (pc->prt, 0x64, 0xae);
	mem_set_uint8 (pc->prt, 0x64, 0xad);
	(void) mem_get_uint8 (pc->prt, 0x60);
	head = pc->kbd.key_i;
	tail = pc->kbd.key_j;
	mem_set_uint8 (pc->prt, 0x64, 0xe0);
	check_value ("E0 supplies response while inhibited", mem_get_uint8 (pc->prt, 0x64) & 1, 1);
	check_value ("E0 remains polling-only", pc->pic.irq_inp & 2, 0);
	for (i = 0; i < 16; i++) {
		check_value ("E0 polling leaves response pending", mem_get_uint8 (pc->prt, 0x64) & 1, 1);
	}
	check_value ("E0 inhibited clock low and idle data high", mem_get_uint8 (pc->prt, 0x60), 2);
	check_value ("E0 response consumed once", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	check_value ("E0 preserves queued keyboard head", pc->kbd.key_i, head);
	check_value ("E0 preserves queued keyboard tail", pc->kbd.key_j, tail);
	check_value ("E0 leaves inhibit set", pc->at_kbd.cfg & 0x10, 0x10);
	check_value ("E0 does not latch self-test SYS", mem_get_uint8 (pc->prt, 0x64) & 4, 0);
	mem_set_uint8 (pc->prt, 0x64, 0xae);
	mem_set_uint8 (pc->prt, 0x64, 0xe0);
	check_value ("E0 released clock and idle data high", mem_get_uint8 (pc->prt, 0x60), 3);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 0x15);
	mem_set_uint8 (pc->prt, 0x64, 0xe0);
	check_value ("E0 honors command-byte inhibit", mem_get_uint8 (pc->prt, 0x60), 2);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 5);
	mem_set_uint8 (pc->prt, 0x64, 0xe0);
	check_value ("E0 honors command-byte release", mem_get_uint8 (pc->prt, 0x60), 3);
	check_value ("E0 does not request IRQ after release", pc->pic.irq_inp & 2, 0);
	pc_del (pc);
}

static
void check_at_commands (void)
{
	ibmpc_t *pc;
	unsigned head, tail;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("AT command machine allocation", 0, 1);
		return;
	}
	mem_set_uint8 (pc->prt, 0x64, 0x20);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	check_value ("read earlier response", mem_get_uint8 (pc->prt, 0x60), 5);
	mem_set_uint8 (pc->prt, 0x60, 0x14);
	check_value ("read preserves pending config parameter", pc->at_kbd.cfg, 0x14);
	check_value ("command byte inhibits keyboard clock", pc->kbd.clk, 0);
	pc->kbd.key_i = pc->kbd.key_j; /* Discard the power-on BAT fixture. */
	pc_kbd_set_keycode (&pc->kbd, 0x1e);
	head = pc->kbd.key_i;
	tail = pc->kbd.key_j;
	mem_set_uint8 (pc->prt, 0x64, 0xae);
	check_value ("AE clears inhibit bit", pc->at_kbd.cfg & 0x10, 0);
	check_value ("AE enables clock", pc->kbd.clk, 1);
	check_value ("AE preserves queue head", pc->kbd.key_i, head);
	check_value ("AE preserves queue tail", pc->kbd.key_j, tail);
	check_value ("AE does not insert BAT", pc->kbd.key_buf[head], 0x1e);
	mem_set_uint8 (pc->prt, 0x64, 0xad);
	check_value ("AD sets inhibit bit", pc->at_kbd.cfg & 0x10, 0x10);
	mem_set_uint8 (pc->prt, 0x64, 0x20);
	check_value ("readback reports inhibit", mem_get_uint8 (pc->prt, 0x60) & 0x10, 0x10);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 4);
	check_value ("config clears clock inhibit", pc->kbd.clk, 1);
	pc->pic.irr = 0;
	mem_set_uint8 (pc->prt, 0x64, 0xd2);
	mem_set_uint8 (pc->prt, 0x60, 0x31);
	check_value ("D2 honors disabled IRQ1", pc->pic.irr & 2, 0);
	check_value ("D2 still supplies polled data", mem_get_uint8 (pc->prt, 0x60), 0x31);
	mem_set_uint8 (pc->prt, 0x64, 0x60);
	mem_set_uint8 (pc->prt, 0x60, 5);
	mem_set_uint8 (pc->prt, 0x64, 0xd2);
	mem_set_uint8 (pc->prt, 0x60, 0x32);
	check_value ("D2 raises enabled IRQ1", pc->pic.irr & 2, 2);
	check_value ("D2 data with IRQ", mem_get_uint8 (pc->prt, 0x60), 0x32);
	check_value ("data read releases IRQ1 input", pc->pic.irq_inp & 2, 0);
	pc_del (pc);
}

static
void check_at_machine (void)
{
	ibmpc_t *pc;
	unsigned i;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("machine allocation", 0, 1);
		return;
	}

	check_value ("AT has no XT PPI mapping", mem_get_blk (pc->prt, 0x60) != NULL, 0);
	check_value ("AT slave PIC present", mem_get_blk (pc->prt, 0xa0) != NULL, 1);
	check_value ("AT second DMA present", mem_get_blk (pc->prt, 0xc0) != NULL, 1);
	check_value ("AT CMOS present", pc->at_rtc != NULL, 1);

	/* Warm POST enters with A20 disabled by the previous BIOS boot. */
	mem_set_uint8 (pc->prt, 0x64, 0xd1);
	mem_set_uint8 (pc->prt, 0x60, 0xdd);
	check_value ("self-test fixture disables A20", e86_get_addr_mask (pc->cpu), 0xefffff);
	mem_set_uint8 (pc->prt, 0x64, 0xaa);
	check_value ("self-test restores output-port A20", e86_get_addr_mask (pc->cpu), 0xffffff);
	/* IBM POST drains old OBF after command acceptance, then waits. */
	check_value ("self-test accepts command before replying",
		mem_get_uint8 (pc->prt, 0x64) & 3, 0);
	for (i = 0; i < 100; i++) {
		(void) mem_get_uint8 (pc->prt, 0x64);
	}
	check_value ("status polling cannot complete self-test",
		mem_get_uint8 (pc->prt, 0x64) & 5, 0);
	keyboard_ticks (pc, 2048);
	check_value ("self-test completes without polling",
		mem_get_uint8 (pc->prt, 0x64) & 5, 5);
	check_value ("8042 self-test response", mem_get_uint8 (pc->prt, 0x60), 0x55);
	check_value ("8042 SYS remains latched", mem_get_uint8 (pc->prt, 0x64) & 4, 4);
	check_value ("8042 read clears response", pc->at_kbd.obuf_full | pc->at_kbd.obuf_irq, 0);
	mem_set_uint8 (pc->prt, 0x64, 0xd0);
	check_value ("self-test A20 visible in output port", mem_get_uint8 (pc->prt, 0x60) & 2, 2);
	/* An unread old reply must survive expiry and draining must not cancel AA. */
	mem_set_uint8 (pc->prt, 0x64, 0xad);
	mem_set_uint8 (pc->prt, 0x64, 0x20);
	mem_set_uint8 (pc->prt, 0x64, 0xaa);
	keyboard_ticks (pc, 2048);
	check_value ("self-test preserves occupied output", mem_get_uint8 (pc->prt, 0x60), 0x15);
	keyboard_ticks (pc, 8);
	check_value ("self-test completes despite keyboard inhibit", mem_get_uint8 (pc->prt, 0x60), 0x55);
	keyboard_ticks (pc, 8);
	check_value ("self-test reply delivered once", mem_get_uint8 (pc->prt, 0x64) & 1, 0);
	mem_set_uint8 (pc->prt, 0x64, 0xae);
	mem_set_uint8 (pc->prt, 0x64, 0xaa);
	pc_reset (pc);
	pc->kbd.key_i = pc->kbd.key_j; /* Discard the power-on BAT fixture. */
	pc->kbd.key_valid = 0;
	keyboard_ticks (pc, 2048);
	check_value ("reset cancels pending self-test", mem_get_uint8 (pc->prt, 0x64) & 5, 0);

	mem_set_uint8 (pc->prt, 0x64, 0x20);
	check_value ("8042 config sets OBF", mem_get_uint8 (pc->prt, 0x64) & 1, 1);
	check_value ("8042 config response", mem_get_uint8 (pc->prt, 0x60), 0x05);
	mem_set_uint8 (pc->prt, 0x64, 0xab);
	check_value ("8042 interface test sets OBF", mem_get_uint8 (pc->prt, 0x64) & 1, 1);
	check_value ("8042 interface test response", mem_get_uint8 (pc->prt, 0x60), 0);
	check_a20_support (pc, 3);

	/* Program the PICs through their mapped ports, as an AT BIOS would. */
	mem_set_uint8 (pc->prt, 0x20, 0x11);
	mem_set_uint8 (pc->prt, 0x21, 0x08);
	mem_set_uint8 (pc->prt, 0x21, 0x04);
	mem_set_uint8 (pc->prt, 0x21, 0x01);
	mem_set_uint8 (pc->prt, 0xa0, 0x11);
	mem_set_uint8 (pc->prt, 0xa1, 0x70);
	mem_set_uint8 (pc->prt, 0xa1, 0x02);
	mem_set_uint8 (pc->prt, 0xa1, 0x01);
	mem_set_uint8 (pc->prt, 0x21, 0xfb);
	mem_set_uint8 (pc->prt, 0xa1, 0xfe);
	e8259_set_irq0 (&pc->pic2, 1);
	check_value ("slave IRQ reaches master IRQ2", pc->pic.irr & 4, 4);
	check_value ("CPU receives RTC vector", pc->cpu->inta (pc->cpu->inta_ext), 0x70);
	check_value ("CPU acknowledges slave ISR", pc->pic2.isr & 1, 1);

	pc_del (pc);
}

static
void check_a20_machine (const char *system)
{
	ibmpc_t *pc;
	mem_blk_t *ext;

	pc = new_machine (system, "80286");
	if (pc == NULL) {
		check_value ("machine allocation", 0, 1);
		return;
	}
	ext = mem_blk_new (0x100000, 0x300000, 1);
	if (ext == NULL) {
		check_value ("extended RAM allocation", 0, 1);
		pc_del (pc);
		return;
	}
	mem_add_blk (pc->mem, ext, 1);
	mem_set_uint8 (pc->mem, 0, 0x11);
	mem_set_uint8 (pc->mem, 0x200000, 0x22);
	mem_set_uint8 (pc->prt, 0x92, 0);
	check_value ("A20 off retains upper address lines", e86_get_addr_mask (pc->cpu), 0xefffff);
	check_value ("A20 off leaves physical RAM active", ext->active, 1);
	pc->cpu->msw = E286_MSW_PE;
	pc->cpu->sreg[E86_REG_DS] = 0x18;
	pc->cpu->seg_base[E86_REG_DS] = 0x300000;
	check_value ("3 MB aliases 2 MB with A20 off", e86_get_mem8 (pc->cpu, 0x18, 0), 0x22);
	e86_reset (pc->cpu);
	check_value ("CPU reset preserves external A20 gate", e86_get_addr_mask (pc->cpu), 0xefffff);
	mem_set_uint8 (pc->prt, 0x92, 2);
	check_value ("A20 on retains 24-bit bus width", e86_get_addr_mask (pc->cpu), 0xffffff);
	e86_set_ax (pc->cpu, 0x2400);
	pc_hook_old (pc, PCEH_INT & 0xff, 0x15);
	check_value ("BIOS A20 off retains upper lines", e86_get_addr_mask (pc->cpu), 0xefffff);
	check_value ("BIOS leaves RAM mapped", ext->active, 1);
	if (strcmp (system, "5170") == 0) {
		mem_set_uint8 (pc->prt, 0x64, 0xdf);
		check_value ("8042 enable uses 24-bit bus", e86_get_addr_mask (pc->cpu), 0xffffff);
		mem_set_uint8 (pc->prt, 0x64, 0xdd);
		check_value ("8042 disable retains upper lines", e86_get_addr_mask (pc->cpu), 0xefffff);
		mem_set_uint8 (pc->prt, 0x64, 0xd1);
		mem_set_uint8 (pc->prt, 0x60, 3);
		check_value ("8042 output-port enable", e86_get_addr_mask (pc->cpu), 0xffffff);
		mem_set_uint8 (pc->prt, 0x92, 0);
		mem_set_uint8 (pc->prt, 0x64, 0xd0);
		check_value ("8042 reads effective A20 state", mem_get_uint8 (pc->prt, 0x60) & 2, 0);
	}
	pc_reset (pc);
	check_value ("machine reset restores startup A20 state", e86_get_addr_mask (pc->cpu), 0xffffff);
	check_value ("machine reset leaves RAM active", ext->active, 1);
	pc_del (pc);
}

static
void check_xms_a20 (const char *cpu)
{
	ibmpc_t *pc;
	ini_sct_t *cfg;
	unsigned fn;
	unsigned long enabled, disabled;

	pc = new_machine ("5160", cpu);
	if (pc == NULL) {
		check_value ("machine allocation", 0, 1);
		return;
	}
	cfg = ini_sct_new (NULL);
	if (cfg == NULL) {
		check_value ("XMS configuration allocation", 0, 1);
		pc_del (pc);
		return;
	}
	pc->xms = xms_new (cfg);
	ini_sct_del (cfg);
	if (pc->xms == NULL) {
		check_value ("XMS allocation", 0, 1);
		pc_del (pc);
		return;
	}
	enabled = strcmp (cpu, "80286") == 0 ? 0xffffffUL : ~0UL;
	disabled = strcmp (cpu, "80286") == 0 ? 0xefffffUL : 0xfffffUL;
	for (fn = 3; fn <= 6; fn++) {
		e86_set_ax (pc->cpu, fn << 8);
		pc_hook_old (pc, PCEH_XMS & 0xff, PCEH_XMS >> 8);
		check_value ("XMS A20 service succeeds", e86_get_ax (pc->cpu), 1);
		check_value ("XMS A20 bus mask", e86_get_addr_mask (pc->cpu),
			(fn & 1) ? enabled : disabled);
		e86_set_ax (pc->cpu, 0x0700);
		pc_hook_old (pc, PCEH_XMS & 0xff, PCEH_XMS >> 8);
		check_value ("XMS A20 query", e86_get_ax (pc->cpu), fn & 1);
	}
	pc_del (pc);
}

static
void check_reset_rom (const char *system, const char *cpu, unsigned flags)
{
	ibmpc_t *pc;
	mem_blk_t *low, *high;

	pc = new_machine_config (system, cpu, flags);
	if (pc == NULL) {
		check_value ("machine allocation", 0, 1);
		return;
	}
	low = mem_get_blk (pc->mem, 0xffff0);
	high = mem_get_blk (pc->mem, 0xfffff0);
	if (strcmp (cpu, "80286") != 0) {
		check_value ("legacy CPU gains no high BIOS mapping", high != NULL, 0);
		pc_del (pc);
		return;
	}
	check_value ("286 high BIOS mapping present", high != NULL, 1);
	if (high == NULL) {
		pc_del (pc);
		return;
	}
	if (flags & 6) {
		check_value ("explicit high mapping preserved", mem_get_uint8 (pc->mem, 0xfffff0), 0x5a);
		check_value ("explicit high mapping has own storage", high->data != low->data, 1);
	}
	else {
		check_value ("BIOS alias shares storage", high->data == low->data, 1);
		mem_set_uint8_rw (pc->mem, 0xffff0, 0xea);
		mem_set_uint8_rw (pc->mem, 0xffff1, 0x00);
		mem_set_uint8_rw (pc->mem, 0xffff2, 0x01);
		mem_set_uint8_rw (pc->mem, 0xffff3, 0x00);
		mem_set_uint8_rw (pc->mem, 0xffff4, 0xf0);
		check_value ("low BIOS patches visible at reset", mem_get_uint8 (pc->mem, 0xfffff0), 0xea);
		mem_set_uint8 (pc->mem, 0xfffff0, 0x11);
		check_value ("high BIOS alias is read-only", mem_get_uint8 (pc->mem, 0xfffff0), 0xea);
		e86_execute (pc->cpu);
		check_value ("machine reset enters BIOS far jump", e86_get_ip (pc->cpu), 0x100);
		check_value ("machine far jump restores low CS base", pc->cpu->seg_base[E86_REG_CS], 0xf0000);
	}
	pc_del (pc);
}

static
void check_at_dma_page_words (void)
{
	ibmpc_t *pc;

	pc = new_machine ("5170", "80286");
	if (pc == NULL) {
		check_value ("DMA page machine allocation", 0, 1);
		return;
	}
	/* IBM POST 27 uses OUT 82,AX followed by byte reads at 82/83. */
	mem_set_uint16_le (pc->prt, 0x82, 0xaa55);
	check_value ("DMA page word write low byte", mem_get_uint8 (pc->prt, 0x82), 0x55);
	check_value ("DMA page word write high byte", mem_get_uint8 (pc->prt, 0x83), 0xaa);
	check_value ("DMA page word read", mem_get_uint16_le (pc->prt, 0x82), 0xaa55);
	check_value ("DMA channel 3 page side effect", pc->dma_page[3], 0x550000);
	check_value ("DMA channel 1 page side effect", pc->dma_page[1], 0xaa0000);
	mem_set_uint8 (pc->prt, 0x82, 0x12);
	mem_set_uint8 (pc->prt, 0x83, 0x34);
	check_value ("DMA page word read follows byte writes", mem_get_uint16_le (pc->prt, 0x82), 0x3412);
	mem_set_uint16_le (pc->prt, 0x8e, 0x5aa5);
	check_value ("DMA page final pair", mem_get_uint16_le (pc->prt, 0x8e), 0x5aa5);
	pc_del (pc);
}

/* Native AT floppy DMA must reach extended RAM; XT retains 20-bit pages. */
static
void check_dma_extended_page (void)
{
	unsigned at;
	for (at=0; at<2; at++) {
		ibmpc_t *pc = new_machine_config(at ? "5170" : "5160", at ? "80286" : "8086", 8);
		mem_blk_t *ext;
		if (!pc) { check_value("DMA allocation",0,1); continue; }
		ext=mem_blk_new(0x480000,0x10000,1);
		if (!ext) { check_value("DMA RAM allocation",0,1); pc_del(pc); continue; }
		mem_add_blk(pc->mem,ext,1);
		mem_set_uint8(pc->mem,0x084d60,0x11);
		mem_set_uint8(pc->mem,0x484d60,0x22);
		mem_set_uint8(pc->prt,0x81,0x48);
		pc->dma.chn[2].memwr(pc->dma.chn[2].memwr_ext,0x4d60,0xff);
		check_value("DMA destination follows machine page width",mem_get_uint8(pc->mem,0x484d60),at ? 0xff : 0x22);
		check_value("AT DMA preserves low-memory alias",mem_get_uint8(pc->mem,0x084d60),at ? 0x11 : 0xff);
		check_value("DMA read uses matching page",pc->dma.chn[2].memrd(pc->dma.chn[2].memrd_ext,0x4d60),0xff);
		pc_del(pc);
	}
}

static
void check_timer_status (const char *system, const char *cpu)
{
	ibmpc_t *pc;
	unsigned i, port, high, low, val;

	pc = new_machine (system, cpu);
	if (pc == NULL) {
		check_value ("timer machine allocation", 0, 1);
		return;
	}
	port = (strcmp (system, "5170") == 0) ? 0x61 : 0x62;
	mem_set_uint8 (pc->prt, 0x61, 1);
	mem_set_uint8 (pc->prt, 0x43, 0xb6);
	mem_set_uint8 (pc->prt, 0x42, 8);
	mem_set_uint8 (pc->prt, 0x42, 0);
	high = low = 0;
	for (i = 0; i < 32; i++) {
		e8253_clock (&pc->pit, 1);
		val = mem_get_uint8 (pc->prt, port) & 0x20;
		high |= val;
		low |= (val == 0);
	}
	check_value ("timer output goes high on status port", high, 0x20);
	check_value ("timer output goes low on status port", low, 1);
	if (port == 0x61) {
		/* One refresh toggle per ch1 pulse, not one per edge. */
		mem_set_uint8 (pc->prt, 0x43, 0x74);
		mem_set_uint8 (pc->prt, 0x41, 8);
		mem_set_uint8 (pc->prt, 0x41, 0);
		for (i = 0; i < 24; i++) {
			unsigned before, out;

			before = mem_get_uint8 (pc->prt, port) & 0x10;
			out = pc->pit.counter[1].out_val;
			e8253_clock (&pc->pit, 1);
			val = mem_get_uint8 (pc->prt, port) & 0x10;
			check_value ("refresh toggles once on rising edge", val ^ before,
				(!out && pc->pit.counter[1].out_val) ? 0x10 : 0);
		}
		/* Gate low forces mode-3 output high, even with speaker muted. */
		mem_set_uint8 (pc->prt, 0x61, 0);
		check_value ("gate low exposes timer high", mem_get_uint8 (pc->prt, port) & 0x20, 0x20);
		val = mem_get_uint8 (pc->prt, port) & 0xf0;
		mem_set_uint8 (pc->prt, port, 0xff);
		check_value ("writes preserve status bits", mem_get_uint8 (pc->prt, port) & 0xf0, val);
		check_value ("writes retain control bits", mem_get_uint8 (pc->prt, port) & 0x0f, 0x0f);
		val = mem_get_uint8 (pc->prt, port);
		for (i = 0; i < 16; i++) {
			check_value ("status polling is passive", mem_get_uint8 (pc->prt, port), val);
		}
	}
	pc_del (pc);
}

static
unsigned cmos_byte (ibmpc_t *pc, unsigned index)
{
	mem_set_uint8 (pc->prt, 0x70, index);
	return (mem_get_uint8 (pc->prt, 0x71));
}

static
void check_cmos_equipment (void)
{
	ibmpc_t *pc;
	ini_sct_t *cfg;
	unsigned i, j, sum;
	static const struct {
		const char *video, *override;
		unsigned count, fdd40, cga40, type0, type1, floppy, equipment;
	} cases[] = {
		{"vga", NULL, 2, 0, 0, 2, 2, 0x22, 0x41},
		{"ega", NULL, 1, 0, 0, 3, 4, 0x30, 0x01},
		{"cga", NULL, 2, 0, 0, 4, 5, 0x45, 0x61},
		{"cga", NULL, 1, 1, 1, 0, 0, 0x10, 0x11},
		{"mda", NULL, 0, 0, 0, 2, 2, 0x00, 0x30},
		{"vga", "mda", 2, 0, 0, 2, 1, 0x21, 0x71},
		{"cga", "vga", 2, 0, 0, 2, 2, 0x22, 0x41},
		{"cga", NULL, 2, 1, 0, 6, 6, 0x14, 0x61}
	};

	for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		cfg = ini_sct_new (NULL);
		test_system = "5170"; test_cpu = "80286";
		ini_set_str (cfg, "system.model", "5170");
		ini_set_uint32 (cfg, "system.floppy_disk_drives", cases[i].count);
		ini_set_uint32 (cfg, "system.fdd40", cases[i].fdd40);
		ini_set_uint32 (cfg, "system.cga40", cases[i].cga40);
		ini_set_str (cfg, "cpu.model", "80286");
		ini_set_uint32 (cfg, "ram.size", 640UL * 1024);
		ini_set_str (cfg, "terminal.driver", "null");
		ini_set_str (cfg, "video.device", cases[i].video);
		if (cases[i].type0) {
			ini_set_uint32 (cfg, "cmos.floppy0_type", cases[i].type0);
			ini_set_uint32 (cfg, "cmos.floppy1_type", cases[i].type1);
		}
		par_video = (char *) cases[i].override;
		pc = pc_new (cfg);
		par_video = NULL;
		if (pc == NULL) {
			check_value ("CMOS machine allocation", 0, 1);
			ini_sct_del (cfg);
			continue;
		}
		check_value ("CMOS floppy types", cmos_byte (pc, 0x10), cases[i].floppy);
		check_value ("CMOS selected display/equipment", cmos_byte (pc, 0x14), cases[i].equipment);
		sum = 0;
		for (j = 0x10; j <= 0x2d; j++) {
			sum += cmos_byte (pc, j);
		}
		check_value ("CMOS checksum includes equipment", (cmos_byte (pc, 0x2e) << 8) |
			cmos_byte (pc, 0x2f), sum);
		mem_set_uint8 (pc->prt, 0x70, 0x10);
		mem_set_uint8 (pc->prt, 0x71, 0x31);
		pc_reset (pc);
		check_value ("reset preserves guest CMOS settings", cmos_byte (pc, 0x10), 0x31);
		pc_del (pc);
	}
}

static
void floppy_seek (ibmpc_t *pc, unsigned drive, unsigned cylinder)
{
	unsigned i;
	unsigned char command[3];

	command[0] = 0x0f;
	command[1] = drive;
	command[2] = cylinder;
	for (i = 0; i < 3; i++) {
		mem_set_uint8 (pc->prt, 0x3f5, command[i]);
		e8272_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2 / 100);
	}
	mem_set_uint8 (pc->prt, 0x3f5, 8);
	e8272_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2 / 100);
	(void) mem_get_uint8 (pc->prt, 0x3f5);
	(void) mem_get_uint8 (pc->prt, 0x3f5);
}

static
void floppy_read_id (ibmpc_t *pc, unsigned rate, unsigned error)
{
	unsigned i, status[7];

	mem_set_uint8 (pc->prt, 0x3f7, rate);
	mem_set_uint8 (pc->prt, 0x3f5, 0x4a);
	mem_set_uint8 (pc->prt, 0x3f5, 0);
	for (i = 0; i < 1000; i++) {
		e8272_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2 / 1000);
		if ((mem_get_uint8 (pc->prt, 0x3f4) & 0xd0) == 0xd0) {
			break;
		}
	}
	check_value ("READ ID reaches result phase", i < 1000, 1);
	for (i = 0; i < 7; i++) {
		status[i] = mem_get_uint8 (pc->prt, 0x3f5);
	}
	check_value ("READ ID rate-dependent termination", status[0] & 0xc0, error ? 0x40 : 0);
	check_value ("READ ID rate-dependent missing address mark", status[1] & 5, error ? 5 : 0);
	check_value ("READ ID sector size code", status[6], error ? 0 : 2);
}

static
void floppy_format_one (ibmpc_t *pc, unsigned rate)
{
	static const unsigned char specify[] = { 3, 0xdf, 3 };
	static const unsigned char format[] = { 0x4d, 0, 2, 1, 0x54, 0xf6 };
	static const unsigned char id[] = { 0, 0, 1, 2 };
	unsigned i, n, status[7];

	mem_set_uint8 (pc->prt, 0x3f7, rate);
	for (i = 0; i < sizeof (specify); i++) {
		mem_set_uint8 (pc->prt, 0x3f5, specify[i]);
	}
	for (i = 0; i < sizeof (format); i++) {
		mem_set_uint8 (pc->prt, 0x3f5, format[i]);
	}
	for (i = 0; i < sizeof (id); i++) {
		for (n = 0; n < 1000; n++) {
			e8272_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2 / 1000);
			if ((mem_get_uint8 (pc->prt, 0x3f4) & 0xc0) == 0x80) break;
		}
		check_value ("FORMAT requests ID byte", n < 1000, 1);
		mem_set_uint8 (pc->prt, 0x3f5, id[i]);
	}
	for (n = 0; n < 1000; n++) {
		e8272_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2 / 1000);
		if ((mem_get_uint8 (pc->prt, 0x3f4) & 0xd0) == 0xd0) break;
	}
	check_value ("FORMAT completes", n < 1000, 1);
	for (i = 0; i < 7; i++) status[i] = mem_get_uint8 (pc->prt, 0x3f5);
	check_value ("FORMAT succeeds", status[0] & 0xc0, 0);
}

static
void check_floppy_density (void)
{
	ibmpc_t *pc;
	disk_t *disk;
	FILE *fp;
	unsigned kind, cylinders, sectors, dd, legacy;

	for (kind = 0; kind < 5; kind++) {
		dd = (kind == 2) || (kind == 3);
		legacy = (kind == 4);
		cylinders = dd ? 40 : 80;
		sectors = dd ? 9 : 15;
		pc = new_machine_config (legacy ? "5160" : "5170", "80286", 8);
		disk = NULL;
		if ((kind == 0) || (kind == 2)) {
			disk = dsk_ram_open (NULL, cylinders * 2 * sectors, cylinders, 2, sectors, 0);
		}
		else {
			fp = tmpfile ();
			if (fp != NULL) {
				if (dsk_psi_create_fp (fp, PSI_FORMAT_RAW, cylinders, 2, sectors) == 0) {
					rewind (fp);
					disk = dsk_psi_open_fp (fp, PSI_FORMAT_RAW, kind != 1);
				}
				fclose (fp);
			}
		}
		check_value ("density fixture allocated", disk != NULL, 1);
		if (disk != NULL) {
			dsk_set_drive (disk, 0);
			check_value ("attach density fixture", dsks_add_disk (pc->dsk, disk), 0);
			mem_set_uint8 (pc->prt, 0x3f2, 0x1c);
			floppy_read_id (pc, 0, dd && !legacy);
			floppy_read_id (pc, 1, !dd && !legacy);
			floppy_read_id (pc, 2, !dd && !legacy);
			floppy_read_id (pc, 0, dd && !legacy);
			if (dd) {
				check_value ("select 300 RPM", dev_fdc_set_rpm (pc->fdc, 0, 300), 0);
				floppy_read_id (pc, 2, 0);
				floppy_read_id (pc, 1, 1);
				check_value ("select 360 RPM", dev_fdc_set_rpm (pc->fdc, 0, 360), 0);
				/* Same CCR: changing RPM must invalidate the track cache. */
				floppy_read_id (pc, 1, 0);
				floppy_read_id (pc, 2, 1);
				check_value ("reject unsupported RPM", dev_fdc_set_rpm (pc->fdc, 0, 720), 1);
				check_value ("invalid RPM preserves setting", pc->fdc->rpm[0], 360);
				dev_fdc_reset (pc->fdc);
				check_value ("reset preserves drive RPM", pc->fdc->rpm[0], 360);
				mem_set_uint8 (pc->prt, 0x3f2, 0x1c);
				floppy_read_id (pc, 1, 0);
				dev_fdc_set_rpm (pc->fdc, 0, 0);
				floppy_read_id (pc, 2, 0);
				floppy_read_id (pc, 1, 0);
			}
			if (kind == 1) {
				psi_sct_t *sector;
				floppy_format_one (pc, 2);
				sector = psi_img_get_sector (((disk_psi_t *) disk->ext)->img, 0, 0, 0, 1);
				check_value ("FORMAT stores DD encoding", sector ? sector->encoding : 0, PSI_ENC_MFM_DD);
				check_value ("FORMAT writes fill byte", sector ? sector->data[0] : 0, 0xf6);
				floppy_read_id (pc, 2, 0);
				floppy_read_id (pc, 0, 1);
				floppy_format_one (pc, 0);
				sector = psi_img_get_sector (((disk_psi_t *) disk->ext)->img, 0, 0, 0, 1);
				check_value ("FORMAT stores HD encoding", sector ? sector->encoding : 0, PSI_ENC_MFM_HD);
				floppy_read_id (pc, 0, 0);
				floppy_read_id (pc, 2, 1);
			}
		}
		pc_del (pc);
	}
	pc = new_machine_config ("5170", "80286", 24);
	check_value ("configuration applies drive RPM", pc->fdc->rpm[0], 360);
	check_value ("other drive retains default RPM", pc->fdc->rpm[1], 0);
	pc_del (pc);

}

static
void check_at_floppy (void)
{
	ibmpc_t *pc;
	disk_t *disk;
	unsigned i;
	static const unsigned long rates[] = { 500000, 300000, 250000 };

	pc = new_machine_config ("5170", "80286", 8);
	check_value ("empty drive reports change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	disk = dsk_ram_open (NULL, 2400, 80, 2, 15, 0);
	check_value ("floppy RAM image allocated", disk != NULL, 1);
	if (disk == NULL) {
		pc_del (pc);
		return;
	}
	dsk_set_drive (disk, 0);
	check_value ("attach RAM floppy", dsks_add_disk (pc->dsk, disk), 0);
	mem_set_uint8 (pc->prt, 0x3f2, 0x1c);
	check_value ("insertion raises change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	check_value ("reading does not acknowledge change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	floppy_seek (pc, 0, 0);
	check_value ("no step cannot clear change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	floppy_seek (pc, 0, 1);
	check_value ("step with media clears change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0);
	mem_set_uint8 (pc->prt, 0x3f2, 0x2d);
	floppy_seek (pc, 1, 1);
	check_value ("empty selected drive stays changed", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	mem_set_uint8 (pc->prt, 0x3f2, 0x1c);
	check_value ("other drive latch preserved", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0);
	dev_fdc_media_changed (pc->fdc, 0);
	mem_set_uint8 (pc->prt, 0x3f2, 0x0c);
	floppy_seek (pc, 0, 2);
	check_value ("motor-off step does not clear change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	mem_set_uint8 (pc->prt, 0x3f2, 0x1c);
	floppy_seek (pc, 0, 1);
	check_value ("motor-on step clears change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0);
	for (i = 0; i < 3; i++) {
		mem_set_uint8 (pc->prt, 0x3f7, i);
		check_value ("CCR selects bit rate", pc->fdc->e8272.data_rate, rates[i]);
		check_value ("CCR write does not change DIR", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0);
	}
	mem_set_uint8 (pc->prt, 0x3f6, 4);
	check_value ("fixed-disk reset leaves floppy latch", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0);
	mem_set_uint8 (pc->prt, 0x3f6, 0);
	pc_reset (pc);
	check_value ("machine reset restores default rate", pc->fdc->e8272.data_rate, 500000);
	check_value ("reset does not invent a disk change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0);
	dsks_rmv_disk (pc->dsk, disk);
	check_value ("ejection raises change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	floppy_seek (pc, 0, 0);
	check_value ("step without media cannot clear change", mem_get_uint8 (pc->prt, 0x3f7) & 0x80, 0x80);
	dsk_del (disk);
	pc_del (pc);
	pc = new_machine_config ("5160", "80286", 8);
	check_value ("XT has no DIR", mem_get_uint8 (pc->prt, 0x3f7), 0xff);
	mem_set_uint8 (pc->prt, 0x3f7, 0);
	check_value ("XT keeps legacy rate", pc->fdc->e8272.data_rate, 250000);
	pc_del (pc);
}

static
void check_at_storage (void)
{
	ibmpc_t *pc;
	unsigned i, j;
	static const unsigned commands[] = { 0, 0x10, 0x20, 0x30, 0x40, 0x50, 0x70, 0xec };
	static const unsigned patterns[] = { 0x55, 0xaa, 0, 0xff };

	pc = new_machine_config ("5170", "80286", 8);
	if (pc == NULL) {
		check_value ("storage machine allocation", 0, 1);
		return;
	}
	check_value ("diskless controller ready bits clear", mem_get_uint8 (pc->prt, 0x1f7), 0);
	check_value ("controller diagnostic result", mem_get_uint8 (pc->prt, 0x1f1), 1);
	for (i = 2; i <= 6; i++) {
		for (j = 0; j < 4; j++) {
			mem_set_uint8 (pc->prt, 0x1f0 + i, patterns[j]);
			check_value ("diskless task register readback",
				mem_get_uint8 (pc->prt, 0x1f0 + i), patterns[j]);
		}
	}
	check_value ("write-only control read", mem_get_uint8 (pc->prt, 0x3f6), 0xff);
	mem_set_uint8 (pc->prt, 0x3f2, 0x0c);
	check_value ("floppy MSR retains ownership", mem_get_uint8 (pc->prt, 0x3f4), 0x80);
	mem_set_uint8 (pc->prt, 0x3f5, 0x08);
	e8272_clock (&pc->fdc->e8272, PCE_IBMPC_CLK2 / 100);
	check_value ("floppy sense interrupt command", mem_get_uint8 (pc->prt, 0x3f4) & 0xc0, 0xc0);
	(void) mem_get_uint8 (pc->prt, 0x3f5);
	(void) mem_get_uint8 (pc->prt, 0x3f5);
	mem_set_uint8 (pc->prt, 0x1f7, 0x90);
	check_value ("diagnostic is asynchronous", mem_get_uint8 (pc->prt, 0x1f7), 0x80);
	check_value ("busy task registers reflect status", mem_get_uint8 (pc->prt, 0x1f4), 0x80);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
	check_value ("diagnostic IRQ14 routed to slave", pc->pic2.irq_inp & 0x40, 0x40);
	check_value ("error read preserves IRQ14", mem_get_uint8 (pc->prt, 0x1f1), 1);
	check_value ("IRQ still pending", pc->pic2.irq_inp & 0x40, 0x40);
	check_value ("diagnostic does not invent a drive", mem_get_uint8 (pc->prt, 0x1f7), 0);
	check_value ("status acknowledges IRQ14", pc->pic2.irq_inp & 0x40, 0);
	for (i = 0; i < sizeof (commands) / sizeof (commands[0]); i++) {
		mem_set_uint8 (pc->prt, 0x1f7, commands[i]);
		wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
		check_value ("no-drive or invalid command fails", mem_get_uint8 (pc->prt, 0x1f7), 1);
		check_value ("no-drive command aborted", mem_get_uint8 (pc->prt, 0x1f1), 4);
		check_value ("no data without DRQ", mem_get_uint16_le (pc->prt, 0x1f0), 0xffff);
	}
	for (i = 0; i < 2; i++) {
		mem_set_uint8 (pc->prt, 0x1f6, 0xa3 | (i << 4));
		mem_set_uint8 (pc->prt, 0x1f2, 17);
		mem_set_uint8 (pc->prt, 0x1f7, 0x91);
		wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
		check_value ("parameters complete with IRQ14", pc->pic2.irq_inp & 0x40, 0x40);
		check_value ("parameters do not assert drive ready", mem_get_uint8 (pc->prt, 0x1f7), 0);
		check_value ("drive head parameter", pc->wd1003->heads[i], 4);
		check_value ("drive sector parameter", pc->wd1003->sectors[i], 17);
	}
	mem_set_uint8 (pc->prt, 0x3f6, 2);
	mem_set_uint8 (pc->prt, 0x1f7, 0x90);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
	check_value ("control masks IRQ14", pc->pic2.irq_inp & 0x40, 0);
	mem_set_uint8 (pc->prt, 0x3f6, 0);
	check_value ("unmask exposes pending IRQ14", pc->pic2.irq_inp & 0x40, 0x40);
	mem_set_uint8 (pc->prt, 0x3f6, 4);
	check_value ("reset clears IRQ14", pc->pic2.irq_inp & 0x40, 0);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2);
	check_value ("held reset stays busy", mem_get_uint8 (pc->prt, 0x1f7), 0x80);
	mem_set_uint8 (pc->prt, 0x1f4, 0x55);
	mem_set_uint8 (pc->prt, 0x3f6, 0);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
	check_value ("reset completes", mem_get_uint8 (pc->prt, 0x1f7), 0);
	check_value ("reset clears cylinder", mem_get_uint8 (pc->prt, 0x1f4), 0);
	check_value ("reset has no command IRQ", pc->pic2.irq_inp & 0x40, 0);
	mem_set_uint8 (pc->prt, 0x1f7, 0x90);
	pc_reset (pc);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2);
	check_value ("machine reset cancels command", pc->pic2.irq_inp & 0x40, 0);
	pc_del (pc);

	pc = new_machine_config ("5160", "80286", 8);
	check_value ("AT adapter not installed on XT", pc->wd1003 != NULL, 0);
	check_value ("XT fixed-disk port remains absent", mem_get_uint8 (pc->prt, 0x1f7), 0xff);
	pc_del (pc);
	pc = new_machine ("5170", "80286");
	check_value ("omitted adapter remains absent", mem_get_uint8 (pc->prt, 0x1f7), 0xff);
	pc_del (pc);
}

static
void check_xt_assisted_reset (void)
{
	ibmpc_t *pc = new_machine_config ("5160", "8086", 1);
	mem_blk_t *rom = mem_blk_new (0xc8000, 8192, 1);
	static const unsigned char entry[] = { 0xfa, 0xb4, 0xd5, 0x9e, 0x73, 0x4a };
	static const unsigned char reset[] = { 0xea, 0x5b, 0xe0, 0, 0xf0 };
	unsigned i, sum, after;

	mem_add_blk (pc->mem, rom, 1);
	mem_set_uint16_le (pc->mem, 0xc8004, 0x4350);
	mem_set_uint16_le (pc->mem, 0xc8006, 0x5845);
	for (i = 0; i < sizeof (entry); i++) mem_set_uint8_rw (pc->mem, 0xfe05b + i, entry[i]);
	for (i = 0; i < sizeof (reset); i++) mem_set_uint8_rw (pc->mem, 0xffff0 + i, reset[i]);
	for (i = 0; i < 8; i++) mem_set_uint8_rw (pc->mem, 0xffff5 + i, "05/09/86"[i]);
	sum = 0;
	for (i = 0; i < 8192; i++) sum += mem_get_uint8 (pc->mem, 0xfe000 + i);
	pc->patch_bios_init = 1;
	pc_reset (pc);
	after = 0;
	for (i = 0; i < 8192; i++) after += mem_get_uint8 (pc->mem, 0xfe000 + i);
	check_value ("PCEX common-entry patch preserves ROM checksum", after & 255, sum & 255);
	check_value ("PCEX common entry is far jump", mem_get_uint8 (pc->mem, 0xfe05b), 0xea);
	check_value ("PCEX common entry offset", mem_get_uint16_le (pc->mem, 0xfe05c), 12);
	check_value ("PCEX common entry segment", mem_get_uint16_le (pc->mem, 0xfe05e), 0xc800);
	for (i = 0; i < sizeof (reset); i++) check_value ("native reset vector unchanged", mem_get_uint8 (pc->mem, 0xffff0 + i), reset[i]);
	pc_reset (pc);
	after = 0;
	for (i = 0; i < 8192; i++) after += mem_get_uint8 (pc->mem, 0xfe000 + i);
	check_value ("PCEX patch idempotent", after & 255, sum & 255);
	pc_del (pc);
}

static
void wd_task (ibmpc_t *pc, unsigned drive, unsigned cylinder,
	unsigned head, unsigned sector, unsigned count, unsigned cmd)
{
	mem_set_uint8 (pc->prt, 0x1f2, count);
	mem_set_uint8 (pc->prt, 0x1f3, sector);
	mem_set_uint8 (pc->prt, 0x1f4, cylinder & 255);
	mem_set_uint8 (pc->prt, 0x1f5, cylinder >> 8);
	mem_set_uint8 (pc->prt, 0x1f6, 0xa0 | (drive << 4) | head);
	mem_set_uint8 (pc->prt, 0x1f7, cmd);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
}

static
void check_wd_media (void)
{
	ibmpc_t *pc = new_machine_config ("5170", "80286", 8 | 32);
	disk_t *disk = pc->wd1003->disk[0];
	disk_t *second = dsk_ram_open (NULL, 544, 16, 2, 17, 0);
	unsigned i, n, bad = 0;
	unsigned char buf[1024];

	check_value ("configured WD1003 disk attached", disk != NULL, 1);
	if (disk == NULL || second == NULL) {
		check_value ("second disk allocated", second != NULL, 1);
		pc_del (pc);
		if (second != NULL) dsk_del (second);
		return;
	}
	check_value ("configured CMOS disk type", pc->at_rtc->data[0x12], 0x20);
	e86_set_dl (pc->cpu, 0x80);
	check_value ("attached WD drive bypasses assisted INT13", pc_int13_check (pc), 0);
	check_value ("second WD drive attach", wd1003_set_disk (pc->wd1003, 1, second), 0);
	wd_task (pc, 0, 0, 1, 1, 17, 0x91);
	(void) mem_get_uint8 (pc->prt, 0x1f7);
	wd_task (pc, 0, 0, 1, 17, 2, 0x30);
	check_value ("first write buffer has no IRQ", pc->pic2.irq_inp & 0x40, 0);
	check_value ("first write DRQ", mem_get_uint8 (pc->prt, 0x1f7), 0x58);
	for (n = 0; n < 2; n++) {
		for (i = 0; i < 256; i++) mem_set_uint16_le (pc->prt, 0x1f0, (i * 37) ^ (n ? 0x5a5a : 0xa5a5));
		check_value ("sector commit asynchronous", mem_get_uint8 (pc->prt, 0x1f7), 0x80);
		wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
		check_value ("write completion IRQ", pc->pic2.irq_inp & 0x40, 0x40);
		check_value ("write next/final status", mem_get_uint8 (pc->prt, 0x1f7), n ? 0x50 : 0x58);
	}
	check_value ("write crosses cylinder", mem_get_uint8 (pc->prt, 0x1f4), 1);
	check_value ("write count exhausted", mem_get_uint8 (pc->prt, 0x1f2), 0);
	check_value ("backend readback", dsk_read_lba (disk, buf, 33, 2), 0);
	for (i = 0; i < 512; i++) {
		unsigned want = ((i & 255) * 37) ^ (i >= 256 ? 0x5a5a : 0xa5a5);
		if ((buf[2*i] | (buf[2*i+1] << 8)) != want) bad++;
	}
	check_value ("host bytes equal guest writes", bad, 0);
	wd_task (pc, 0, 0, 1, 17, 2, 0x20);
	bad = 0;
	for (n = 0; n < 2; n++) {
		check_value ("each read sector IRQ", pc->pic2.irq_inp & 0x40, 0x40);
		check_value ("read DRQ", mem_get_uint8 (pc->prt, 0x1f7), 0x58);
		for (i = 0; i < 256; i++) if (mem_get_uint16_le (pc->prt, 0x1f0) != ((i * 37) ^ (n ? 0x5a5a : 0xa5a5))) bad++;
		if (!n) wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
	}
	check_value ("PIO readback matches", bad, 0);
	check_value ("no extra final read interrupt", pc->pic2.irq_inp & 0x40, 0);
	check_value ("final read ready without DRQ", mem_get_uint8 (pc->prt, 0x1f7), 0x50);
	check_value ("read outside transfer", mem_get_uint16_le (pc->prt, 0x1f0), 0xffff);
	wd_task (pc, 0, 0, 0, 0, 1, 0x20);
	check_value ("sector zero rejected", mem_get_uint8 (pc->prt, 0x1f1), 0x10);
	wd_task (pc, 0, 16, 0, 1, 1, 0x20);
	check_value ("past end rejected", mem_get_uint8 (pc->prt, 0x1f7), 0x51);
	wd_task (pc, 0, 0, 1, 17, 2, 0x40);
	check_value ("verify finishes without DRQ", mem_get_uint8 (pc->prt, 0x1f7), 0x50);
	wd_task (pc, 0, 0, 0, 1, 0, 0x20);
	for (n = 0; n < 256; n++) {
		(void) mem_get_uint8 (pc->prt, 0x1f7);
		for (i = 0; i < 256; i++) (void) mem_get_uint16_le (pc->prt, 0x1f0);
		wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
	}
	check_value ("zero sector count transfers 256 sectors", mem_get_uint8 (pc->prt, 0x1f7), 0x50);
	check_value ("zero count transfer completed", pc->wd1003->remaining, 0);
	wd_task (pc, 1, 0, 0, 1, 1, 0x20);
	(void) mem_get_uint8 (pc->prt, 0x1f7);
	bad = 0;
	for (i = 0; i < 256; i++) bad |= mem_get_uint16_le (pc->prt, 0x1f0);
	check_value ("second drive independent data", bad, 0);
	second->readonly = 1;
	wd_task (pc, 1, 0, 0, 1, 1, 0x30);
	check_value ("readonly write rejected", mem_get_uint8 (pc->prt, 0x1f7), 0x51);
	wd_task (pc, 0, 0, 0, 1, 1, 0x30);
	mem_set_uint16_le (pc->prt, 0x1f0, 0x1234);
	mem_set_uint8 (pc->prt, 0x3f6, 4);
	mem_set_uint8 (pc->prt, 0x3f6, 0);
	wd1003_clock (pc->wd1003, PCE_IBMPC_CLK2 / 100);
	check_value ("reset cancels partial write", dsk_read_lba (disk, buf, 0, 1), 0);
	check_value ("partial write did not reach media", buf[0] | (buf[1] << 8), 0);
	pc_del (pc);
	dsk_del (second);
}

static
void check_shutdown_reset (const char *system)
{
	ibmpc_t *pc = new_machine (system, "80286");
	if (pc == NULL) return;
	mem_set_uint8 (pc->mem, 0x467, 0x5a);
	mem_set_uint8 (pc->prt, 0x70, 0x0f);
	mem_set_uint8 (pc->prt, 0x71, 9);
	pc->cpu->msw |= E286_MSW_PE;
	pc->cpu->state = E86_STATE_SHUTDOWN;
	pc->cpu->delay = 0;
	pc_clock (pc, 1);
	if (strcmp (system, "5170") == 0) {
		check_value ("AT shutdown resets CPU", pc->cpu->msw & E286_MSW_PE, 0);
		check_value ("AT shutdown reset preserves RAM", mem_get_uint8(pc->mem, 0x467), 0x5a);
		mem_set_uint8 (pc->prt, 0x70, 0x0f);
		check_value ("AT shutdown reset preserves CMOS resume code", mem_get_uint8(pc->prt, 0x71), 9);
	}
	else check_value ("XT has no AT shutdown reset circuit", pc->cpu->state, E86_STATE_SHUTDOWN);
	pc_del (pc);
}

static
void check_rtc_periodic (void)
{
	mc146818a_t rtc;
	mc146818a_init(&rtc);
	mc146818a_set_input_clock(&rtc, 32768);
	mc146818a_set_uint8(&rtc, 0x0a, 0x2b);
	mc146818a_set_uint8(&rtc, 0x0b, 0xc2);
	mc146818a_clock(&rtc, 1023);
	check_value("RTC 32 Hz no early event", mc146818a_get_uint8(&rtc, 0x0c), 0);
	mc146818a_clock(&rtc, 1);
	check_value("RTC periodic IRQ even with SET", mc146818a_get_uint8(&rtc, 0x0c), 0xc0);
	check_value("RTC C read acknowledges", mc146818a_get_uint8(&rtc, 0x0c), 0);
	mc146818a_set_uint8(&rtc, 0x0b, 0x82);
	mc146818a_clock(&rtc, 1024);
	check_value("RTC PF without PIE", rtc.data[0x0c], 0x40);
	mc146818a_set_uint8(&rtc, 0x0b, 0xc2);
	check_value("Enabling PIE exposes pending PF", mc146818a_get_uint8(&rtc, 0x0c), 0xc0);
	mc146818a_set_uint8(&rtc, 0x0a, 0x21);
	mc146818a_clock(&rtc, 128);
	check_value("RTC rate 1 aliases 256 Hz", mc146818a_get_uint8(&rtc, 0x0c), 0xc0);
	mc146818a_set_uint8(&rtc, 0x0a, 0x20);
	mc146818a_clock(&rtc, 32768);
	check_value("RTC rate zero disables periodic", mc146818a_get_uint8(&rtc, 0x0c), 0);
	mc146818a_free(&rtc);
}

int main (void)
{
	static const char *systems[] = { "5150", "5160", "m24", "atari-pc" };
	static const char *cpus[] = { "8086", "8088", "80186", "80188", "v20", "v30", "80286" };
	unsigned i, j;

	par_terminal = "null";
	for (i = 0; i < sizeof (systems) / sizeof (systems[0]); i++) {
		for (j = 0; j < sizeof (cpus) / sizeof (cpus[0]); j++) {
			check_xt_machine (systems[i], cpus[j]);
		}
	}
	check_shutdown_reset ("5170");
	check_shutdown_reset ("5160");
	check_rtc_periodic ();
	check_at_floppy ();
	check_at_storage ();
	check_wd_media ();
	check_xt_assisted_reset ();
	check_cmos_equipment ();
	check_at_dma_page_words ();
	check_dma_extended_page ();
	check_timer_status ("5170", "80286");
	check_timer_status ("5160", "8088");
	check_timer_status ("5160", "80286");
	check_at_machine ();
	check_floppy_density ();
	check_at_test_inputs ();
	check_at_commands ();
	check_at_delivery ();
	check_at_protocol ();
	check_at_host_write ();
	check_a20_machine ("5160");
	check_a20_machine ("5170");
	check_xms_a20 ("8086");
	check_xms_a20 ("80286");
	check_reset_rom ("5160", "8086", 1);
	check_reset_rom ("5160", "8088", 1);
	check_reset_rom ("5160", "80286", 1);
	check_reset_rom ("5170", "80286", 1);
	check_reset_rom ("5170", "80286", 3);
	check_reset_rom ("5160", "80286", 5);
	check_reset_rom ("m24", "80286", 1);
	check_reset_rom ("atari-pc", "80286", 1);

	printf ("%u machine I/O checks, %u failures\n", checks, failures);
	return (failures != 0);
}
