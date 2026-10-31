; Native OS/2 1.x NE calculation probe. GPL-2.0-or-later.
; NASM -f bin. Exit 0 for round(sqrt(1+1)^2)==2, otherwise exit 1.
; Code/data segments are ring 3. DOSCALLS ordinal 5 is DosExit.
bits 16
org 0
db 'MZ'
dw 64,1,0,4,0,0xffff,0,0,0,0,0,0x40,0
times 0x3c-($-$$) db 0
dd ne
ne: db 'NE',5,1
dw entries-ne,2
dd 0
dw 0x202,2,0,1024
dw 0,1,0,2,2,1,0
dw segments-ne,resident-ne,resident-ne,modules-ne,imports-ne
dd 0
dw 0,4,0
db 1,0
times 0x40-($-ne) db 0
segments: dw (code-$$)/16,codeend-code,0xd10,codeend-code
dw 0,1024,0xc01,1024
resident: db 5,'NPX13'
dw 0
db 0
modules: dw 0
imports: db 8,'DOSCALLS'
entries: dw 0
align 16,db 0
code:
 fninit
 fld1
 fld1
 faddp
 fsqrt
 fmul st0,st0
 sub sp,2
 mov bp,sp
 fistp word [bp]
 wait
 pop ax
 cmp ax,2
 mov ax,1
 jne exit
 dec ax
exit:
 push word 1
 push ax
 db 0x9a
fixup: dw 0xffff,0
codeend:
 dw 1
 db 3,1
 dw fixup-code,1,5
