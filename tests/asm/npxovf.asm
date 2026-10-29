; Early 8087/80287 overflow characterization, GPL-2.0-or-later.
; DOS .COM, real hardware with an NPX required. Not a conformance oracle.
; FNSAVE/FWAIT capture follows Intel 287 PRM figure 4-3. No WAIT may be
; inserted between the exception-producing FADDP and the no-wait FNSAVE.
bits 16
cpu 8086
org 100h
start:
    push cs
    pop ds
    mov si, records
    mov cx, case_count
.next:
    pushf
    cli
    fninit
    fwait
    ; FNINIT distinguishes original 8087 IEM from the 287 reserved bit.
    ; Mask 8087 INT (often wired to NMI); CLI alone cannot mask NMI.
    fnstcw [initial_cw]
    fwait
    test word [initial_cw], 80h
    jz .control_ready
    or word [si+2], 80h
.control_ready:
    fldcw [si+2]
    fwait
    fld tword [si+4]
    fwait
    fld tword [si+14]
    fwait
    faddp st1, st0
    fnsave [si+24]
    fwait
    popf
    add si, record_size
    loop .next
    ; All DOS calls run outside the interrupt-disabled capture interval.
    mov dx, filename
    xor cx, cx
    mov ah, 3ch
    int 21h
    jc .error
    mov bx, ax
    mov dx, header
    mov cx, data_end-header
    mov ah, 40h
    int 21h
    jc .close_error
    cmp ax, data_end-header
    jne .close_error
    mov ah, 3eh
    int 21h
    jc .error
    mov dx, success
    mov ah, 9
    int 21h
    mov ax, 4c00h
    int 21h
.close_error:
    mov ah, 3eh
    int 21h
.error:
    mov dx, failure
    mov ah, 9
    int 21h
    mov ax, 4c01h
    int 21h
initial_cw: dw 0
filename: db 'NPXOVF.BIN',0
success: db 'Captured 192 cases in NPXOVF.BIN',13,10,'$'
failure: db 'Could not write complete NPXOVF.BIN',13,10,'$'
align 2
header:
    db 'NPXOVF1',0
    dw case_count, record_size
records:
%assign id 0
%assign kind 0
%rep 2
%assign pc 0
%rep 3
%if pc = 0
 %assign width 24
 %assign pc_bits 0
%elif pc = 1
 %assign width 53
 %assign pc_bits 200h
%else
 %assign width 64
 %assign pc_bits 300h
%endif
%assign sign 0
%rep 2
%assign rc 0
%rep 4
%assign om 0
%rep 2
%assign pm 0
%rep 2
    dw id
    ; Runtime sets IEM for 8087; 287 keeps bit 7 clear. Other exceptions masked.
    dw 47h | pc_bits | (rc << 10) | (om << 3) | (pm << 5)
%if kind = 0
    dq 0ffffffffffffffffh
    dw 7ffeh | (sign << 15)
    dq 0ffffffffffffffffh
    dw 7ffeh | (sign << 15)
%else
    dq (0ffffffffffffffffh << (64-width))
    dw 7ffeh | (sign << 15)
    dq 8000000000000000h
    dw (7ffeh-width) | (sign << 15)
%endif
    times 94 db 0
%assign id id+1
%assign pm pm+1
%endrep
%assign om om+1
%endrep
%assign rc rc+1
%endrep
%assign sign sign+1
%endrep
%assign pc pc+1
%endrep
%assign kind kind+1
%endrep
record_size equ 118
case_count equ id
data_end:
