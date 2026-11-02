; GPL-2.0-only. Call at .COM startup while CS and DS address the PSP.
%macro require_dos2 0
    mov ax, 3000h
    int 21h
    cmp al, 2
    jae %%supported
    mov dx, %%message
    mov ah, 09h
    int 21h
    int 20h
%%message db 'Requires DOS 2.0 or later',13,10,'$'
%%supported:
%endmacro
