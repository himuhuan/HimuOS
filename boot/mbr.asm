; MBR

%include "boot.inc"

SECTION MBR VSTART=0x7C00
    MOV AX, CS
    MOV DS, AX
    MOV ES, AX
    MOV SS, AX
    MOV FS, AX
    MOV SP, 0X7C00
    MOV AX, 0XB800
    MOV GS, AX

    mov bx, 0x07
    call cleanscr

    mov ax, Message
    mov bx, 0007h
    mov cx, MessageLen
    mov dx, 0
    call printstr

    ; read loader to memory 
    mov byte [disk_address_packet + 2], 4
    mov word [disk_address_packet + 4], LOADER_BASE_ADDRESS
    mov dword [disk_address_packet + 8], LOADER_START_SECTOR
    call rddisk

    ; jump to loader
    jmp LOADER_BASE_ADDRESS

; ----------------------------------------------------
; Clean the screen
; bx: color
cleanscr:
; ----------------------------------------------------
    mov ax, 0x600
    mov cx, 0
    mov dh, 24
    mov dl, 79
    int 10h
    ret
; ----------------------------------------------------
; printstr: Print a string
; ----------------------------------------------------
; ax: string address
; bx: color
; cx: string length
; dx: cursor position (zero to use current position)
printstr:
    cmp dx, 0
    jnz .print
    mov ah, 3
    mov bx, 0
    int 10h
.print:
    mov bp, ax
    mov ax, 0x1301
    int 10h
    ret
; ----------------------------------------------------
; rdisk: read disk sector to memory
; ----------------------------------------------------
; Entry:
;     - fields disk_address_packet should have been filled
;       before invoking the routine
; Exit:
;     - es:bx -> data read
; registers changed:
;     - eax, ebx, dl, si, es
rddisk:
    xor	ebx, ebx

	mov	ah, 0x42
	mov	dl, 0x80
	mov	si, disk_address_packet
	int	0x13

	mov	ax, [disk_address_packet + 6]
	mov	es, ax
	mov	bx, [disk_address_packet + 4]

	ret
; ----------------------------------------------------
; String & Data
; ----------------------------------------------------
    Message db "Now starting loader...", 0
    MessageLen equ $-Message
disk_address_packet:
    db 0x10 ; [0] size of packet
    db 0x00 ; [1] reserved
    db 0x00 ; [2] number of blocks to read
    db 0x00 ; [3] reserved
    dw 0x0000 ; [4] offset of buffer
    dw 0x0000 ; [6] segment of buffer
    dd 0 ; [8] LBA. low 32 bits
    dd 0 ; [12] LBA. high 32 bits  

    times 510-($-$$) db 0

    db 0x55, 0xaa