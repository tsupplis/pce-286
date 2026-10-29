; Early-NPX transcendental characterization, GPL-2.0-or-later.
; Raw observations only. Run explicitly on a known original 8087/80287.
bits 16
cpu 8086
org 100h
start:
 push cs
 pop ds
 mov si,records
 mov cx,case_count
.next:
 pushf
 cli
 fninit
 fwait
 fnstcw [initial_cw]
 fwait
 test word [initial_cw],80h
 jz .setup
 or word [si+2],80h
.setup:
 mov al,[si+4]
 mov [operation+1],al
 cmp byte [si+5],1
 je .ready
 cmp byte [si+5],2
 je .full
 fld tword [si+18] ; Y (ignored by unary operations)
 fwait
 fld tword [si+8] ; X
 fwait
 jmp short .ready
.full:
 fld tword [si+8]
 fwait
 push cx
 mov cx,7
.fill:
 fld st0
 fwait
 loop .fill
 pop cx
.ready:
 fldcw [si+2]
 fwait
operation:
 db 0d9h,0f2h
 ; No waiting instruction before saving a pending unmasked exception.
 fnsave [si+28]
 fwait
 popf
 add si,record_size
 loop start.next
 mov dx,filename
 xor cx,cx
 mov ah,3ch
 int 21h
 jc error
 mov bx,ax
 mov dx,header
 mov cx,data_end-header
 mov ah,40h
 int 21h
 jc .close_error
 cmp ax,data_end-header
 jne .close_error
 mov ah,3eh
 int 21h
 jc error
 mov ax,4c00h
 int 21h
.close_error:
 mov ah,3eh
 int 21h
error:
 mov ax,4c01h
 int 21h
initial_cw: dw 0
filename: db 'NPXTRANS.BIN',0
align 2
header:
 db 'NPXTRN1',0
 dw case_count,record_size
records:
%assign id 0
%assign kind 0
%rep 10
%assign rc 0
%rep 4
%assign masks 0
%rep 2
 dw id
 ; Either all masks, or I/U/O/P unmasked (D/Z stay masked).
 dw 0346h | (rc<<10) | (masks*39h)
%if kind = 0 || kind = 1 || kind = 2 || kind = 3 || kind = 4 || kind = 5
 db 0f2h
%elif kind = 6 || kind = 7
 db 0f0h
%elif kind = 8
 db 0f1h
%else
 db 0f9h
%endif
%if kind = 4
 db 1 ; empty
%elif kind = 5
 db 2 ; full
%else
 db 0
%endif
 dw 0
%if kind = 0
 dq 0
 dw 0
%elif kind = 1
 dq 8000000000000000h
 dw 3fbfh ; 2^-64
%elif kind = 2
 dq 8000000000000000h
 dw 3fc0h ; 2^-63
%elif kind = 3 || kind = 4 || kind = 5
 dq 8000000000000000h
 dw 3ffdh ; 1/4
%elif kind = 6 || kind = 9
 dq 8000000000000000h
 dw 1 ; smallest normal
%elif kind = 7
 dq 0
 dw 8000h ; negative zero
%else
 dq 8000000000000000h
 dw 4001h ; 4, log2 = 2
%endif
%if kind = 8
 dq 0ffffffffffffffffh
 dw 7ffeh ; largest normal Y, logarithm overflows
%elif kind = 9
 dq 8000000000000000h
 dw 1 ; logarithm product underflows
%else
 dq 8000000000000000h
 dw 3fffh
%endif
 times 94 db 0
%assign id id+1
%assign masks masks+1
%endrep
%assign rc rc+1
%endrep
%assign kind kind+1
%endrep
case_count equ id
record_size equ 122
data_end:
