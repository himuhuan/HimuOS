//
// HIMU OPERATING SYSTEM
//
// File: krnlio.h
// Kernel I/O Library
// Copyright (C) 2024 HimuOS Project, all rights reserved.

#include "krnlio.h"
#include <string.h>

static char buffer[33];

void PrintInt(int32_t num) {
    int index;

    memset(buffer, 0, sizeof(buffer));
    if (num < 0) {
        PrintChar('-');
        num = -num;
    }

    index = 0;
    do {
        buffer[index++] = (num % 10) + '0';
        num /= 10;
    } while (num > 0);

    for (int i = index - 1; i >= 0; i--) {
        PrintChar(buffer[i]);
    }
}

int SetCursor(uint8_t /* < 25 */ rows, uint8_t /* < 80 */ cols) {
    if (rows >= 25 || cols >= 80)
        return -1;
    return setcr(cols, rows);
}

void PrintPrettyAddr(void *paddr, uint8_t useUppercase) {
    uint32_t addr = (int32_t)paddr;
    PrintChar('0');
    PrintChar('x');
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (addr >> (i << 2)) & 0xF;
        if (nibble < 10)
            PrintChar('0' + nibble);
        else {
            if (useUppercase)
                PrintChar('A' + (nibble - 10));
            else
                PrintChar('a' + (nibble - 10));
        }
    }
}