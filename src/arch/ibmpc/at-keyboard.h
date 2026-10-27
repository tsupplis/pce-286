/* IBM 84-key AT keyboard command protocol; no motherboard dependencies. */
#ifndef PCE_IBMPC_AT_KEYBOARD_H
#define PCE_IBMPC_AT_KEYBOARD_H 1

#define AT_KEYBOARD_REPLIES 16
#define AT_KEYBOARD_CLEAR_SCAN 1

typedef struct {
	unsigned char command;
	unsigned char scanning;
	unsigned char resume_scanning;
	unsigned char leds;
	unsigned char typematic;
	unsigned char last;
	unsigned char last_valid;
	unsigned head;
	unsigned count;
	unsigned char reply[AT_KEYBOARD_REPLIES];
} at_keyboard_t;

void at_keyboard_init (at_keyboard_t *kbd);
/* Returns -1 when replies are full, otherwise flags for the scan producer. */
int at_keyboard_write (at_keyboard_t *kbd, unsigned char val);
/* Transfer a reply to the controller; returns nonzero when no reply is ready. */
int at_keyboard_read (at_keyboard_t *kbd, unsigned char *val);
/* Record a scan byte transmitted by the separate scan producer for resend. */
void at_keyboard_sent (at_keyboard_t *kbd, unsigned char val);

#endif
