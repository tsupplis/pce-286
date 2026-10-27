/* Test the production 84-key AT command device, not a copied controller stub. */
#include "arch/ibmpc/at-keyboard.h"

#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

static
void check (const char *name, unsigned got, unsigned want)
{
	checks += 1;
	if (got != want) {
		printf ("FAIL %s: got=%x want=%x\n", name, got, want);
		failures += 1;
	}
}

static
void reply (at_keyboard_t *kbd, unsigned want)
{
	unsigned char val;

	val = 0;
	check ("response available", at_keyboard_read (kbd, &val), 0);
	check ("response byte", val, want);
}

static
void commands (void)
{
	at_keyboard_t kbd;
	unsigned char val;
	unsigned i;

	at_keyboard_init (&kbd);
	check ("scanning starts enabled", kbd.scanning, 1);
	check ("empty device has no phantom reply", at_keyboard_read (&kbd, &val), 1);
	check ("reset requests scan buffer clear", at_keyboard_write (&kbd, 0xff), AT_KEYBOARD_CLEAR_SCAN);
	reply (&kbd, 0xfa);
	reply (&kbd, 0xaa);
	at_keyboard_write (&kbd, 0xfe);
	reply (&kbd, 0xaa);
	at_keyboard_write (&kbd, 0xed);
	reply (&kbd, 0xfa);
	check ("LED parameter suspends scanning", kbd.scanning, 0);
	at_keyboard_write (&kbd, 5);
	reply (&kbd, 0xfa);
	check ("LED value retained", kbd.leds, 5);
	check ("LED parameter resumes scanning", kbd.scanning, 1);
	at_keyboard_write (&kbd, 0xf3);
	reply (&kbd, 0xfa);
	at_keyboard_write (&kbd, 0x80);
	reply (&kbd, 0xfe);
	check ("bad typematic parameter remains pending", kbd.command, 0xf3);
	at_keyboard_write (&kbd, 0x20);
	reply (&kbd, 0xfa);
	check ("typematic parameter retained", kbd.typematic, 0x20);
	at_keyboard_write (&kbd, 0xf5);
	reply (&kbd, 0xfa);
	check ("F5 disables scanning", kbd.scanning, 0);
	check ("F5 restores default LEDs", kbd.leds, 0);
	check ("F5 restores typematic default", kbd.typematic, 0x2b);
	at_keyboard_write (&kbd, 0xf6);
	reply (&kbd, 0xfa);
	check ("F6 preserves disabled scanning", kbd.scanning, 0);
	at_keyboard_write (&kbd, 0xf4);
	reply (&kbd, 0xfa);
	check ("F4 enables scanning", kbd.scanning, 1);
	at_keyboard_write (&kbd, 0xf6);
	reply (&kbd, 0xfa);
	check ("F6 preserves enabled scanning", kbd.scanning, 1);

	/* Original 84-key firmware acknowledges these as NOPs. */
	for (i = 0xef; i <= 0xfd; i++) {
		if ((i >= 0xf3) && (i <= 0xf6)) {
			continue;
		}
		at_keyboard_write (&kbd, i);
		reply (&kbd, 0xfa);
		check ("84-key NOP preserves scanning", kbd.scanning, 1);
	}
	at_keyboard_write (&kbd, 0xed);
	reply (&kbd, 0xfa);
	at_keyboard_write (&kbd, 0xee);
	reply (&kbd, 0xee);
	check ("new command cancels parameter", kbd.command, 0);
	check ("cancelled parameter leaves scanning stopped", kbd.scanning, 0);
	at_keyboard_write (&kbd, 0xf4);
	reply (&kbd, 0xfa);
	at_keyboard_sent (&kbd, 0x1e);
	at_keyboard_write (&kbd, 0);
	reply (&kbd, 0xfe);
	at_keyboard_write (&kbd, 0xfe);
	reply (&kbd, 0x1e);
	at_keyboard_write (&kbd, 0xfe);
	reply (&kbd, 0x1e);
}

static
void buffering (void)
{
	at_keyboard_t kbd, saved;
	unsigned i, j;

	at_keyboard_init (&kbd);
	for (j = 0; j < 3; j++) {
		for (i = 0; i < AT_KEYBOARD_REPLIES; i++) {
			check ("reply queue accepts capacity", at_keyboard_write (&kbd, 0xee), 0);
		}
		saved = kbd;
		check ("full queue refuses another command", at_keyboard_write (&kbd, 0xed) == -1, 1);
		check ("full queue refusal is atomic", memcmp (&saved, &kbd, sizeof (kbd)), 0);
		for (i = 0; i < AT_KEYBOARD_REPLIES; i++) {
			reply (&kbd, 0xee);
		}
	}
	at_keyboard_write (&kbd, 0xed);
	at_keyboard_write (&kbd, 0xff);
	check ("reset replaces unfinished command", kbd.command, 0);
	check ("reset restores scanning", kbd.scanning, 1);
	reply (&kbd, 0xfa);
	reply (&kbd, 0xaa);
	check ("reset leaves no stale responses", kbd.count, 0);
}

int main (void)
{
	commands ();
	buffering ();
	printf ("%u AT keyboard protocol checks, %u failures\n", checks, failures);
	return (failures != 0);
}
