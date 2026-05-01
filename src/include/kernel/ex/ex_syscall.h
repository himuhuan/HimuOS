/**
 * HimuOperatingSystem
 *
 * File: ex/ex_syscall.h
 * Description: Ex-owned syscall dispatcher interface for the user ABI.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef enum EX_SYSCALL_DISPOSITION
{
    EX_SYSCALL_DISPOSITION_RETURN_TO_USER = 0,
    EX_SYSCALL_DISPOSITION_EXIT_CURRENT_THREAD = 1,
} EX_SYSCALL_DISPOSITION;

typedef struct EX_SYSCALL_ARGUMENTS
{
    uint64_t Number;
    uint64_t Arg0;
    uint64_t Arg1;
    uint64_t Arg2;
} EX_SYSCALL_ARGUMENTS;

typedef struct EX_SYSCALL_DISPATCH_RESULT
{
    EX_SYSCALL_DISPOSITION Disposition;
    int64_t ReturnValue;
} EX_SYSCALL_DISPATCH_RESULT;

HO_KERNEL_API HO_NODISCARD HO_STATUS ExDispatchSyscall(const EX_SYSCALL_ARGUMENTS *args,
                                                       EX_SYSCALL_DISPATCH_RESULT *outResult);
