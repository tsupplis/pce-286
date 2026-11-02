; Experimental uPD9002 boot-sector fixture. Assemble with nasm -f bin.
; At HLT, AX=9002h means pass, DEADh means failed. No BIOS calls.
; Transition reference: nakatamaho/pc88va-dd, src/m01/m01test.asm
; commit faec29f61f710c8f170c30ea11897573a33f0f4c.
bits 16
org 7c00h
    jmp 0:start
start:
    cli
    xor ax,ax
    mov ds,ax
    mov ss,ax
    mov sp,7000h
    mov word [70h*4],zcode
    mov word [70h*4+2],ax
    mov word [71h*4],handler
    mov word [71h*4+2],ax
    xor si,si
    db 0fh,0ffh,70h       ; BRKEM
    cmp word [observed_ix],1234h
    jne failed
    cmp cx,5678h
    jne failed
    cmp al,5ah
    jne failed
    cmp sp,7000h
    jne failed
    mov ax,9002h
    jmp done
failed:
    mov ax,0deadh
done:
    hlt
    jmp done
zcode:
    db 0ddh,21h,34h,12h  ; LD IX,1234h
    db 01h,78h,56h        ; LD BC,5678h
    db 0d9h               ; EXX saves BC in the alternate bank
    db 3eh,00h,0d6h,01h  ; LD A,0 / SUB 1 sets carry
    db 0edh,0edh,71h     ; CALLN to native handler
    db 0d9h               ; EXX restores BC after IRET
    db 0deh,00h           ; SBC A,0 consumes restored carry
    db 0feh,0feh          ; CP FEh
    db 20h,02h            ; JR NZ skips success marker
    db 3eh,5ah           ; LD A,5Ah after IRET
    db 0edh,0fdh         ; RETEM to original caller
handler:
    mov [cs:observed_ix],si
    iret
observed_ix: dw 0
times 510-($-$$) db 0
dw 0aa55h
