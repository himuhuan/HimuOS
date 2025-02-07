/**
 * HIMU OPERATING SYSTEM
 *
 * File: console.h
 * Console device
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_LIB_DEVICE_CONSOLE_H
#define __HIMUOS_LIB_DEVICE_CONSOLE_H 1

void InitConsole(void);

void ConsoleAcquire(void);

void ConsoleRelease(void);

void ConsoleWriteStr(const char *str);

void ConsoleWriteLine(const char *str);

void ConsoleWriteChar(char ch);

void ConsoleWriteAddress(void *addr);

void ConsoleWriteInt(int val, int base);

#endif
