; BIOS/DOS acceptance of the configured NPX and its board error route.
bits 16
org 100h
start:
    push cs
    pop ds
    int 11h
    test ax,2
    jz fail
    fninit
    fld1
    fld1
    faddp st1,st0
    fistp word [result]
    wait
    cmp word [result],2
    jne fail
    ; Install INT2 handler: XT NMI, or AT BIOS IRQ13 -> INT2 bridge.
    mov dx,handler
    mov ax,2502h
    int 21h
%ifdef AT
    in al,0xa1
    and al,0xdf
    out 0xa1,al
    in al,0x21
    and al,0xfb
    out 0x21,al
%else
    mov al,0x80
    out 0xa0,al
%endif
    fldcw [cw]
    fld1
    fldz
    fdivp st1,st0
    wait
    cmp word [handled],1
    jne fail
    test word [saved],4
    jz fail
    ; Recovery must permit another numeric instruction.
    fld1
    fistp word [result]
    wait
    cmp word [result],1
    jne fail
    xor bx,bx
    jmp exit
fail:
    mov bx,1
exit:
    mov ax,0101h
    db 0xd5,0
    hlt
handler:
    fnstsw [cs:saved]
    fnclex
    inc word [cs:handled]
    iret
cw dw 037bh
result dw 0
saved dw 0
handled dw 0
