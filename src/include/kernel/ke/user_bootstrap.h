/**
 * HimuOperatingSystem
 *
 * File: ke/user_bootstrap.h
 * Description: Ke-internal bootstrap staging and first-entry helpers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#include <kernel/ex/ex_bootstrap_abi.h>
#include <kernel/ke/kthread.h>

typedef struct KE_USER_BOOTSTRAP_STAGING KE_USER_BOOTSTRAP_STAGING;

typedef struct KE_USER_BOOTSTRAP_CREATE_PARAMS
{
    const void *CodeBytes;
    uint64_t CodeLength;
    uint64_t EntryOffset;
    const void *ConstBytes;
    uint64_t ConstLength;
} KE_USER_BOOTSTRAP_CREATE_PARAMS;

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapCreateStaging(
    const KE_USER_BOOTSTRAP_CREATE_PARAMS *params,
    KE_USER_BOOTSTRAP_STAGING **outStaging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapDestroyStaging(KE_USER_BOOTSTRAP_STAGING *staging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapAttachThread(KTHREAD *thread,
                                                                 KE_USER_BOOTSTRAP_STAGING *staging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapRawSyscallInit(void);

HO_KERNEL_API void KeUserBootstrapObserveCurrentThreadUserTimerPreemption(void);

HO_KERNEL_API HO_NORETURN void KeUserBootstrapEnterCurrentThread(void);
