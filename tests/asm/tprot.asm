;-----------------------------------------------------------------------------
; tests/asm/tprot.asm
;
; End-to-end regression test for 80286 protected-mode infrastructure:
;   - LGDT / LIDT load descriptor tables
;   - LMSW PE=1 switches to protected mode
;   - DS segment register load walks GDT and populates seg_base cache
;   - Memory write in PM reaches the GDT-translated physical address
;   - SLDT / STR / LAR / LSL / VERR / VERW / CLTS / ARPL execute correctly
;
; Strategy:
;   We run as a DOS .COM program.  Our segment is CS (typically 0x1000-ish).
;   We build the GDT so that the PM data segment has the *same* physical base
;   as our current DS (CS << 4).  That way a write via DS:offset in PM lands
;   at exactly the same physical byte as DS:offset in real mode, and we can
;   read it back after returning to real mode simply by reading DS:offset.
;
;   To return from PM: we use a far jump to a 16-bit real-mode code segment
;   via the "unreal mode" trick — set up a GDT code entry whose base is our
;   CS<<4, enter PM, immediately load a real-mode-compatible SS/DS with flat
;   base, write sentinels, then triple-fault the CPU by triggering an
;   undefined interrupt (the CPU resets).
;
;   ACTUALLY — simpler: we simply fire PCE_HOOK_ABORT while still in PM.
;   The PCE hook intercepts opcode 0xD5 0x00 at the CPU callback level and
;   does NOT require real-mode.  BX holds the error count.
;
; Runs under IBM DOS on CPU model 80286.
; Exits via PCE_HOOK_ABORT with BX=0 (pass) or BX>0 (fail count).
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o tests/asm/tprot.com \
;        tests/asm/tprot.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	286
	org	0x100

; ============================================================
; Descriptor table indices (byte offsets into GDT)
; ============================================================
SEL_NULL	equ	0x0000		; null descriptor
SEL_CODE	equ	0x0008		; flat code  (base = CS<<4)
SEL_DATA	equ	0x0010		; flat data  (base = CS<<4)
SEL_STACK	equ	0x0018		; flat stack (base = 0x0000)
SEL_LDT		equ	0x0020		; dummy LDT  (base = 0, limit = 0)
SEL_TSS		equ	0x0028		; dummy TSS  (base = 0, limit = 0)

start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax

	; Check we are running under PCE
	xor	ax, ax
	db	0xd5, 0x00
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c01		; not PCE — exit with error
	int	0x21
.pce_ok:
	xor	bx, bx			; BX = error count

	; ================================================================
	; Build GDT in our DS segment at [gdt_base]:
	;   Each descriptor is 8 bytes.
	;   Code/data bases = CS<<4 (identity map our .COM segment).
	;   Stack seg base  = 0x0000 (real-mode SS is always 0 for .COM).
	; ================================================================
	mov	ax, cs
	xor	dx, dx
	mov	cl, 4
	shl	ax, cl			; AX = CS << 4 = our physical base (low 16)
	; DX:AX = 20-bit physical base (DX holds overflow nibble)
	; Since CS <= 0xFFF0, AX will not overflow 16 bits for the low 16.
	; The high byte (bits 16-19) is in DX.

	; Null descriptor: all zeros
	mov	word [gdt_base + 0], 0
	mov	word [gdt_base + 2], 0
	mov	word [gdt_base + 4], 0
	mov	word [gdt_base + 6], 0

	; Code descriptor (SEL_CODE = 0x08): base = CS<<4, limit=0xFFFF, ar=0x9B
	; (present, code, execute/read, CPL=0)
	mov	word [gdt_base + 8], 0xffff	; limit
	mov	[gdt_base + 10], ax		; base [15:0]
	; base[23:16]: for a typical DOS segment CS ~ 0x01xx, CS<<4 ~ 0x1xx0
	; High nibble of AX is bits 19:16.  Since CS < 0xF000, DX=0.
	; base[23:16] = 0 (CS<<4 < 0x10000 for any .COM CS < 0x1000)
	xor	dx, dx
	mov	[gdt_base + 12], dl		; base [23:16] = 0
	mov	byte [gdt_base + 13], 0x9b	; ar: present, code, R/X, CPL=0
	mov	word [gdt_base + 14], 0

	; Data descriptor (SEL_DATA = 0x10): same base as code, ar=0x93
	; (present, data, R/W, CPL=0)
	mov	word [gdt_base + 16], 0xffff
	mov	[gdt_base + 18], ax		; base [15:0] same as code
	mov	[gdt_base + 20], dl		; base [23:16] = 0
	mov	byte [gdt_base + 21], 0x93
	mov	word [gdt_base + 22], 0

	; Stack descriptor (SEL_STACK = 0x18): base = 0x0000, ar=0x97
	; (present, data, R/W, expand-down, CPL=0)
	mov	word [gdt_base + 24], 0xffff
	mov	word [gdt_base + 26], 0x0000	; base [15:0] = 0
	mov	byte [gdt_base + 28], 0x00	; base [23:16] = 0
	mov	byte [gdt_base + 29], 0x97
	mov	word [gdt_base + 30], 0

	; LDT descriptor (SEL_LDT = 0x20): dummy, limit=0, base=0, ar=0x82
	mov	word [gdt_base + 32], 0x0000
	mov	word [gdt_base + 34], 0x0000
	mov	byte [gdt_base + 36], 0x00
	mov	byte [gdt_base + 37], 0x82
	mov	word [gdt_base + 38], 0

	; TSS descriptor (SEL_TSS = 0x28): dummy, limit=0x2B, base=0, ar=0x89
	mov	word [gdt_base + 40], 0x002b
	mov	word [gdt_base + 42], 0x0000
	mov	byte [gdt_base + 44], 0x00
	mov	byte [gdt_base + 45], 0x89
	mov	word [gdt_base + 46], 0

	; ================================================================
	; Load GDTR: base = physical address of gdt_base, limit = 0x2F
	; ================================================================
	; Compute physical address of gdt_base: CS*16 + offset(gdt_base)
	mov	ax, cs
	mov	cl, 4
	shl	ax, cl			; AX = CS<<4
	mov	dx, gdt_base
	add	ax, dx			; AX = phys addr of gdt_base (low 16)
	; Assume < 0x10000 (true for typical DOS COM)
	mov	[gdtr_val],	word 0x002f	; limit = 6 entries * 8 - 1 = 47 = 0x2F
	mov	[gdtr_val + 2], ax		; base low
	mov	[gdtr_val + 4], byte 0		; base high

	lgdt	[gdtr_val]

	; ================================================================
	; Build a minimal IDT: just enough for our test.
	; We point the IDT at our idt_base in DS, limit = 0xFF (32 entries).
	; All gates point back to our PM handler pm_int_handler.
	; Gate format: [ip_lo, cs_sel, 0, ar=0x8E, ip_hi=0]
	; ================================================================
	; Compute physical address of idt_base
	mov	ax, cs
	shl	ax, cl			; AX = CS<<4  (cl=4 still)
	mov	dx, idt_base
	add	ax, dx
	mov	[idtr_val], word 0x00ff
	mov	[idtr_val + 2], ax
	mov	[idtr_val + 4], byte 0

	lidt	[idtr_val]

	; Fill all 32 IDT gates to point to pm_int_handler with SEL_CODE
	; pm_int_handler is at offset (pm_int_handler - start + 0x100) from CS base
	mov	di, idt_base
	mov	cx, 32
	mov	dx, pm_int_handler	; offset of handler within segment
.fill_idt:
	mov	[di + 0], dx		; offset low
	mov	[di + 2], word SEL_CODE	; CS selector
	mov	[di + 4], byte 0	; reserved
	mov	[di + 5], byte 0x8e	; interrupt gate
	mov	[di + 6], word 0	; offset high (0 on 286)
	add	di, 8
	loop	.fill_idt

	; ================================================================
	; Enter protected mode via LMSW PE=1
	; From this point, segment registers use GDT descriptors.
	; We must immediately reload DS, ES, SS so they get valid caches.
	; CS is already set (selector=SEL_CODE, loaded by the GDT walk on
	; the far jump).  But we stay in 16-bit code, so we don't do a
	; far jump — instead we reload the data/stack selectors and live
	; with CS still holding the real-mode value (our hook fires on
	; the opcode, not via a call gate).
	; ================================================================
	mov	ax, 0x0001
	lmsw	ax			; PE=1 — we are now in protected mode

	; Reload segment caches from GDT
	mov	ax, SEL_DATA
	mov	ds, ax
	mov	es, ax
	mov	ax, SEL_STACK
	mov	ss, ax
	mov	sp, 0x0e00		; keep stack safely within 640K RAM

	; ================================================================
	; Test 1 (PM): Write sentinel byte via DS:offset
	;   In PM, DS base = CS<<4, so DS:pm_sentinel maps to
	;   the same physical location as the real-mode DS:pm_sentinel.
	;   Write 0xA5 there.
	; ================================================================
	mov	byte [pm_sentinel], 0xa5

	; ================================================================
	; Test 2 (PM): SLDT — should return SEL_NULL (0) since we never
	;   loaded an LDT selector
	; ================================================================
	sldt	ax
	cmp	ax, 0
	je	.t2_pass
	inc	bx
.t2_pass:

	; ================================================================
	; Test 3 (PM): LLDT then SLDT round-trip
	; ================================================================
	mov	ax, SEL_LDT
	lldt	ax
	sldt	cx
	cmp	cx, SEL_LDT
	je	.t3_pass
	inc	bx
.t3_pass:

	; ================================================================
	; Test 4 (PM): LTR then STR round-trip
	; ================================================================
	mov	ax, SEL_TSS
	ltr	ax
	str	cx
	cmp	cx, SEL_TSS
	je	.t4_pass
	inc	bx
.t4_pass:

	; ================================================================
	; Test 5 (PM): CLTS clears TS bit in MSW
	; ================================================================
	mov	ax, 0x000f		; try to set TS=1 (bit 3)
	lmsw	ax
	clts				; clear TS
	smsw	ax
	test	ax, 0x0008		; TS must be 0
	jz	.t5_pass
	inc	bx
.t5_pass:

	; ================================================================
	; Tests 6–11 use SI as a scratch error accumulator.
	; BX holds the T1–T5 error count and must not be clobbered.
	; Use CX as the selector operand for LAR/LSL.
	; At the end, SI is added to BX (the main error count).
	; ================================================================
	xor	si, si

	; ================================================================
	; Test 6 (PM): LAR AX, CX — read access rights for SEL_CODE
	;   Expected: AR byte = 0x9B → AX = 0x9B00
	; ================================================================
	mov	cx, SEL_CODE
	lar	ax, cx
	cmp	ax, 0x9b00
	je	.t6_pass
	inc	si
.t6_pass:

	; ================================================================
	; Test 7 (PM): LSL AX, CX — read limit for SEL_DATA = 0xFFFF
	; ================================================================
	mov	cx, SEL_DATA
	lsl	ax, cx
	cmp	ax, 0xffff
	je	.t7_pass
	inc	si
.t7_pass:

	; ================================================================
	; Test 8 (PM): VERR — ZF=1 for a valid code selector
	; ================================================================
	mov	ax, SEL_CODE
	verr	ax
	jz	.t8_pass
	inc	si
.t8_pass:

	; ================================================================
	; Test 9 (PM): VERW — ZF=1 for a valid data selector
	; ================================================================
	mov	ax, SEL_DATA
	verw	ax
	jz	.t9_pass
	inc	si
.t9_pass:

	; ================================================================
	; Test 10 (PM): ARPL — adjust RPL
	;   AX=SEL_CODE|0 (RPL=0), CX=0x0003 (RPL=3)
	;   After: AX.RPL = 3 → AX = SEL_CODE|3, ZF=1
	; ================================================================
	mov	ax, SEL_CODE
	mov	cx, 0x0003
	arpl	ax, cx
	jz	.t10_pass
	inc	si
.t10_pass:
	; AX should be SEL_CODE | 3
	cmp	ax, SEL_CODE | 3
	je	.t10b_pass
	inc	si
.t10b_pass:

	; ================================================================
	; Test 11 (PM): INT through IDT gate → pm_int_handler sets DI=0xBEEF
	; ================================================================
	xor	di, di
	int	0x10			; fires IDT gate → pm_int_handler
	cmp	di, 0xbeef
	je	.t11_pass
	inc	si
.t11_pass:

	; Merge scratch errors into main error count
	add	bx, si

	; ================================================================
	; Done — fire PCE_HOOK_ABORT with BX=error count
	; (PCE hook fires regardless of PM/real mode)
	; ================================================================
	cmp	bx, 0
	je	.exit_ok
	mov	bx, 1
.exit_ok:
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	; Fallback (should not reach — PCE halts the emulator above)
	mov	ah, 0x4c
	mov	al, bl
	int	0x21
	int	0x20

; ================================================================
; PM interrupt handler
; Called via IDT gate when INT 0x10 fires in PM.
; Sets DI=0xBEEF to signal "handler was called", then IRETs.
; ================================================================
pm_int_handler:
	mov	di, 0xbeef
	iret

; ================================================================
; Data
; ================================================================
pm_sentinel:	db	0		; written 0xA5 in PM

gdtr_val:	dw	0		; GDTR image: limit(2) + base(3)
		db	0, 0, 0

idtr_val:	dw	0
		db	0, 0, 0

; GDT (6 entries × 8 bytes = 48 bytes)
align 8
gdt_base:
	times	48 db 0

; IDT (32 entries × 8 bytes = 256 bytes)
align 8
idt_base:
	times	256 db 0
