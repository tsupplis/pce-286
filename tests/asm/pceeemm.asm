cpu 286
org 100h
%macro check 0
 or ah,ah
 jnz fail
%endmacro
mov ah,42h
int 67h
check
cmp bx,104
jne fail
mov ah,41h
int 67h
check
mov es,bx
mov bx,104
mov ah,43h
int 67h
check
mov [h],dx
xor bx,bx
.fill:
mov ax,4400h
int 67h
check
mov [es:0],bx
mov [es:3ffeh],bx
inc bx
cmp bx,104
jb .fill
mov ah,47h
int 67h
check
xor bx,bx
.verify:
mov ax,4400h
int 67h
check
cmp [es:0],bx
jne fail
cmp [es:3ffeh],bx
jne fail
inc bx
cmp bx,104
jb .verify
xor bx,bx
mov ax,4400h
int 67h
check
mov ah,48h
int 67h
check
cmp word [es:0],103
jne fail
; Independent access through board ports aliases the last logical page in slot 1.
mov dx,219h
mov al,0b9h
out dx,al
dec dx
mov al,0ffh
out dx,al
cmp word [es:4000h],103
jne fail
mov dx,[h]
mov ah,45h
int 67h
check
mov ah,42h
int 67h
check
cmp bx,104
jne fail
mov dx,ok
jmp done
fail: mov dx,bad
done: mov ah,9
int 21h
int 20h
ok db 'PCEEEMM 104 PAGES AND BOARD ALIAS PASS',13,10,'$'
bad db 'PCEEEMM FAILED',13,10,'$'
h dw 0
