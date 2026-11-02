; DOS 2+ .COM acceptance probe for the experimental upd9002 PC CPU.
bits 16
cpu 8086
org 100h
start:
    ; Save the two vectors on the native stack. Mask IRQs only for the probe.
    pushf
    cli
    xor ax,ax
    mov es,ax
    mov bx,70h*4
    push word [es:bx]
    push word [es:bx+2]
    push word [es:bx+4]
    push word [es:bx+6]
    mov word [es:bx],guest
    mov [es:bx+2],cs
    mov word [es:bx+4],handler
    mov [es:bx+6],cs
    mov ax,cs
    add ax,100h
    mov ds,ax              ; DS base is 4096 bytes above CS
    db 0fh,0ffh,70h        ; BRKEM
    mov bx,70h*4
    pop word [es:bx+6]
    pop word [es:bx+4]
    pop word [es:bx+2]
    pop word [es:bx]
    popf
    cmp byte [slot],5ah
    jne fail
    cmp byte [slot+1],0a5h
    jne fail
    cmp word [cs:slot],0cccch
    jne fail
    cmp ax,0a5h            ; handler cleared AH, Z80 loaded A from DS
    jne fail
    cmp byte [cs:called],1
    jne fail
    mov dx,passed
    xor bx,bx
    jmp report
fail:
    mov dx,failed
    mov bx,1
report:
    push cs
    pop ds
    mov ah,09h
    int 21h
    mov ax,4c00h
    or ax,bx
    int 21h

guest:
    db 0ddh,21h            ; LD IX,slot-2
    dw slot-2
    db 0ddh,36h,2,5ah      ; LD (IX+2),5Ah
    db 0fdh,21h            ; LD IY,slot+3
    dw slot+3
    db 0fdh,36h,0feh,0a5h  ; LD (IY-2),A5h
    db 0edh,0edh,71h       ; CALLN
    db 0fdh,7eh,0feh       ; LD A,(IY-2)
    db 0edh,0fdh           ; RETEM
handler:
    inc byte [cs:called]
    xor ax,ax
    iret
called: db 0
passed: db 'UPD9002 PASS',13,10,'$'
failed: db 'UPD9002 FAIL',13,10,'$'
slot: dw 0cccch
; Give DS its own storage, including different bytes at the code offsets.
times 4096 db 0
