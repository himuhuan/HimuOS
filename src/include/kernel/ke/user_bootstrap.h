/**
 * HimuOperatingSystem
 *
 * File: ke/user_bootstrap.h
 * Description: Minimal bootstrap-only user-mode ABI and log anchors.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/kthread.h>

/*
 * The fixed low-half user window below is a staging/bootstrap model only.
 * It reuses the shared imported kernel root for the first user-mode slice and
 * must not be treated as the long-term per-process address-space contract.
 * Keep it above the boot-time low 2GB identity import so hole validation sees
 * an unmapped slot in the shared root.
 */
#define KE_USER_BOOTSTRAP_PAGE_SIZE              0x1000ULL
#define KE_USER_BOOTSTRAP_WINDOW_BASE            0x0000000080000000ULL
#define KE_USER_BOOTSTRAP_WINDOW_PAGE_COUNT      4ULL
#define KE_USER_BOOTSTRAP_WINDOW_END_EXCLUSIVE   (KE_USER_BOOTSTRAP_WINDOW_BASE +                     \
                                                  (KE_USER_BOOTSTRAP_WINDOW_PAGE_COUNT *              \
                                                   KE_USER_BOOTSTRAP_PAGE_SIZE))
#define KE_USER_BOOTSTRAP_CODE_BASE              KE_USER_BOOTSTRAP_WINDOW_BASE
#define KE_USER_BOOTSTRAP_CONST_BASE             (KE_USER_BOOTSTRAP_CODE_BASE + KE_USER_BOOTSTRAP_PAGE_SIZE)
#define KE_USER_BOOTSTRAP_STACK_GUARD_BASE       (KE_USER_BOOTSTRAP_CONST_BASE + KE_USER_BOOTSTRAP_PAGE_SIZE)
#define KE_USER_BOOTSTRAP_STACK_BASE             (KE_USER_BOOTSTRAP_STACK_GUARD_BASE + KE_USER_BOOTSTRAP_PAGE_SIZE)
#define KE_USER_BOOTSTRAP_STACK_TOP              (KE_USER_BOOTSTRAP_STACK_BASE + KE_USER_BOOTSTRAP_PAGE_SIZE)
#define KE_USER_BOOTSTRAP_STACK_PAGE_COUNT       1ULL
#define KE_USER_BOOTSTRAP_STACK_GUARD_PAGE_COUNT 1ULL
#define KE_USER_BOOTSTRAP_STACK_MAILBOX_OFFSET   0ULL
#define KE_USER_BOOTSTRAP_STACK_MAILBOX_ADDRESS  (KE_USER_BOOTSTRAP_STACK_BASE + KE_USER_BOOTSTRAP_STACK_MAILBOX_OFFSET)
#define KE_USER_BOOTSTRAP_P1_MAILBOX_CLOSED      0U
#define KE_USER_BOOTSTRAP_P1_MAILBOX_GATE_OPEN   0x31504741U

/*
 * Bootstrap raw syscall ABI:
 * - Entry is a synchronous int 0x80 trap.
 * - RAX carries the raw syscall number on entry and the return value on exit.
 * - RDI, RSI, and RDX carry the first three bootstrap arguments.
 *
 * The SYS_RAW_* namespace is intentionally scoped to bring-up only. Future
 * handle-oriented SYS_* services may use different numbers and semantics.
 */
#define KE_USER_BOOTSTRAP_SYSCALL_VECTOR             0x80U
#define KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH   256U

#define SYS_RAW_INVALID                              0U
#define SYS_RAW_WRITE                                1U
#define SYS_RAW_EXIT                                 2U

/*
 * Stable user_hello evidence-chain anchors. Phase one uses explicit
 * first-entry / timer-hit / gate-armed milestones before raw syscalls run.
 */
#define KE_USER_BOOTSTRAP_LOG_ENTER_USER_MODE          "[USERBOOT] enter user mode"
#define KE_USER_BOOTSTRAP_LOG_P1_FIRST_ENTRY           KE_USER_BOOTSTRAP_LOG_ENTER_USER_MODE
#define KE_USER_BOOTSTRAP_LOG_TIMER_FROM_USER_FORMAT   "[USERBOOT] timer from user #%u"
#define KE_USER_BOOTSTRAP_LOG_P1_GATE_ARMED            "[USERBOOT] P1 gate armed"
#define KE_USER_BOOTSTRAP_LOG_HELLO                    "[USERBOOT] hello"
#define KE_USER_BOOTSTRAP_LOG_SYS_RAW_EXIT             "[USERBOOT] SYS_RAW_EXIT"
#define KE_USER_BOOTSTRAP_LOG_INVALID_SYSCALL          "[USERBOOT] invalid raw syscall"
#define KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER      "[USERBOOT] invalid user buffer"
#define KE_USER_BOOTSTRAP_LOG_TEARDOWN_FAILED          "[USERBOOT] bootstrap teardown failed"
#define KE_USER_BOOTSTRAP_LOG_THREAD_TERMINATED_FORMAT "[SCHED] Thread %u terminated"
#define KE_USER_BOOTSTRAP_LOG_IDLE_REAPER              "[USERBOOT] idle/reaper reclaimed user_hello thread"

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

HO_KERNEL_API HO_NORETURN void KeUserBootstrapEnterCurrentThread(void);
