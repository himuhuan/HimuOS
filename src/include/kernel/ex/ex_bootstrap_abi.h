/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_abi.h
 * Description: Ex-owned bootstrap ABI constants and evidence-chain anchors.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

/*
 * Bootstrap layout defaults for the current bring-up path. The live contract
 * comes from the current thread's attached staging/process state, while these
 * constants remain the default placements used when that layout is created.
 * When the owning EX_PROCESS holds a process-private root, the pages live in
 * that root's low-half rather than the shared imported kernel root. A dynamic
 * per-process layout allocator is a future phase.
 */
#define KE_USER_BOOTSTRAP_PAGE_SIZE              0x1000ULL
#define KE_USER_BOOTSTRAP_WINDOW_BASE            0x0000000080000000ULL
#define KE_USER_BOOTSTRAP_WINDOW_PAGE_COUNT      4ULL
#define KE_USER_BOOTSTRAP_WINDOW_END_EXCLUSIVE   (KE_USER_BOOTSTRAP_WINDOW_BASE +                     \
                                                  (KE_USER_BOOTSTRAP_WINDOW_PAGE_COUNT *              \
                                                   KE_USER_BOOTSTRAP_PAGE_SIZE))
/* Default single-page placements inside the current bootstrap layout. */
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
 * The SYS_RAW_* namespace is intentionally scoped to bring-up only. Bootstrap
 * capability SYS_* services use a separate number range so raw semantics stay
 * fixed while the pilot evolves.
 */
#define KE_USER_BOOTSTRAP_SYSCALL_VECTOR             0x80U
#define KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH   256U

#define SYS_RAW_INVALID                              0U
#define SYS_RAW_WRITE                                1U
#define SYS_RAW_EXIT                                 2U

/*
 * Bootstrap capability syscall ABI:
 * - Shares the int 0x80 trap entry and register convention with SYS_RAW_*.
 * - Uses a distinct number range and process-private generation-checked handles.
 */
#define KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE         0x100U

#define SYS_WRITE                                          (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 0U)
#define SYS_CLOSE                                          (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 1U)

#define KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE        0U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION          1U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION_OFFSET   0U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_SIZE_OFFSET      4U
#define KE_USER_BOOTSTRAP_CAPABILITY_PROCESS_SELF_OFFSET   8U
#define KE_USER_BOOTSTRAP_CAPABILITY_THREAD_SELF_OFFSET    12U
#define KE_USER_BOOTSTRAP_CAPABILITY_STDOUT_OFFSET         16U
#define KE_USER_BOOTSTRAP_CAPABILITY_WAIT_OBJECT_OFFSET    20U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE       24U
#define KE_USER_BOOTSTRAP_CONST_PAYLOAD_OFFSET             KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE

typedef struct __attribute__((packed)) KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK
{
    uint32_t Version;
    uint32_t Size;
    uint32_t ProcessSelf;
    uint32_t ThreadSelf;
    uint32_t Stdout;
    uint32_t WaitObject;
} KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK;

/*
 * Bootstrap raw write keeps both rejection and success paths on the same
 * helper family in ke/user_bootstrap_syscall.c:
 * - query the current thread's live bootstrap layout from Ke staging state
 * - validate the supplied range against that live layout
 * - validate that covered pages remain user accessible in the layout owner's root
 * - bounded copy-in into a kernel scratch buffer
 *
 * Stable user_hello evidence-chain anchors then record P1 milestones first,
 * followed by raw-write rejection/success and raw-exit handoff. The fixed
 * addresses above remain bootstrap layout defaults, not a shared-root ABI.
 */
#define KE_USER_BOOTSTRAP_LOG_ENTER_USER_MODE          "[USERBOOT] enter user mode"
#define KE_USER_BOOTSTRAP_LOG_P1_FIRST_ENTRY           KE_USER_BOOTSTRAP_LOG_ENTER_USER_MODE
#define KE_USER_BOOTSTRAP_LOG_TIMER_FROM_USER_FORMAT   "[USERBOOT] timer from user #%u"
#define KE_USER_BOOTSTRAP_LOG_P1_GATE_ARMED            "[USERBOOT] P1 gate armed"
#define KE_USER_BOOTSTRAP_LOG_HELLO                    "[USERBOOT] hello"
#define KE_USER_BOOTSTRAP_LOG_INVALID_RAW_WRITE        "[USERBOOT] invalid raw write rejected"
#define KE_USER_BOOTSTRAP_LOG_HELLO_WRITE_SUCCEEDED    "[USERBOOT] hello write succeeds"
#define KE_USER_BOOTSTRAP_LOG_SYS_RAW_EXIT             "[USERBOOT] SYS_RAW_EXIT"
#define KE_USER_BOOTSTRAP_LOG_INVALID_SYSCALL          "[USERBOOT] invalid raw syscall"
#define KE_USER_BOOTSTRAP_LOG_INVALID_CAP_SYSCALL      "[USERCAP] invalid capability syscall"
#define KE_USER_BOOTSTRAP_LOG_CAP_WRITE_SUCCEEDED      "[USERCAP] stdout capability write succeeds"
#define KE_USER_BOOTSTRAP_LOG_CAP_CLOSE_SUCCEEDED      "[USERCAP] SYS_CLOSE succeeded"
#define KE_USER_BOOTSTRAP_LOG_CAP_REJECTED             "[USERCAP] capability syscall rejected"
#define KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER      "[USERBOOT] invalid user buffer"
#define KE_USER_BOOTSTRAP_LOG_TEARDOWN_FAILED          "[USERBOOT] bootstrap teardown failed"
#define KE_USER_BOOTSTRAP_LOG_TEARDOWN_COMPLETE        "[USERBOOT] bootstrap teardown complete"
#define KE_USER_BOOTSTRAP_LOG_FALLBACK_RECLAIM         "[USERBOOT] fallback staging reclaim in finalizer"
#define KE_USER_BOOTSTRAP_LOG_THREAD_TERMINATED_FORMAT "[SCHED] Thread %u terminated"
#define KE_USER_BOOTSTRAP_LOG_IDLE_REAPER              "[USERBOOT] idle/reaper reclaimed user_hello thread"
