/**
 * HimuOperatingSystem
 *
 * File: user/libsys_bringup.h
 * Description: Raw bring-up helpers for sentinel userspace only.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "libsys.h"

#include <kernel/ex/user_bringup_sentinel_abi.h>
#include <kernel/ex/user_image_abi.h>
#include <kernel/ex/user_regression_anchors.h>

static inline int64_t
HoUserRawSyscall3(uint64_t rawSyscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    return HoUserSyscall3(rawSyscallNumber, arg0, arg1, arg2);
}

static inline void
HoUserWaitForP1Gate(void)
{
    volatile const uint32_t *mailbox = (volatile const uint32_t *)(uint64_t)EX_USER_BRINGUP_P1_MAILBOX_ADDRESS;

    while (*mailbox != EX_USER_BRINGUP_P1_MAILBOX_GATE_OPEN)
        ;
}

static inline int64_t
HoUserRawWrite(const void *buffer, uint64_t length)
{
    return HoUserRawSyscall3(EX_USER_BRINGUP_SYS_RAW_WRITE, (uint64_t)(const void *)buffer, length, 0);
}

static inline int64_t
HoUserRawProbeGuardPageByte(void)
{
    return HoUserRawWrite(HoUserImageStackGuardBase(), 1U);
}

static inline HO_NORETURN void
HoUserRawExit(uint64_t exitCode)
{
    (void)HoUserRawSyscall3(EX_USER_BRINGUP_SYS_RAW_EXIT, exitCode, 0, 0);
    HoUserAbort();
    __builtin_unreachable();
}
