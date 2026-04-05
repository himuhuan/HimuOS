/**
 * HimuOperatingSystem
 *
 * File: user/libsys.h
 * Description: Minimal bootstrap-only userspace syscall wrappers for staged C
 *              payloads.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ex/ex_bootstrap_abi.h>

#define HO_USER_STRINGIFY_INNER(value) #value
#define HO_USER_STRINGIFY(value)       HO_USER_STRINGIFY_INNER(value)

static inline int64_t
HoUserRawSyscall3(uint64_t syscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    register uint64_t rax __asm__("rax") = syscallNumber;
    register uint64_t rdi __asm__("rdi") = arg0;
    register uint64_t rsi __asm__("rsi") = arg1;
    register uint64_t rdx __asm__("rdx") = arg2;

    __asm__ volatile("int $" HO_USER_STRINGIFY(KE_USER_BOOTSTRAP_SYSCALL_VECTOR)
                     : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx)
                     : "cc", "memory");

    return (int64_t)rax;
}

static inline void
HoUserAbort(void)
{
    __builtin_trap();
}

static inline void
HoUserWaitForP1Gate(void)
{
    volatile const uint32_t *mailbox = (volatile const uint32_t *)(uint64_t)KE_USER_BOOTSTRAP_STACK_MAILBOX_ADDRESS;

    while (*mailbox != KE_USER_BOOTSTRAP_P1_MAILBOX_GATE_OPEN)
        ;
}

static inline int64_t
HoUserRawWrite(const void *buffer, uint64_t length)
{
    return HoUserRawSyscall3(SYS_RAW_WRITE, (uint64_t)(const void *)buffer, length, 0);
}

static inline HO_NORETURN void
HoUserRawExit(uint64_t exitCode)
{
    (void)HoUserRawSyscall3(SYS_RAW_EXIT, exitCode, 0, 0);
    HoUserAbort();
    __builtin_unreachable();
}

#undef HO_USER_STRINGIFY
#undef HO_USER_STRINGIFY_INNER
