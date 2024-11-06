;
; HIMU OPERATING SYSTEM
;
; File: krnlio.asm
; Kernel I/O functions
; Copyright (C) 2024 HimuOS Project, all rights reserved.

; All builtin functions has __himuos__ prefix to avoid conflicts with other functions

%include "osbase32.inc"

section .data
put_int_buffer    dq    0

[bits 32]
section .text

; Function: getcrpos
; get current cursor position
; - return ax: cursor position
; ---------------------------------------------------------
global __himuos__getcrpos
__himuos__getcrpos:
    push edx
    mov dx, 0x03D4
    mov al, 0x0E
    out dx, al
    mov dx, 0x03D5
    in al, dx
    mov ah, al
    mov dx, 0x03D4
    mov al, 0x0F
    out dx, al
    mov dx, 0x03D5
    in al, dx
    pop edx
    ret

; Function: printc
; write a character in stack to the screen
; ---------------------------------------------------------
global __himuos__printc
__himuos__printc:
    pushad
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    call __himuos__getcrpos
    mov bx, ax
    mov ecx, [esp + 36]

    cmp cl, 0xd
    jz .key_cr
    cmp cl, 0xa
    jz .key_lf
    cmp cl, 0x8
    jz .key_backspace
    jmp .key_other
.key_backspace:
    dec bx
    shl bx, 1
    mov byte [gs:bx], 0
    inc bx
    mov byte [gs:bx], 0x07
    shr bx, 1
    jmp .set_cursor
.key_other:
    shl bx, 1
    mov byte [gs:bx], cl
    inc bx
    mov byte [gs:bx], 0x07
    shr bx, 1
    inc bx
    cmp bx, 2000
    jl .set_cursor
; we consider carriage returns and line breaks as the same situation (CRLF)
.key_lf:
.key_cr:
    xor dx, dx
    mov ax, bx
    mov si, 80
    div si
    sub bx, dx
    add bx, 80
    cmp bx, 2000
    jl .set_cursor
.roll_screeen:
    cld 
    mov ecx, 960

    mov esi, 0xc00b80a0
    mov edi, 0xc00b8000
    rep movsd

    mov ebx, 3840
    mov ecx, 80

.cls:
    mov word [gs:ebx], 0x0720
    add ebx, 2
    loop .cls
    mov bx, 1920

.set_cursor:
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5
    mov al, bh
    out dx, al

    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al
.printc_end:
    popad
    ret

; Function: clscr
; clear the screen and set the cursor to the top left corner
; ---------------------------------------------------------
global __himuos__clscr
__himuos__clscr:
    pushad
    mov ax, SELECTOR_VIDEO
    mov gs, ax
    xor ebx, ebx
    mov ecx, 2000
.clscr_loop:
    mov word [gs:bx], 0x0720
    add ebx, 2
    loop .clscr_loop
    mov bx, 0
    mov dx, 0x03d4
    mov al, 0x0e
    out dx, al
    mov dx, 0x03d5
    mov al, bh
    out dx, al
    mov dx, 0x03d4
    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al
    popad
    ret

; Function: printstr
; print a string to the screen
; ---------------------------------------------------------
global __himuos__printstr
__himuos__printstr:
    push ebx
    push ecx
    mov ecx, 0
    mov ebx, [esp + 12]
.printstr_loop:
    mov cl, [ebx]
    cmp cl, 0
    jz .printstr_end
    push ecx
    call __himuos__printc
    add esp, 4
    inc ebx
    jmp .printstr_loop
.printstr_end:
    pop ecx
    pop ebx
    ret
; Function: printintx
; print a integer to the screen, 16-bit
global __himuos__printintx
__himuos__printintx:
   pushad
   mov ebp, esp
   mov eax, [ebp+4*9]		       ; call的返回地址占4字节+pushad的8个4字节
   mov edx, eax
   mov edi, 7                          ; 指定在put_int_buffer中初始的偏移量
   mov ecx, 8			       ; 32位数字中,16进制数字的位数是8个
   mov ebx, put_int_buffer

;将32位数字按照16进制的形式从低位到高位逐个处理,共处理8个16进制数字
.16based_4bits:			       ; 每4位二进制是16进制数字的1位,遍历每一位16进制数字
   and edx, 0x0000000F		       ; 解析16进制数字的每一位。and与操作后,edx只有低4位有效
   cmp edx, 9			       ; 数字0～9和a~f需要分别处理成对应的字符
   jg .is_A2F 
   add edx, '0'			       ; ascii码是8位大小。add求和操作后,edx低8位有效。
   jmp .store
.is_A2F:
   sub edx, 10			       ; A~F 减去10 所得到的差,再加上字符A的ascii码,便是A~F对应的ascii码
   add edx, 'A'

;将每一位数字转换成对应的字符后,按照类似“大端”的顺序存储到缓冲区put_int_buffer
;高位字符放在低地址,低位字符要放在高地址,这样和大端字节序类似,只不过咱们这里是字符序.
.store:
; 此时dl中应该是数字对应的字符的ascii码
   mov [ebx+edi], dl		       
   dec edi
   shr eax, 4
   mov edx, eax 
   loop .16based_4bits

;现在put_int_buffer中已全是字符,打印之前,
;把高位连续的字符去掉,比如把字符000123变成123
.ready_to_print:
   inc edi			       ; 此时edi退减为-1(0xffffffff),加1使其为0
.skip_prefix_0:  
   cmp edi,8			       ; 若已经比较第9个字符了，表示待打印的字符串为全0 
   je .full0 
;找出连续的0字符, edi做为非0的最高位字符的偏移
.go_on_skip:   
   mov cl, [put_int_buffer+edi]
   inc edi
   cmp cl, '0' 
   je .skip_prefix_0		       ; 继续判断下一位字符是否为字符0(不是数字0)
   dec edi			       ;edi在上面的inc操作中指向了下一个字符,若当前字符不为'0',要恢复edi指向当前字符		       
   jmp .put_each_num

.full0:
   mov cl,'0'			       ; 输入的数字为全0时，则只打印0
.put_each_num:
   push ecx			       ; 此时cl中为可打印的字符
   call __himuos__printc
   add esp, 4
   inc edi			       ; 使edi指向下一个字符
   mov cl, [put_int_buffer+edi]	       ; 获取下一个字符到cl寄存器
   cmp edi,8
   jl .put_each_num
   popad
   ret
