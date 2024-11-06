//
// HIMU OPERATING SYSTEM
//
// File: krnlio.h
// Kernel I/O Library
// Copyright (C) 2024 HimuOS Project, all rights reserved.

#ifndef __HIMUOS__LIB_KRNLIO_H
#define __HIMUOS__LIB_KRNLIO_H 1

#include "lib/shared/stdint.h"

/*
 * All builtin functions has __himuos__ prefix to avoid conflicts with other
 * libraries
 */

int  __himuos__getcrpos(void);
void __himuos__printc(uint8_t c);
void __himuos__clscr(void);
void __himuos__printstr(const char *restrict str);
void __himuos__printintx(void *addr);

void PrintInt(int32_t num);

#define GetCursorPos()  __himuos__getcrpos()
#define PrintChar(c)    __himuos__printc(c)
#define ClearScreen()   __himuos__clscr()
#define PrintStr(str)   __himuos__printstr(str)
#define PrintAddr(addr) __himuos__printintx(addr)
#define PrintHex(val)   __himuos__printintx((void *)(uint32_t)(val))

#endif // ^^ __HIMUOS__LIB_KRNLIO_H ^^