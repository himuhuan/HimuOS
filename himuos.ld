/*
 * HimuOS Kernel Linker Script
 *
 * This script produces a simple, clean ELF layout for the HimuOS kernel,
 * conforming to its specific loading requirements.
 */
 
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

KERNEL_BASE = 0xFFFF800000000000;
KERNEL_LIMIT = 0xFFFF808000000000; /* Kernel must be below its STACK */

PHDRS
{
    ro PT_LOAD FLAGS(5); /* RE */
    rw PT_LOAD FLAGS(6); /* RW */
}

SECTIONS
{
    . = KERNEL_BASE;
    PROVIDE(__kernel_start = .);
    
    .text : ALIGN(0x1000)
    {
        *(.text .text.*) /* Kernel text sections */
        *(.rodata .rodata.*) /* Read-only data sections */
        *(.init .init.*) /* Initialization code */
        *(.fini .fini.*) /* Finalization code */
        *(.eh_frame) /* Exception handling frame */
        *(.note .note.*) /* Note sections */
    } : ro
    
    . = ALIGN(0x1000);
    
    .data : ALIGN(0x1000)
    {
        *(.data .data.*)
    } :rw
    
    .bss : ALIGN(0x1000)
    {
        *(.bss .bss.*)
    } :rw
    
    . = ALIGN(0x1000);
    PROVIDE(__kernel_end = .);

    /DISCARD/ :
    {
        *(.comment)
    }
}

ASSERT(__kernel_end < KERNEL_LIMIT, "Kernel image exceeds allowed virtual address limit (must be < 0xFFFFC00000000000)");
