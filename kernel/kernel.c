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
#include "structs/list.h"

int KrnlEntry(void);

void KernelThread(void *args);

int KrnlEntry(void) {
    InitKernel();

    PrintStr("\n\n Welcome!\n\n");

    asm volatile("hlt");
    return 0;
}
