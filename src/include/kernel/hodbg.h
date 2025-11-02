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
#include "arch/amd64/idt.h"

#define kprintf(fmt, ...) ConsoleWriteFmt(fmt, ##__VA_ARGS__)

typedef struct HO_PANIC_CONTEXT
{
    const char *FileName;
    uint64_t LineNumber;
    const char *FunctionName;
    void *InstructionPointer; // RIP at the point of assertion failure
    void *BasePointer;        // RBP at the point of assertion failure
} HO_PANIC_CONTEXT;

/**
 * @brief Traps the OS into a non-recoverable state.
 * This function provides a BSOD-like output and halts the system.
 * @param ec
 * ec > 0 indicates a kernel panic has occurred
 * ec < 0 indicates an interrupt vector number which means a CPU exception occurred
 * @param dump Pointer to a CPU dump context.
 * @remarks dump can be varied based on the value of ec:
 * - If ec > 0, dump is expected to be `HO_PANIC_CONTEXT`.
 * - If ec < 0 and dump->CS indicates a user mode segment, dump is expected to be `USR_INTERRUPT_FRAME`.
 * - If ec < 0 and dump->CS indicates a kernel mode segment, dump is expected to be `KRNL_INTERRUPT_FRAME`.
 */
HO_PUBLIC_API HO_NORETURN void KernelHalt(int64_t ec, void *dump);

HO_KERNEL_API HO_NORETURN void kassert(BOOL expr);

HO_KERNEL_API const char *KrGetStatusMessage(HO_STATUS status);
