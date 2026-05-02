/**
 * HimuOperatingSystem
 *
 * File: ke/input.h
 * Description: Ke runtime input subsystem for foreground-owned line input.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define KE_INPUT_LINE_CAPACITY 128U

HO_KERNEL_API HO_STATUS KeInputInit(void);
HO_KERNEL_API BOOL KeInputIsReady(void);
HO_KERNEL_API const char *KeInputGetSourceName(void);
HO_KERNEL_API uint8_t KeInputGetVector(void);
HO_KERNEL_API HO_STATUS KeInputSetForegroundOwnerThreadId(uint32_t threadId);
HO_KERNEL_API uint32_t KeInputGetForegroundOwnerThreadId(void);
HO_KERNEL_API uint64_t KeInputGetCompletedReadCount(void);
HO_KERNEL_API HO_STATUS KeInputWaitForForegroundLine(void);
HO_KERNEL_API HO_STATUS KeInputCopyCompletedLineForCurrentThread(char *buffer, uint32_t capacity, uint32_t *outLength);
HO_KERNEL_API HO_STATUS KeInputConsumeCompletedLineForCurrentThread(void);
