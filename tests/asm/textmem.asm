;-----------------------------------------------------------------------------
; tests/asm/textmem.asm
;
; End-to-end regression test for T5-C: extended memory above 1 MB.
;
; Verifies the complete INT 15h AH=87h / AH=88h path with a real [ram]
; block above 0x100000 in the emulator config:
;
;   1. INT 15h AH=88h — query extended memory size: must return > 0.
;   2. INT 15h AH=87h — copy 512 bytes from conventional to 0x110000
;      (just above HMA, safe whether hma=0 or hma=1).
;   3. INT 15h AH=87h — copy 512 bytes back from 0x110000 to conventional.
;   4. Compare: the round-trip data must match the original.
;
; The test does NOT require pcehimem.sys or DOS=HIGH.  It talks to the
; BIOS INT 15h hook directly (pc_int_15 in hook.c).
;
; Exits via PCE_HOOK_ABORT BX=0 (pass) or BX=1 (fail).
;
; Requires: cpu.model=80286, [ram] address=0x110000 size>=512 in config.
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o tests/asm/textmem.com \
;        tests/asm/textmem.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	286
	org	0x100

EXTMEM_PHYS	equ	0x110000	; physical dest: just above HMA
BUF_SIZE	equ	512		; bytes to copy (must be even)

start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax

	; ================================================================
	; Confirm running under PCE
	; ================================================================
	xor	ax, ax
	db	0xd5, 0x00
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c01
	int	0x21
.pce_ok:

	xor	bx, bx			; BX = error count

	; ================================================================
	; Test 1: INT 15h AH=88h — extended memory size must be > 0
	; ================================================================
	mov	ah, 0x88
	int	0x15
	jnc	.t1_cfok
	inc	bx			; CF=1 means error
.t1_cfok:
	or	ax, ax
	jnz	.t1_pass
	inc	bx			; AX=0 means no extended memory reported
.t1_pass:

	; ================================================================
	; Test 2: Fill source buffer with known pattern
	; ================================================================
	mov	cx, BUF_SIZE
	mov	di, src_buf
	xor	ax, ax
.fill:
	mov	[di], al
	inc	di
	inc	al
	loop	.fill

	; ================================================================
	; Test 3: INT 15h AH=87h — copy src_buf → physical 0x110000
	; Descriptor table layout (48 bytes at gdt_buf):
	;   +0x00: dummy descriptor (zeros)
	;   +0x08: dummy descriptor (zeros)
	;   +0x10: source descriptor: base = linear addr of src_buf
	;   +0x18: dest   descriptor: base = EXTMEM_PHYS
	;   +0x20: dummy descriptor (zeros)
	;   +0x28: dummy descriptor (zeros)
	; ================================================================

	; Compute linear address of src_buf: (CS << 4) + offset
	mov	ax, cs
	mov	dx, ax
	shl	ax, 4
	xor	cx, cx
	add	ax, src_buf
	adc	cx, 0			; CX:AX = 20-bit linear of src_buf

	; Fill source descriptor at gdt_buf+0x12/0x14
	mov	[gdt_buf + 0x12], ax		; base lo16
	mov	byte [gdt_buf + 0x14], cl	; base hi8
	mov	byte [gdt_buf + 0x15], 0x93	; access byte (present, RW data)
	mov	word [gdt_buf + 0x10], BUF_SIZE - 1	; limit

	; Fill dest descriptor at gdt_buf+0x1a/0x1c
	mov	word [gdt_buf + 0x18], BUF_SIZE - 1	; limit
	mov	word [gdt_buf + 0x1a], EXTMEM_PHYS & 0xffff	; base lo16
	mov	byte [gdt_buf + 0x1c], (EXTMEM_PHYS >> 16) & 0xff ; base hi8
	mov	byte [gdt_buf + 0x1d], 0x93	; access byte

	mov	ah, 0x87
	mov	cx, BUF_SIZE / 2	; word count
	mov	si, gdt_buf
	int	0x15
	jnc	.t3_pass
	inc	bx			; CF=1 means block move failed
.t3_pass:

	; ================================================================
	; Test 4: INT 15h AH=87h — copy back: physical 0x110000 → dst_buf
	; Swap src and dst descriptors
	; ================================================================

	; Compute linear address of dst_buf
	mov	ax, cs
	shl	ax, 4
	xor	cx, cx
	add	ax, dst_buf
	adc	cx, 0

	; Source = EXTMEM_PHYS
	mov	word [gdt_buf + 0x10], BUF_SIZE - 1
	mov	word [gdt_buf + 0x12], EXTMEM_PHYS & 0xffff
	mov	byte [gdt_buf + 0x14], (EXTMEM_PHYS >> 16) & 0xff
	mov	byte [gdt_buf + 0x15], 0x93

	; Dest = dst_buf
	mov	word [gdt_buf + 0x18], BUF_SIZE - 1
	mov	[gdt_buf + 0x1a], ax
	mov	[gdt_buf + 0x1c], cl
	mov	byte [gdt_buf + 0x1d], 0x93

	mov	ah, 0x87
	mov	cx, BUF_SIZE / 2
	mov	si, gdt_buf
	int	0x15
	jnc	.t4_pass
	inc	bx
.t4_pass:

	; ================================================================
	; Test 5: Compare src_buf and dst_buf — must be identical
	; ================================================================
	mov	cx, BUF_SIZE
	mov	si, src_buf
	mov	di, dst_buf
.cmp:
	mov	al, [si]
	cmp	al, [di]
	je	.cmp_ok
	inc	bx
	jmp	.cmp_done
.cmp_ok:
	inc	si
	inc	di
	loop	.cmp
.cmp_done:

	; ================================================================
	; Exit
	; ================================================================
	cmp	bx, 0
	je	.pass
	mov	bx, 1
.pass:
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	mov	ah, 0x4c
	mov	al, bl
	int	0x21

; ----------------------------------------------------------------
; Data

gdt_buf:	times 48 db 0		; 6 × 8-byte descriptors, zero-filled

src_buf:	times BUF_SIZE db 0
dst_buf:	times BUF_SIZE db 0
