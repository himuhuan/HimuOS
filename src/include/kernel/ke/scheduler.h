/**
 * HimuOperatingSystem
 *
 * File: ke/scheduler.h
 * Description:
 * Ke Layer - Minimal Round-Robin tickless scheduler public API.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/event.h>
#include <kernel/ke/mutex.h>
#include <kernel/ke/semaphore.h>

// ─────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────

#define KE_DEFAULT_QUANTUM_NS 10000000ULL // 10 ms
#define KE_MAX_THREADS        64
#define KE_THREAD_STACK_PAGES 4
#define KE_THREAD_STACK_SIZE  (KE_THREAD_STACK_PAGES * 0x1000ULL)

// ─────────────────────────────────────────────────────────────
// Scheduler statistics (returned via sysinfo)
// ─────────────────────────────────────────────────────────────

typedef struct KE_SCHEDULER_STATS
{
    uint32_t ActiveThreadCount;
    uint64_t ContextSwitchCount;
    uint64_t PreemptionCount;
    uint64_t YieldCount;
    uint64_t SleepWakeCount;
    uint64_t TotalThreadsCreated;
} KE_SCHEDULER_STATS;

typedef struct KE_SYSINFO_SCHEDULER_DATA
{
    BOOL SchedulerEnabled;
    uint32_t CurrentThreadId;
    uint32_t IdleThreadId;
    uint32_t ReadyQueueDepth;
    uint32_t SleepQueueDepth;
    uint32_t ActiveThreadCount;
    uint64_t EarliestWakeDeadline;
    uint64_t NextProgrammedDeadline;
    uint64_t ContextSwitchCount;
    uint64_t PreemptionCount;
    uint64_t YieldCount;
    uint64_t SleepWakeCount;
    uint64_t TotalThreadsCreated;
} KE_SYSINFO_SCHEDULER_DATA;

// ─────────────────────────────────────────────────────────────
// Scheduler API
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS KeSchedulerInit(void);

HO_KERNEL_API HO_STATUS KeThreadCreate(KTHREAD **outThread, KTHREAD_ENTRY entryPoint, void *arg);
HO_KERNEL_API HO_STATUS KeThreadStart(KTHREAD *thread);

HO_KERNEL_API void KeYield(void);
HO_KERNEL_API void KeSleep(uint64_t durationNs);
HO_KERNEL_API HO_NORETURN void KeThreadExit(void);

/**
 * @brief Wait for a single dispatcher object to become signaled.
 * @param object    Pointer to a dispatcher object (KEVENT, KSEMAPHORE, KMUTEX, etc.).
 * @param timeoutNs Timeout in nanoseconds. KE_WAIT_INFINITE = wait forever;
 *                  0 = zero-timeout poll (never blocks).
 * @return EC_SUCCESS if the object was signaled; EC_TIMEOUT if timed out.
 */
HO_KERNEL_API HO_STATUS KeWaitForSingleObject(void *object, uint64_t timeoutNs);

HO_KERNEL_API KTHREAD *KeGetCurrentThread(void);

/**
 * @brief Query scheduler state for sysinfo.
 */
HO_KERNEL_API HO_STATUS KeQuerySchedulerInfo(KE_SYSINFO_SCHEDULER_DATA *out);

/**
 * @brief Idle loop — reaps terminated threads and halts the CPU.
 *        Called from kmain after scheduler setup; never returns.
 */
HO_KERNEL_API HO_NORETURN void KeIdleLoop(void);
