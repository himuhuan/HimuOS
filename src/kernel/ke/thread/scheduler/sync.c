/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler/sync.c
 * Description: Event, semaphore, and mutex synchronization logic for the scheduler.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "scheduler_internal.h"

// Internal: validate runtime mutex invariants
void
KiAssertMutexState(const KMUTEX *mutex)
{
    HO_KASSERT(mutex != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(mutex->Header.Signature == KDISPATCHER_SIGNATURE, EC_INVALID_STATE);
    HO_KASSERT(mutex->Header.Type == DISPATCHER_TYPE_MUTEX, EC_NOT_SUPPORTED);
    HO_KASSERT(mutex->Header.SignalState == 0 || mutex->Header.SignalState == 1, EC_INVALID_STATE);

    if (mutex->Header.SignalState != 0)
    {
        HO_KASSERT(mutex->OwnerThread == NULL, EC_INVALID_STATE);
    }
    else
    {
        HO_KASSERT(mutex->OwnerThread != NULL, EC_INVALID_STATE);
    }
}

// Internal: validate runtime semaphore invariants
void
KiAssertSemaphoreState(const KSEMAPHORE *semaphore)
{
    HO_KASSERT(semaphore != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(semaphore->Header.Signature == KDISPATCHER_SIGNATURE, EC_INVALID_STATE);
    HO_KASSERT(semaphore->Header.Type == DISPATCHER_TYPE_SEMAPHORE, EC_NOT_SUPPORTED);
    HO_KASSERT(semaphore->Limit > 0, EC_INVALID_STATE);
    HO_KASSERT(semaphore->Header.SignalState >= 0, EC_INVALID_STATE);
    HO_KASSERT(semaphore->Header.SignalState <= semaphore->Limit, EC_INVALID_STATE);
}

void
KiIncrementOwnedMutexCount(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread->OwnedMutexCount != 0xFFFFFFFFU, EC_OUT_OF_RESOURCE);
    thread->OwnedMutexCount++;
}

void
KiDecrementOwnedMutexCount(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread->OwnedMutexCount != 0, EC_INVALID_STATE);
    thread->OwnedMutexCount--;
}

void
KiAcquireMutexOwnership(KMUTEX *mutex, KTHREAD *thread)
{
    HO_KASSERT(mutex != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    KiAssertMutexState(mutex);
    HO_KASSERT(mutex->Header.SignalState == 1, EC_INVALID_STATE);
    HO_KASSERT(mutex->OwnerThread == NULL, EC_INVALID_STATE);

    mutex->OwnerThread = thread;
    mutex->Header.SignalState = 0;
    KiIncrementOwnedMutexCount(thread);
    KiAssertMutexState(mutex);
}

void
KiReleaseMutexOwnership(KMUTEX *mutex)
{
    KTHREAD *owner;

    HO_KASSERT(mutex != NULL, EC_ILLEGAL_ARGUMENT);
    KiAssertMutexState(mutex);
    owner = mutex->OwnerThread;
    HO_KASSERT(owner != NULL, EC_INVALID_STATE);

    KiDecrementOwnedMutexCount(owner);
    mutex->OwnerThread = NULL;
    mutex->Header.SignalState = 1;
    KiAssertMutexState(mutex);
}

void
KiHandOffMutexOwnership(KMUTEX *mutex, KTHREAD *thread)
{
    KTHREAD *owner;

    HO_KASSERT(mutex != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    KiAssertMutexState(mutex);
    owner = mutex->OwnerThread;
    HO_KASSERT(owner != NULL, EC_INVALID_STATE);
    HO_KASSERT(owner != thread, EC_INVALID_STATE);

    KiDecrementOwnedMutexCount(owner);
    mutex->OwnerThread = thread;
    mutex->Header.SignalState = 0;
    KiIncrementOwnedMutexCount(thread);
    KiAssertMutexState(mutex);
}

// ─────────────────────────────────────────────────────────────
// KeInitializeEvent
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeInitializeEvent(KEVENT *event, BOOL initialState)
{
    HO_KASSERT(event != NULL, EC_ILLEGAL_ARGUMENT);

    event->Header.Signature = KDISPATCHER_SIGNATURE;
    event->Header.Type = DISPATCHER_TYPE_EVENT;
    event->Header.SignalState = initialState ? 1 : 0;
    LinkedListInit(&event->Header.WaitListHead);

    klog(KLOG_LEVEL_DEBUG, "[EVENT] Initialized (signaled=%u)\n", (unsigned)initialState);
}

// ─────────────────────────────────────────────────────────────
// KeSetEvent
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeSetEvent(KEVENT *event)
{
    HO_KASSERT(event != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(event->Header.Signature == KDISPATCHER_SIGNATURE, EC_INVALID_STATE);
    HO_KASSERT(event->Header.Type == DISPATCHER_TYPE_EVENT, EC_NOT_SUPPORTED);

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    event->Header.SignalState = 1;
    uint32_t releasedCount = 0;

    // Release all waiters (manual-reset: wake everyone)
    while (!LinkedListIsEmpty(&event->Header.WaitListHead))
    {
        LINKED_LIST_TAG *entry = event->Header.WaitListHead.Flink;
        KWAIT_BLOCK *block = CONTAINING_RECORD(entry, KWAIT_BLOCK, WaitListLink);
        KiCompleteWait(block, EC_SUCCESS);
        releasedCount++;
    }

    // If CPU is idle and threads became ready, trigger immediate reschedule
    BOOL needSchedule = (gCurrentThread == gIdleThread && !LinkedListIsEmpty(KiGetDefaultReadyQueue()));

    klog(KLOG_LEVEL_DEBUG, "[EVENT] Set (released=%u)\n", releasedCount);

    KeLeaveCriticalSection(&criticalSection);

    if (needSchedule)
    {
        klog(KLOG_LEVEL_DEBUG, "[EVENT] Signal during idle, triggering reschedule\n");
        KiSchedule();
    }

    KeReleaseIrqlGuard(&irqlGuard);
}

// ─────────────────────────────────────────────────────────────
// KeResetEvent
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeResetEvent(KEVENT *event)
{
    HO_KASSERT(event != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(event->Header.Signature == KDISPATCHER_SIGNATURE, EC_INVALID_STATE);
    HO_KASSERT(event->Header.Type == DISPATCHER_TYPE_EVENT, EC_NOT_SUPPORTED);

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    event->Header.SignalState = 0;

    klog(KLOG_LEVEL_DEBUG, "[EVENT] Reset\n");

    KeLeaveCriticalSection(&criticalSection);
}

// ─────────────────────────────────────────────────────────────
// KeInitializeSemaphore
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeInitializeSemaphore(KSEMAPHORE *semaphore, int32_t initialCount, int32_t limit)
{
    if (semaphore == NULL || initialCount < 0 || limit <= 0 || initialCount > limit)
        return EC_ILLEGAL_ARGUMENT;

    semaphore->Header.Signature = KDISPATCHER_SIGNATURE;
    semaphore->Header.Type = DISPATCHER_TYPE_SEMAPHORE;
    semaphore->Header.SignalState = initialCount;
    LinkedListInit(&semaphore->Header.WaitListHead);
    semaphore->Limit = limit;

    klog(KLOG_LEVEL_DEBUG, "[SEMAPHORE] Initialized (count=%ld, limit=%ld)\n", (long)initialCount, (long)limit);
    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// KeInitializeMutex
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeInitializeMutex(KMUTEX *mutex)
{
    HO_KASSERT(mutex != NULL, EC_ILLEGAL_ARGUMENT);

    mutex->Header.Signature = KDISPATCHER_SIGNATURE;
    mutex->Header.Type = DISPATCHER_TYPE_MUTEX;
    mutex->Header.SignalState = 1;
    LinkedListInit(&mutex->Header.WaitListHead);
    mutex->OwnerThread = NULL;

    KiAssertMutexState(mutex);
    klog(KLOG_LEVEL_DEBUG, "[MUTEX] Initialized (available=1)\n");
}

// ─────────────────────────────────────────────────────────────
// KeReleaseSemaphore
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeReleaseSemaphore(KSEMAPHORE *semaphore, int32_t releaseCount)
{
    if (semaphore == NULL || releaseCount <= 0)
        return EC_ILLEGAL_ARGUMENT;

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    KiAssertSemaphoreState(semaphore);

    uint32_t waiterCount = KiCountQueueDepth(&semaphore->Header.WaitListHead);
    int64_t remainingPermitCount =
        releaseCount > (int32_t)waiterCount ? (int64_t)releaseCount - (int64_t)waiterCount : 0;
    int64_t resultingCount = (int64_t)semaphore->Header.SignalState + remainingPermitCount;

    if (resultingCount > semaphore->Limit)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return EC_ILLEGAL_ARGUMENT;
    }

    int32_t permitsToDispatch = releaseCount;
    uint32_t releasedWaiters = 0;

    while (permitsToDispatch > 0 && !LinkedListIsEmpty(&semaphore->Header.WaitListHead))
    {
        LINKED_LIST_TAG *entry = semaphore->Header.WaitListHead.Flink;
        KWAIT_BLOCK *block = CONTAINING_RECORD(entry, KWAIT_BLOCK, WaitListLink);
        KiCompleteWait(block, EC_SUCCESS);
        permitsToDispatch--;
        releasedWaiters++;
    }

    semaphore->Header.SignalState += permitsToDispatch;
    KiAssertSemaphoreState(semaphore);

    BOOL needSchedule = (gCurrentThread == gIdleThread && !LinkedListIsEmpty(KiGetDefaultReadyQueue()));

    klog(KLOG_LEVEL_DEBUG, "[SEMAPHORE] Release(count=%ld, woke=%u, available=%ld)\n", (long)releaseCount,
         releasedWaiters, (long)semaphore->Header.SignalState);

    KeLeaveCriticalSection(&criticalSection);

    if (needSchedule)
    {
        klog(KLOG_LEVEL_DEBUG, "[SEMAPHORE] Release during idle, triggering reschedule\n");
        KiSchedule();
    }

    KeReleaseIrqlGuard(&irqlGuard);
    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// KeReleaseMutex
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeReleaseMutex(KMUTEX *mutex)
{
    if (mutex == NULL)
        return EC_ILLEGAL_ARGUMENT;

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    KiAssertMutexState(mutex);

    if (mutex->OwnerThread != gCurrentThread)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return EC_INVALID_STATE;
    }

    if (LinkedListIsEmpty(&mutex->Header.WaitListHead))
    {
        KiReleaseMutexOwnership(mutex);
        klog(KLOG_LEVEL_DEBUG, "[MUTEX] Release(owner=%u, handoff=none)\n", gCurrentThread->ThreadId);
    }
    else
    {
        LINKED_LIST_TAG *entry = mutex->Header.WaitListHead.Flink;
        KWAIT_BLOCK *block = CONTAINING_RECORD(entry, KWAIT_BLOCK, WaitListLink);
        KTHREAD *nextOwner = CONTAINING_RECORD(block, KTHREAD, WaitBlock);

        KiHandOffMutexOwnership(mutex, nextOwner);
        KiCompleteWait(block, EC_SUCCESS);
        klog(KLOG_LEVEL_DEBUG, "[MUTEX] Release(owner=%u, handoff=%u)\n", gCurrentThread->ThreadId,
             nextOwner->ThreadId);
    }

    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);
    return EC_SUCCESS;
}
