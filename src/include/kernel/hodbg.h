/**
 * HimuOperatingSystem
 *
 * File: hodbg.h
 * Description: HimuOS Debug Toolsets.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <hostdlib.h>
#include <kernel/ke/console.h>
#include <kernel/log.h>
// #include "arch/amd64/idt.h"

#if HO_ENABLE_TIMESTAMP_LOG
#define kprintf(fmt, ...) KLogWriteFmt(fmt, ##__VA_ARGS__)
#else
#define kprintf(fmt, ...) ConsoleWriteFmt(fmt, ##__VA_ARGS__)
#endif

/**
 * @brief Structure representing the context information when a kernel panic occurs.
 *
 * This structure is used to capture relevant details about the system state
 * at the time of a panic, which can be useful for debugging and error reporting.
 */
typedef struct HO_PANIC_CONTEXT
{
    const char *Message;
    const char *FileName;
    uint64_t LineNumber;
    const char *FunctionName;
    void *InstructionPointer; // RIP at the point of assertion failure
    void *BasePointer;        // RBP at the point of assertion failure
} HO_PANIC_CONTEXT;

/**
 * @brief Triggers a kernel panic with the specified error code.
 *
 * This macro is used to indicate a critical error in the kernel,
 * halting execution and providing an error code for debugging purposes.
 *
 * @param code The error code representing the reason for the panic.
 */
#define HO_KPANIC(code, message)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        HO_PANIC_CONTEXT __hoKrnlCtx = {.Message = message,                                                            \
                                        .FileName = __FILE__,                                                          \
                                        .LineNumber = (uint64_t)__LINE__,                                              \
                                        .FunctionName = __func__,                                                      \
                                        .InstructionPointer = __builtin_return_address(0),                             \
                                        .BasePointer = (void *)__builtin_frame_address(0)};                            \
        KernelHalt((code), &__hoKrnlCtx);                                                                              \
    } while (FALSE)

/**
 * @brief Kernel assertion macro.
 *
 * Evaluates the given expression `expr`. If the assertion fails, it triggers
 * a kernel-specific action identified by `code`.
 *
 * @param expr The expression to be evaluated for the assertion.
 * @param code The code or action to execute if the assertion fails.
 */
#define HO_KASSERT(expr, code)                                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            HO_KPANIC(code, "Assertion " #expr " Failed!");                                                            \
    } while (FALSE)

/**
 * @brief Traps the OS into a non-recoverable state.
 * This function provides a BSOD-like output and halts the system.
 * @param ec
 * ec > 0 indicates a kernel panic has occurred
 * ec < 0 indicates an interrupt vector number which means a CPU exception occurred
 * @param dump Pointer to a CPU dump context.
 * @remarks dump can be varied based on the value of ec:
 * - If ec > 0, dump is expected to be `HO_PANIC_CONTEXT`.
 * - If ec <= 0 and dump->CS indicates a user mode segment, dump is expected to be `USR_INTERRUPT_FRAME`.
 * - If ec <= 0 and dump->CS indicates a kernel mode segment, dump is expected to be `KRNL_INTERRUPT_FRAME`.
 */
HO_PUBLIC_API HO_NORETURN void KernelHalt(int64_t ec, void *dump);

/**
 * KrGetStatusMessage - Convert an HO_STATUS value to a human-readable message.
 * @status: Status code to translate.
 *
 * Returns a pointer to a NUL-terminated, read-only string describing the
 * provided status code. The returned pointer refers to static/internal memory
 * owned by the kernel subsystem; the caller MUST NOT modify or free it.
 */
HO_KERNEL_API const char *KrGetStatusMessage(HO_STATUS status);
