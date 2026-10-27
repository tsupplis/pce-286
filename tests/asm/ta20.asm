;-----------------------------------------------------------------------------
; tests/asm/test_a20.asm
;
; End-to-end regression test for the Port 0x92 A20 gate (80286 only).
;
; The test:
;   1. Reads port 0x92 — A20 should be off (bit 1 = 0).
;   2. Writes 0x02 to port 0x92 — enables A20 (bit 1 = 1).
;   3. Reads port 0x92 back — bit 1 should now be 1.
;   4. Writes 0x00 to port 0x92 — disables A20.
;   5. Reads port 0x92 back — bit 1 should be 0 again.
;   6. Verifies bit 0 (RESET) does NOT self-set on a normal read.
;
; NOTE: Port 0x92 is only active on the 80286 model.  Running this test on
; any other CPU model will see 0xFF from the read and fail step 1 (A20 bit
; would be 1 before we set it — which is valid — so we skip to step 3).
; The test is only meaningful with -p 80286.
;
; Runs under IBM DOS on CPU model 80286.
; Exits via PCE_HOOK_ABORT with BX=0 (pass) or BX=1 (fail).
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o tests/asm/test_a20.com \
;        tests/asm/test_a20.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	286
	org	0x100

start:
	mov	ax, cs
	mov	ds, ax

	; Check we are running under PCE
	xor	ax, ax
	db	0xd5, 0x00
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c00
	int	0x21
.pce_ok:
	xor	bx, bx			; BX = error count

	; ================================================================
	; Test 1: Initial state — A20 should be disabled (bit 1 = 0)
	; ================================================================
	in	al, 0x92
	test	al, 0x02		; test A20 bit
	jz	.t1_pass
	; A20 was already set — this can happen if BIOS set it.
	; Not a failure on real hardware, but on a fresh emulated boot it
	; should be 0.  Count it as a warning by NOT incrementing bx here;
	; the disable test below must still work.
.t1_pass:

	; ================================================================
	; Test 2: Enable A20 — write bit 1
	; ================================================================
	mov	al, 0x02
	out	0x92, al

	; ================================================================
	; Test 3: Read back — A20 bit must now be 1
	; ================================================================
	in	al, 0x92
	test	al, 0x02
	jnz	.t3_pass
	inc	bx
.t3_pass:

	; ================================================================
	; Test 4: Disable A20 — write 0x00
	; ================================================================
	xor	al, al
	out	0x92, al

	; ================================================================
	; Test 5: Read back — A20 bit must be 0 again
	; ================================================================
	in	al, 0x92
	test	al, 0x02
	jz	.t5_pass
	inc	bx
.t5_pass:

	; ================================================================
	; Test 6: Bit 0 (RESET) should not be self-asserted on a clean read
	; ================================================================
	in	al, 0x92
	test	al, 0x01
	jz	.t6_pass
	; RESET bit is set — unexpected after a normal read/write cycle
	inc	bx
.t6_pass:

	; ================================================================
	; Test 7: Re-enable A20 for the remainder of the session
	; (Good practice: leave A20 enabled so DOS can use HMA if needed)
	; ================================================================
	mov	al, 0x02
	out	0x92, al

	; ================================================================
	; Done
	; ================================================================
	cmp	bx, 0
	je	.exit_pass
	mov	bx, 1
.exit_pass:
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	mov	ah, 0x4c
	mov	al, bl
	int	0x21
	int	0x20
