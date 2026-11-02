; DOS 2+ on PCE upd9002. ZEXRUN filename (CP/M ZEXDOC or ZEXALL).
; Dedicated 64K guest segment. BDOS 2 and 9 write to DOS stdout.
bits 16
cpu 8086
org 100h
    cld
    mov si,81h
    xor cx,cx
    mov cl,[80h]
.skip:
    or cx,cx
    jz error
    lodsb
    dec cx
    cmp al,' '
    je .skip
    lea dx,[si-1]
    add si,cx
    mov byte [si],0
    mov ax,3d00h
    int 21h
    jc error
    mov bx,ax
    mov ax,cs
    add ax,1000h             ; leave native stack in its original segment
    jc error
    mov dx,[2]               ; PSP end of allocation, in paragraphs
    sub dx,ax
    jc error
    cmp dx,1000h
    jb error
    mov ds,ax
    mov dx,100h
    mov cx,0fe00h
    mov ah,3fh
    int 21h
    jc error
    cmp ax,cx
    jae error
    or ax,ax
    jz error
    mov ah,3eh
    int 21h
    jc error
    mov word [0],0fdedh      ; warm boot -> RETEM
    mov byte [5],0c3h        ; JP FF00h; word at 6 also gives initial SP
    mov word [6],0ff00h
    mov word [0ff00h],0ededh ; CALLN 61h
    mov word [0ff02h],0c961h ; vector, RET
    pushf
    cli
    xor ax,ax
    mov es,ax
    mov bx,60h*4
    push word [es:bx]
    push word [es:bx+2]
    push word [es:bx+4]
    push word [es:bx+6]
    mov word [es:bx],100h
    mov [es:bx+2],ds
    mov word [es:bx+4],bdos
    mov [es:bx+6],cs
    sti
    db 0fh,0ffh,60h
    cli
    xor ax,ax
    mov es,ax
    mov bx,60h*4
    pop word [es:bx+6]
    pop word [es:bx+4]
    pop word [es:bx+2]
    pop word [es:bx]
    popf
    mov ax,4c00h
    int 21h
error:
    push cs
    pop ds
    mov dx,errmsg
    mov ah,9
    int 21h
    mov ax,4c01h
    int 21h
bdos:
    cmp cl,2
    je .print
    cmp cl,9
    jne .done
.print:
    mov ah,cl
    int 21h
.done:
    iret
errmsg: db 'Usage: ZEXRUN filename (requires a readable CP/M binary and 128K)',13,10,'$'
