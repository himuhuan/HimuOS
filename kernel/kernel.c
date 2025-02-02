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
#include "task/sched.h"
#include "structs/list.h"
#include "interrupt.h"

int KrnlEntry(void);

void ChildThreadA(void *);
void ChildThreadB(void *);

int KrnlEntry(void) {
    InitKernel();

    PrintStr("\n\n Welcome!\n\n");

    (void)KrCreateThread("thread_a", 8, ChildThreadA, "ArgA ");
    (void)KrCreateThread("thread_b", 4, ChildThreadB, "ArgB ");

    EnableIntr();
    while (1) {
        DisableIntr();
        PrintStr("MAIN ");
        EnableIntr();
    }
    return 0;
}

void ChildThreadA(void *arg) {
    char *p = arg;
    while (1) {
        DisableIntr();
        PrintStr(p);
        EnableIntr();
    }
}

void ChildThreadB(void *arg) {
    char *p = arg;
    while (1) {
        DisableIntr();
        PrintStr(p);
        EnableIntr();
    }
}