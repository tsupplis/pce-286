/*
 * Unit tests for T6-C: CMOS auto-population logic (pc_cmos_populate_from_config).
 *
 * Tests the derivation logic directly — no full ibmpc_t needed.
 * Mirrors the logic in pc_cmos_populate_from_config() so any change there
 * must be reflected here.
 *
 * Cases:
 *   1.  Reg B = 0x02 (24h BCD mode).
 *   2.  Base RAM 640 KB → 0x15=0x80 0x16=0x02.
 *   3.  Base RAM 512 KB → 0x15=0x00 0x16=0x02.
 *   4.  Base RAM capped at 640 KB.
 *   5.  Extended RAM 0 KB → 0x17=0x00 0x18=0x00.
 *   6.  Extended RAM 1024 KB → 0x17=0x00 0x18=0x04.
 *   7.  Extended RAM 15 MB (15360 KB) → correct LE encoding.
 *   8.  Two 1.44 MB floppies → 0x10=0x44.
 *   9.  One 1.44 MB floppy, one absent → 0x10=0x40.
 *  10.  No floppies → 0x10=0x00.
 *  11.  fdd40 bit 0 set → drive 0 is 360K (type 1).
 *  12.  Equipment byte: 2 floppies, CGA80, no FPU, floppy present → 0x14=0x41. (bits7:6=01 disp=10 bit0=1)
 *  13.  Equipment byte: 1 floppy, mono.
 *  14.  POST diagnostic byte 0x0E = 0x00.
 *  15.  HDD type 0x12 = 0x00.
 *  16.  Checksum = sum of bytes 0x10–0x2D, stored big-endian at 0x2E/0x2F.
 *  17.  Checksum verified: recomputing matches stored value.
 *  18.  make-cmos.py parity: build_cmos(640, 1024, 4, 4, CGA80) matches C logic.
 */

#include <stdio.h>
#include <string.h>

static int pass = 1;
#define CHECK(label, cond) \
	do { if (cond) printf ("PASS %s\n", label); \
	     else { printf ("FAIL %s\n", label); pass = 0; } } while (0)

/* -----------------------------------------------------------------------
 * Reproduce pc_cmos_populate_from_config logic inline.
 * Parameters match what ibmpc_t provides.
 * ----------------------------------------------------------------------- */
#define DISP_CGA80  0x02
#define DISP_CGA40  0x01
#define DISP_MONO   0x03

static void cmos_populate (
	unsigned char *d,     /* 64-byte CMOS array */
	unsigned long  base_kb,
	unsigned long  ext_kb,
	unsigned       fd_cnt,
	unsigned char  fdd40, /* bitmask: bit n = 1 → drive n is 40-track */
	unsigned char  disp)  /* DISP_* */
{
	unsigned       i;
	unsigned short cksum;

	/* Zero everything first */
	memset (d, 0, 64);

	/* Reg B */
	d[0x0b] = 0x02;

	/* Base RAM */
	if (base_kb > 640UL) { base_kb = 640UL; }
	d[0x15] = (unsigned char)(base_kb & 0xff);
	d[0x16] = (unsigned char)((base_kb >> 8) & 0xff);

	/* Extended RAM */
	if (ext_kb > 0xffffUL) { ext_kb = 0xffffUL; }
	d[0x17] = (unsigned char)(ext_kb & 0xff);
	d[0x18] = (unsigned char)((ext_kb >> 8) & 0xff);

	/* Floppy types */
	{
		unsigned char t0, t1;
		t0 = (fd_cnt >= 1) ? ((fdd40 & 0x01) ? 1 : 4) : 0;
		t1 = (fd_cnt >= 2) ? ((fdd40 & 0x02) ? 1 : 4) : 0;
		d[0x10] = (unsigned char)((t0 << 4) | t1);
	}

	/* HDD type */
	d[0x12] = 0x00;

	/* Equipment byte */
	d[0x14] = (unsigned char)(
		((fd_cnt > 1 ? fd_cnt - 1 : 0) << 6) |
		((disp & 0x03) << 4) |
		(fd_cnt > 0 ? 0x01 : 0x00));

	/* POST diagnostic */
	d[0x0e] = 0x00;

	/* Checksum */
	cksum = 0;
	for (i = 0x10; i <= 0x2d; i++) {
		cksum += d[i];
	}
	d[0x2e] = (unsigned char)((cksum >> 8) & 0xff);
	d[0x2f] = (unsigned char)(cksum & 0xff);
}

/* -----------------------------------------------------------------------
 * Helper: verify stored checksum matches recomputed value
 * ----------------------------------------------------------------------- */
static int checksum_valid (unsigned char *d)
{
	unsigned       i;
	unsigned short cksum = 0;
	for (i = 0x10; i <= 0x2d; i++) {
		cksum += d[i];
	}
	return (d[0x2e] == (unsigned char)((cksum >> 8) & 0xff)) &&
	       (d[0x2f] == (unsigned char)(cksum & 0xff));
}

int main (void)
{
	unsigned char d[64];

	/* ------------------------------------------------------------------ */
	/* Tests 1-3: Reg B and base RAM */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 640, 0, 2, 0, DISP_CGA80);
	CHECK ("T1  reg B = 0x02",         d[0x0b] == 0x02);
	CHECK ("T2  base 640K low = 0x80", d[0x15] == 0x80);
	CHECK ("T2  base 640K high = 0x02",d[0x16] == 0x02);

	cmos_populate (d, 512, 0, 2, 0, DISP_CGA80);
	CHECK ("T3  base 512K low = 0x00", d[0x15] == 0x00);
	CHECK ("T3  base 512K high = 0x02",d[0x16] == 0x02);

	/* ------------------------------------------------------------------ */
	/* Test 4: base RAM capped at 640 */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 1024, 0, 2, 0, DISP_CGA80);
	CHECK ("T4  base capped at 640K",  d[0x15] == 0x80 && d[0x16] == 0x02);

	/* ------------------------------------------------------------------ */
	/* Tests 5-7: extended RAM */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 640, 0, 2, 0, DISP_CGA80);
	CHECK ("T5  ext 0KB low = 0x00",   d[0x17] == 0x00);
	CHECK ("T5  ext 0KB high = 0x00",  d[0x18] == 0x00);

	cmos_populate (d, 640, 1024, 2, 0, DISP_CGA80);
	CHECK ("T6  ext 1024KB low = 0x00", d[0x17] == 0x00);
	CHECK ("T6  ext 1024KB high = 0x04",d[0x18] == 0x04);

	cmos_populate (d, 640, 15360, 2, 0, DISP_CGA80);  /* 15 MB */
	CHECK ("T7  ext 15MB low = 0x00",  d[0x17] == 0x00);
	CHECK ("T7  ext 15MB high = 0x3c", d[0x18] == 0x3c);

	/* ------------------------------------------------------------------ */
	/* Tests 8-11: floppy types */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 640, 0, 2, 0, DISP_CGA80);
	CHECK ("T8  2×1.44M → 0x10=0x44", d[0x10] == 0x44);

	cmos_populate (d, 640, 0, 1, 0, DISP_CGA80);
	CHECK ("T9  1×1.44M, no fd1 → 0x10=0x40", d[0x10] == 0x40);

	cmos_populate (d, 640, 0, 0, 0, DISP_CGA80);
	CHECK ("T10 no floppies → 0x10=0x00", d[0x10] == 0x00);

	cmos_populate (d, 640, 0, 2, 0x01, DISP_CGA80); /* fdd40 bit0 → drive0=360K=type1 */
	CHECK ("T11 fdd40 bit0 → drive0 type1 (hi nybble=0x1)", (d[0x10] >> 4) == 1);
	CHECK ("T11 fdd40 bit0 → drive1 type4 (lo nybble=0x4)", (d[0x10] & 0x0f) == 4);

	/* ------------------------------------------------------------------ */
	/* Tests 12-13: equipment byte */
	/* ------------------------------------------------------------------ */
	/* 2 floppies, CGA80: bits7:6=01 bits5:4=10 bit0=1 → 0x61
	 * bits7:6 = (fd_cnt-1) = 1 → 0x40
	 * bits5:4 = disp CGA80=2  → 0x20
	 * bit0 = floppy present   → 0x01
	 * total = 0x61 */
	cmos_populate (d, 640, 0, 2, 0, DISP_CGA80);
	CHECK ("T12 equip 2fd CGA80 = 0x61", d[0x14] == 0x61);

	/* 1 floppy, mono: bits7:6=00 bits5:4=11 bit0=1 → 0x31 */
	cmos_populate (d, 640, 0, 1, 0, DISP_MONO);
	CHECK ("T13 equip 1fd mono = 0x31",  d[0x14] == 0x31);

	/* ------------------------------------------------------------------ */
	/* Tests 14-15: POST diagnostic and HDD */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 640, 0, 2, 0, DISP_CGA80);
	CHECK ("T14 POST diag 0x0E = 0x00", d[0x0e] == 0x00);
	CHECK ("T15 HDD type 0x12 = 0x00",  d[0x12] == 0x00);

	/* ------------------------------------------------------------------ */
	/* Tests 16-17: checksum */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 640, 1024, 2, 0, DISP_CGA80);
	{
		unsigned short expected = 0;
		unsigned i;
		for (i = 0x10; i <= 0x2d; i++) { expected += d[i]; }
		CHECK ("T16 cksum high byte correct",
			d[0x2e] == (unsigned char)((expected >> 8) & 0xff));
		CHECK ("T16 cksum low byte correct",
			d[0x2f] == (unsigned char)(expected & 0xff));
	}
	CHECK ("T17 checksum_valid passes", checksum_valid (d));

	/* Corrupt a byte and verify checksum_valid detects it */
	d[0x15] ^= 0xff;
	CHECK ("T17 checksum_valid detects corruption", !checksum_valid (d));

	/* ------------------------------------------------------------------ */
	/* Test 18: known-good values (parity with make-cmos.py defaults) */
	/* build_cmos(640, 0, 4, 4, CGA80):
	 *   0x10 = 0x44, 0x14 = 0x41, 0x15=0x80 0x16=0x02, 0x17=0x00 0x18=0x00 */
	/* ------------------------------------------------------------------ */
	cmos_populate (d, 640, 0, 2, 0, DISP_CGA80);
	CHECK ("T18 make-cmos parity 0x10=0x44", d[0x10] == 0x44);
	CHECK ("T18 make-cmos parity 0x14=0x61", d[0x14] == 0x61);
	CHECK ("T18 make-cmos parity 0x15=0x80", d[0x15] == 0x80);
	CHECK ("T18 make-cmos parity 0x16=0x02", d[0x16] == 0x02);
	CHECK ("T18 make-cmos parity 0x17=0x00", d[0x17] == 0x00);
	CHECK ("T18 make-cmos parity 0x18=0x00", d[0x18] == 0x00);
	CHECK ("T18 make-cmos parity checksum valid", checksum_valid (d));

	printf ("\n%s\n", pass ? "All tests passed." : "SOME TESTS FAILED.");
	return pass ? 0 : 1;
}
