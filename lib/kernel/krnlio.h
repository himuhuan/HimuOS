//
// HIMU OPERATING SYSTEM
//
// File: krnlio.h
// Kernel I/O Library
// Copyright (C) 2024 HimuOS Project, all rights reserved.

#ifndef __HIMUOS__LIB_KRNLIO_H
#define __HIMUOS__LIB_KRNLIO_H 1

#include "lib/shared/stdint.h"

int  getcrpos(void);
void printc(uint8_t c);
void clscr(void);
void printstr(const char *restrict str);
void printintx(void *addr);
int  setcr(uint8_t x, uint8_t y);

void PrintInt(int32_t num);
int  SetCursor(uint8_t /* < 25 */ rows, uint8_t /* < 80 */ cols);
void PrintPrettyAddr(void *addr, uint8_t useUppercase);

#define GetCursorPos() getcrpos()
#define PrintChar(c)   printc(c)
#define ClearScreen()  clscr()
#define PrintStr(str)  printstr(str)
#ifdef __HIMUOS_RELEASE__
#define PrintAddr(addr) printintx(addr)
#else
#define PrintAddr(addr) PrintPrettyAddr(addr, 1)
#endif //! __HIMUOS_RELEASE__
#define PrintHex(val) printintx((void *)(uint32_t)(val))

#endif // ^^ __HIMUOS__LIB_KRNLIO_H ^^