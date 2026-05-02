/**
 * HimuOperatingSystem
 *
 * File: user/libsys.h
 * Description: Stable Ex-facing userspace ABI wrappers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ex/user_capability_abi.h>
#include <kernel/ex/user_image_abi.h>
#include <kernel/ex/user_syscall_abi.h>
#include <kernel/ex/user_sysinfo_abi.h>

#define HO_USER_STRINGIFY_INNER(value) #value
#define HO_USER_STRINGIFY(value)       HO_USER_STRINGIFY_INNER(value)

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

    __asm__ volatile("int $" HO_USER_STRINGIFY(EX_USER_SYSCALL_VECTOR)
                     : "+a"(rax)
                     : "D"(rdi), "S"(rsi), "d"(rdx)
                     : "cc", "memory");

    return (int64_t)rax;
}

static inline HO_NORETURN void
HoUserAbort(void)
{
    __builtin_trap();
    __builtin_unreachable();
}

static inline const EX_USER_CAPABILITY_SEED_BLOCK *
HoUserCapabilitySeedBlock(void)
{
    return (const EX_USER_CAPABILITY_SEED_BLOCK *)(uint64_t)EX_USER_CAPABILITY_SEED_ADDRESS;
}

static inline BOOL
HoUserCapabilitySeedBlockIsValid(const EX_USER_CAPABILITY_SEED_BLOCK *seed)
{
    return seed != NULL && seed->Version == EX_USER_CAPABILITY_SEED_VERSION &&
           seed->Size == EX_USER_CAPABILITY_SEED_BLOCK_SIZE;
}

static inline BOOL
HoUserCurrentCapabilitySeedBlockIsValid(void)
{
    return HoUserCapabilitySeedBlockIsValid(HoUserCapabilitySeedBlock());
}

static inline const void *
HoUserImageStackGuardBase(void)
{
    return (const void *)(uint64_t)EX_USER_IMAGE_STACK_GUARD_BASE;
}

static inline uint32_t
HoUserStdoutHandle(void)
{
    const EX_USER_CAPABILITY_SEED_BLOCK *seed = HoUserCapabilitySeedBlock();

    return seed->Stdout;
}

static inline int64_t
HoUserWrite(uint32_t handle, const void *buffer, uint64_t length)
{
    return HoUserSyscall3(EX_USER_SYS_WRITE, handle, (uint64_t)(const void *)buffer, length);
}

static inline int64_t
HoUserWriteStdout(const void *buffer, uint64_t length)
{
    uint32_t stdoutHandle = HoUserStdoutHandle();
    if (stdoutHandle == EX_USER_CAPABILITY_INVALID_HANDLE)
        return -(int64_t)EC_INVALID_STATE;

    return HoUserWrite(stdoutHandle, buffer, length);
}

static inline int64_t
HoUserClose(uint32_t handle)
{
    return HoUserSyscall3(EX_USER_SYS_CLOSE, handle, 0, 0);
}

static inline int64_t
HoUserWaitOne(uint32_t handle, uint64_t timeoutMs)
{
    return HoUserSyscall3(EX_USER_SYS_WAIT_ONE, handle, timeoutMs, 0);
}

static inline HO_NORETURN void
HoUserExit(uint64_t exitCode)
{
    (void)HoUserSyscall3(EX_USER_SYS_EXIT, exitCode, 0, 0);
    HoUserAbort();
    __builtin_unreachable();
}

static inline int64_t
HoUserReadLine(void *buffer, uint64_t capacity)
{
    return HoUserSyscall3(EX_USER_SYS_READLINE, (uint64_t)(void *)buffer, capacity, 0);
}

#define HoUserSpawnProgramLiteral(name, flags) HoUserSpawnProgram((name), sizeof(name) - 1U, (flags))

static inline int64_t
HoUserSpawnProgram(const char *name, uint64_t nameLength, uint64_t flags)
{
    return HoUserSyscall3(EX_USER_SYS_SPAWN_PROGRAM, (uint64_t)(const void *)name, nameLength, flags);
}

static inline int64_t
HoUserWaitPid(uint64_t pid)
{
    return HoUserSyscall3(EX_USER_SYS_WAIT_PID, pid, 0, 0);
}

static inline int64_t
HoUserSleepMs(uint64_t milliseconds)
{
    return HoUserSyscall3(EX_USER_SYS_SLEEP_MS, milliseconds, 0, 0);
}

static inline int64_t
HoUserKillPid(uint64_t pid)
{
    return HoUserSyscall3(EX_USER_SYS_KILL_PID, pid, 0, 0);
}

static inline int64_t
HoUserQuerySysinfo(uint64_t infoClass, void *buffer, uint64_t bufferSize)
{
    return HoUserSyscall3(EX_USER_SYS_QUERY_SYSINFO, infoClass, (uint64_t)(void *)buffer, bufferSize);
}

static inline int64_t
HoUserQuerySysinfoOverview(EX_SYSINFO_OVERVIEW *overview)
{
    return HoUserQuerySysinfo(EX_SYSINFO_CLASS_OVERVIEW, overview, sizeof(*overview));
}

static inline int64_t
HoUserQuerySysinfoProcessList(EX_SYSINFO_PROCESS_LIST *processList)
{
    return HoUserQuerySysinfo(EX_SYSINFO_CLASS_PROCESS_LIST, processList, sizeof(*processList));
}

static inline int64_t
HoUserQuerySysinfoThreadList(EX_SYSINFO_THREAD_LIST *threadList)
{
    return HoUserQuerySysinfo(EX_SYSINFO_CLASS_THREAD_LIST, threadList, sizeof(*threadList));
}

#undef HO_USER_STRINGIFY
#undef HO_USER_STRINGIFY_INNER
