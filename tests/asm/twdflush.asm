; Guest PIO write to cylinder 0, head 0, sector 2. BH holds expected ERR.
bits 16
mov dx,1f2h
mov al,1
out dx,al
inc dx
mov al,2
out dx,al
inc dx
xor al,al
out dx,al
inc dx
out dx,al
inc dx
mov al,0a0h
out dx,al
inc dx
mov al,30h
out dx,al
drq: in al,dx
test al,80h
jnz drq
test al,8
jz fail
mov dx,1f0h
mov ax,5555h
mov cx,256
data: out dx,ax
loop data
mov dx,1f7h
busy: in al,dx
test al,80h
jnz busy
and al,1
cmp al,bh
jne fail
mov bl,0
jmp done
fail: mov bl,1
done: xor bh,bh
mov ax,0101h
db 0d5h,0
hlt
