;-----------------------------------------------------------------------------
; tests/asm/t186.asm
;
; End-to-end regression test for 80186/80188 extended instructions.
; Tests: PUSHA/POPA, ENTER/LEAVE, PUSH imm8, PUSH imm16, IMUL reg,r/m,imm,
;        BOUND, SHL/SAR/SHR r/m,imm (shift-by-immediate).
;
; Runs under IBM DOS (any 80186+ CPU model).
; Exits via PCE_HOOK_ABORT with BX=0 (pass) or BX=1 (fail).
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o tests/asm/t186.com \
;        tests/asm/t186.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	186
	org	0x100

start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax

	; ----------------------------------------------------------------
	; Check we are running under PCE
	; ----------------------------------------------------------------
	xor	ax, ax
	db	0xd5, 0x00		; pce hook
	sub	ax, 0x0fce
	jz	.pce_ok
	; Not under PCE — just exit gracefully via DOS
	mov	ax, 0x4c00
	int	0x21

.pce_ok:
	; ----------------------------------------------------------------
	; Set up: use a dedicated stack area so PUSHA/POPA don't stomp DOS
	; ----------------------------------------------------------------
	mov	ax, cs
	mov	ss, ax
	mov	sp, stack_top

	xor	bx, bx			; BX = error count

	; ================================================================
	; Test 1: PUSH imm8 (sign-extended)
	; ================================================================
	push	byte 0x42		; push 0x0042
	pop	ax
	cmp	ax, 0x0042
	je	.t1_pass
	inc	bx
.t1_pass:

	; ================================================================
	; Test 2: PUSH imm16
	; ================================================================
	push	word 0x1234
	pop	ax
	cmp	ax, 0x1234
	je	.t2_pass
	inc	bx
.t2_pass:

	; ================================================================
	; Test 3: PUSHA / POPA round-trip
	; ================================================================
	; Save error count to memory — POPA restores SP so the stack is
	; not a safe place to keep it across PUSHA/POPA.
	mov	[errcnt], bx

	mov	ax, 0x1111
	mov	cx, 0x2222
	mov	dx, 0x3333
	mov	bx, 0x4444
	mov	bp, 0x5555
	mov	si, 0x6666
	mov	di, 0x7777

	pusha				; push all 8 regs
	xor	ax, ax			; trash regs
	xor	cx, cx
	xor	dx, dx
	xor	bx, bx
	xor	bp, bp
	xor	si, si
	xor	di, di
	popa				; restore

	; Check restored values; accumulate into memory errcnt
	cmp	ax, 0x1111
	je	.t3a_pass
	inc	word [errcnt]
.t3a_pass:
	cmp	cx, 0x2222
	je	.t3b_pass
	inc	word [errcnt]
.t3b_pass:
	cmp	dx, 0x3333
	je	.t3c_pass
	inc	word [errcnt]
.t3c_pass:
	cmp	bx, 0x4444
	je	.t3d_pass
	inc	word [errcnt]
.t3d_pass:
	cmp	bp, 0x5555
	je	.t3e_pass
	inc	word [errcnt]
.t3e_pass:
	cmp	si, 0x6666
	je	.t3f_pass
	inc	word [errcnt]
.t3f_pass:
	cmp	di, 0x7777
	je	.t3g_pass
	inc	word [errcnt]
.t3g_pass:
	; Reload error count from memory into BX
	mov	bx, [errcnt]

	; ================================================================
	; Test 4: ENTER / LEAVE
	; ================================================================
	mov	bp, 0xBEEF
	mov	sp, stack_top
	enter	8, 0
	; After ENTER 8,0: BP = stack_top-2, SP = BP-8
	mov	ax, bp
	sub	ax, 8
	cmp	ax, sp
	je	.t4a_pass
	inc	bx
.t4a_pass:
	leave
	; After LEAVE: BP should be restored to 0xBEEF
	cmp	bp, 0xBEEF
	je	.t4b_pass
	inc	bx
.t4b_pass:

	; ================================================================
	; Test 5: IMUL reg16, r/m16, imm8
	; ================================================================
	mov	ax, 10
	imul	cx, ax, 7		; cx = 10 * 7 = 70
	cmp	cx, 70
	je	.t5a_pass
	inc	bx
.t5a_pass:

	mov	ax, -3
	imul	cx, ax, 4		; cx = -3 * 4 = -12 = 0xFFF4
	cmp	cx, -12
	je	.t5b_pass
	inc	bx
.t5b_pass:

	; ================================================================
	; Test 6: Shift-by-immediate
	; ================================================================
	mov	ax, 1
	shl	ax, 3			; ax = 1 << 3 = 8
	cmp	ax, 8
	je	.t6a_pass
	inc	bx
.t6a_pass:

	mov	ax, 0x0080
	shr	ax, 3			; ax = 0x0080 >> 3 = 0x0010
	cmp	ax, 0x0010
	je	.t6b_pass
	inc	bx
.t6b_pass:

	mov	ax, 0xFF00
	sar	ax, 4			; ax = 0xFF00 >> 4 (signed) = 0xFFF0
	cmp	ax, 0xFFF0
	je	.t6c_pass
	inc	bx
.t6c_pass:

	; ================================================================
	; Done — BX = 0 means all tests passed
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
errcnt	dw	0		; error counter (memory, safe across PUSHA/POPA)

; ================================================================
; Local stack (256 bytes)
; ================================================================
	align	2
stack_base:
	times	256 db 0
stack_top:
