;-----------------------------------------------------------------------------
; tests/asm/tprot_min.asm
;
; Minimal protected-mode smoke test.
; Enters PM with identity-mapped CS, writes a sentinel byte via DS,
; then exits via PCE_HOOK_ABORT.  NO INT, NO LLDT, NO STR, nothing fancy.
; BX=0 → pass.
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin \
;        -o tests/asm/tprot_min.com tests/asm/tprot_min.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	286
	org	0x100

SEL_CODE  equ 0x0008
SEL_DATA  equ 0x0010
SEL_STACK equ 0x0018

start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax
	mov	ss, ax
	mov	sp, 0x0ffe		; safe stack top

	; Verify running under PCE
	xor	ax, ax
	db	0xd5, 0x00
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c01
	int	0x21
.pce_ok:
	xor	bx, bx			; BX = error count

	; Build GDT at [gdt_base]: CS<<4 identity-mapped
	mov	ax, cs
	mov	cl, 4
	shl	ax, cl			; AX = CS<<4 (low 16)

	; Null descriptor
	mov	word [gdt_base + 0], 0
	mov	word [gdt_base + 2], 0
	mov	word [gdt_base + 4], 0
	mov	word [gdt_base + 6], 0

	; Code (SEL_CODE=0x08): base=CS<<4, limit=0xFFFF, ar=0x9B
	mov	word [gdt_base + 8],  0xffff
	mov	[gdt_base + 10], ax
	mov	byte [gdt_base + 12], 0x00
	mov	byte [gdt_base + 13], 0x9b
	mov	word [gdt_base + 14], 0

	; Data (SEL_DATA=0x10): same base, ar=0x93
	mov	word [gdt_base + 16], 0xffff
	mov	[gdt_base + 18], ax
	mov	byte [gdt_base + 20], 0x00
	mov	byte [gdt_base + 21], 0x93
	mov	word [gdt_base + 22], 0

	; Stack (SEL_STACK=0x18): base=CS<<4, ar=0x97
	mov	word [gdt_base + 24], 0xffff
	mov	[gdt_base + 26], ax
	mov	byte [gdt_base + 28], 0x00
	mov	byte [gdt_base + 29], 0x97
	mov	word [gdt_base + 30], 0

	; LGDT
	mov	ax, cs
	shl	ax, cl
	mov	dx, gdt_base
	add	ax, dx
	mov	[gdtr_val],     word 0x001f	; limit = 4 entries - 1 = 31
	mov	[gdtr_val + 2], ax
	mov	[gdtr_val + 4], byte 0
	lgdt	[gdtr_val]

	; Enter PM
	mov	ax, 0x0001
	lmsw	ax

	; Reload DS/ES/SS from GDT
	mov	ax, SEL_DATA
	mov	ds, ax
	mov	es, ax
	mov	ax, SEL_STACK
	mov	ss, ax

	; Test 1: write sentinel via DS
	mov	byte [sentinel], 0xa5

	; Verify (must read back via DS too)
	mov	al, [sentinel]
	cmp	al, 0xa5
	je	.t1_pass
	inc	bx
.t1_pass:

	; Done
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	mov	ah, 0x4c
	mov	al, bl
	int	0x21

sentinel:	db 0

gdtr_val:	dw 0
		db 0, 0, 0

align 8
gdt_base:
	times	32 db 0
