; NEC native-mode guest regression. Encodings from uPD70108/70116 manual.
; 8080 mode is not used. Exit through PCE hook: BX=0 success, BX=1 failure.
%include "pce.inc"
cpu 186
org 0x100
start:
 mov ax,cs
 mov ds,ax
 mov es,ax
 mov al,5
 mov cl,2
 stc
 db 0x0f,0x10,0xc0 ; TEST1 AL,CL
 jc fail
 jz fail
 cmp al,5
 jne fail
 db 0x0f,0x18,0xc0,3 ; TEST1 AL,3
 jnz fail
 mov ax,0xffff
 mov cl,15
 stc
 db 0x0f,0x13,0xc0 ; CLR1 AX,CL
 jnc fail
 cmp ax,0x7fff
 jne fail
 db 0x0f,0x1d,0xc0,15 ; SET1 AX,15
 cmp ax,0xffff
 jne fail
 db 0x0f,0x1f,0xc0,15 ; NOT1 AX,15
 cmp ax,0x7fff
 jne fail
 mov al,3
 mov byte [operand],0x95
 stc
 db 0x0f,0x28,0x06 ; ROL4 [operand]
 dw operand
 jnc fail
 and al,15 ; upper AL nibble is undefined
 cmp al,9
 jne fail
 cmp byte [operand],0x53
 jne fail
 mov al,3
 mov byte [operand],0x95
 stc
 db 0x0f,0x2a,0x06 ; ROR4 [operand]
 dw operand
 jnc fail
 and al,15
 cmp al,5
 jne fail
 cmp byte [operand],0x39
 jne fail
 ; Decimal strings: 9999 + 0001 = 0000 with carry, then subtract.
 mov si,bcdsrc
 mov di,bcddst
 mov cl,4
 db 0x0f,0x20
 jnc fail
 jnz fail
 cmp word [bcddst],0
 jne fail
 db 0x0f,0x22
 jnc fail
 cmp word [bcddst],0x9999
 jne fail
 mov word [bcdsrc],0x9999
 db 0x0f,0x26
 jnz fail
 jc fail
 ; Manual INS/EXT example crossing a word boundary.
 mov di,field
 mov ax,0x5555
 mov cl,3
 mov dl,4
 db 0x0f,0x31,0xd1 ; INS CL,DL: five bits
 db 0x0f,0x39,0xc1,12 ; INS CL,12: thirteen bits
 cmp cl,5
 jne fail
 mov si,field
 mov cl,3
 db 0x0f,0x33,0xd1 ; EXT CL,DL
 cmp ax,0x15
 jne fail
 db 0x0f,0x3b,0xc1,12
 cmp ax,0x1555
 jne fail
 ; REPC continues while subtraction borrows, then stops.
 mov si,compare_src
 mov di,compare_dst
 mov cx,3
 db 0x65,0xa6
 cmp cx,1
 jne fail
 xor bx,bx
 jmp done
fail:
 mov bx,1
done:
 mov ax,PCE_HOOK_ABORT
 db 0xd5,0
 mov ax,0x4c01
 int 0x21
operand db 0

bcdsrc dw 1
bcddst dw 0x9999
field dd 0
compare_src db 1,3,0
compare_dst db 2,2,0
