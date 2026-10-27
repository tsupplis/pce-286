;-----------------------------------------------------------------------------
; tests/asm/tprot_int.asm  — minimal PM + INT test
; Enters PM, reloads DS/SS, fires INT 0x10 via IDT, verifies DI=0xBEEF.
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

	; Check PCE
	xor	ax, ax
	db	0xd5, 0x00
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c01
	int	0x21
.pce_ok:
	xor	bx, bx

	; Build GDT: CS<<4 identity-mapped
	mov	ax, cs
	mov	cl, 4
	shl	ax, cl			; AX = CS<<4

	; Null
	mov	word [gdt_base + 0], 0
	mov	word [gdt_base + 2], 0
	mov	word [gdt_base + 4], 0
	mov	word [gdt_base + 6], 0
	; Code
	mov	word [gdt_base + 8],  0xffff
	mov	[gdt_base + 10], ax
	mov	byte [gdt_base + 12], 0x00
	mov	byte [gdt_base + 13], 0x9b
	mov	word [gdt_base + 14], 0
	; Data
	mov	word [gdt_base + 16], 0xffff
	mov	[gdt_base + 18], ax
	mov	byte [gdt_base + 20], 0x00
	mov	byte [gdt_base + 21], 0x93
	mov	word [gdt_base + 22], 0
	; Stack (base=0)
	mov	word [gdt_base + 24], 0xffff
	mov	word [gdt_base + 26], 0x0000
	mov	byte [gdt_base + 28], 0x00
	mov	byte [gdt_base + 29], 0x97
	mov	word [gdt_base + 30], 0

	; LGDT
	mov	ax, cs
	shl	ax, cl
	mov	dx, gdt_base
	add	ax, dx
	mov	[gdtr_val],     word 0x001f
	mov	[gdtr_val + 2], ax
	mov	[gdtr_val + 4], byte 0
	lgdt	[gdtr_val]

	; Build IDT: all gates → pm_int_handler, SEL_CODE
	mov	ax, cs
	shl	ax, cl
	mov	dx, idt_base
	add	ax, dx
	mov	[idtr_val],     word 0x00ff
	mov	[idtr_val + 2], ax
	mov	[idtr_val + 4], byte 0
	lidt	[idtr_val]

	mov	di, idt_base
	mov	cx, 32
	mov	dx, pm_int_handler
.fill:
	mov	[di + 0], dx
	mov	[di + 2], word SEL_CODE
	mov	[di + 4], byte 0
	mov	[di + 5], byte 0x8e
	mov	[di + 6], word 0
	add	di, 8
	loop	.fill

	; Enter PM
	mov	ax, 0x0001
	lmsw	ax

	; Reload segment caches
	mov	ax, SEL_DATA
	mov	ds, ax
	mov	es, ax
	mov	ax, SEL_STACK
	mov	ss, ax

	; Fire INT 0x10
	xor	di, di
	int	0x10

	; Verify DI = 0xBEEF
	cmp	di, 0xbeef
	je	.t1_pass
	inc	bx
.t1_pass:

	; Done
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	mov	ah, 0x4c
	mov	al, bl
	int	0x21

pm_int_handler:
	mov	di, 0xbeef
	iret

gdtr_val:	dw 0
		db 0, 0, 0
idtr_val:	dw 0
		db 0, 0, 0

align 8
gdt_base:
	times	32 db 0

align 8
idt_base:
	times	256 db 0
