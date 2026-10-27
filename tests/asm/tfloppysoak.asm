; Floppy soak: repeated DOS writes/readback, then guest reboot verification.
%ifndef ROUNDS
%define ROUNDS 64
%endif
; STOP hooks are synchronization points only; ABORT reports the final result.
; No BIOS/DOS services are replaced. Run on a fresh disposable floppy image.
cpu 286
org 100h
start:
 push cs
 pop ds
 push cs
 pop es
 cld
 mov dx,filename
 mov ax,3d00h
 int 21h
 jnc second_boot
 cmp ax,2
 jne fail_open
 mov bx,1111h
 mov ax,0100h
 db 0d5h,0
 xor ah,ah
 int 16h
 cmp ax,1e61h
 jne fail_keys
 xor ah,ah
 int 16h
 cmp ax,1e41h
 jne fail_keys
 xor ah,ah
 int 16h
 cmp ax,1c0dh
 jne fail_keys
.cycle:
 mov dx,filename
 xor cx,cx
 mov ah,3ch
 int 21h
 jc fail_create
 mov bx,ax
 mov dx,payload
 mov cx,payload_size
 mov ah,40h
 int 21h
 jc fail_write
 cmp ax,payload_size
 jne fail_write
 mov ah,3eh
 int 21h
 jc fail_close
 mov dx,filename
 mov ax,3d00h
 int 21h
 jc fail_open
 mov bx,ax
 call verify
 inc byte [payload]
 dec word [remaining]
 jnz .cycle
 mov bx,2222h
 mov ax,0100h
 db 0d5h,0
 ; Only the real BIOS keyboard handler can leave this loop by rebooting.
 sti
.wait:
 hlt
 jmp .wait
second_boot:
 mov byte [payload], (0a5h + ROUNDS - 1) & 255
 mov bx,ax
 call verify
 ; Keep this marker independent of the final exit status in BX: the CPU
 ; clock batch may execute the next instruction before the monitor stops.
 mov di,3333h
 mov ax,0100h
 db 0d5h,0
 xor bx,bx
 jmp exit
verify:
 mov dx,buffer
 mov cx,payload_size+1
 mov ah,3fh
 int 21h
 jc fail_read
 cmp ax,payload_size
 jne fail_read
 mov ah,3eh
 int 21h
 jc fail_close
 push ds
 pop es
 mov si,payload
 mov di,buffer
 mov cx,payload_size
 repe cmpsb
 jne fail_compare
 ret
fail_keys: mov bx,11
 jmp exit
fail_create: mov bx,12
 jmp exit
fail_write: mov bx,13
 jmp exit
fail_close: mov bx,14
 jmp exit
fail_open: mov bx,15
 jmp exit
fail_read: mov bx,16
 jmp exit
fail_compare: mov bx,17
exit:
 mov ax,0101h
 db 0d5h,0
 hlt
filename: db 'VERIFY.BIN',0
remaining: dw ROUNDS
payload:
%assign i 0
%rep 8193
 db ((i * 37) ^ (i >> 3) ^ 0a5h) & 255
%assign i i+1
%endrep
payload_size equ $-payload
buffer: times payload_size+1 db 0
