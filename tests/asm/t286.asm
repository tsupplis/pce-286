;-----------------------------------------------------------------------------
; tests/asm/test_286sys.asm
;
; End-to-end regression test for 80286 system-register instructions in
; real mode:  LGDT / SGDT / LIDT / SIDT / LMSW / SMSW round-trips.
;
; Runs under IBM DOS on CPU model 80286.
; Exits via PCE_HOOK_ABORT with BX=0 (pass) or BX=1 (fail).
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o tests/asm/test_286sys.com \
;        tests/asm/test_286sys.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	286
	org	0x100

start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax

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
	; Test 1: SMSW — read initial MSW value
	; Expected: PE bit (bit 0) clear in real mode
	; ================================================================
	smsw	ax
	test	ax, 0x0001		; test PE bit
	jz	.t1_pass
	inc	bx
.t1_pass:

	; ================================================================
	; Test 2: LMSW / SMSW round-trip (set MP and EM bits)
	; ================================================================
	mov	ax, 0x0006		; MP=1, EM=1
	lmsw	ax
	smsw	cx
	and	cx, 0x0006
	cmp	cx, 0x0006
	je	.t2_pass
	inc	bx
.t2_pass:

	; Clear EM bit (MP stays; PE stays if already set)
	mov	ax, 0x0002		; MP=1, EM=0
	lmsw	ax
	smsw	cx
	test	cx, 0x0004		; EM should be clear
	jz	.t2b_pass
	inc	bx
.t2b_pass:

	; ================================================================
	; Test 3: PE bit is sticky — LMSW cannot clear it once set
	; ================================================================
	; First set PE
	mov	ax, 0x0001
	lmsw	ax
	; Try to clear PE
	xor	ax, ax
	lmsw	ax
	smsw	cx
	test	cx, 0x0001		; PE must still be set
	jnz	.t3_pass
	inc	bx
.t3_pass:

	; ================================================================
	; Test 4: LGDT / SGDT round-trip
	; Load a known GDT descriptor, store it back, verify bytes match.
	;
	; DTR layout at [gdt_src]: limit(2), base_lo(1), base_mid(1), base_hi(1)
	; ================================================================
	lgdt	[gdt_src]		; load from gdt_src
	sgdt	[gdt_out]		; store to gdt_out

	; Compare limit
	mov	ax, [gdt_src]
	cmp	ax, [gdt_out]
	je	.t4a_pass
	inc	bx
.t4a_pass:

	; Compare base bytes
	mov	al, [gdt_src + 2]
	cmp	al, [gdt_out + 2]
	je	.t4b_pass
	inc	bx
.t4b_pass:

	mov	al, [gdt_src + 3]
	cmp	al, [gdt_out + 3]
	je	.t4c_pass
	inc	bx
.t4c_pass:

	mov	al, [gdt_src + 4]
	cmp	al, [gdt_out + 4]
	je	.t4d_pass
	inc	bx
.t4d_pass:

	; ================================================================
	; Test 5: LIDT / SIDT round-trip
	; ================================================================
	lidt	[idt_src]
	sidt	[idt_out]

	mov	ax, [idt_src]
	cmp	ax, [idt_out]
	je	.t5a_pass
	inc	bx
.t5a_pass:

	mov	al, [idt_src + 2]
	cmp	al, [idt_out + 2]
	je	.t5b_pass
	inc	bx
.t5b_pass:

	mov	al, [idt_src + 3]
	cmp	al, [idt_out + 3]
	je	.t5c_pass
	inc	bx
.t5c_pass:

	mov	al, [idt_src + 4]
	cmp	al, [idt_out + 4]
	je	.t5d_pass
	inc	bx
.t5d_pass:

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

; ================================================================
; Data
; ================================================================

; Source GDT descriptor: limit=0x07FF, base=0x0B8000
gdt_src:
	dw	0x07FF		; limit
	db	0x00		; base [7:0]
	db	0x80		; base [15:8]
	db	0x0B		; base [23:16]

; Output area for SGDT (6 bytes — PCE writes 5 usable bytes)
gdt_out:
	times	6 db 0

; Source IDT descriptor: limit=0x03FF, base=0x000000
idt_src:
	dw	0x03FF		; limit
	db	0x00		; base [7:0]
	db	0x00		; base [15:8]
	db	0x00		; base [23:16]

; Output area for SIDT
idt_out:
	times	6 db 0
