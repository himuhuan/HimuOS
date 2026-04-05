/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler/diag.c
 * Description: Scheduler diagnostics and observability helpers.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "scheduler_internal.h"

KE_SCHEDULER_STATS gStats;

// ─────────────────────────────────────────────────────────────
// Queue depth counter
// ─────────────────────────────────────────────────────────────

uint32_t
KiCountQueueDepth(LINKED_LIST_TAG *head)
{
    uint32_t count = 0;
    for (LINKED_LIST_TAG *p = head->Flink; p != head; p = p->Flink)
        count++;
    return count;
}

// ─────────────────────────────────────────────────────────────
// Sysinfo query
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeQuerySchedulerInfo(KE_SYSINFO_SCHEDULER_DATA *out)
{
    if (!out)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    memset(out, 0, sizeof(*out));

    out->SchedulerEnabled = gSchedulerEnabled;

    if (!gSchedulerEnabled)
    {
        KeLeaveCriticalSection(&criticalSection);
        return EC_SUCCESS;
    }

    out->CurrentThreadId = gCurrentThread ? gCurrentThread->ThreadId : 0;
    out->IdleThreadId = gIdleThread ? gIdleThread->ThreadId : 0;
    out->ReadyQueueDepth = KiCountAllReadyThreads();
    out->SleepQueueDepth = KiCountQueueDepth(&gTimeoutQueue);

    if (!LinkedListIsEmpty(&gTimeoutQueue))
    {
        KWAIT_BLOCK *block = CONTAINING_RECORD(gTimeoutQueue.Flink, KWAIT_BLOCK, TimeoutLink);
        out->EarliestWakeDeadline = block->DeadlineNs;
    }

    out->NextProgrammedDeadline = gNextProgrammedDeadlineNs;
    out->ContextSwitchCount = gStats.ContextSwitchCount;
    out->PreemptionCount = gStats.PreemptionCount;
    out->YieldCount = gStats.YieldCount;
    out->SleepWakeCount = gStats.SleepWakeCount;
    out->TotalThreadsCreated = gStats.TotalThreadsCreated;
    out->ActiveThreadCount = gStats.ActiveThreadCount;

    KeLeaveCriticalSection(&criticalSection);
    return EC_SUCCESS;
}
