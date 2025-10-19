/**
 * HimuOperatingSystem
 *
 * File: hodbg.h
 * Description: HimuOS Debug Toolsets.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <hostdlib.h>
#include <kernel/console.h>

#define kprintf(fmt, ...) ConFormatPrint(fmt, ##__VA_ARGS__)

HO_KERNEL_API HO_NORETURN void kpanic(uint32_t ec);

HO_KERNEL_API HO_NORETURN void kassert(BOOL expr);

HO_KERNEL_API const char *KrGetStatusMessage(HO_STATUS status);

HO_KERNEL_API void KrPrintStautsMessage(const char *msg, HO_STATUS status);

HO_KERNEL_API void KrPrintHexMessage(const char *msg, uint64_t value);