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
#include "device/keyboard.h"
#include "interrupt.h"

int  KrnlEntry(void);
void KrThreadA(void *);
void KrThreadB(void *);

int KrnlEntry(void) {
    InitKernel();

    KrCreateThread("ta", 31, KrThreadA, " A_");
    KrCreateThread("tb", 31, KrThreadB, " B_");

    EnableIntr();
    while (1)
        ;
    return 0;
}

void KrThreadA(void *arg) {
    while (1) {
        uint8_t status = DisableIntr();
        if (!IcbEmpty(&gKeyboardBuffer)) {
            ConsoleWriteStr((const char *)arg);
            char c = IcbGet(&gKeyboardBuffer);
            ConsoleWriteChar(c);
        }
        SetIntrStatus(status);
    }
}

void KrThreadB(void *arg) {
    while (1) {
        uint8_t status = DisableIntr();
        if (!IcbEmpty(&gKeyboardBuffer)) {
            ConsoleWriteStr((const char *)arg);
            char c = IcbGet(&gKeyboardBuffer);
            ConsoleWriteChar(c);
        }
        SetIntrStatus(status);
    }
}
