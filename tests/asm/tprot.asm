; 80286 protected-mode guest regression. Runs from any DOS COM segment.
; Correct 286 descriptors/gates, local stack and deterministic fault exit.
; BX=0 means pass; unexpected PM exceptions abort with BX=1.
%include "pce.inc"
 cpu 286
 org 0x100
SEL_NULL equ 0
SEL_CODE equ 8
SEL_DATA equ 16
SEL_STACK equ 24
SEL_LDT equ 32
SEL_TSS equ 40
start:
 mov ax,cs
 mov ds,ax
 mov es,ax
 xor ax,ax
 db 0xd5,0
 cmp ax,0x0fce
 je .pce_ok
 mov ax,0x4c01
 int 0x21
.pce_ok:
 cli
 xor bx,bx
 ; Full 20-bit DOS segment base, including the high nibble.
 mov ax,cs
 mov dx,ax
 mov cl,12
 shr dx,cl
 mov cl,4
 shl ax,cl
 mov [gdt_base+10],ax
 mov [gdt_base+12],dl
 mov [gdt_base+18],ax
 mov [gdt_base+20],dl
 mov [gdt_base+26],ax
 mov [gdt_base+28],dl
 ; Descriptor-table addresses can carry into the next 64K physical bank.
 push ax
 push dx
 add ax,gdt_base
 adc dl,0
 mov [gdtr_val+2],ax
 mov [gdtr_val+4],dl
 pop dx
 pop ax
 add ax,idt_base
 adc dl,0
 mov [idtr_val+2],ax
 mov [idtr_val+4],dl
 mov di,idt_base
 mov cx,32
.fill_idt:
 mov word [di],pm_fault_handler
 mov word [di+2],SEL_CODE
 mov word [di+4],0x8600 ; present 286 interrupt gate
 mov word [di+6],0
 add di,8
 loop .fill_idt
 mov word [idt_base+0x10*8],pm_int_handler
 lgdt [gdtr_val]
 lidt [idtr_val]
 mov ax,1
 lmsw ax
 jmp SEL_CODE:pm_entry
pm_entry:
 mov ax,SEL_DATA
 mov ds,ax
 mov es,ax
 mov ax,SEL_STACK
 mov ss,ax
 mov sp,stack_top
 xor ax,ax
 lldt ax
	; ================================================================
	; Test 1 (PM): Write sentinel byte via DS:offset
	;   In PM, DS base = CS<<4, so DS:pm_sentinel maps to
	;   the same physical location as the real-mode DS:pm_sentinel.
	;   Write 0xA5 there.
	; ================================================================
	mov	byte [pm_sentinel], 0xa5
	cmp	byte [pm_sentinel], 0xa5
	je	.sentinel_ok
	inc	bx
.sentinel_ok:

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
	; The clock batch may finish after ABORT. Stay inert; DOS INT 21h
	; is not a valid protected-mode exit and would overwrite a pass result.
	cli
.exit_halt:
	hlt
	jmp .exit_halt

; ================================================================
; PM interrupt handler
; Called via IDT gate when INT 0x10 fires in PM.
; Sets DI=0xBEEF to signal "handler was called", then IRETs.
; ================================================================
pm_int_handler:
	mov	di, 0xbeef
	iret

; A fault must report failure, not IRET through an error-code frame.
pm_fault_handler:
 mov bx,1
 mov ax,PCE_HOOK_ABORT
 db 0xd5,0
 cli
.halt: hlt
 jmp .halt

pm_sentinel: db 0
gdtr_val: dw 47
 dd 0
idtr_val: dw 255
 dd 0
align 8
gdt_base:
 dq 0
 dw 0xffff,0
 db 0,0x9b,0,0
 dw 0xffff,0
 db 0,0x93,0,0
 dw 0xffff,0
 db 0,0x93,0,0 ; ordinary writable stack, not empty expand-down segment
 dw 0,0
 db 0,0x82,0,0 ; LDT, not used for lookup
 dw 0x2b,0
 db 0,0x81,0,0 ; available 286 TSS; LTR loads but does not switch tasks
align 8
idt_base: times 256 db 0
align 2
stack_base: times 256 db 0
stack_top:
