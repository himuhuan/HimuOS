; Loader

%include "boot.inc"
SECTION LOADER VSTART=LOADER_BASE_ADDRESS
LOADER_STACK_TOP EQU LOADER_BASE_ADDRESS
jmp loader_main
times 13 db 0
; -------------------------
; GDT start from 0x910
; -------------------------
GDT_BASE: 
    dd 0x00000000
    dd 0x00000000
FLAT_CODE_DESC:
    dd 0x0000FFFF
    dd DESC_CODE_HIGH4
FLAT_DATA_DESC:
    dd 0x0000FFFF
    dd DESC_DATA_HIGH4
VIDEO_DESC:
    dd 0x80000007
    dd DESC_VIDEO_HIGH4
GDT_SIZE equ $ - GDT_BASE
GDT_LIMIT equ GDT_SIZE - 1
times 60 dq 0 ; Reserve space for future use
; ^^^ LOADER_BASE_ADDRESS + 0x200 = 0xb00 ^^^
; -------------------------
; Data
; -------------------------
TOTAL_MEMORY_BYTES_ADDR EQU $
LOADER_DATA_ADDR EQU $
TotalMemoryBytes dd 0
GdtPtr: 
    dw GDT_LIMIT 
    dd GDT_BASE
Message db "Failed to get memory", 0
MessageLength equ $ - Message

ArdsBuf:
    times 320 db 0 ; 20 * 16
ArdsCnt dw 0
; -------------------------
; Selectors
; -------------------------
SELECTOR_CODE equ (0x0001 << 3) + TI_GDT + RPL0
SELECTOR_DATA equ (0x0002 << 3) + TI_GDT + RPL0
SELECTOR_VIDEO equ (0x0003 << 3) + TI_GDT + RPL0

loader_main:
    mov sp, LOADER_STACK_TOP

    ; -------------------------
    ; Memory
    ; -------------------------
    xor ebx, ebx
    mov edx, 0x534D4150 ; "SMAP"
    mov di, ArdsBuf
.try_e820:
    mov eax, 0x0000E820
    mov ecx, 20
    int 0x15
    jc .e820_fail
    add di, cx
    inc word [ArdsCnt]
    cmp ebx, 0
    jnz .try_e820
    mov cx, word [ArdsCnt]
    mov ebx, ArdsBuf
    xor edx, edx
.find_usable_memory:
    mov eax, [ebx]
    add eax, [ebx + 8]
    add ebx, 20
    cmp edx, eax
    jge .next_ard
    mov edx, eax
.next_ard:
    loop .find_usable_memory
    jmp .success_get_memory
.e820_fail:
    mov ax, Message
    mov bx, 0x07
    mov cx, MessageLength
    xor dx, dx
    call printstr
.success_get_memory:
    mov [TotalMemoryBytes], edx

    ; -------------------------
    ; Protected mode
    ; -------------------------
    ; Open A20 gate
    in	al, 92h
	or	al, 00000010b
	out	92h, al

    ; Load GDT
    lgdt [GdtPtr]

    ; Enter protected mode
    mov eax, cr0
    or eax, 0x00000001
    mov cr0, eax

    jmp dword SELECTOR_CODE:pmain

; ----------------------------------------------------
; printstr: Print a string
; ----------------------------------------------------
; ax: string address
; bx: color
; cx: string length
; dx: cursor position (zero to use next line)
printstr:
    cmp dx, 0
    jnz .print
    mov ah, 3
    mov bx, 0
    int 10h
    mov dl, 0
    inc dh
.print:
    mov bp, ax
    mov ax, 0x1301
    int 10h
    ret

[bits 32]
pmain:
    mov ax, SELECTOR_DATA
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, LOADER_STACK_TOP
    mov ax, SELECTOR_VIDEO
    mov gs, ax

    ; copy kernel file to KERNEL_BIN_ADDR
    mov eax, KERNEL_BIN_SECTOR
    mov ebx, KERNEL_BIN_ADDR
    mov ecx, 200
    call rd_disk_m_32

    call startup_page

    ; 修改 GdtPtr, 重新映射 Gdt 到内核空间
    ; 并修改显存地址使其位于内核空间
    sgdt [GdtPtr]
    mov ebx, [GdtPtr + 2]
    or dword [ebx + 0x18 + 4], 0xC0000000
    add dword [GdtPtr + 2], 0xC0000000

    ; 将栈指针移到内核空间
    add esp, 0xC0000000

    mov eax, PAGE_DIR_TABLE_BASE
    mov cr3, eax

    ; open cr0 pg bit
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [GdtPtr]

    jmp SELECTOR_CODE:enter_kernel

enter_kernel:
    call kernel_init
    mov esp, KERNEL_STACK_BOTTOM
    jmp KERNEL_ENTRY_POINT

    hlt

startup_page:
    mov ecx, 4096
    mov esi, 0
.clear_page_dir:
    mov byte [PAGE_DIR_TABLE_BASE + esi], 0
    inc esi
    loop .clear_page_dir
.create_pde:
    mov eax, PAGE_DIR_TABLE_BASE
    add eax, 0x1000
    mov ebx, eax
    or eax, PG_US_U | PG_RW_W | PG_P
    ; 第 0 PDE 和 第 768 PDE 都指向同一个页表（第0PTE)
    ; 第 0 PDE 是为了将 0x00000000 - 0x003FFFFF 映射到 0x00000000 - 0x003FFFFF.
    ; 第 768 PDE 是为了将 0x00000000 - 0x003FFFFF 映射到 0xC0000000 - 0xC03FFFFF. 
    ; 因为我们内核和 loader 位于 低端 4 MB 之内, 而我们规定内核将会映射到虚拟地址的高 3GB 以上 (0xC0000000 - 0xFFFFFFFF)
    ; 至于 0 PDE 是为了保证，对于 loader 代码 (0 - 0xfffff) ，线性地址和物理地址是一样的。 
    mov dword [PAGE_DIR_TABLE_BASE + 0x0], eax
    mov dword [PAGE_DIR_TABLE_BASE + 0xc00], eax
    ; 将最后 PDE 设为页目录表的物理地址，这是为了动态操作页表
    sub eax, 0x1000
    mov dword [PAGE_DIR_TABLE_BASE + 4092], eax

    ; 创建第 0 PDE 内的所有 PTE
    mov ecx, 1024 ; map 4MB
    mov esi, 0
    mov edx, PG_US_U | PG_RW_W | PG_P
.create_pte:
    mov dword [ebx + esi * 4], edx
    add edx, 4096
    inc esi
    loop .create_pte

    ; 创建内核其它 PDE
    mov eax, PAGE_DIR_TABLE_BASE
    add eax, 0x2000

    or eax, PG_RW_W | PG_US_U | PG_P
    mov ebx, PAGE_DIR_TABLE_BASE
    mov ecx, 254 ; 769 - 1022 PDE
    mov esi, 769
.create_kernel_pde:
    mov [ebx + esi * 4], eax
    inc esi
    add eax, 0x1000
    loop .create_kernel_pde
    ret

;-------------------------------------------------------------------------------
               ;功能:读取硬盘n个扇区
rd_disk_m_32:      
;-------------------------------------------------------------------------------
                             ; eax=LBA扇区号
                             ; ebx=将数据写入的内存地址
                             ; ecx=读入的扇区数
      mov esi,eax      ; 备份eax
      mov di,cx        ; 备份扇区数到di
;读写硬盘:
;第1步：设置要读取的扇区数
      mov dx,0x1f2
      mov al,cl
      out dx,al            ;读取的扇区数

      mov eax,esi      ;恢复ax

;第2步：将LBA地址存入0x1f3 ~ 0x1f6

      ;LBA地址7~0位写入端口0x1f3
      mov dx,0x1f3                       
      out dx,al                          

      ;LBA地址15~8位写入端口0x1f4
      mov cl,8
      shr eax,cl
      mov dx,0x1f4
      out dx,al

      ;LBA地址23~16位写入端口0x1f5
      shr eax,cl
      mov dx,0x1f5
      out dx,al

      shr eax,cl
      and al,0x0f      ;lba第24~27位
      or al,0xe0       ; 设置7～4位为1110,表示lba模式
      mov dx,0x1f6
      out dx,al

;第3步：向0x1f7端口写入读命令，0x20 
      mov dx,0x1f7
      mov al,0x20                        
      out dx,al

;;;;;;; 至此,硬盘控制器便从指定的lba地址(eax)处,读出连续的cx个扇区,下面检查硬盘状态,不忙就能把这cx个扇区的数据读出来

;第4步：检测硬盘状态
  .not_ready:          ;测试0x1f7端口(status寄存器)的的BSY位
      ;同一端口,写时表示写入命令字,读时表示读入硬盘状态
      nop
      in al,dx
      and al,0x88      ;第4位为1表示硬盘控制器已准备好数据传输,第7位为1表示硬盘忙
      cmp al,0x08
      jnz .not_ready       ;若未准备好,继续等。

;第5步：从0x1f0端口读数据
      mov ax, di       ;以下从硬盘端口读数据用insw指令更快捷,不过尽可能多的演示命令使用,
               ;在此先用这种方法,在后面内容会用到insw和outsw等

      mov dx, 256      ;di为要读取的扇区数,一个扇区有512字节,每次读入一个字,共需di*512/2次,所以di*256
      mul dx
      mov cx, ax       
      mov dx, 0x1f0
  .go_on_read:
      in ax,dx      
      mov [ebx], ax
      add ebx, 2
      loop .go_on_read
      ret

kernel_init:
    xor eax, eax
    xor ebx, ebx
    xor ecx, ecx
    xor edx, edx

    mov dx, [KERNEL_BIN_ADDR + 42]
    mov ebx, [KERNEL_BIN_ADDR + 28]

    add ebx, KERNEL_BIN_ADDR
    mov cx, [KERNEL_BIN_ADDR + 44]

.each_segment:
    cmp byte [ebx], PT_NULL
    je .PTNULL

    ; 准备mem_cpy参数
    push dword [ebx + 16]
    mov eax, [ebx + 4]
    add eax, KERNEL_BIN_ADDR
    push eax
    push dword [ebx + 8]

    call mem_cpy
    add esp, 12

.PTNULL:
    add ebx, edx
    loop .each_segment
    ret

mem_cpy:
    cld
    push ebp
    mov ebp, esp
    push ecx

    mov edi, [ebp + 8]
    mov esi, [ebp + 12]
    mov ecx, [ebp + 16]
    rep movsb

    pop ecx
    pop ebp
    ret