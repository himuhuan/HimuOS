/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_abi.h
 * Description: Ex-owned bootstrap ABI constants and evidence-chain anchors.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#ifndef __ASSEMBLER__
#include <_hobase.h>
#endif

/*
 * Bootstrap layout defaults for the current bring-up path. The live contract
 * comes from the current thread's attached staging/process state, while these
 * constants remain the default placements used when that layout is created.
 * When the owning EX_PROCESS holds a process-private root, the pages live in
 * that root's low-half rather than the shared imported kernel root. A dynamic
 * per-process layout allocator is a future phase.
 */
#define KE_USER_BOOTSTRAP_PAGE_SIZE         0x1000ULL
#define KE_USER_BOOTSTRAP_WINDOW_BASE       0x0000000080000000ULL
#define KE_USER_BOOTSTRAP_WINDOW_PAGE_COUNT 4ULL
#define KE_USER_BOOTSTRAP_WINDOW_END_EXCLUSIVE                                                                         \
    (KE_USER_BOOTSTRAP_WINDOW_BASE + (KE_USER_BOOTSTRAP_WINDOW_PAGE_COUNT * KE_USER_BOOTSTRAP_PAGE_SIZE))
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
 * Shared bootstrap syscall trap ABI:
 * - Entry is a synchronous int 0x80 trap.
 * - RAX carries the syscall number on entry and the return value on exit.
 * - RDI, RSI, and RDX carry the first three bootstrap arguments.
 * - Failures return negative HO_STATUS values.
 */
#define KE_USER_BOOTSTRAP_SYSCALL_VECTOR         0x80U
#define KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH       256U
#define KE_USER_BOOTSTRAP_READLINE_MAX_LENGTH            128U

/*
 * Raw bootstrap-only syscall ABI.
 *
 * The SYS_RAW_* namespace is intentionally scoped to bring-up helpers and the
 * raw userspace regression sentinel. Formal Ex-facing SYS_* services use a
 * separate number range so raw semantics stay fixed while the pilot evolves.
 */
#define SYS_RAW_INVALID                                  0U
#define SYS_RAW_WRITE                                    1U
#define SYS_RAW_EXIT                                     2U

/*
 * Ex-facing bootstrap syscall ABI:
 * - Shares the int 0x80 trap entry and register convention with SYS_RAW_*.
 * - Uses a distinct number range and process-private generation-checked handles.
 * - SYS_EXIT(exit_code, reserved0, reserved1) is the formal userspace
 *   termination contract for compiled bootstrap userspace.
 */
#define KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE        0x100U

#define SYS_WRITE                                        (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 0U)
#define SYS_CLOSE                                        (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 1U)
#define SYS_WAIT_ONE                                     (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 2U)
#define SYS_EXIT                                         (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 3U)
#define SYS_READLINE                                     (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 4U)
#define SYS_SPAWN_BUILTIN                                (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 5U)
#define SYS_WAIT_PID                                     (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 6U)
#define SYS_SLEEP_MS                                     (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 7U)
#define SYS_KILL_PID                                     (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 8U)
#define SYS_QUERY_SYSINFO                                (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 9U)

#define KE_USER_BOOTSTRAP_WAIT_ONE_TIMEOUT_MAX_MS        0xFFFFFFFFULL
#define KE_USER_BOOTSTRAP_WAIT_ONE_TIMEOUT_NS_PER_MS     1000000ULL
#define KE_USER_BOOTSTRAP_SLEEP_MS_MAX                   0xFFFFFFFFULL
#define KE_USER_BOOTSTRAP_SLEEP_NS_PER_MS                1000000ULL

#define KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_NONE           0U
#define KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_HSH            1U
#define KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_CALC           2U
#define KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_TICK1S         3U
#define KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_DE       4U
#define KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_PF       5U

#define KE_USER_BOOTSTRAP_SPAWN_FLAG_NONE                0U
#define KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND          0x00000001U

#define KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE      0U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION        1U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION_OFFSET 0U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_SIZE_OFFSET    4U
#define KE_USER_BOOTSTRAP_CAPABILITY_PROCESS_SELF_OFFSET 8U
#define KE_USER_BOOTSTRAP_CAPABILITY_THREAD_SELF_OFFSET  12U
#define KE_USER_BOOTSTRAP_CAPABILITY_STDOUT_OFFSET       16U
#define KE_USER_BOOTSTRAP_CAPABILITY_WAIT_OBJECT_OFFSET  20U
#define KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE     24U
#define KE_USER_BOOTSTRAP_CONST_PAYLOAD_OFFSET           KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE

#define EX_SYSINFO_OVERVIEW_VERSION                      1U
#define EX_SYSINFO_TEXT_MAX_LENGTH                       1024U
#define EX_SYSINFO_OVERVIEW_CPU_NAME_LENGTH              128U
#define EX_SYSINFO_OVERVIEW_SOURCE_NAME_LENGTH           32U

#define EX_SYSINFO_OVERVIEW_VALID_CPU                    (1ULL << 0)
#define EX_SYSINFO_OVERVIEW_VALID_MEMORY                 (1ULL << 1)
#define EX_SYSINFO_OVERVIEW_VALID_VMM                    (1ULL << 2)
#define EX_SYSINFO_OVERVIEW_VALID_SCHEDULER              (1ULL << 3)
#define EX_SYSINFO_OVERVIEW_VALID_UPTIME                 (1ULL << 4)
#define EX_SYSINFO_OVERVIEW_VALID_CLOCK                  (1ULL << 5)
#define EX_SYSINFO_OVERVIEW_VALID_TIME_SOURCE            (1ULL << 6)
#define EX_SYSINFO_OVERVIEW_VALID_VERSION                (1ULL << 7)

#ifndef __ASSEMBLER__
typedef struct __attribute__((packed)) KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK
{
    uint32_t Version;
    uint32_t Size;
    uint32_t ProcessSelf;
    uint32_t ThreadSelf;
    uint32_t Stdout;
    uint32_t WaitObject;
} KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK;

typedef enum EX_SYSINFO_CLASS
{
    EX_SYSINFO_CLASS_INVALID = 0,
    EX_SYSINFO_CLASS_OVERVIEW = 1,
    EX_SYSINFO_CLASS_OVERVIEW_TEXT = 2,
    EX_SYSINFO_CLASS_THREAD_LIST = 3,
    EX_SYSINFO_CLASS_THREAD_LIST_TEXT = 4,
    EX_SYSINFO_CLASS_MEMMAP_TEXT = 5,
    EX_SYSINFO_CLASS_PROCESS_LIST = 6,
    EX_SYSINFO_CLASS_PROCESS_LIST_TEXT = 7,
} EX_SYSINFO_CLASS;

#define EX_SYSINFO_THREAD_LIST_VERSION     1U
#define EX_SYSINFO_THREAD_NAME_LENGTH      16U
#define EX_SYSINFO_THREAD_LIST_MAX_ENTRIES 8U

typedef enum EX_SYSINFO_THREAD_STATE
{
    EX_SYSINFO_THREAD_STATE_INVALID = 0,
    EX_SYSINFO_THREAD_STATE_READY = 1,
    EX_SYSINFO_THREAD_STATE_RUNNING = 2,
    EX_SYSINFO_THREAD_STATE_BLOCKED = 3,
    EX_SYSINFO_THREAD_STATE_SLEEPING = 4,
    EX_SYSINFO_THREAD_STATE_TERMINATED = 5,
    EX_SYSINFO_THREAD_STATE_IDLE = 6,
} EX_SYSINFO_THREAD_STATE;

typedef struct EX_SYSINFO_THREAD_ENTRY
{
    uint32_t ThreadId;
    uint32_t State;
    uint32_t Priority;
    char Name[EX_SYSINFO_THREAD_NAME_LENGTH];
} EX_SYSINFO_THREAD_ENTRY;

typedef struct EX_SYSINFO_THREAD_LIST
{
    uint32_t Version;
    uint32_t Size;
    uint32_t TotalCount;
    uint32_t ReturnedCount;
    BOOL Truncated;
    uint8_t Reserved[3];
    EX_SYSINFO_THREAD_ENTRY Entries[EX_SYSINFO_THREAD_LIST_MAX_ENTRIES];
} EX_SYSINFO_THREAD_LIST;

#define EX_SYSINFO_PROCESS_LIST_VERSION     1U
#define EX_SYSINFO_PROCESS_NAME_LENGTH      16U
#define EX_SYSINFO_PROCESS_LIST_MAX_ENTRIES 8U

typedef enum EX_SYSINFO_PROCESS_STATE
{
    EX_SYSINFO_PROCESS_STATE_INVALID = 0,
    EX_SYSINFO_PROCESS_STATE_CREATED = 1,
    EX_SYSINFO_PROCESS_STATE_READY = 2,
    EX_SYSINFO_PROCESS_STATE_RUNNING = 3,
    EX_SYSINFO_PROCESS_STATE_BLOCKED = 4,
    EX_SYSINFO_PROCESS_STATE_SLEEPING = 5,
    EX_SYSINFO_PROCESS_STATE_TERMINATED = 6,
} EX_SYSINFO_PROCESS_STATE;

typedef struct EX_SYSINFO_PROCESS_ENTRY
{
    uint32_t ProcessId;
    uint32_t ParentProcessId;
    uint32_t MainThreadId;
    uint32_t State;
    char Name[EX_SYSINFO_PROCESS_NAME_LENGTH];
} EX_SYSINFO_PROCESS_ENTRY;

typedef struct EX_SYSINFO_PROCESS_LIST
{
    uint32_t Version;
    uint32_t Size;
    uint32_t TotalCount;
    uint32_t ReturnedCount;
    BOOL Truncated;
    uint8_t Reserved[3];
    EX_SYSINFO_PROCESS_ENTRY Entries[EX_SYSINFO_PROCESS_LIST_MAX_ENTRIES];
} EX_SYSINFO_PROCESS_LIST;

typedef struct EX_SYSINFO_OVERVIEW
{
    uint32_t Version;
    uint32_t Size;
    uint64_t ValidMask;
    char CpuModel[EX_SYSINFO_OVERVIEW_CPU_NAME_LENGTH];
    uint64_t PhysicalTotalBytes;
    uint64_t PhysicalFreeBytes;
    uint64_t PhysicalAllocatedBytes;
    uint64_t PhysicalReservedBytes;
    uint64_t StackArenaTotalPages;
    uint64_t StackArenaUsedPages;
    uint64_t HeapArenaTotalPages;
    uint64_t HeapArenaUsedPages;
    uint64_t FixmapTotalSlots;
    uint64_t FixmapActiveSlots;
    uint32_t SchedulerEnabled;
    uint32_t CurrentThreadId;
    uint32_t IdleThreadId;
    uint32_t ReadyQueueDepth;
    uint32_t SleepQueueDepth;
    uint32_t ActiveThreadCount;
    uint64_t UptimeNanoseconds;
    uint32_t ClockReady;
    uint32_t ClockVectorNumber;
    uint64_t ClockFrequencyHz;
    char ClockSourceName[EX_SYSINFO_OVERVIEW_SOURCE_NAME_LENGTH];
    uint64_t TimeSourceFrequencyHz;
    char TimeSourceName[EX_SYSINFO_OVERVIEW_SOURCE_NAME_LENGTH];
    uint16_t SystemMajor;
    uint16_t SystemMinor;
    uint16_t SystemPatch;
    char BuildDate[12];
    char BuildTime[10];
} EX_SYSINFO_OVERVIEW;
#endif

/*
 * Bootstrap raw write keeps both rejection and success paths on the same
 * Ex syscall path, backed by Ke user-copy mechanics:
 * - query the current thread's live bootstrap layout from Ke staging state
 * - validate the supplied range against that live layout
 * - validate that covered pages remain user accessible in the layout owner's root
 * - bounded copy-in into a kernel scratch buffer
 *
 * Stable user_hello evidence-chain anchors then record P1 milestones first,
 * followed by raw-write rejection/success, SYS_RAW_EXIT, thread termination,
 * finalizer teardown completion, and idle/reaper reclaim. The fixed addresses
 * above remain bootstrap layout defaults, not a shared-root ABI.
 */
#define KE_USER_BOOTSTRAP_LOG_ENTER_USER_MODE          "[USERBOOT] enter user mode"
#define KE_USER_BOOTSTRAP_LOG_P1_FIRST_ENTRY           KE_USER_BOOTSTRAP_LOG_ENTER_USER_MODE
#define KE_USER_BOOTSTRAP_LOG_TIMER_FROM_USER_FORMAT   "[USERBOOT] timer from user #%u"
#define KE_USER_BOOTSTRAP_LOG_P1_GATE_ARMED            "[USERBOOT] P1 gate armed"
#define KE_USER_BOOTSTRAP_LOG_HELLO                    "[USERBOOT] hello"
#define KE_USER_BOOTSTRAP_LOG_INVALID_RAW_WRITE        "[USERBOOT] invalid raw write rejected"
#define KE_USER_BOOTSTRAP_LOG_HELLO_WRITE_SUCCEEDED    "[USERBOOT] hello write succeeds"
#define KE_USER_BOOTSTRAP_LOG_SYS_RAW_EXIT             "[USERBOOT] SYS_RAW_EXIT"
#define KE_USER_BOOTSTRAP_LOG_SYS_EXIT                 "[USERBOOT] SYS_EXIT"
#define KE_USER_BOOTSTRAP_LOG_INVALID_SYSCALL          "[USERBOOT] invalid raw syscall"
#define KE_USER_BOOTSTRAP_LOG_INVALID_CAP_SYSCALL      "[USERCAP] invalid capability syscall"
#define KE_USER_BOOTSTRAP_LOG_CAP_WRITE_SUCCEEDED      "[USERCAP] stdout capability write succeeds"
#define KE_USER_BOOTSTRAP_LOG_CAP_CLOSE_SUCCEEDED      "[USERCAP] SYS_CLOSE succeeded"
#define KE_USER_BOOTSTRAP_LOG_CAP_WAIT_SUCCEEDED       "[USERCAP] SYS_WAIT_ONE succeeded"
#define KE_USER_BOOTSTRAP_LOG_CAP_REJECTED             "[USERCAP] capability syscall rejected"
#define KE_USER_BOOTSTRAP_LOG_READLINE_SUCCEEDED       "[USERINPUT] SYS_READLINE succeeded"
#define KE_USER_BOOTSTRAP_LOG_READLINE_REJECTED        "[USERINPUT] SYS_READLINE rejected"
#define KE_USER_BOOTSTRAP_LOG_SPAWN_BUILTIN_SUCCEEDED  "[DEMOSHELL] SYS_SPAWN_BUILTIN succeeded"
#define KE_USER_BOOTSTRAP_LOG_SPAWN_BUILTIN_REJECTED   "[DEMOSHELL] SYS_SPAWN_BUILTIN rejected"
#define KE_USER_BOOTSTRAP_LOG_WAIT_PID_SUCCEEDED       "[DEMOSHELL] SYS_WAIT_PID succeeded"
#define KE_USER_BOOTSTRAP_LOG_WAIT_PID_REJECTED        "[DEMOSHELL] SYS_WAIT_PID rejected"
#define KE_USER_BOOTSTRAP_LOG_SLEEP_MS_SUCCEEDED       "[DEMOSHELL] SYS_SLEEP_MS succeeded"
#define KE_USER_BOOTSTRAP_LOG_SLEEP_MS_REJECTED        "[DEMOSHELL] SYS_SLEEP_MS rejected"
#define KE_USER_BOOTSTRAP_LOG_KILL_PID_SUCCEEDED       "[DEMOSHELL] SYS_KILL_PID succeeded"
#define KE_USER_BOOTSTRAP_LOG_KILL_PID_REJECTED        "[DEMOSHELL] SYS_KILL_PID rejected"
#define KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED  "[SYSINFO] SYS_QUERY_SYSINFO succeeded"
#define KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_REJECTED   "[SYSINFO] SYS_QUERY_SYSINFO rejected"
#define KE_USER_BOOTSTRAP_LOG_KILL_EXIT                "[DEMOSHELL] kill exit"
#define KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER      "[USERBOOT] invalid user buffer"
#define KE_USER_BOOTSTRAP_LOG_TEARDOWN_FAILED          "[USERBOOT] bootstrap teardown failed"
#define KE_USER_BOOTSTRAP_LOG_TEARDOWN_COMPLETE        "[USERBOOT] bootstrap teardown complete"
#define KE_USER_BOOTSTRAP_LOG_THREAD_TERMINATED_FORMAT "[SCHED] Thread %u terminated"
#define KE_USER_BOOTSTRAP_LOG_IDLE_REAPER              "[USERBOOT] idle/reaper reclaimed user_hello thread"
