; Bounded CPL0 286 guest soak. No BIOS/DOS calls after entering PM.
; 10000 rounds: distinct data base, indexed word access, INT gate and IRET.
; STOP exposes BX=0 on success, BX=stage on failure, BP=completed rounds.
; Does not test privilege changes, task switching or return to real mode.
cpu 286
org 100h
start:
 cli
 mov ax,cs
 mov ds,ax
 mov bx,16
 mul bx                         ; DX:AX = full physical COM base
 mov [gdt+10],ax
 mov [gdt+12],dl
 mov [gdt+18],ax
 mov [gdt+20],dl
 mov [gdt+34],ax
 mov [gdt+36],dl
 push ax
 push dx
 add ax,gdt
 adc dx,0
 mov [gdtr+2],ax
 mov [gdtr+4],dl
 pop dx
 pop ax
 add ax,idt
 adc dx,0
 mov [idtr+2],ax
 mov [idtr+4],dl
 lgdt [gdtr]
 lidt [idtr]
 mov ax,1
 lmsw ax
 jmp 8:protected
protected:
 mov ax,16
 mov ds,ax
 mov ax,24
 mov es,ax
 mov ax,32
 mov ss,ax
 mov sp,stack_end
 xor bp,bp
round:
 mov bx,1
 smsw ax
 test ax,1
 jz fail
 mov ax,bp
 xor ax,0a55ah
 mov si,bp
 and si,1022
 mov [es:si],ax
 cmp [es:si],ax
 mov bx,2
 jne fail
 mov dx,ax
 xor di,di
 int 20h
 mov bx,3
 cmp di,0beefh
 jne fail
 cmp ax,dx
 jne fail
 cmp sp,stack_end
 jne fail
 cmp [es:si],dx
 jne fail
 inc bp
 cmp bp,10000
 jb round
 xor bx,bx
 mov di,0cafeh
fail:
 mov ax,0100h
 db 0d5h,0
 cli
halt: hlt
 jmp halt
handler:
 push ax
 mov ax,1234h
 mov di,0beefh
 pop ax
 iret
align 8
gdt:
 dq 0
 dw 0ffffh,0
 db 0,09ah,0,0
 dw 0ffffh,0
 db 0,092h,0,0
 dw 0ffffh,0
 db 8,092h,0,0              ; base 80000h, distinct from the COM segment
 dw 0ffffh,0
 db 0,092h,0,0
gdtr: dw 39
 dd 0
idtr: dw 20h*8+7
 dd 0
align 8
idt:
 times 20h*8 db 0
 dw handler,8
 db 0,086h,0,0             ; 286 interrupt gate
 times 512 db 0
stack_end:
