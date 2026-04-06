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
#include <kernel/ke/mm.h>
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

typedef struct KE_USER_BOOTSTRAP_LAYOUT
{
    HO_VIRTUAL_ADDRESS UserRangeBase;
    HO_VIRTUAL_ADDRESS UserRangeEndExclusive;
    HO_VIRTUAL_ADDRESS EntryPoint;
    HO_VIRTUAL_ADDRESS GuardBase;
    HO_VIRTUAL_ADDRESS StackBase;
    HO_VIRTUAL_ADDRESS StackTop;
    HO_VIRTUAL_ADDRESS PhaseOneMailboxAddress;
    HO_PHYSICAL_ADDRESS OwnerRootPageTablePhys;
} KE_USER_BOOTSTRAP_LAYOUT;

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapCreateStaging(
    const KE_USER_BOOTSTRAP_CREATE_PARAMS *params,
    KE_PROCESS_ADDRESS_SPACE *targetSpace,
    KE_USER_BOOTSTRAP_STAGING **outStaging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapDestroyStaging(KE_USER_BOOTSTRAP_STAGING *staging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapAttachThread(KTHREAD *thread,
                                                                 KE_USER_BOOTSTRAP_STAGING *staging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapDetachThread(KTHREAD *thread,
                                                                 KE_USER_BOOTSTRAP_STAGING *staging);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapPatchConstBytes(KE_USER_BOOTSTRAP_STAGING *staging,
                                                                    uint64_t offset,
                                                                    const void *bytes,
                                                                    uint64_t length);

/*
 * Query the live bootstrap layout attached to the current thread. The range
 * spans the full bootstrap slice so guard holes remain range-valid and are
 * rejected later by page validation rather than by the range gate.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapQueryCurrentThreadLayout(KE_USER_BOOTSTRAP_LAYOUT *outLayout);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapCopyInBytes(void *kernelDestination,
                                                                HO_VIRTUAL_ADDRESS userSource,
                                                                uint64_t length);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapCopyOutBytes(HO_VIRTUAL_ADDRESS userDestination,
                                                                 const void *kernelSource,
                                                                 uint64_t length);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapWriteConsoleBytes(const char *bytes,
                                                                      uint64_t length,
                                                                      uint64_t *outWritten);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeUserBootstrapRawSyscallInit(void);

HO_KERNEL_API void KeUserBootstrapObserveCurrentThreadUserTimerPreemption(void);

HO_KERNEL_API HO_NORETURN void KeUserBootstrapEnterCurrentThread(void);
