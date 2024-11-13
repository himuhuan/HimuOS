/**
 * HIMU OPERATING SYSTEM
 *
 * File: krnldbg.c
 * Kernel Debug Utilities
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "krnldbg.h"
#include "interrupt.h"
#include "lib/kernel/krnlio.h"

void KPanic(const char *fileName, int line, const char *func, const char *msg) {
	DisableIntr();
	PrintStr("\n\n**KERNEL PANIC**\n\n");
	PrintStr("STOP INFORMATION: \n");
	PrintStr("    File Name: "); PrintStr(fileName); PrintChar('\n');
	PrintStr("    Line: 0x"); PrintHex(line); PrintChar('\n');
	PrintStr("    Function: "); PrintStr(func); PrintChar('\n');
	PrintStr("    Message: "); PrintStr(msg); PrintChar('\n');
	asm volatile("hlt");
}