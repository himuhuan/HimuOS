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
#include "task/sync.h"
#include "structs/list.h"
#include "interrupt.h"

int KrnlEntry(void);

void ChildThreadA(void *);
void ChildThreadB(void *);

struct KR_LOCK gLock;

int KrnlEntry(void) {
    InitKernel();

    PrintStr("\n\n Welcome!\n\n");

    KrLockInit(&gLock);

    (void)KrCreateThread("thread_a", 8, ChildThreadA, "ArgA ");
    (void)KrCreateThread("thread_b", 4, ChildThreadB, "ArgB ");

    EnableIntr();
    while (1) {
        KrLockAcquire(&gLock);
        PrintStr("MAIN ");
        KrLockRelease(&gLock);
    }
    return 0;
}

void ChildThreadA(void *arg) {
    char *p = arg;
    while (1) {
        KrLockAcquire(&gLock);
        PrintStr(p);
        KrLockRelease(&gLock);
    }
}

void ChildThreadB(void *arg) {
    char *p = arg;
    while (1) {
        KrLockAcquire(&gLock);
        PrintStr(p);
        KrLockRelease(&gLock);
    }
}