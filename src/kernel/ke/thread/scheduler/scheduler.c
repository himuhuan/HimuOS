/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler/scheduler.c
 * Description:
 * Ke Layer - Minimal Round-Robin tickless scheduler.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "scheduler_internal.h"

#include <kernel/ke/user_bootstrap.h>

// ─────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────

LINKED_LIST_TAG gReadyQueue;
LINKED_LIST_TAG gTimeoutQueue;
LINKED_LIST_TAG gTerminatedList;

KTHREAD *gCurrentThread;
KTHREAD *gIdleThread;
BOOL gSchedulerEnabled;

uint64_t gQuantumDeadlineNs;
uint64_t gNextProgrammedDeadlineNs;

// ─────────────────────────────────────────────────────────────
// Thread trampoline — first entry point for new threads
// ─────────────────────────────────────────────────────────────

void
KiThreadTrampoline(void)
{
    KTHREAD *self = KeGetCurrentThread();

    if (self->UserBootstrapContext != NULL)
    {
        KeUserBootstrapEnterCurrentThread();
    }

    ARCH_INTERRUPT_STATE enabledState = {.MaskableInterruptEnabled = TRUE};
    ArchRestoreInterruptState(enabledState);

    self->EntryPoint(self->EntryArg);
    KeThreadExit();
    __builtin_unreachable();
}

// ─────────────────────────────────────────────────────────────
// KeSchedulerInit
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeSchedulerInit(void)
{
    LinkedListInit(&gReadyQueue);
    LinkedListInit(&gTimeoutQueue);
    LinkedListInit(&gTerminatedList);
    memset(&gStats, 0, sizeof(gStats));

    // Create IdleThread using the KTHREAD pool
    gIdleThread = (KTHREAD *)KePoolAlloc(&gKThreadPool);
    if (!gIdleThread)
        return EC_OUT_OF_RESOURCE;

    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    uint64_t bootStackTop = capsule->CpuInfo.Tss.RSP0;

    gIdleThread->ThreadId = 0;
    gIdleThread->State = KTHREAD_STATE_RUNNING;
    memset(&gIdleThread->Context, 0, sizeof(KTHREAD_CONTEXT));
    gIdleThread->StackBase = bootStackTop - HO_STACK_SIZE;
    gIdleThread->StackSize = HO_STACK_SIZE;
    gIdleThread->StackGuardBase = 0;
    gIdleThread->StackOwnedByKva = FALSE;
    memset(&gIdleThread->StackRange, 0, sizeof(gIdleThread->StackRange));
    gIdleThread->Priority = 0;
    gIdleThread->Quantum = 0;
    gIdleThread->OwnedMutexCount = 0;
    KeInitializeIrqlState(&gIdleThread->IrqlState);
    KiInitWaitBlock(&gIdleThread->WaitBlock);
    KeInitializeEvent(&gIdleThread->TerminationCompletion, FALSE);
    gIdleThread->TerminationMode = KTHREAD_TERMINATION_MODE_DETACHED;
    gIdleThread->TerminationClaimState = KTHREAD_TERMINATION_CLAIM_STATE_UNCLAIMED;
    LinkedListInit(&gIdleThread->ReadyLink);
    gIdleThread->EntryPoint = NULL;
    gIdleThread->EntryArg = NULL;
    gIdleThread->Flags = KTHREAD_FLAG_IDLE;
    gIdleThread->UserBootstrapContext = NULL;

    gCurrentThread = gIdleThread;
    KeSetCurrentIrqlState(&gIdleThread->IrqlState);
    gStats.TotalThreadsCreated = 1;
    gStats.ActiveThreadCount = 1;

    // Replace timer ISR with scheduler entry
    uint8_t timerVector = KeClockEventGetVector();
    HO_STATUS status = IdtRegisterInterruptHandler(timerVector, KiSchedulerTimerISR, NULL);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[SCHED] Failed to register timer ISR\n");
        return status;
    }

    gSchedulerEnabled = TRUE;
    klog(KLOG_LEVEL_INFO, "[SCHED] Scheduler initialized (quantum=%lu ns, maxThreads=%u)\n", KE_DEFAULT_QUANTUM_NS,
         KE_MAX_THREADS);
    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// KeThreadStart
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeThreadStart(KTHREAD *thread)
{
    if (!thread)
        return EC_ILLEGAL_ARGUMENT;

    if (thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    thread->State = KTHREAD_STATE_READY;
    LinkedListInsertTail(&gReadyQueue, &thread->ReadyLink);
    gStats.TotalThreadsCreated++;
    gStats.ActiveThreadCount++;

    // If the CPU is idle, arm the timer with a minimal delta so the ISR
    // fires promptly and picks up the newly-ready thread.  This
    // bootstraps the tickless loop the very first time a thread is started.
    if (gCurrentThread == gIdleThread && gNextProgrammedDeadlineNs == 0)
    {
        KiArmClockEvent(KeClockEventGetMinDeltaNs());
    }

    KeLeaveCriticalSection(&criticalSection);

    klog(KLOG_LEVEL_INFO, "[SCHED] Thread %u started\n", thread->ThreadId);
    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// KeYield
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeYield(void)
{
    KiAssertBlockingAllowed();

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    gStats.YieldCount++;

    if (LinkedListIsEmpty(&gReadyQueue))
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return;
    }

    gCurrentThread->State = KTHREAD_STATE_READY;
    LinkedListInsertTail(&gReadyQueue, &gCurrentThread->ReadyLink);

    KeLeaveCriticalSection(&criticalSection);
    KiSchedule();
    KeReleaseIrqlGuard(&irqlGuard);
}

// ─────────────────────────────────────────────────────────────
// KeSleep
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeSleep(uint64_t durationNs)
{
    KiAssertBlockingAllowed();

    if (durationNs == 0)
    {
        KeYield();
        return;
    }

    HO_KASSERT(gCurrentThread != gIdleThread, EC_INVALID_STATE);

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    uint64_t nowNs = KiNowNs();

    // Set up timeout-only wait (Dispatcher = NULL)
    KWAIT_BLOCK *wb = &gCurrentThread->WaitBlock;
    KiInitWaitBlock(wb);
    wb->DeadlineNs = nowNs + durationNs;

    gCurrentThread->State = KTHREAD_STATE_BLOCKED;
    KiInsertTimeoutQueue(wb);

    klog(KLOG_LEVEL_DEBUG, "[SCHED] Thread %u sleep %lu ns (deadline=%lu)\n", gCurrentThread->ThreadId,
         (unsigned long)durationNs, (unsigned long)wb->DeadlineNs);

    KeLeaveCriticalSection(&criticalSection);
    KiSchedule();
    KeReleaseIrqlGuard(&irqlGuard);
}

static BOOL
KiIsThreadTerminationCompletionPublished(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);

    return thread->TerminationCompletion.Header.SignalState != 0;
}

static BOOL
KiIsThreadQueuedForReaper(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);

    return thread->ReadyLink.Flink != &thread->ReadyLink || thread->ReadyLink.Blink != &thread->ReadyLink;
}

static void
KiQueueDetachedThreadForReaper(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread != gIdleThread, EC_INVALID_STATE);
    HO_KASSERT(thread->State == KTHREAD_STATE_TERMINATED, EC_INVALID_STATE);
    HO_KASSERT(thread->TerminationMode == KTHREAD_TERMINATION_MODE_DETACHED, EC_INVALID_STATE);
    HO_KASSERT(thread->TerminationClaimState != KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS, EC_INVALID_STATE);
    HO_KASSERT(thread->TerminationClaimState != KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED, EC_INVALID_STATE);
    HO_KASSERT(KiIsThreadTerminationCompletionPublished(thread), EC_INVALID_STATE);

    if (KiIsThreadQueuedForReaper(thread))
    {
        return;
    }

    LinkedListInsertTail(&gTerminatedList, &thread->ReadyLink);
}

static HO_STATUS
KiRejectThreadLifecycleAction(const char *action, const KTHREAD *thread, const char *reason)
{
    HO_KASSERT(action != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(reason != NULL, EC_ILLEGAL_ARGUMENT);

    if (thread != NULL)
    {
        klog(KLOG_LEVEL_WARNING, "[SCHED] %s rejected for thread %u: %s\n", action, thread->ThreadId, reason);
    }
    else
    {
        klog(KLOG_LEVEL_WARNING, "[SCHED] %s rejected: %s\n", action, reason);
    }

    return EC_INVALID_STATE;
}

static void
KiMarkThreadTerminationConsumed(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread->TerminationClaimState != KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED, EC_INVALID_STATE);

    thread->TerminationClaimState = KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED;
}

static void
KiReleaseThreadJoinClaim(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS, EC_INVALID_STATE);

    thread->TerminationClaimState = KTHREAD_TERMINATION_CLAIM_STATE_UNCLAIMED;
}

void
KiFinalizeThread(KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(thread != gIdleThread, EC_INVALID_STATE);
    HO_KASSERT(thread->State == KTHREAD_STATE_TERMINATED, EC_INVALID_STATE);
    HO_KASSERT(thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED, EC_INVALID_STATE);

    if (thread->UserBootstrapContext != NULL)
    {
        klog(KLOG_LEVEL_WARNING, KE_USER_BOOTSTRAP_LOG_FALLBACK_RECLAIM " thread=%u\n", thread->ThreadId);
        HO_STATUS stagingStatus = KeUserBootstrapDestroyStaging(thread->UserBootstrapContext);
        if (stagingStatus != EC_SUCCESS)
        {
            HO_KPANIC(stagingStatus, "Failed to release terminated KTHREAD user bootstrap staging");
        }
    }

    if (thread->StackOwnedByKva)
    {
        HO_STATUS status = KeKvaReleaseRangeHandle(&thread->StackRange);
        if (status != EC_SUCCESS)
        {
            HO_KPANIC(status, "Failed to release terminated KTHREAD stack");
        }
    }

    KePoolFree(&gKThreadPool, thread);
}

// ─────────────────────────────────────────────────────────────
// KeThreadExit
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_NORETURN void
KeThreadExit(void)
{
    KiAssertBlockingAllowed();
    HO_KASSERT(gCurrentThread != gIdleThread, EC_INVALID_STATE);
    HO_KASSERT(gCurrentThread->OwnedMutexCount == 0, EC_INVALID_STATE);

    KTHREAD *thread = gCurrentThread;

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    thread->State = KTHREAD_STATE_TERMINATED;
    gStats.ActiveThreadCount--;

    klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_THREAD_TERMINATED_FORMAT "\n", thread->ThreadId);

    KeLeaveCriticalSection(&criticalSection);

    KeSetEvent(&thread->TerminationCompletion);

    KeEnterCriticalSection(&criticalSection);

    if (thread->TerminationMode == KTHREAD_TERMINATION_MODE_DETACHED)
    {
        KiQueueDetachedThreadForReaper(thread);
    }

    KeLeaveCriticalSection(&criticalSection);
    KiSchedule();
    __builtin_unreachable();
}

HO_KERNEL_API HO_STATUS
KeThreadJoin(KTHREAD *thread, uint64_t timeoutNs)
{
    KiAssertBlockingAllowed();

    if (!thread)
        return EC_ILLEGAL_ARGUMENT;

    if (thread == gCurrentThread)
    {
        return KiRejectThreadLifecycleAction("join", thread, "self-join is not allowed");
    }

    if (gCurrentThread == gIdleThread)
    {
        return KiRejectThreadLifecycleAction("join", thread, "IdleThread cannot block on thread join");
    }

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    if (thread == gIdleThread)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("join", thread, "IdleThread cannot be joined");
    }

    if (thread->TerminationMode == KTHREAD_TERMINATION_MODE_DETACHED)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("join", thread, "thread is detached");
    }

    if (thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("join", thread, "thread termination already consumed");
    }

    if (thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("join", thread, "another join is already in progress");
    }

    thread->TerminationClaimState = KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS;

    if (thread->State == KTHREAD_STATE_TERMINATED && KiIsThreadTerminationCompletionPublished(thread))
    {
        KiMarkThreadTerminationConsumed(thread);
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        KiFinalizeThread(thread);
        return EC_SUCCESS;
    }

    if (timeoutNs == 0)
    {
        KiReleaseThreadJoinClaim(thread);
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return EC_TIMEOUT;
    }

    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);

    HO_STATUS waitStatus = KeWaitForSingleObject(&thread->TerminationCompletion, timeoutNs);
    if (waitStatus != EC_SUCCESS)
    {
        KE_IRQL_GUARD completionIrqlGuard = {0};
        KeAcquireIrqlGuard(&completionIrqlGuard, KE_IRQL_DISPATCH_LEVEL);
        KE_CRITICAL_SECTION completionCriticalSection = {0};
        KeEnterCriticalSection(&completionCriticalSection);

        if (thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS)
        {
            KiReleaseThreadJoinClaim(thread);
        }

        KeLeaveCriticalSection(&completionCriticalSection);
        KeReleaseIrqlGuard(&completionIrqlGuard);
        return waitStatus;
    }

    KE_IRQL_GUARD completionIrqlGuard = {0};
    KeAcquireIrqlGuard(&completionIrqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION completionCriticalSection = {0};
    KeEnterCriticalSection(&completionCriticalSection);

    HO_KASSERT(thread->TerminationMode == KTHREAD_TERMINATION_MODE_JOINABLE, EC_INVALID_STATE);
    HO_KASSERT(thread->State == KTHREAD_STATE_TERMINATED, EC_INVALID_STATE);
    HO_KASSERT(KiIsThreadTerminationCompletionPublished(thread), EC_INVALID_STATE);
    HO_KASSERT(thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS, EC_INVALID_STATE);

    KiMarkThreadTerminationConsumed(thread);

    KeLeaveCriticalSection(&completionCriticalSection);
    KeReleaseIrqlGuard(&completionIrqlGuard);

    KiFinalizeThread(thread);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeThreadDetach(KTHREAD *thread)
{
    if (!thread)
        return EC_ILLEGAL_ARGUMENT;

    KE_IRQL_GUARD irqlGuard = {0};
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    if (thread == gIdleThread)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("detach", thread, "IdleThread lifetime is fixed to detached");
    }

    if (thread->TerminationMode == KTHREAD_TERMINATION_MODE_DETACHED)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("detach", thread, "thread is already detached");
    }

    if (thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("detach", thread, "thread termination already consumed");
    }

    if (thread->TerminationClaimState == KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS)
    {
        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);
        return KiRejectThreadLifecycleAction("detach", thread, "join is already in progress");
    }

    thread->TerminationMode = KTHREAD_TERMINATION_MODE_DETACHED;
    // Keep completion publication ahead of detached reaper handoff.
    if (thread->State == KTHREAD_STATE_TERMINATED && KiIsThreadTerminationCompletionPublished(thread))
    {
        KiQueueDetachedThreadForReaper(thread);
    }

    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);
    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// KeGetCurrentThread
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API KTHREAD *
KeGetCurrentThread(void)
{
    return gCurrentThread;
}

// ─────────────────────────────────────────────────────────────
// KiSchedule — unified scheduling decision point
// ─────────────────────────────────────────────────────────────

void
KiSchedule(void)
{
    KiAssertDispatchLevel();

    KTHREAD *prev = gCurrentThread;
    KTHREAD *next;

    if (!LinkedListIsEmpty(&gReadyQueue))
    {
        LINKED_LIST_TAG *entry = gReadyQueue.Flink;
        LinkedListRemove(entry);
        next = CONTAINING_RECORD(entry, KTHREAD, ReadyLink);
    }
    else
    {
        next = gIdleThread;
    }

    if (next == prev)
    {
        next->State = KTHREAD_STATE_RUNNING;

        // Same thread, just re-arm and continue
        uint64_t nowNs = KiNowNs();
        KiArmForNextEvent(nowNs, next);
        return;
    }

    next->State = KTHREAD_STATE_RUNNING;
    gStats.ContextSwitchCount++;

    // Update TSS.RSP0 to target thread's kernel stack top
    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    capsule->CpuInfo.Tss.RSP0 = next->StackBase + next->StackSize;

    klog(KLOG_LEVEL_DEBUG, "[SCHED] Switch %u -> %u\n", prev->ThreadId, next->ThreadId);

    // Arm clock event for next deadline
    uint64_t nowNs = KiNowNs();
    KiArmForNextEvent(nowNs, next);

    gCurrentThread = next;
    KeSetCurrentIrqlState(&next->IrqlState);

    // Context switch — does not return until this thread is resumed
    KiSwitchContext(&prev->Context, &next->Context);
}

// ─────────────────────────────────────────────────────────────
// IdleThread reaper — reclaims terminated thread resources
// ─────────────────────────────────────────────────────────────

void
KiReapTerminatedThreads(void)
{
    while (TRUE)
    {
        KTHREAD *thread = NULL;
        KE_CRITICAL_SECTION criticalSection = {0};
        KeEnterCriticalSection(&criticalSection);

        if (!LinkedListIsEmpty(&gTerminatedList))
        {
            LINKED_LIST_TAG *entry = gTerminatedList.Flink;
            LinkedListRemove(entry);
            thread = CONTAINING_RECORD(entry, KTHREAD, ReadyLink);
            KiMarkThreadTerminationConsumed(thread);
        }

        KeLeaveCriticalSection(&criticalSection);
        if (!thread)
            return;

        if ((thread->Flags & KTHREAD_FLAG_BOOTSTRAP_USER) != 0)
        {
            klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_IDLE_REAPER " thread=%u\n", thread->ThreadId);
        }

        KiFinalizeThread(thread);
    }
}

// ─────────────────────────────────────────────────────────────
// IdleThread body — called from kmain after scheduler init
// ─────────────────────────────────────────────────────────────

void
KeIdleLoop(void)
{
    while (TRUE)
    {
        KiReapTerminatedThreads();
        __asm__ __volatile__("sti; hlt" ::: "memory");
    }
}
