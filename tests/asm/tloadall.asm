;-----------------------------------------------------------------------------
; tests/asm/tloadall.asm
;
; End-to-end regression test for T7-A: LOADALL (0F 05).
;
; Simplified strategy — real mode only:
;   1. Fill the LOADALL table at physical 0x000800 (using DS=0).
;      Fields set:
;        MSW    = 0x0000   (stay in real mode, PE=0)
;        CS     = our .COM segment
;        IP     = offset of real_mode_resume
;        DS=SS=ES = our segment
;        SP     = 0x0E00   (safe stack)
;        AX     = 0x1234, BX = 0x5678, CX = 0xABCD  (register sentinels)
;        All segment caches: base = CS<<4, limit = 0xFFFF, AR = 0x93/0x9B
;   2. Execute DB 0Fh, 05h  (LOADALL) — still in real mode.
;   3. At real_mode_resume verify AX/BX/CX sentinels then HOOK_ABORT.
;
; NOTE: The C unit test (test_loadall.c) verifies all 14 internal aspects
; of LOADALL (descriptor caches, GDT/IDT/TR/LDT registers, EM flag, etc.).
; This e2e test only needs to confirm the opcode fires and redirects CS:IP.
;
; Build:
;   nasm -O6 -Isrc/ -Isrc/arch/ibmpc/ -f bin -o tests/asm/tloadall.com \
;        tests/asm/tloadall.asm
;-----------------------------------------------------------------------------

%include "pce.inc"

	cpu	286
	org	0x100

LOADALL_PHYS	equ	0x0800		; physical 0x000800 = DS:0x0800 when DS=0

start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax

	; Verify running under PCE
	xor	ax, ax
	db	0xd5, 0x00
	sub	ax, 0x0fce
	jz	.pce_ok
	mov	ax, 0x4c01
	int	0x21
.pce_ok:

	; Compute CS<<4 as a 24-bit value
	;   seg_lo = (CS<<4) & 0xFFFF
	;   seg_hi = (CS<<4) >> 16
	mov	ax, cs
	mov	cl, 4
	shl	ax, cl
	mov	[seg_lo], ax
	mov	ax, cs
	mov	cl, 12
	shr	ax, cl
	mov	[seg_hi], al

	; ================================================================
	; Save CS<<4 into registers BEFORE switching DS to 0.
	; After DS=0, [seg_lo] would read physical offset seg_lo (wrong);
	; we need CS<<4 in BP (low 16) and BH (high byte).
	; ================================================================
	mov	bp, [seg_lo]		; BP = CS<<4 low 16
	mov	bl, [seg_hi]		; BL = CS<<4 high byte  (BH free)

	; ================================================================
	; Point DS at physical 0 so DS:0x0800 = physical 0x000800
	; ================================================================
	xor	ax, ax
	mov	ds, ax

	; ================================================================
	; Clear LOADALL table (102 bytes at DS:0x0800)
	; ================================================================
	mov	di, LOADALL_PHYS
	mov	cx, 102
	xor	al, al
.clr:
	mov	[di], al
	inc	di
	loop	.clr

	; ================================================================
	; Fill table fields (DS=0, so [n] addresses physical n)
	; ================================================================

	; +0x06  MSW = 0x0000 (real mode)
	mov	word [LOADALL_PHYS + 0x06], 0x0000

	; +0x18  FLAGS = 0x0202 (IF=1, reserved bit 1)
	mov	word [LOADALL_PHYS + 0x18], 0x0202

	; +0x1A  IP = real_mode_resume
	mov	word [LOADALL_PHYS + 0x1A], real_mode_resume

	; +0x1E..+0x24  segment selectors = our .COM segment (CS)
	; CS is still valid (we only changed DS above)
	mov	ax, cs
	mov	[LOADALL_PHYS + 0x1E], ax	; DS
	mov	[LOADALL_PHYS + 0x20], ax	; SS
	mov	[LOADALL_PHYS + 0x22], ax	; CS
	mov	[LOADALL_PHYS + 0x24], ax	; ES

	; +0x26..+0x35  GP register sentinels
	mov	word [LOADALL_PHYS + 0x26], 0x0000	; DI
	mov	word [LOADALL_PHYS + 0x28], 0x0000	; SI
	mov	word [LOADALL_PHYS + 0x2A], 0x0000	; BP (clobbered; that is ok)
	mov	word [LOADALL_PHYS + 0x2C], 0x0e00	; SP
	mov	word [LOADALL_PHYS + 0x2E], 0x5678	; BX  T2
	mov	word [LOADALL_PHYS + 0x30], 0x0000	; DX
	mov	word [LOADALL_PHYS + 0x32], 0xabcd	; CX  T3
	mov	word [LOADALL_PHYS + 0x34], 0x1234	; AX  T1

	; +0x36..+0x4D  segment descriptor caches (ES CS SS DS, 6B each)
	; All: base = CS<<4 (24-bit), limit = 0xFFFF
	; BP = low 16 of CS<<4, BL = high byte of CS<<4 (saved before DS switch)
	mov	ax, bp			; AX = CS<<4 low 16
	mov	dl, bl			; DL = CS<<4 high byte

	; ES cache (+0x36): AR=0x93
	mov	[LOADALL_PHYS + 0x3A], word 0xffff
	mov	[LOADALL_PHYS + 0x36], ax
	mov	[LOADALL_PHYS + 0x38], dl
	mov	byte [LOADALL_PHYS + 0x39], 0x93
	; CS cache (+0x3C): AR=0x9B
	mov	[LOADALL_PHYS + 0x40], word 0xffff
	mov	[LOADALL_PHYS + 0x3C], ax
	mov	[LOADALL_PHYS + 0x3E], dl
	mov	byte [LOADALL_PHYS + 0x3F], 0x9b
	; SS cache (+0x42): AR=0x93
	mov	[LOADALL_PHYS + 0x46], word 0xffff
	mov	[LOADALL_PHYS + 0x42], ax
	mov	[LOADALL_PHYS + 0x44], dl
	mov	byte [LOADALL_PHYS + 0x45], 0x93
	; DS cache (+0x48): AR=0x93
	mov	[LOADALL_PHYS + 0x4C], word 0xffff
	mov	[LOADALL_PHYS + 0x48], ax
	mov	[LOADALL_PHYS + 0x4A], dl
	mov	byte [LOADALL_PHYS + 0x4B], 0x93

	; GDT/LDT/IDT/TR entries (+0x4E..+0x65): already zeroed

	; ================================================================
	; Execute LOADALL — redirects CS:IP to real_mode_resume
	; ================================================================
	db	0x0f, 0x05

	; Should never reach here
	int	0x20

; ================================================================
; real_mode_resume:
;   Execution resumes here after LOADALL (still in real mode).
;   DS = our .COM segment (loaded from table +0x1E).
;   AX=0x1234, BX=0x5678, CX=0xABCD from table.
; ================================================================
real_mode_resume:
	xor	si, si			; SI = error count

	; T1: AX must be 0x1234
	cmp	ax, 0x1234
	je	.t1_pass
	inc	si
.t1_pass:

	; T2: BX must be 0x5678
	cmp	bx, 0x5678
	je	.t2_pass
	inc	si
.t2_pass:

	; T3: CX must be 0xABCD
	cmp	cx, 0xabcd
	je	.t3_pass
	inc	si
.t3_pass:

	mov	bx, si
	cmp	bx, 0
	je	.exit_ok
	mov	bx, 1
.exit_ok:
	mov	ax, PCE_HOOK_ABORT
	db	0xd5, 0x00
	; Fallback (should not reach)
	mov	ah, 0x4c
	mov	al, bl
	int	0x21

; ================================================================
; Data — sits in our .COM segment, accessible after LOADALL
; restores DS = CS.
; ================================================================
seg_lo:		dw	0		; (CS<<4) & 0xFFFF
seg_hi:		db	0		; (CS<<4) >> 16
