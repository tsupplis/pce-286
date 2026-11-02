; Resident LIM 3.2 services for the 2 MiB PCE EEMS board. GPL-2.0-only.
; Physical pages 0..23 back conventional RAM. Allocate only pages 24..127.
; A logical page is the nth physical page owned by its handle.
%define NPAGES 104
%define NHANDLES 32
owners: times NPAGES db 0
counts: times NHANDLES+1 db 0
saved: times NHANDLES+1 db 0
contexts: times (NHANDLES+1)*4 db 0
mapping: times 4 db 0
available: dw NPAGES
active: dw 0

int_67:
 push ds
 push si
 push bp
 push cs
 pop ds
 cld
 cmp ah,40h
 jb .unsupported
 cmp ah,4dh
 ja .unsupported
 mov si,ax
 shr si,8
 sub si,40h
 shl si,1
 jmp word [dispatch+si]
.unsupported:
 mov ah,84h
 jmp finish
dispatch: dw success,frame,capacity,allocate,map_page,release,version,save_map,restore_map
          dw int_67.unsupported,int_67.unsupported,handle_count,handle_size,enumerate
frame:
 mov bx,0e000h
 jmp success
capacity:
 mov bx,[available]
 mov dx,NPAGES
 jmp success
version:
 mov al,32h
 jmp success
handle_count:
 mov bx,[active]
 jmp success
handle_size:
 call valid
 jc finish
 xor bx,bx
 mov bl,[counts+si]
 jmp success
valid:
 mov si,dx
 cmp si,NHANDLES
 ja .bad
 cmp byte [counts+si],0
 je .bad
 clc
 ret
.bad:
 mov ah,83h
 stc
 ret
allocate:
 test bx,bx
 jz .zero
 cmp bx,NPAGES
 ja .too_many
 cmp bx,[available]
 ja .insufficient
 mov si,1
.find:
 cmp byte [counts+si],0
 je .found
 inc si
 cmp si,NHANDLES
 jbe .find
 mov ah,85h
 jmp finish
.zero: mov ah,89h
 jmp finish
.too_many: mov ah,87h
 jmp finish
.insufficient: mov ah,88h
 jmp finish
.found:
 mov dx,si
 mov [counts+si],bl
 inc word [active]
 sub [available],bx
 mov bp,bx
 xor si,si
.assign:
 cmp byte [owners+si],0
 jne .next
 mov [owners+si],dl
 dec bp
 jz success
.next:
 inc si
 jmp .assign
map_page:
 call valid
 jc finish
 cmp al,3
 ja .badslot
 xor ah,ah
 mov bp,ax
 cmp bx,0ffffh
 je .unmap
 cmp bl,[counts+si]
 jae .badpage
 test bh,bh
 jnz .badpage
 push bx
 xor si,si
.search:
 cmp [owners+si],dl
 jne .next
 test bx,bx
 jz .found
 dec bx
.next:
 inc si
 jmp .search
.found:
 pop bx
 mov ax,si
 add al,80h+24
 jmp .map
.unmap:
 xor al,al
.map:
 mov si,bp
 call program_slot
 mov ax,bp
 jmp success
.badslot: mov ah,8bh
 jmp finish
.badpage: mov ah,8ah
 jmp finish
; SI = frame slot, AL = encoded board page (0 disables the slot).
program_slot:
 push ax
 push dx
 mov [mapping+si],al
 mov ax,si
 add al,80h+56
 mov dx,219h
 out dx,al
 mov al,[mapping+si]
 dec dx
 out dx,al
 pop dx
 pop ax
 ret
save_map:
 call valid
 jc finish
 cmp byte [saved+si],0
 jne .already
 mov byte [saved+si],1
 shl si,2
 mov bp,[mapping]
 mov [contexts+si],bp
 mov bp,[mapping+2]
 mov [contexts+si+2],bp
 jmp success
.already: mov ah,8dh
 jmp finish
restore_map:
 call valid
 jc finish
 cmp byte [saved+si],0
 je .none
 mov byte [saved+si],0
 shl si,2
 mov bp,si
 xor si,si
.loop:
 mov al,[ds:contexts+bp+si]
 call program_slot
 inc si
 cmp si,4
 jb .loop
 jmp success
.none: mov ah,8eh
 jmp finish
release:
 call valid
 jc finish
 cmp byte [saved+si],0
 jne .busy
 xor ah,ah
 mov al,[counts+si]
 add [available],ax
 mov byte [counts+si],0
 dec word [active]
 xor si,si
.page:
 cmp [owners+si],dl
 jne .next
 mov byte [owners+si],0
 push si
 mov ax,si
 add al,80h+24
 ; Invalidate references before another handle can reuse the physical page.
 xor si,si
.slot:
 cmp [mapping+si],al
 jne .skip
 push ax
 xor al,al
 call program_slot
 pop ax
.skip:
 inc si
 cmp si,4
 jb .slot
 xor si,si
.saved_slot:
 cmp [contexts+si],al
 jne .keep
 mov byte [contexts+si],0
.keep:
 inc si
 cmp si,(NHANDLES+1)*4
 jb .saved_slot
 pop si
.next:
 inc si
 cmp si,NPAGES
 jb .page
 jmp success
.busy: mov ah,86h
 jmp finish
enumerate:
 push di
 xor bx,bx
 mov si,1
.loop:
 xor ax,ax
 mov al,[counts+si]
 test ax,ax
 jz .next
 mov [es:di],si
 mov [es:di+2],ax
 add di,4
 inc bx
.next:
 inc si
 cmp si,NHANDLES
 jbe .loop
 pop di
success:
 xor ah,ah
finish:
 pop bp
 pop si
 pop ds
 iret
