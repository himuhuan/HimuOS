; loader & kernel defines

; ---------------------------------------------------
; LOADER
; ---------------------------------------------------

LOADER_BASE_ADDRESS equ 0x900
LOADER_START_SECTOR equ 0x2

; ---------------------------------------------------
; KERNEL
; ---------------------------------------------------
PAGE_DIR_TABLE_BASE equ     0x500000
KERNEL_ENTRY_POINT equ    0xc0100000
KERNEL_BIN_SECTOR equ            0x9
KERNEL_BIN_ADDR equ         0x300000
KERNEL_STACK_BOTTOM equ   0xc0400000

; ---------------------------------------------------
; GDT ATTRIBUTES
; ---------------------------------------------------
; gdt描述符属性

DESC_G_4K equ       100000000000000000000000b ; 段描述符高23位，表示段界限的粒度为4KB 
DESC_D_32 equ        10000000000000000000000b ; D/B为，1表示运行在32位模式下
DESC_L equ            0000000000000000000000b ; 高21位，如果为1表示为64位代码段，目前我们都是在32位模式下操作，故为零
DESC_AVL equ           000000000000000000000b ; Reserved
; 第二部分段界限值，由于采用了32位平坦模型，所以段界限为(4GB / 4KB) - 1 = 0xFFFFF，故为全1
DESC_LIMIT_CODE2 equ    1111_0000_0000_0000_0000b
DESC_LIMIT_DATA2 equ    1111_0000_0000_0000_0000b
; 视频内存段：0xB8000 - 0xBFFFF 对于 0xB8000, 低16位在 0x8000, 高4位在此处定义
DESC_LIMIT_VIDEO2 equ   0000_0000_0000_0000_1011b
; Present: 段是否存在于内存中，在平坦模型下总是为 1
DESC_P equ                   1000_0000_0000_0000b
DESC_DPL_0 equ                000_0000_0000_0000b
DESC_DPL_1 equ                010_0000_0000_0000b
DESC_DPL_2 equ                100_0000_0000_0000b
DESC_DPL_3 equ                110_0000_0000_0000b
; S: 1 表示代码段或数据段，0 表示系统段
DESC_S_CODE equ                 1_0000_0000_0000b
DESC_S_DATA equ                 1_0000_0000_0000b
DESC_S_sys equ                  0_0000_0000_0000b
; Type:
; 代码段: x 可执行、c 一致、r 可读、a 已访问
; 1000: 可执行、非一致、不可读
DESC_TYPE_CODE equ                1000_0000_0000b
; 数据段: x 可执行、e 向下扩展、w 可写、a 已访问
; 0010: 不可执行、向上扩展、可写
DESC_TYPE_DATA equ                0010_0000_0000b

; 代码段描述符的高32位表示，其中(0x00 << 24表示最高8位的段基址值，由于我们采用的是平坦模型，故基址为零)，后面唯一可变的就是段界限值
DESC_CODE_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
    DESC_L + DESC_AVL + DESC_LIMIT_CODE2 + \
    DESC_P + DESC_DPL_0 + DESC_S_CODE + DESC_TYPE_CODE + 0x00

DESC_DATA_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
    DESC_L + DESC_AVL + DESC_LIMIT_DATA2 + \
    DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00

DESC_VIDEO_HIGH4 equ (0x00 << 24) + DESC_G_4K + DESC_D_32 + \
    DESC_L + DESC_AVL + DESC_LIMIT_VIDEO2 + \
    DESC_P + DESC_DPL_0 + DESC_S_DATA + DESC_TYPE_DATA + 0x00

; 选择字属性
RPL0 equ 00b
RPL1 equ 01b
RPL2 equ 10b
RPL3 equ 11b
TI_GDT equ 000b
TI_LDT equ 100b

; Page Table Attributes
PG_P equ 1b
PG_RW_R equ 00b
PG_RW_W equ 10b
PG_US_S equ 000b
PG_US_U equ 100b

PT_NULL equ 0