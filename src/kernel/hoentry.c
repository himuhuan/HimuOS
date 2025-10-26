/**
 * HimuOperatingSystem
 *
 * File: hoentry.c
 * Description: The entry point for the kernel.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "boot/boot.h"
#include <kernel/hodbg.h>
#include <kernel/init.h>

void kmain(STAGING_BLOCK *stagingBlock);

void
kmain(STAGING_BLOCK *stagingBlock)
{
    InitKernel(stagingBlock);
    kprintf("Hello, HimuOS Kernel World!\n");
    while (1)
        ;
}
