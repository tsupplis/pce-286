/* IBM 84-key AT keyboard command protocol. See status.md for remaining timing
 * and scan-code work; this module does not implement the electrical interface. */
#include "at-keyboard.h"

#include <string.h>

static
void at_keyboard_defaults (at_keyboard_t *kbd)
{
	kbd->leds = 0;
	kbd->typematic = 0x2b;
}

void at_keyboard_init (at_keyboard_t *kbd)
{
	memset (kbd, 0, sizeof (*kbd));
	at_keyboard_defaults (kbd);
	kbd->scanning = 1;
}

static
void at_keyboard_reply (at_keyboard_t *kbd, unsigned char val)
{
	kbd->reply[(kbd->head + kbd->count) % AT_KEYBOARD_REPLIES] = val;
	kbd->count += 1;
}

void at_keyboard_sent (at_keyboard_t *kbd, unsigned char val)
{
	if (val != 0xfe) {
		kbd->last = val;
		kbd->last_valid = 1;
	}
}

int at_keyboard_read (at_keyboard_t *kbd, unsigned char *val)
{
	if (kbd->count == 0) {
		return (1);
	}
	*val = kbd->reply[kbd->head];
	kbd->head = (kbd->head + 1) % AT_KEYBOARD_REPLIES;
	kbd->count -= 1;
	at_keyboard_sent (kbd, *val);
	return (0);
}

int at_keyboard_write (at_keyboard_t *kbd, unsigned char val)
{
	int clear;

	clear = (val == 0xff) || ((val >= 0xf4) && (val <= 0xf6));
	if (!clear && (kbd->count == AT_KEYBOARD_REPLIES)) {
		return (-1);
	}
	if (clear) {
		kbd->head = 0;
		kbd->count = 0;
	}

	if (kbd->command && (val < 0xed)) {
		if (((kbd->command == 0xed) && (val > 7)) ||
			((kbd->command == 0xf3) && (val & 0x80)))
		{
			at_keyboard_reply (kbd, 0xfe);
			return (0);
		}
		if (kbd->command == 0xed) {
			kbd->leds = val;
		}
		else {
			kbd->typematic = val;
		}
		kbd->command = 0;
		kbd->scanning = kbd->resume_scanning;
		at_keyboard_reply (kbd, 0xfa);
		return (0);
	}

	/* A new command cancels a parameter transaction. Scanning stays stopped
	 * until enabled explicitly, as specified for the original AT keyboard. */
	kbd->command = 0;
	switch (val) {
	case 0xff:
		at_keyboard_defaults (kbd);
		kbd->scanning = 1;
		at_keyboard_reply (kbd, 0xfa);
		at_keyboard_reply (kbd, 0xaa);
		break;
	case 0xfe:
		at_keyboard_reply (kbd, kbd->last_valid ? kbd->last : 0xfe);
		break;
	case 0xf6:
		at_keyboard_defaults (kbd);
		at_keyboard_reply (kbd, 0xfa);
		break;
	case 0xf5:
		at_keyboard_defaults (kbd);
		kbd->scanning = 0;
		at_keyboard_reply (kbd, 0xfa);
		break;
	case 0xf4:
		kbd->scanning = 1;
		at_keyboard_reply (kbd, 0xfa);
		break;
	case 0xf3:
	case 0xed:
		kbd->command = val;
		kbd->resume_scanning = kbd->scanning;
		kbd->scanning = 0;
		at_keyboard_reply (kbd, 0xfa);
		break;
	case 0xee:
		at_keyboard_reply (kbd, 0xee);
		break;
	default:
		/* EF-F2 and F7-FD are acknowledged NOPs on the 84-key keyboard.
		 * Enhanced-keyboard ID and scan-set commands are a separate model. */
		at_keyboard_reply (kbd, (val >= 0xef) ? 0xfa : 0xfe);
		break;
	}
	return (clear ? AT_KEYBOARD_CLEAR_SCAN : 0);
}
