/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler_wait.c
 * Description: Wait/timeout queue management, timer arming, and KeWaitForSingleObject.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "scheduler_internal.h"

void
KiInitWaitBlock(KWAIT_BLOCK *block)
{
    block->Dispatcher = NULL;
    LinkedListInit(&block->WaitListLink);
    LinkedListInit(&block->TimeoutLink);
    block->DeadlineNs = 0;
    block->CompletionStatus = EC_SUCCESS;
    block->Completed = FALSE;
}

void
KiInsertTimeoutQueue(KWAIT_BLOCK *block)
{
    LINKED_LIST_TAG *pos;
    for (pos = gTimeoutQueue.Flink; pos != &gTimeoutQueue; pos = pos->Flink)
    {
        KWAIT_BLOCK *existing = CONTAINING_RECORD(pos, KWAIT_BLOCK, TimeoutLink);
        if (existing->DeadlineNs > block->DeadlineNs)
            break;
    }
    block->TimeoutLink.Flink = pos;
    block->TimeoutLink.Blink = pos->Blink;
    pos->Blink->Flink = &block->TimeoutLink;
    pos->Blink = &block->TimeoutLink;
}

void
KiCompleteWait(KWAIT_BLOCK *block, HO_STATUS status)
{
    if (block->Completed)
        return;

    block->Completed = TRUE;
    block->CompletionStatus = status;

    // Remove from dispatcher wait list if attached
    if (block->Dispatcher != NULL)
    {
        LinkedListRemove(&block->WaitListLink);
        LinkedListInit(&block->WaitListLink);
    }

    // Remove from timeout queue if attached
    if (block->DeadlineNs != 0)
    {
        LinkedListRemove(&block->TimeoutLink);
        LinkedListInit(&block->TimeoutLink);
    }

    KTHREAD *thread = CONTAINING_RECORD(block, KTHREAD, WaitBlock);
    thread->State = KTHREAD_STATE_READY;
    LinkedListInsertTail(&gReadyQueue, &thread->ReadyLink);
    gStats.SleepWakeCount++;

    klog(KLOG_LEVEL_DEBUG, "[SCHED] Thread %u wait completed (%s)\n", thread->ThreadId,
         status == EC_SUCCESS ? "signaled" : "timeout");
}

void
KiWakeTimeouts(uint64_t nowNs)
{
    while (!LinkedListIsEmpty(&gTimeoutQueue))
    {
        KWAIT_BLOCK *block = CONTAINING_RECORD(gTimeoutQueue.Flink, KWAIT_BLOCK, TimeoutLink);
        if (block->DeadlineNs > nowNs)
            break;

        KiCompleteWait(block, EC_TIMEOUT);
    }
}

void
KiArmClockEvent(uint64_t deltaNs)
{
    if (deltaNs == 0)
        return;

    uint64_t minDelta = KeClockEventGetMinDeltaNs();
    uint64_t maxDelta = KeClockEventGetMaxDeltaNs();

    if (minDelta > 0 && deltaNs < minDelta)
        deltaNs = minDelta;
    if (maxDelta > 0 && deltaNs > maxDelta)
        deltaNs = maxDelta;

    HO_STATUS armStatus = KeClockEventSetNextEvent(deltaNs);
    if (armStatus != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING, "KiArmClockEvent: SetNextEvent failed (0x%x)\n", armStatus);
    }
}

void
KiArmForNextEvent(uint64_t nowNs, KTHREAD *next)
{
    if (next == gIdleThread)
    {
        if (!LinkedListIsEmpty(&gTimeoutQueue))
        {
            KWAIT_BLOCK *block = CONTAINING_RECORD(gTimeoutQueue.Flink, KWAIT_BLOCK, TimeoutLink);
            uint64_t delta = block->DeadlineNs > nowNs ? block->DeadlineNs - nowNs : 1;
            gNextProgrammedDeadlineNs = block->DeadlineNs;
            KiArmClockEvent(delta);
        }
        else
        {
            gNextProgrammedDeadlineNs = 0;
        }
    }
    else
    {
        next->Quantum = KE_DEFAULT_QUANTUM_NS;
        gQuantumDeadlineNs = nowNs + KE_DEFAULT_QUANTUM_NS;

        uint64_t targetDeadline = gQuantumDeadlineNs;

        if (!LinkedListIsEmpty(&gTimeoutQueue))
        {
            KWAIT_BLOCK *block = CONTAINING_RECORD(gTimeoutQueue.Flink, KWAIT_BLOCK, TimeoutLink);
            if (block->DeadlineNs < targetDeadline)
                targetDeadline = block->DeadlineNs;
        }

        uint64_t delta = targetDeadline > nowNs ? targetDeadline - nowNs : 1;
        gNextProgrammedDeadlineNs = targetDeadline;
        KiArmClockEvent(delta);
    }
}

// ─────────────────────────────────────────────────────────────
// KeWaitForSingleObject
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeWaitForSingleObject(void *object, uint64_t timeoutNs)
{
    KiAssertDispatchContextAllowed();

    if (object == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_KASSERT(gCurrentThread != gIdleThread, EC_INVALID_STATE);

    KDISPATCHER_HEADER *header = (KDISPATCHER_HEADER *)object;
    HO_STATUS validationStatus = KiValidateDispatcherHeader(header);
    if (validationStatus != EC_SUCCESS)
        return validationStatus;

    ARCH_INTERRUPT_STATE savedInterruptState = ArchDisableInterrupts();
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    // Path 1: object already signaled / has a permit — immediate success
    BOOL acquired = FALSE;
    HO_STATUS acquireStatus = KiTryAcquireDispatcherObject(header, gCurrentThread, &acquired);
    if (acquireStatus != EC_SUCCESS)
    {
        KeLeaveCriticalSection(&criticalSection);
        ArchRestoreInterruptState(savedInterruptState);
        return acquireStatus;
    }

    if (acquired)
    {
        klog(KLOG_LEVEL_DEBUG, "[WAIT] Thread %u immediate satisfy (type=%d, state=%ld)\n", gCurrentThread->ThreadId,
             header->Type, (long)header->SignalState);
        KeLeaveCriticalSection(&criticalSection);
        ArchRestoreInterruptState(savedInterruptState);
        return EC_SUCCESS;
    }

    // Path 2: zero-timeout poll — immediate timeout
    if (timeoutNs == 0)
    {
        klog(KLOG_LEVEL_DEBUG, "[WAIT] Thread %u zero-timeout poll miss (type=%d, state=%ld)\n",
             gCurrentThread->ThreadId, header->Type, (long)header->SignalState);
        KeLeaveCriticalSection(&criticalSection);
        ArchRestoreInterruptState(savedInterruptState);
        return EC_TIMEOUT;
    }

    // Path 3: blocking wait
    KWAIT_BLOCK *wb = &gCurrentThread->WaitBlock;
    KiInitWaitBlock(wb);
    wb->Dispatcher = header;

    // Attach to dispatcher's wait list
    LinkedListInsertTail(&header->WaitListHead, &wb->WaitListLink);

    // Set up timeout if not infinite
    if (timeoutNs != KE_WAIT_INFINITE)
    {
        uint64_t nowNs = KiNowNs();
        wb->DeadlineNs = nowNs + timeoutNs;
        KiInsertTimeoutQueue(wb);
    }

    gCurrentThread->State = KTHREAD_STATE_BLOCKED;

    klog(KLOG_LEVEL_DEBUG, "[WAIT] Thread %u blocking (type=%d, timeout=%lu)\n", gCurrentThread->ThreadId, header->Type,
         (unsigned long)timeoutNs);

    KeLeaveCriticalSection(&criticalSection);
    KiSchedule();

    HO_STATUS completionStatus = gCurrentThread->WaitBlock.CompletionStatus;
    klog(KLOG_LEVEL_DEBUG, "[WAIT] Thread %u resumed (%s)\n", gCurrentThread->ThreadId,
         completionStatus == EC_SUCCESS ? "signaled" : "timeout");
    ArchRestoreInterruptState(savedInterruptState);

    return completionStatus;
}
