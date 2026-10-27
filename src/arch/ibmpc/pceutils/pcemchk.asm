;-----------------------------------------------------------------------------
; pce
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; File name:    pcemchk.asm
; Created:      2026 (PCE project)
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; pcemchk — PCE memory checker
;
; Displays a structured summary of all memory available to the system:
;
;   CPU          CPU model (8086/80286)
;   Hardware     Conventional RAM (INT 12h) and extended RAM (INT 15h/88h)
;   XMS          Total / free EMB pool and UMB size (via PCE hook + XMS fn 08h)
;   HMA          Present / allocated state
;   EMS          Total EMS pages (via PCE hook)
;
; Output example (80286 with 15 MB extended RAM, hma=1):
;
;   PCE Memory Check
;   ----------------
;   CPU:         80286
;   Conv RAM:    640K (INT 12h)
;   Ext RAM:     15360K (INT 15h/88h)
;   XMS total:   14272K   free: 14272K
;   XMS UMB:     88K
;   HMA:         present, free
;   EMS total:   16384K   free: 6144K
;-----------------------------------------------------------------------------


%include "pce.inc"


; Force 8086-only instruction set — prevents NASM from emitting
; 286-only two-byte conditional jumps (0F 8x) for long branches.
cpu	8086

section .text

	org	0x100

	jmp	start


%define PCE_USE_PRINT_CHAR    1
%define PCE_USE_PRINT_STRING  1
%define PCE_USE_PRINT_UINT16  1
%define PCE_USE_HOOK_CHECK    1
%define PCE_USE_HOOK          1

%include "pce-lib.inc"


;-----------------------------------------------------------------------------
; Entry point
;-----------------------------------------------------------------------------
start:
	mov	ax, cs
	mov	ds, ax
	mov	es, ax
	cld

	; ----------------------------------------------------------------
	; CPU detection: try PUSHF/POPF trick to distinguish 8086 from 286
	; On 8086/8088 bits 12-15 of FLAGS are always 1.
	; On 80286 they can be cleared.
	; ----------------------------------------------------------------
	mov	si, msg_cpu_label
	call	pce_print_string

	pushf
	pop	ax
	mov	cx, ax			; save original flags
	and	ax, 0x0fff		; clear bits 12-15
	push	ax
	popf
	pushf
	pop	ax
	and	ax, 0xf000		; isolate bits 12-15
	push	cx
	popf				; restore flags

	cmp	ax, 0xf000		; if still all-1: 8086/8088
	je	.is_8086

	; 80286
	mov	si, msg_cpu_286
	call	pce_print_string
	mov	byte [is_286], 1
	jmp	.cpu_done

.is_8086:
	mov	si, msg_cpu_8086
	call	pce_print_string

.cpu_done:
	; ----------------------------------------------------------------
	; Conventional RAM — INT 12h → AX = KB
	; ----------------------------------------------------------------
	mov	si, msg_conv_label
	call	pce_print_string

	int	0x12			; AX = conventional KB
	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_int12
	call	pce_print_string

	; ----------------------------------------------------------------
	; Extended RAM — INT 15h AH=88h → AX = KB above 1 MB
	; Only meaningful on 286; on 8086 the PCE hook returns it anyway
	; if an XMS malloc pool is configured.
	; ----------------------------------------------------------------
	mov	si, msg_ext_label
	call	pce_print_string

	mov	ah, 0x88
	int	0x15
	jc	.no_ext
	or	ax, ax
	jz	.no_ext

	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_int15
	call	pce_print_string
	jmp	.ext_done

.no_ext:
	mov	si, msg_none
	call	pce_print_string
	mov	si, msg_nl
	call	pce_print_string

.ext_done:

	; ----------------------------------------------------------------
	; XMS — use PCE_HOOK_XMS_INFO first (knows emb_max, umb, hma)
	; then call XMS driver fn 08h for the live free count.
	;
	; PCE_HOOK_XMS_INFO returns:
	;   AX bit0 = XMS present
	;   AX bit1 = HMA present
	;   DX = emb_max in KB
	;   CX = umb_size in paragraphs
	; ----------------------------------------------------------------
	call	pce_hook_check
	jc	.no_pce_xms

	mov	ax, PCE_HOOK_XMS_INFO
	call	pce_hook

	test	al, 0x01
	jz	.no_xms

	; Save HMA flag from bit1 of AL
	mov	bl, al
	and	bl, 0x02
	mov	[hma_flag], bl

	; Save emb_max and umb_size
	mov	[xms_total_k], dx
	mov	[umb_paras],   cx

	; Get free XMS via INT 2Fh → XMS fn 08h
	; First check XMS driver installed: INT 2Fh AX=4300h
	mov	ax, 0x4300
	int	0x2f
	cmp	al, 0x80
	jne	.xms_no_driver

	; Get driver entry point: INT 2Fh AX=4310h → ES:BX
	mov	ax, 0x4310
	int	0x2f
	mov	[xms_entry],    bx
	mov	[xms_entry+2],  es
	mov	byte [xms_driver_ok], 1

	; Call XMS fn 08h: query free extended memory
	; Returns: AX=largest free block (KB), DX=total free (KB), BL=error
	mov	ah, 0x08
	call	far [xms_entry]
	mov	[xms_free_k], dx

	jmp	.print_xms

.xms_no_driver:
	mov	ax, [xms_total_k]
	mov	[xms_free_k], ax	; assume all free if no driver

.print_xms:
	; Print XMS total/free line
	mov	si, msg_xms_label
	call	pce_print_string

	mov	ax, [xms_total_k]
	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_k_free
	call	pce_print_string
	mov	ax, [xms_free_k]
	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_k
	call	pce_print_string

	; Print UMB line (only if non-zero)
	mov	ax, [umb_paras]
	or	ax, ax
	jz	.no_umb

	mov	si, msg_umb_label
	call	pce_print_string

	; Convert paragraphs to KB: paras / 64
	mov	cl, 6
	shr	ax, cl
	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_k
	call	pce_print_string

.no_umb:

	; Print HMA line
	mov	si, msg_hma_label
	call	pce_print_string

	mov	al, [hma_flag]
	or	al, al
	jz	.hma_absent

	; HMA present — probe state only if XMS driver is loaded
	mov	si, msg_present
	call	pce_print_string

	cmp	byte [xms_driver_ok], 1
	jne	.hma_done		; no driver — just report present

	; Try to allocate HMA to probe its state (DX=0ffffh = "all")
	mov	ah, 0x01
	mov	dx, 0xffff
	call	far [xms_entry]
	or	ax, ax
	jnz	.hma_is_free

	; ax=0: allocation failed — BL=91h means already in use
	mov	si, msg_comma_used
	call	pce_print_string
	jmp	.hma_done

.hma_is_free:
	; We got it — release it immediately (fn 02h) then report free
	mov	ah, 0x02
	call	far [xms_entry]
	mov	si, msg_comma_free
	call	pce_print_string
	jmp	.hma_done

.hma_absent:
	mov	si, msg_absent
	call	pce_print_string

.hma_done:
	mov	si, msg_nl
	call	pce_print_string
	jmp	.ems_section

.no_xms:
	mov	si, msg_xms_label
	call	pce_print_string
	mov	si, msg_none
	call	pce_print_string
	mov	si, msg_nl
	call	pce_print_string
	jmp	.ems_section

.no_pce_xms:
	mov	si, msg_xms_label
	call	pce_print_string
	mov	si, msg_no_pce
	call	pce_print_string
	mov	si, msg_nl
	call	pce_print_string

	; ----------------------------------------------------------------
	; EMS — PCE_HOOK_EMS_INFO
	;   AX = 0001 if EMS present
	;   DX = total EMS in KB (pages_max * 16)
	; Then call EMS driver (INT 67h) fn 42h for free page count.
	; ----------------------------------------------------------------
.ems_section:
	call	pce_hook_check
	jc	.no_pce_ems

	mov	ax, PCE_HOOK_EMS_INFO
	call	pce_hook

	test	ax, 0x01
	jz	.no_ems

	mov	[ems_total_k], dx

	; Verify EMS driver at INT 67h by checking for "EMMXXXX0" at
	; segment:000Ah of the handler.  If absent, skip the INT 67h call.
	push	ds
	xor	ax, ax
	mov	ds, ax
	mov	ax, [4 * 0x67 + 2]	; segment of INT 67h handler
	pop	ds

	or	ax, ax
	jz	.ems_no_driver

	; Compare 8 bytes at handler_seg:000Ah with "EMMXXXX0"
	push	ds
	push	si
	push	cx
	mov	ds, ax
	mov	si, 0x000a
	mov	cx, 8
	mov	di, emm_name
	push	cs
	pop	es
	cld
	repe	cmpsb
	pop	cx
	pop	si
	pop	ds
	jne	.ems_no_driver

	; Driver confirmed — get free pages via fn 42h
	; Returns: BX=free pages, DX=total pages, AH=status
	mov	ah, 0x42
	int	0x67
	or	ah, ah
	jnz	.ems_no_driver

	; free KB = BX * 16
	mov	ax, bx
	mov	cl, 4
	shl	ax, cl
	mov	[ems_free_k], ax
	jmp	.print_ems

.ems_no_driver:
	; EMS configured in PCE but driver not loaded — say so
	mov	si, msg_ems_label
	call	pce_print_string
	mov	si, msg_ems_nodrv
	call	pce_print_string
	mov	si, msg_nl
	call	pce_print_string
	jmp	.done

.print_ems:
	mov	si, msg_ems_label
	call	pce_print_string

	mov	ax, [ems_total_k]
	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_k_free
	call	pce_print_string
	mov	ax, [ems_free_k]
	mov	bx, 10
	call	pce_print_uint16
	mov	si, msg_k
	call	pce_print_string
	jmp	.done

.no_ems:
	mov	si, msg_ems_label
	call	pce_print_string
	mov	si, msg_none
	call	pce_print_string
	mov	si, msg_nl
	call	pce_print_string
	jmp	.done

.no_pce_ems:
	mov	si, msg_ems_label
	call	pce_print_string
	mov	si, msg_no_pce
	call	pce_print_string
	mov	si, msg_nl
	call	pce_print_string

.done:
	mov	ax, 0x4c00
	int	0x21


;-----------------------------------------------------------------------------
; Data
;-----------------------------------------------------------------------------
section .data

msg_cpu_label	db "CPU:         ", 0
msg_cpu_8086	db "8086/8088", 0x0d, 0x0a, 0
msg_cpu_286	db "80286", 0x0d, 0x0a, 0

msg_conv_label	db "Conv RAM:    ", 0
msg_ext_label	db "Ext RAM:     ", 0
msg_xms_label	db "XMS total:   ", 0
msg_umb_label	db "XMS UMB:     ", 0
msg_hma_label	db "HMA:         ", 0
msg_ems_label	db "EMS total:   ", 0

msg_k		db "K", 0x0d, 0x0a, 0
msg_k_free	db "K   free: ", 0
msg_int12	db "K (INT 12h)", 0x0d, 0x0a, 0
msg_int15	db "K (INT 15h/88h)", 0x0d, 0x0a, 0
msg_header	db 0
msg_none	db "none", 0
msg_no_pce	db "(no PCE hook)", 0
msg_present	db "present", 0
msg_absent	db "absent", 0
msg_comma_free	db ", free", 0
msg_comma_used	db ", in use", 0
msg_nl		db 0x0d, 0x0a, 0
msg_ems_nodrv	db "configured, no driver", 0

emm_name	db "EMMXXXX0"	; EMS driver signature at handler_seg:000Ah


;-----------------------------------------------------------------------------
; BSS
;-----------------------------------------------------------------------------
section .bss

is_286		resb 1
hma_flag	resb 1
xms_total_k	resw 1
xms_free_k	resw 1
umb_paras	resw 1
ems_total_k	resw 1
ems_free_k	resw 1
xms_entry	resd 1		; far pointer to XMS driver entry
xms_driver_ok	resb 1		; 1 = INT 2Fh driver confirmed present
