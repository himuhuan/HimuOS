/**
 * HIMU OPERATING SYSTEM
 *
 * File: console.c
 * Console device
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "console.h"
#include "kernel/task/sync.h"
#include "krnlio.h"
#include "krnldbg.h"

static struct KR_LOCK gConsoleLock;

void InitConsole(void) { KrLockInit(&gConsoleLock); }

void ConsoleAcquire(void) { KrLockAcquire(&gConsoleLock); }

void ConsoleRelease(void) { KrLockRelease(&gConsoleLock); }

void ConsoleWriteStr(const char *str) {
    ConsoleAcquire();
    PrintStr(str);
    ConsoleRelease();
}

void ConsoleWriteLine(const char *str) {
    ConsoleAcquire();
    PrintStr(str);
    PrintChar('\n');
    ConsoleRelease();
}

void ConsoleWriteChar(char ch) {
    ConsoleAcquire();
    PrintChar(ch);
    ConsoleRelease();
}

void ConsoleWriteAddress(void *addr) {
    ConsoleAcquire();
    PrintAddr(addr);
    ConsoleRelease();
}

void ConsoleWriteInt(int val, int base) {
    char     buffer[sizeof(val) * 8 + 1];
    char    *ptr;
    int      neg;
    uint32_t num;

    ConsoleAcquire();
    KASSERT(!(base < 2 || base > 36));
    if (base < 2 || base > 36)
        goto release_console;
    if (base == 10) {
        PrintInt(val);
        goto release_console;
    }
    if (base == 16) {
        PrintHex(val);
        goto release_console;
    }

    ptr  = &buffer[sizeof(buffer) - 1];
    *ptr = '\0';
    neg  = 0;

    if (val < 0 && base == 10) {
        neg = 1;
        num = (uint32_t)(-val);
    } else {
        num = (uint32_t)val;
    }

    do {
        uint32_t rem = num % base;
        *--ptr       = (rem < 10) ? (rem + '0') : (rem - 10 + 'A');
        num /= base;
    } while (num > 0);

    if (neg) {
        *--ptr = '-';
    }

    PrintStr(ptr);
release_console:
    ConsoleRelease();
}
