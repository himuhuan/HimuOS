/*
 * Minimal bootstrap userspace linker script.
 *
 * Keep .text at the staged code page base and start .rodata at the const-page
 * payload slot so the runtime-owned capability seed block can be prepended at
 * EX_USER_IMAGE_CONST_BASE without requiring a runtime ELF loader.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x0000000080000000;
    .text :
    {
        KEEP(*(.text._start))
        *(.text .text.*)
    }

    .rodata 0x0000000080001018 : SUBALIGN(1)
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    .bss :
    {
        *(.bss .bss.* COMMON)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note*)
    }
}

ASSERT(SIZEOF(.text) <= 0x1000, "userspace bootstrap .text exceeds one code page");
ASSERT((SIZEOF(.rodata) + 0x18) <= 0x1000, "userspace bootstrap .rodata exceeds const payload page");
ASSERT(SIZEOF(.data) == 0, "userspace bootstrap .data is unsupported");
ASSERT(SIZEOF(.bss) == 0, "userspace bootstrap .bss is unsupported");
