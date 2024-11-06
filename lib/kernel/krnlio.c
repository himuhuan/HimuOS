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