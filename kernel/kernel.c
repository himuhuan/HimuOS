/**
 * HIMU OPERATING SYSTEM
 *
 * File: kernel.c
 * The entry point of HimuOS Kernel
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "init.h"
#include "task/sync.h"
#include "device/console.h"
#include "interrupt.h"

int KrnlEntry(void);

int KrnlEntry(void) {
    InitKernel();

    EnableIntr();
    while (1)
        ;
    return 0;
}
