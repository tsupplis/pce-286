;=============================================================================
; demos/pmshow.asm
;
; 80286 Protected-Mode Visual Demo
;
; Demonstrates all major PM features with labelled output written directly
; to CGA video RAM (physical 0xB8000) so output works while in PM.
;
; Features demonstrated:
;   1. GDT / IDT loaded (SGDT / SIDT round-trip)
;   2. LMSW PE=1 confirmed (SMSW)
;   3. PM memory sentinel write+read
;   4. SLDT / LLDT round-trip
;   5. STR (task register)
;   6. SMSW / CLTS (clear TS bit)
;   7. LAR (load access rights)
;   8. LSL (load segment limit)
;   9. VERR + VERW (segment verify)
;  10. ARPL (adjust RPL)
;  11. INT via IDT gate
;  12. Task switch via CALL FAR to TSS + IRET back
;  13. Error count summary
;  14. PCE_HOOK_ABORT (pass/fail)
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o demos/pmshow.com demos/pmshow.asm
;
; Run:
;   make demo           (SDL window, visual output)
;   make demo-headless  (headless, exit 0=pass)
;=============================================================================

%include "pce.inc"

	cpu	286
	org	0x100

; ============================================================
; Selector constants (GDT byte offsets)
; ============================================================
SEL_NULL	equ	0x0000
SEL_CODE	equ	0x0008	; code  base=CS<<4  limit=FFFF  ar=9B
SEL_DATA	equ	0x0010	; data  base=CS<<4  limit=FFFF  ar=93
SEL_STACK	equ	0x0018	; stack base=0000   limit=FFFF  ar=97
SEL_LDT		equ	0x0020	; dummy LDT                     ar=82
SEL_CURR_TSS	equ	0x0028	; current task TSS              ar=89
SEL_DEMO_TSS	equ	0x0030	; demo task TSS                 ar=89
SEL_VIDEO	equ	0x0038	; CGA text buffer 0xB8000       ar=93

GDT_ENTRIES	equ	8	; 8 descriptors × 8 bytes = 64 bytes
GDTR_LIMIT	equ	(GDT_ENTRIES * 8 - 1)

IDT_GATES	equ	32	; 32 gates (INT 0..31) × 8 bytes = 256 bytes
IDTR_LIMIT	equ	(IDT_GATES * 8 - 1)

CGA_COLS	equ	80
CGA_ROWS	equ	25

; Delay loop: ~5,000,000 inner iterations at speed=24 ≈ 1 second
; Two nested 16-bit loops: outer × inner = total
DELAY_OUTER	equ	10
DELAY_INNER	equ	0	; 0 = 65536 in dec cx / jnz loop

; ============================================================
; Phase 0 — real mode: PCE check, screen clear, GDT/IDT build
; ============================================================
start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax

	; PCE presence check
	xor	ax, ax
	db	0xd5, 0x00		; PCE_HOOK_CHECK
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c01		; not PCE — exit with error
	int	0x21
.pce_ok:

	; Clear screen (INT 10h AH=0 AL=3: set 80×25 colour text mode)
	mov	ax, 0x0003
	int	0x10

	; Print banner in real mode via INT 10h teletype (AH=0Eh)
	mov	si, banner
	call	print_rm

	; Pause for keypress before starting (INT 16h AH=0)
	xor	ax, ax
	int	0x16

	; ----------------------------------------------------------------
	; Build GDT at runtime
	; Compute CS<<4 = physical base of our .COM segment
	; ----------------------------------------------------------------
	mov	ax, cs
	xor	dx, dx
	mov	cl, 4
	shl	ax, cl			; AX = CS<<4 (low 16), DX = 0

	; Null descriptor
	mov	word [gdt_base +  0], 0
	mov	word [gdt_base +  2], 0
	mov	word [gdt_base +  4], 0
	mov	word [gdt_base +  6], 0

	; SEL_CODE (0x08): base=CS<<4, limit=FFFF, ar=9B
	mov	word [gdt_base +  8], 0xffff
	mov	[gdt_base + 10], ax
	mov	byte [gdt_base + 12], dl	; base[23:16] = 0
	mov	byte [gdt_base + 13], 0x9b
	mov	word [gdt_base + 14], 0

	; SEL_DATA (0x10): base=CS<<4, limit=FFFF, ar=93
	mov	word [gdt_base + 16], 0xffff
	mov	[gdt_base + 18], ax
	mov	byte [gdt_base + 20], dl
	mov	byte [gdt_base + 21], 0x93
	mov	word [gdt_base + 22], 0

	; SEL_STACK (0x18): base=0, limit=FFFF, ar=97
	mov	word [gdt_base + 24], 0xffff
	mov	word [gdt_base + 26], 0x0000
	mov	byte [gdt_base + 28], 0x00
	mov	byte [gdt_base + 29], 0x97
	mov	word [gdt_base + 30], 0

	; SEL_LDT (0x20): base=0, limit=0, ar=82
	mov	word [gdt_base + 32], 0x0000
	mov	word [gdt_base + 34], 0x0000
	mov	byte [gdt_base + 36], 0x00
	mov	byte [gdt_base + 37], 0x82
	mov	word [gdt_base + 38], 0

	; SEL_CURR_TSS (0x28): base = phys(curr_tss), limit=0x2B, ar=89
	; Compute physical address: CS<<4 + offset(curr_tss)
	push	ax			; save CS<<4
	mov	bx, curr_tss
	add	ax, bx			; AX = phys(curr_tss) low 16
	adc	dl, 0			; DL = phys(curr_tss) high byte
	mov	word [gdt_base + 40], 0x002b
	mov	[gdt_base + 42], ax
	mov	[gdt_base + 44], dl
	mov	byte [gdt_base + 45], 0x89
	mov	word [gdt_base + 46], 0
	pop	ax			; restore CS<<4
	xor	dl, dl

	; SEL_DEMO_TSS (0x30): base = phys(demo_tss), limit=0x2B, ar=89
	mov	bx, demo_tss
	add	ax, bx
	adc	dl, 0
	mov	word [gdt_base + 48], 0x002b
	mov	[gdt_base + 50], ax
	mov	[gdt_base + 52], dl
	mov	byte [gdt_base + 53], 0x89
	mov	word [gdt_base + 54], 0
	; restore CS<<4 for SEL_VIDEO
	mov	ax, cs
	shl	ax, cl			; AX = CS<<4 again
	xor	dl, dl

	; SEL_VIDEO (0x38): base=0xB8000, limit=0x0FFF, ar=93
	mov	word [gdt_base + 56], 0x0fff
	mov	word [gdt_base + 58], 0x8000	; base[15:0]
	mov	byte [gdt_base + 60], 0x0b	; base[23:16]
	mov	byte [gdt_base + 61], 0x93
	mov	word [gdt_base + 62], 0

	; ----------------------------------------------------------------
	; Compute GDTR physical base = CS<<4 + offset(gdt_base)
	; ----------------------------------------------------------------
	mov	bx, gdt_base
	add	ax, bx			; AX = phys(gdt_base) low 16
	adc	dl, 0
	mov	[gdtr_val], word GDTR_LIMIT
	mov	[gdtr_val + 2], ax
	mov	[gdtr_val + 4], dl
	lgdt	[gdtr_val]

	; ----------------------------------------------------------------
	; Build IDT: 32 interrupt gates → pm_stub_handler via SEL_CODE
	; Gate format: [off_lo(2), cs_sel(2), 0(1), ar=0x8E(1), off_hi=0(2)]
	; ----------------------------------------------------------------
	mov	ax, cs
	shl	ax, cl			; AX = CS<<4
	xor	dl, dl
	mov	bx, idt_base
	add	ax, bx
	adc	dl, 0
	mov	[idtr_val], word IDTR_LIMIT
	mov	[idtr_val + 2], ax
	mov	[idtr_val + 4], dl

	; Fill all IDT gates
	mov	di, idt_base
	mov	cx, IDT_GATES
	mov	dx, pm_stub_handler	; offset in our segment
.fill_idt:
	mov	[di + 0], dx		; offset low
	mov	[di + 2], word SEL_CODE
	mov	byte [di + 4], 0
	mov	byte [di + 5], 0x8e	; interrupt gate, present
	mov	word [di + 6], 0
	add	di, 8
	loop	.fill_idt

	lidt	[idtr_val]

	; ----------------------------------------------------------------
	; Pre-populate demo_tss so task switch puts AX=0xCAFE visible
	; demo_task_entry runs in the "new task" after CALL FAR to TSS.
	; On task switch: all regs loaded from demo_tss image.
	; TSS field offsets (286 TSS):
	;   +0x0E IP, +0x10 FLAGS, +0x12 AX, +0x24 CS, +0x26 SS, +0x28 DS
	;   +0x1A SP
	; ----------------------------------------------------------------
	; We leave AX=0 in demo_tss; demo_task_entry itself writes demo_result.
	; What matters: IP, CS, SS, SP, FLAGS must be valid for the new task.
	mov	ax, cs
	shl	ax, cl			; CS<<4 — but CS selector in PM = SEL_CODE
	xor	ax, ax			; zero AX again

	mov	word [demo_tss + 0x0E], demo_task_entry	; IP
	mov	word [demo_tss + 0x10], 0x0202		; FLAGS (IF=1)
	mov	word [demo_tss + 0x12], 0x0000		; AX = 0
	mov	word [demo_tss + 0x1A], 0x0d00		; SP (safe stack)
	mov	word [demo_tss + 0x22], SEL_DATA	; ES
	mov	word [demo_tss + 0x24], SEL_CODE	; CS
	mov	word [demo_tss + 0x26], SEL_STACK	; SS
	mov	word [demo_tss + 0x28], SEL_DATA	; DS
	mov	word [demo_tss + 0x2A], 0x0000		; LDTR

	; ----------------------------------------------------------------
	; Load task register with SEL_CURR_TSS (requires PM already active?
	; No — LTR is legal in real mode on 286. But we're about to enter PM.
	; Load it right after LMSW below.)
	; ----------------------------------------------------------------

	; ================================================================
	; Enter protected mode
	; ================================================================
	mov	ax, 0x0001
	lmsw	ax			; PE=1

	; Far jump to flush CS descriptor cache from GDT (CS←SEL_CODE)
	db	0xEA			; JMP FAR imm16:imm16
	dw	pm_cs_reload		; offset
	dw	SEL_CODE		; new CS selector
pm_cs_reload:

	; Reload segment caches from GDT
	mov	ax, SEL_DATA
	mov	ds, ax
	mov	ax, SEL_VIDEO
	mov	es, ax			; ES → CGA text buffer
	mov	ax, SEL_STACK
	mov	ss, ax
	mov	sp, 0x0e00

	; Load task register now we are in PM
	mov	ax, SEL_CURR_TSS
	ltr	ax

	; Initialise CGA cursor to top-left
	mov	byte [ds:cursor_row], 0
	mov	byte [ds:cursor_col], 0

	; Initialise PM error accumulator (DI = error count)
	xor	di, di

	; Print banner line via CGA
	mov	si, pm_banner
	call	print_pm
	call	newline_pm
	call	newline_pm

	; ================================================================
	; Step 1: SGDT / SIDT — verify descriptor table registers loaded
	; ================================================================
	mov	si, s1_label
	call	print_pm

	; Check gdtr_limit field in memory (we know it = GDTR_LIMIT)
	mov	ax, [ds:gdtr_val]	; should be GDTR_LIMIT
	call	print_hex4
	mov	si, s_slash
	call	print_pm
	sidt	[ds:idtr_chk]
	mov	ax, [ds:idtr_chk]	; should be IDTR_LIMIT
	call	print_hex4

	mov	ax, [ds:gdtr_val]
	cmp	ax, GDTR_LIMIT
	jne	.s1_fail
	mov	ax, [ds:idtr_chk]
	cmp	ax, IDTR_LIMIT
	je	.s1_ok
.s1_fail:
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s1_done
.s1_ok:
	mov	si, s_ok
	call	print_pm
.s1_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d1_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 2: SMSW — confirm PE=1
	; ================================================================
	mov	si, s2_label
	call	print_pm
	smsw	ax
	call	print_hex4
	test	ax, 0x0001		; PE bit
	jnz	.s2_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s2_done
.s2_ok:
	mov	si, s_ok
	call	print_pm
.s2_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d2_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 3: PM memory sentinel write + read-back
	; ================================================================
	mov	si, s3_label
	call	print_pm
	mov	byte [ds:pm_sentinel], 0xa5
	mov	al, [ds:pm_sentinel]
	xor	ah, ah
	call	print_hex4
	cmp	al, 0xa5
	je	.s3_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s3_done
.s3_ok:
	mov	si, s_ok
	call	print_pm
.s3_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d3_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 4: LLDT / SLDT round-trip
	; ================================================================
	mov	si, s4_label
	call	print_pm
	mov	ax, SEL_LDT
	lldt	ax
	sldt	ax
	call	print_hex4
	cmp	ax, SEL_LDT
	je	.s4_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s4_done
.s4_ok:
	mov	si, s_ok
	call	print_pm
.s4_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d4_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 5: STR — task register was loaded with SEL_CURR_TSS
	; ================================================================
	mov	si, s5_label
	call	print_pm
	str	ax
	call	print_hex4
	cmp	ax, SEL_CURR_TSS
	je	.s5_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s5_done
.s5_ok:
	mov	si, s_ok
	call	print_pm
.s5_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d5_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 6: LMSW set TS, then CLTS clears it
	; ================================================================
	mov	si, s6_label
	call	print_pm
	mov	ax, 0x000f		; try to set MP+EM+TS
	lmsw	ax
	clts				; clear TS bit
	smsw	ax
	call	print_hex4
	test	ax, 0x0008		; TS must be 0 after CLTS
	jz	.s6_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s6_done
.s6_ok:
	mov	si, s_ok
	call	print_pm
.s6_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d6_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 7: LAR — read access rights for SEL_CODE
	; ================================================================
	mov	si, s7_label
	call	print_pm
	mov	cx, SEL_CODE
	lar	ax, cx
	call	print_hex4
	cmp	ax, 0x9b00
	je	.s7_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s7_done
.s7_ok:
	mov	si, s_ok
	call	print_pm
.s7_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d7_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 8: LSL — read segment limit for SEL_DATA
	; ================================================================
	mov	si, s8_label
	call	print_pm
	mov	cx, SEL_DATA
	lsl	ax, cx
	call	print_hex4
	cmp	ax, 0xffff
	je	.s8_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s8_done
.s8_ok:
	mov	si, s_ok
	call	print_pm
.s8_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d8_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 9: VERR + VERW
	; ================================================================
	mov	si, s9_label
	call	print_pm
	mov	ax, SEL_CODE
	verr	ax
	jz	.verr_ok
	inc	di
	jmp	.verw_check
.verr_ok:
.verw_check:
	mov	ax, SEL_DATA
	verw	ax
	jz	.s9_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s9_done
.s9_ok:
	mov	si, s9_val
	call	print_pm
	mov	si, s_ok
	call	print_pm
.s9_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d9_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 10: ARPL — promote RPL of SEL_CODE from 0 to 3
	; ================================================================
	mov	si, s10_label
	call	print_pm
	mov	ax, SEL_CODE		; AX = 0x0008, RPL=0
	mov	cx, 0x0003		; CX RPL=3
	arpl	ax, cx			; AX should become 0x000B, ZF=1
	call	print_hex4
	cmp	ax, SEL_CODE | 3
	jne	.s10_fail
	jz	.s10_ok			; ZF set by ARPL when promoted
.s10_fail:
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s10_done
.s10_ok:
	mov	si, s_ok
	call	print_pm
.s10_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d10_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 11: INT via IDT gate
	; pm_stub_handler sets DI... but we're using DI as error counter.
	; Use a dedicated memory cell instead — pm_stub_handler writes to
	; [ds:int_result], main code reads it back.
	; ================================================================
	mov	si, s11_label
	call	print_pm
	mov	word [ds:int_result], 0
	int	0x10			; fires IDT gate 0x10 → pm_stub_handler
	mov	ax, [ds:int_result]
	call	print_hex4
	cmp	ax, 0xbeef
	je	.s11_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s11_done
.s11_ok:
	mov	si, s_ok
	call	print_pm
.s11_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d11_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 12: Task switch via CALL FAR to SEL_DEMO_TSS
	; demo_task_entry writes 0xCAFE to [ds:demo_result], then IRETs.
	; Because we used CALL (is_call=1), NT flag is set in new task FLAGS,
	; so IRET in demo_task_entry performs a back-switch to here.
	; After the back-switch, we read demo_result.
	; ================================================================
	mov	si, s12_label
	call	print_pm
	mov	word [ds:demo_result], 0
	; CALL FAR with CS=SEL_DEMO_TSS triggers task switch (op_9a / op_ff_03)
	; Encode as: db 0x9A, offset(lo), offset(hi), SEL_DEMO_TSS(lo), SEL_DEMO_TSS(hi)
	; The IP operand is ignored by the task switch (IP comes from TSS image).
	db	0x9a
	dw	0x0000			; ignored IP operand
	dw	SEL_DEMO_TSS		; CS selector → triggers task switch

	; After back-switch we land here
	mov	ax, [ds:demo_result]
	call	print_hex4
	cmp	ax, 0xcafe
	je	.s12_ok
	inc	di
	mov	si, s_fail
	call	print_pm
	jmp	.s12_done
.s12_ok:
	mov	si, s_ok
	call	print_pm
.s12_done:
	mov	byte [ds:cursor_col], 40
	mov	si, d12_desc
	call	print_pm_dim
	call	newline_pm
	call	delay_pm

	; ================================================================
	; Step 13: Error summary
	; ================================================================
	call	newline_pm
	mov	si, s13_label
	call	print_pm
	mov	ax, di			; DI = error count
	call	print_hex4
	cmp	di, 0
	je	.s13_ok
	mov	si, s_fail
	call	print_pm
	jmp	.s13_done
.s13_ok:
	mov	si, s_allok
	call	print_pm
.s13_done:
	call	newline_pm
	call	newline_pm

	; Print large DONE banner
	mov	si, s_done_banner
	call	print_pm
	call	newline_pm

	; Long pause before exit (~10 seconds)
	mov	bx, 10
.long_pause:
	call	delay_pm
	dec	bx
	jnz	.long_pause

	; ================================================================
	; Step 14: PCE_HOOK_ABORT — BX = error count (0 = pass)
	; ================================================================
	mov	bx, di
	cmp	bx, 0
	je	.exit_ok
	mov	bx, 1
.exit_ok:
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	; Should not reach here
	mov	ah, 0x4c
	mov	al, bl
	int	0x21

; ============================================================
; PM interrupt stub handler
; Called when any IDT gate fires in PM.
; Writes 0xBEEF to [ds:int_result] so step 11 can verify it.
; Note: ES still points to SEL_VIDEO on entry (segments preserved
; across the interrupt if IDT saves/restores them correctly).
; We use a known DS-relative address to write the result.
; ============================================================
pm_stub_handler:
	; DS may not be valid inside the interrupt — it was SEL_DATA before
	; the interrupt, and INT preserves segment regs on 286, so DS is fine.
	mov	word [ds:int_result], 0xbeef
	iret

; ============================================================
; Demo task entry point (runs in the "new task" after CALL FAR to TSS)
; TSS image has: IP=demo_task_entry, CS=SEL_CODE, DS=SEL_DATA, SS=SEL_STACK
; Writes 0xCAFE to demo_result, then IRETs (NT=1 → back-switch).
; ============================================================
demo_task_entry:
	mov	word [ds:demo_result], 0xcafe
	iret				; NT=1 → task back-switch to caller

; ============================================================
; print_rm — real-mode string print via INT 10h teletype
; IN: SI = pointer to NUL-terminated string (DS-relative)
; ============================================================
print_rm:
	mov	ah, 0x0e
	mov	bx, 0x0007		; page 0, grey attribute
.loop:
	lodsb
	test	al, al
	jz	.done
	int	0x10
	jmp	.loop
.done:
	ret

; ============================================================
; print_pm — PM string print via direct CGA write
; IN: SI = DS-relative pointer to NUL-terminated string
; Uses cursor_row, cursor_col in DS to track position.
; Writes white-on-black (attr 0x0F) to ES:SEL_VIDEO.
; ============================================================
print_pm:
	push	ax
	push	bx
	push	si
.char_loop:
	lodsb				; AL = next char from DS:SI
	test	al, al
	jz	.done
	; Compute video offset = (row*80 + col) * 2
	xor	ah, ah
	mov	bl, [ds:cursor_row]
	mov	bh, 0
	; BX = row*80: multiply BX by 80
	push	ax
	mov	ax, bx
	mov	bx, CGA_COLS * 2
	mul	bx			; AX = row * 160 (bytes)
	mov	bx, ax
	pop	ax
	xor	ch, ch
	mov	cl, [ds:cursor_col]
	shl	cx, 1			; col * 2
	add	bx, cx			; BX = video byte offset
	mov	[es:bx], al		; character
	mov	byte [es:bx+1], 0x0f	; attribute: white on black
	; Advance column
	inc	byte [ds:cursor_col]
	cmp	byte [ds:cursor_col], CGA_COLS
	jl	.char_loop
	; Wrap to next line
	mov	byte [ds:cursor_col], 0
	inc	byte [ds:cursor_row]
	cmp	byte [ds:cursor_row], CGA_ROWS
	jl	.char_loop
	mov	byte [ds:cursor_row], CGA_ROWS - 1
	jmp	.char_loop
.done:
	pop	si
	pop	bx
	pop	ax
	ret

; ============================================================
; print_pm_dim — same as print_pm but uses attr 0x03 (dark cyan)
; IN: SI = DS-relative pointer to NUL-terminated string
; ============================================================
print_pm_dim:
	push	ax
	push	bx
	push	si
.char_loop:
	lodsb
	test	al, al
	jz	.done
	push	ax
	mov	bl, [ds:cursor_row]
	mov	bh, 0
	mov	ax, bx
	mov	bx, CGA_COLS * 2
	mul	bx
	mov	bx, ax
	pop	ax
	xor	ch, ch
	mov	cl, [ds:cursor_col]
	shl	cx, 1
	add	bx, cx
	mov	[es:bx], al
	mov	byte [es:bx+1], 0x03	; attribute: dark cyan on black
	inc	byte [ds:cursor_col]
	cmp	byte [ds:cursor_col], CGA_COLS
	jl	.char_loop
	mov	byte [ds:cursor_col], 0
	inc	byte [ds:cursor_row]
	cmp	byte [ds:cursor_row], CGA_ROWS
	jl	.char_loop
	mov	byte [ds:cursor_row], CGA_ROWS - 1
	jmp	.char_loop
.done:
	pop	si
	pop	bx
	pop	ax
	ret

; ============================================================
; print_hex4 — print AX as 4 hex digits via print_pm
; ============================================================
print_hex4:
	push	cx
	push	si
	push	ax
	; Convert AX to 4 hex chars in hex_buf
	mov	cx, 4
	mov	si, hex_buf + 3
.nibble:
	mov	bx, ax
	and	bx, 0x000f
	cmp	bl, 10
	jl	.digit
	add	bl, 'A' - 10
	jmp	.store
.digit:
	add	bl, '0'
.store:
	mov	[ds:si], bl
	shr	ax, 4
	dec	si
	loop	.nibble
	pop	ax
	; print the 4-char buffer
	mov	si, hex_buf
	call	print_pm
	pop	si
	pop	cx
	ret

; ============================================================
; newline_pm — advance to next row, reset column
; ============================================================
newline_pm:
	mov	byte [ds:cursor_col], 0
	inc	byte [ds:cursor_row]
	cmp	byte [ds:cursor_row], CGA_ROWS
	jl	.ok
	mov	byte [ds:cursor_row], CGA_ROWS - 1
.ok:
	ret

; ============================================================
; delay_pm — busy-loop pause (~1 second at speed=24)
; Two nested 16-bit loops, each iterating ~65536 times.
; ============================================================
delay_pm:
	push	ax
	push	cx
	push	bx
	mov	bx, DELAY_OUTER
.outer:
	xor	cx, cx			; CX=0 → dec/jnz runs 65536 times
.inner:
	dec	cx
	jnz	.inner
	dec	bx
	jnz	.outer
	pop	bx
	pop	cx
	pop	ax
	ret

; ============================================================
; Data section
; ============================================================

DEMO_VERSION_STR	equ	"2"	; update string on each change

; Real-mode strings (printed before PM via print_rm)
banner:
	db	13, 10
	db	"PCE 80286 Protected-Mode Demo v", DEMO_VERSION_STR, 13, 10
	db	"Press any key to begin...", 13, 10, 0

; PM strings (printed via print_pm in CGA direct mode)
pm_banner:	db	"=== 80286 PM Demo v", DEMO_VERSION_STR, " ===", 0

s1_label:	db	"1 GDTR/IDTR  lim=", 0
s2_label:	db	"2 SMSW PE=1  msw=", 0
s3_label:	db	"3 Sentinel   val=", 0
s4_label:	db	"4 SLDT/LLDT  sel=", 0
s5_label:	db	"5 STR        tr= ", 0
s6_label:	db	"6 CLTS       msw=", 0
s7_label:	db	"7 LAR        ar= ", 0
s8_label:	db	"8 LSL        lim=", 0
s9_label:	db	"9 VERR+VERW      ", 0
s9_val:		db	"ZF=1", 0
s10_label:	db	"10 ARPL      ax= ", 0
s11_label:	db	"11 IDT INT   di= ", 0
s12_label:	db	"12 TSS CALL  res=", 0
s13_label:	db	"  Errors=", 0
s_slash:	db	"/", 0
s_ok:		db	"  OK", 0
s_fail:		db	"  FAIL", 0
s_allok:	db	"  ALL OK", 0
s_done_banner:	db	"=== ALL TESTS DONE ===", 0

; Per-step dim descriptions (col 40+, attr 0x03 dark cyan)
d1_desc:	db	"GDTR/IDTR limits verify GDT+IDT load", 0
d2_desc:	db	"MSW PE=1 confirms protected mode on", 0
d3_desc:	db	"Write 0xA5 / read-back via DS segment", 0
d4_desc:	db	"LLDT loads LDT sel; SLDT reads it back", 0
d5_desc:	db	"STR reads task register = SEL_CURR_TSS", 0
d6_desc:	db	"LMSW sets TS; CLTS clears it in MSW", 0
d7_desc:	db	"LAR loads access-rights byte from GDT", 0
d8_desc:	db	"LSL loads segment limit word from GDT", 0
d9_desc:	db	"VERR/VERW: ZF=1 readable+writable", 0
d10_desc:	db	"ARPL raises RPL field of selector", 0
d11_desc:	db	"INT 10h dispatched through IDT gate", 0
d12_desc:	db	"CALL FAR TSS switch; IRET NT back", 0

; Scratch buffer for print_hex4 (4 bytes + NUL)
hex_buf:	db	0, 0, 0, 0, 0

; CGA cursor state
cursor_row:	db	0
cursor_col:	db	0

; PM sentinel byte (step 3)
pm_sentinel:	db	0

; INT handler result (step 11)
int_result:	dw	0

; Task switch result (step 12)
demo_result:	dw	0

; SGDT/SIDT scratch area (5 bytes each)
gdtr_val:	dw	0, 0, 0		; limit(2) + base(3)
idtr_val:	dw	0, 0, 0
idtr_chk:	dw	0, 0, 0		; scratch for SIDT in step 1

; GDT (8 entries × 8 bytes = 64 bytes)
align 8
gdt_base:
	times	64 db 0

; IDT (32 gates × 8 bytes = 256 bytes)
align 8
idt_base:
	times	256 db 0

; Current-task TSS (44 bytes — old task state saved here on task switch)
align 4
curr_tss:
	times	44 db 0

; Demo-task TSS (44 bytes — new task state loaded from here)
align 4
demo_tss:
	times	44 db 0
