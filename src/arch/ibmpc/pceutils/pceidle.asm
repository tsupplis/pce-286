; GPL-2.0-only. Cooperative DOS idle hook for real-mode, single-task DOS.
cpu 8086
org 100h
jmp install
previous dw 0,0
signature db 'PCEIDLE1'
idle:
 pushf
 call far [cs:previous]
 push bp
 mov bp,sp
 test word [bp+6],0200h
 jz .return
 sti
 hlt
.return:
 pop bp
 iret
resident_end:
%include "pceutils/dos-version.inc"
install:
	require_dos2
 mov ax,3528h
 int 21h
 mov ax,es
 or ax,bx
 jz .no_vector
 cmp bx,idle
 jne .new
 mov si,signature
 mov di,si
 mov cx,8
 cld
 repe cmpsb
 je .done
.new:
 mov [previous],bx
 mov [previous+2],es
 mov dx,idle
 mov ax,2528h
 int 21h
 mov dx,msg
 mov ah,9
 int 21h
 mov dx,(resident_end-$$+100h+15)/16
 mov ax,3100h
 int 21h
.done:
 mov ax,4c00h
 int 21h
.no_vector:
 mov dx,no_idle
 mov ah,9
 int 21h
 mov ax,4c01h
 int 21h
no_idle db 'PCEIDLE: no INT 28h handler',13,10,'$'
msg db 'PCEIDLE installed',13,10,'$'
