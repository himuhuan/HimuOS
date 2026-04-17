/**
 * HimuOperatingSystem
 *
 * File: user/libsys.h
 * Description: Stable Ex-facing userspace ABI wrappers for compiled bootstrap
 *              payloads, plus raw-sentinel helpers kept only for bootstrap
 *              regression coverage.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ex/ex_bootstrap_abi.h>

#define HO_USER_STRINGIFY_INNER(value) #value
#define HO_USER_STRINGIFY(value)       HO_USER_STRINGIFY_INNER(value)

enum
{
    HO_USER_INTERNAL_SYS_RAW_INVALID = SYS_RAW_INVALID,
    HO_USER_INTERNAL_SYS_RAW_WRITE = SYS_RAW_WRITE,
    HO_USER_INTERNAL_SYS_RAW_EXIT = SYS_RAW_EXIT,
};

/*
 * Stable userspace trap ABI:
 * - Entry is a synchronous int 0x80 trap.
 * - RAX carries the syscall number on entry and the return value on exit.
 * - RDI, RSI, and RDX carry the first three arguments.
 * - Failures return negative HO_STATUS values.
 */
static inline int64_t
HoUserSyscall3(uint64_t syscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
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

/*
 * Raw bootstrap-only helper kept for internal/raw-sentinel userspace. Ordinary
 * compiled userspace should prefer the Ex-facing wrappers below.
 */
static inline int64_t
HoUserRawSyscall3(uint64_t rawSyscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    return HoUserSyscall3(rawSyscallNumber, arg0, arg1, arg2);
}

static inline HO_NORETURN void
HoUserAbort(void)
{
    __builtin_trap();
    __builtin_unreachable();
}

static inline void
HoUserWaitForP1Gate(void)
{
    volatile const uint32_t *mailbox = (volatile const uint32_t *)(uint64_t)KE_USER_BOOTSTRAP_STACK_MAILBOX_ADDRESS;

    while (*mailbox != KE_USER_BOOTSTRAP_P1_MAILBOX_GATE_OPEN)
        ;
}

static inline const void *
HoUserBootstrapStackGuardBase(void)
{
    return (const void *)(uint64_t)KE_USER_BOOTSTRAP_STACK_GUARD_BASE;
}

static inline const KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK *
HoUserCapabilitySeedBlock(void)
{
    return (const KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK *)(uint64_t)KE_USER_BOOTSTRAP_CONST_BASE;
}

static inline BOOL
HoUserCapabilitySeedBlockIsValid(const KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK *seed)
{
    return seed != NULL && seed->Version == KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION &&
           seed->Size == KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE;
}

static inline BOOL
HoUserCurrentCapabilitySeedBlockIsValid(void)
{
    return HoUserCapabilitySeedBlockIsValid(HoUserCapabilitySeedBlock());
}

static inline uint32_t
HoUserStdoutHandle(void)
{
    const KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK *seed = HoUserCapabilitySeedBlock();

    return seed->Stdout;
}

static inline int64_t
HoUserWrite(uint32_t handle, const void *buffer, uint64_t length)
{
    return HoUserSyscall3(SYS_WRITE, handle, (uint64_t)(const void *)buffer, length);
}

static inline int64_t
HoUserWriteStdout(const void *buffer, uint64_t length)
{
    uint32_t stdoutHandle = HoUserStdoutHandle();
    if (stdoutHandle == KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE)
        return -(int64_t)EC_INVALID_STATE;

    return HoUserWrite(stdoutHandle, buffer, length);
}

static inline HO_NORETURN void
HoUserExit(uint64_t exitCode)
{
    (void)HoUserSyscall3(SYS_EXIT, exitCode, 0, 0);
    HoUserAbort();
    __builtin_unreachable();
}

static inline int64_t
HoUserReadLine(void *buffer, uint64_t capacity)
{
    return HoUserSyscall3(SYS_READLINE, (uint64_t)(void *)buffer, capacity, 0);
}

static inline int64_t
HoUserSpawnBuiltin(uint64_t programId, uint64_t flags)
{
    return HoUserSyscall3(SYS_SPAWN_BUILTIN, programId, flags, 0);
}

static inline int64_t
HoUserWaitPid(uint64_t pid)
{
    return HoUserSyscall3(SYS_WAIT_PID, pid, 0, 0);
}

static inline int64_t
HoUserSleepMs(uint64_t milliseconds)
{
    return HoUserSyscall3(SYS_SLEEP_MS, milliseconds, 0, 0);
}

static inline int64_t
HoUserKillPid(uint64_t pid)
{
    return HoUserSyscall3(SYS_KILL_PID, pid, 0, 0);
}

static inline int64_t
HoUserQuerySysinfo(uint64_t infoClass, void *buffer, uint64_t bufferSize)
{
    return HoUserSyscall3(SYS_QUERY_SYSINFO, infoClass, (uint64_t)(void *)buffer, bufferSize);
}

/*
 * Raw bootstrap-only helpers kept for the user_hello sentinel. They are not
 * part of the stable Ex-facing userspace ABI surface.
 */
static inline int64_t
HoUserRawWrite(const void *buffer, uint64_t length)
{
    return HoUserRawSyscall3(HO_USER_INTERNAL_SYS_RAW_WRITE, (uint64_t)(const void *)buffer, length, 0);
}

static inline int64_t
HoUserRawProbeGuardPageByte(void)
{
    return HoUserRawWrite(HoUserBootstrapStackGuardBase(), 1U);
}

static inline HO_NORETURN void
HoUserRawExit(uint64_t exitCode)
{
    (void)HoUserRawSyscall3(HO_USER_INTERNAL_SYS_RAW_EXIT, exitCode, 0, 0);
    HoUserAbort();
    __builtin_unreachable();
}

#undef SYS_RAW_INVALID
#undef SYS_RAW_WRITE
#undef SYS_RAW_EXIT
#undef HO_USER_STRINGIFY
#undef HO_USER_STRINGIFY_INNER
