/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler/wait.c
 * Description: Wait and dispatcher satisfaction logic for the scheduler.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "scheduler_internal.h"

void
KiAssertBlockingAllowed(void)
{
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);
}

void
KiAssertDispatchLevel(void)
{
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
}

// Internal: initialize a wait block to clean state
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

// Internal: validate dispatcher headers before generic wait logic
HO_STATUS
KiValidateDispatcherHeader(const KDISPATCHER_HEADER *header)
{
    if (header == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Signature != KDISPATCHER_SIGNATURE)
        return EC_INVALID_STATE;

    switch (header->Type)
    {
    case DISPATCHER_TYPE_EVENT:
    case DISPATCHER_TYPE_SEMAPHORE:
    case DISPATCHER_TYPE_MUTEX:
        return EC_SUCCESS;
    default:
        return EC_NOT_SUPPORTED;
    }
}

// Internal: test whether a dispatcher object can satisfy immediately.
// For counted or owned objects, this helper also consumes the grant atomically.
HO_STATUS
KiTryAcquireDispatcherObject(KDISPATCHER_HEADER *header, KTHREAD *thread, BOOL *acquired)
{
    HO_KASSERT(header != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(acquired != NULL, EC_ILLEGAL_ARGUMENT);

    *acquired = FALSE;

    switch (header->Type)
    {
    case DISPATCHER_TYPE_EVENT:
        *acquired = header->SignalState != 0;
        return EC_SUCCESS;

    case DISPATCHER_TYPE_SEMAPHORE: {
        KSEMAPHORE *semaphore = (KSEMAPHORE *)header;
        KiAssertSemaphoreState(semaphore);

        if (header->SignalState == 0)
            return EC_SUCCESS;

        header->SignalState--;
        HO_KASSERT(header->SignalState >= 0, EC_INVALID_STATE);
        *acquired = TRUE;
        return EC_SUCCESS;
    }

    case DISPATCHER_TYPE_MUTEX: {
        KMUTEX *mutex = (KMUTEX *)header;
        KiAssertMutexState(mutex);

        if (mutex->OwnerThread == thread)
            return EC_INVALID_STATE;

        if (header->SignalState == 0)
            return EC_SUCCESS;

        KiAcquireMutexOwnership(mutex, thread);
        *acquired = TRUE;
        return EC_SUCCESS;
    }

    default:
        return EC_NOT_SUPPORTED;
    }
}

// Internal: unified wait completion — signal or timeout
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

// KeWaitForSingleObject
HO_KERNEL_API HO_STATUS
KeWaitForSingleObject(void *object, uint64_t timeoutNs)
{
    KiAssertBlockingAllowed();

    if (object == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_KASSERT(gCurrentThread != gIdleThread, EC_INVALID_STATE);

    KDISPATCHER_HEADER *header = (KDISPATCHER_HEADER *)object;
    HO_STATUS validationStatus = KiValidateDispatcherHeader(header);
    if (validationStatus != EC_SUCCESS)
        return validationStatus;

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    // Path 1: object already signaled / has a permit — immediate success
    BOOL acquired = FALSE;
    HO_STATUS acquireStatus = KiTryAcquireDispatcherObject(header, gCurrentThread, &acquired);
    if (acquireStatus != EC_SUCCESS)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return acquireStatus;
    }

    if (acquired)
    {
        klog(KLOG_LEVEL_DEBUG, "[WAIT] Thread %u immediate satisfy (type=%d, state=%ld)\n", gCurrentThread->ThreadId,
             header->Type, (long)header->SignalState);
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return EC_SUCCESS;
    }

    // Path 2: zero-timeout poll — immediate timeout
    if (timeoutNs == 0)
    {
        klog(KLOG_LEVEL_DEBUG, "[WAIT] Thread %u zero-timeout poll miss (type=%d, state=%ld)\n",
             gCurrentThread->ThreadId, header->Type, (long)header->SignalState);
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
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
    KeReleaseIrqlGuard(&irqlGuard);

    return completionStatus;
}
