/**
 * HIMU OPERATING SYSTEM
 *
 * File: kernel.c
 * The entry point of HimuOS Kernel
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "init.h"
#include "lib/kernel/krnlio.h"
#include "kernel/krnldbg.h"
#include "lib/shared/string.h"
#include "structs/bitmap.h"
#include "memory.h"

int KrnlEntry(void);

int KrnlEntry(void) {
    InitKernel();

    void *page = KrAllocKernelMemPage(3);
    PrintStr("Page: 0x");
    PrintHex((uint32_t)page);
    PrintChar('\n');

    PrintStr("\n\n Welcome!\n\n");

    asm volatile("hlt");
    return 0;
}
