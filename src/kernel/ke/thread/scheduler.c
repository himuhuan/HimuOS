/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler.c
 * Description:
 * Ke Layer - Minimal Round-Robin tickless scheduler.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/event.h>
#include <kernel/ke/semaphore.h>
#include <kernel/ke/clock_event.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/mm.h>
#include <kernel/hodefs.h>
#include <kernel/hodbg.h>
#include <kernel/init.h>
#include <arch/amd64/idt.h>
#include <boot/boot_capsule.h>
#include <libc/string.h>

// ─────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────

static LINKED_LIST_TAG gReadyQueue;
static LINKED_LIST_TAG gTimeoutQueue;
static LINKED_LIST_TAG gTerminatedList;

static KTHREAD *gCurrentThread;
static KTHREAD *gIdleThread;
static BOOL gSchedulerEnabled;

static KE_SCHEDULER_STATS gStats;

static uint64_t gQuantumDeadlineNs;
static uint64_t gNextProgrammedDeadlineNs;

// ─────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────

static void KiSchedule(void);
static void KiSchedulerTimerISR(void *frame, void *context);
static void KiWakeTimeouts(uint64_t nowNs);
static void KiArmClockEvent(uint64_t deltaNs);
static void KiArmForNextEvent(uint64_t nowNs, KTHREAD *next);
static void KiReapTerminatedThreads(void);
static uint32_t KiCountQueueDepth(LINKED_LIST_TAG *head);
static void KiCompleteWait(KWAIT_BLOCK *block, HO_STATUS status);
static void KiInsertTimeoutQueue(KWAIT_BLOCK *block);
static void KiInitWaitBlock(KWAIT_BLOCK *block);
static HO_STATUS KiValidateDispatcherHeader(const KDISPATCHER_HEADER *header);
static void KiAssertSemaphoreState(const KSEMAPHORE *semaphore);
static BOOL KiTryAcquireDispatcherObject(KDISPATCHER_HEADER *header);

void KiThreadTrampoline(void);

static inline uint64_t
KiNowNs(void)
{
    return KeGetSystemUpRealTime() * 1000ULL;
}

// ─────────────────────────────────────────────────────────────
// Thread trampoline — first entry point for new threads
// ─────────────────────────────────────────────────────────────

void
KiThreadTrampoline(void)
{
    ARCH_INTERRUPT_STATE enabledState = {.MaskableInterruptEnabled = TRUE};
    ArchRestoreInterruptState(enabledState);

    KTHREAD *self = KeGetCurrentThread();
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
    gIdleThread->StackPhys = 0; // boot stack, not our allocation
    gIdleThread->Priority = 0;
    gIdleThread->Quantum = 0;
    KiInitWaitBlock(&gIdleThread->WaitBlock);
    LinkedListInit(&gIdleThread->ReadyLink);
    gIdleThread->EntryPoint = NULL;
    gIdleThread->EntryArg = NULL;
    gIdleThread->Flags = KTHREAD_FLAG_IDLE;

    gCurrentThread = gIdleThread;
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
    ARCH_INTERRUPT_STATE savedInterruptState = ArchDisableInterrupts();
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    gStats.YieldCount++;

    if (LinkedListIsEmpty(&gReadyQueue))
    {
        KeLeaveCriticalSection(&criticalSection);
        ArchRestoreInterruptState(savedInterruptState);
        return;
    }

    gCurrentThread->State = KTHREAD_STATE_READY;
    LinkedListInsertTail(&gReadyQueue, &gCurrentThread->ReadyLink);

    KeLeaveCriticalSection(&criticalSection);
    KiSchedule();
    ArchRestoreInterruptState(savedInterruptState);
}

// ─────────────────────────────────────────────────────────────
// KeSleep
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API void
KeSleep(uint64_t durationNs)
{
    if (durationNs == 0)
    {
        KeYield();
        return;
    }

    HO_KASSERT(gCurrentThread != gIdleThread, EC_INVALID_STATE);

    ARCH_INTERRUPT_STATE savedInterruptState = ArchDisableInterrupts();
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
    ArchRestoreInterruptState(savedInterruptState);
}

// ─────────────────────────────────────────────────────────────
// KeThreadExit
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_NORETURN void
KeThreadExit(void)
{
    (void)ArchDisableInterrupts();
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    HO_KASSERT(gCurrentThread != gIdleThread, EC_INVALID_STATE);

    gCurrentThread->State = KTHREAD_STATE_TERMINATED;
    gStats.ActiveThreadCount--;

    klog(KLOG_LEVEL_INFO, "[SCHED] Thread %u terminated\n", gCurrentThread->ThreadId);

    // Park on terminated list for IdleThread reaper (reuse ReadyLink)
    LinkedListInsertTail(&gTerminatedList, &gCurrentThread->ReadyLink);

    KeLeaveCriticalSection(&criticalSection);
    KiSchedule();
    __builtin_unreachable();
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

static void
KiSchedule(void)
{
    // Must be called with IF=0

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
        // Same thread, just re-arm and continue
        uint64_t nowNs = KiNowNs();
        KiArmForNextEvent(nowNs, next);
        return;
    }

    next->State = KTHREAD_STATE_RUNNING;
    gCurrentThread = next;
    gStats.ContextSwitchCount++;

    // Update TSS.RSP0 to target thread's kernel stack top
    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    capsule->CpuInfo.Tss.RSP0 = next->StackBase + next->StackSize;

    klog(KLOG_LEVEL_DEBUG, "[SCHED] Switch %u -> %u\n", prev->ThreadId, next->ThreadId);

    // Arm clock event for next deadline
    uint64_t nowNs = KiNowNs();
    KiArmForNextEvent(nowNs, next);

    // Context switch — does not return until this thread is resumed
    KiSwitchContext(&prev->Context, &next->Context);
}

// ─────────────────────────────────────────────────────────────
// Timer ISR — scheduler entry from interrupt
// ─────────────────────────────────────────────────────────────

static void
KiSchedulerTimerISR(void *frame, void *context)
{
    (void)frame;
    (void)context;

    KeClockEventOnInterrupt(); // EOI + InterruptCount

    if (!gSchedulerEnabled)
        return;

    uint64_t nowNs = KiNowNs();

    // Segmented re-arm: if we haven't reached the real deadline, re-arm remainder
    if (gNextProgrammedDeadlineNs != 0 && nowNs < gNextProgrammedDeadlineNs)
    {
        KiArmClockEvent(gNextProgrammedDeadlineNs - nowNs);
        return;
    }

    // Wake timed-out wait blocks whose deadlines have passed
    KiWakeTimeouts(nowNs);

    BOOL needReschedule = FALSE;

    // If IdleThread is running and ready queue has threads, switch
    if (gCurrentThread == gIdleThread && !LinkedListIsEmpty(&gReadyQueue))
    {
        needReschedule = TRUE;
    }
    // If current thread's quantum expired, preempt
    else if (gCurrentThread != gIdleThread && nowNs >= gQuantumDeadlineNs)
    {
        gCurrentThread->State = KTHREAD_STATE_READY;
        LinkedListInsertTail(&gReadyQueue, &gCurrentThread->ReadyLink);
        gStats.PreemptionCount++;
        needReschedule = TRUE;
    }

    if (needReschedule)
    {
        KiSchedule();
    }
    else
    {
        // Re-arm for next event
        KiArmForNextEvent(nowNs, gCurrentThread);
    }
}

// Internal: initialize a wait block to clean state
static void
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
static HO_STATUS
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
        return EC_SUCCESS;
    default:
        return EC_NOT_SUPPORTED;
    }
}

// Internal: validate runtime semaphore invariants
static void
KiAssertSemaphoreState(const KSEMAPHORE *semaphore)
{
    HO_KASSERT(semaphore != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(semaphore->Header.Signature == KDISPATCHER_SIGNATURE, EC_INVALID_STATE);
    HO_KASSERT(semaphore->Header.Type == DISPATCHER_TYPE_SEMAPHORE, EC_NOT_SUPPORTED);
    HO_KASSERT(semaphore->Limit > 0, EC_INVALID_STATE);
    HO_KASSERT(semaphore->Header.SignalState >= 0, EC_INVALID_STATE);
    HO_KASSERT(semaphore->Header.SignalState <= semaphore->Limit, EC_INVALID_STATE);
}

// Internal: test whether a dispatcher object can satisfy immediately.
// For counted objects, this helper also consumes the permit atomically.
static BOOL
KiTryAcquireDispatcherObject(KDISPATCHER_HEADER *header)
{
    switch (header->Type)
    {
    case DISPATCHER_TYPE_EVENT:
        return header->SignalState != 0;

    case DISPATCHER_TYPE_SEMAPHORE: {
        KSEMAPHORE *semaphore = (KSEMAPHORE *)header;
        KiAssertSemaphoreState(semaphore);

        if (header->SignalState == 0)
            return FALSE;

        header->SignalState--;
        HO_KASSERT(header->SignalState >= 0, EC_INVALID_STATE);
        return TRUE;
    }

    default:
        return FALSE;
    }
}

// Internal: insert wait block into timeout queue (sorted)
static void
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

// Internal: unified wait completion — signal or timeout
static void
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

// Internal: process timed-out wait blocks
static void
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

// Internal: arm clock event with clamping
static void
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

// Internal: compute and arm next event for a thread
static void
KiArmForNextEvent(uint64_t nowNs, KTHREAD *next)
{
    if (next == gIdleThread)
    {
        // IdleThread: arm for earliest timeout deadline only
        if (!LinkedListIsEmpty(&gTimeoutQueue))
        {
            KWAIT_BLOCK *block = CONTAINING_RECORD(gTimeoutQueue.Flink, KWAIT_BLOCK, TimeoutLink);
            uint64_t delta = block->DeadlineNs > nowNs ? block->DeadlineNs - nowNs : 1;
            gNextProgrammedDeadlineNs = block->DeadlineNs;
            KiArmClockEvent(delta);
        }
        else
        {
            // Truly idle — don't arm, CPU halts until external interrupt
            gNextProgrammedDeadlineNs = 0;
        }
    }
    else
    {
        // Set quantum deadline for the scheduled thread
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

// KeWaitForSingleObject
HO_KERNEL_API HO_STATUS
KeWaitForSingleObject(void *object, uint64_t timeoutNs)
{
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
    if (KiTryAcquireDispatcherObject(header))
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

    ARCH_INTERRUPT_STATE savedInterruptState = ArchDisableInterrupts();
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
    BOOL needSchedule = (gCurrentThread == gIdleThread && !LinkedListIsEmpty(&gReadyQueue));

    klog(KLOG_LEVEL_DEBUG, "[EVENT] Set (released=%u)\n", releasedCount);

    KeLeaveCriticalSection(&criticalSection);

    if (needSchedule)
    {
        klog(KLOG_LEVEL_DEBUG, "[EVENT] Signal during idle, triggering reschedule\n");
        KiSchedule();
    }

    ArchRestoreInterruptState(savedInterruptState);
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
// KeReleaseSemaphore
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeReleaseSemaphore(KSEMAPHORE *semaphore, int32_t releaseCount)
{
    if (semaphore == NULL || releaseCount <= 0)
        return EC_ILLEGAL_ARGUMENT;

    ARCH_INTERRUPT_STATE savedInterruptState = ArchDisableInterrupts();
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
        ArchRestoreInterruptState(savedInterruptState);
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

    BOOL needSchedule = (gCurrentThread == gIdleThread && !LinkedListIsEmpty(&gReadyQueue));

    klog(KLOG_LEVEL_DEBUG, "[SEMAPHORE] Release(count=%ld, woke=%u, available=%ld)\n", (long)releaseCount,
         releasedWaiters, (long)semaphore->Header.SignalState);

    KeLeaveCriticalSection(&criticalSection);

    if (needSchedule)
    {
        klog(KLOG_LEVEL_DEBUG, "[SEMAPHORE] Release during idle, triggering reschedule\n");
        KiSchedule();
    }

    ArchRestoreInterruptState(savedInterruptState);
    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// IdleThread reaper — reclaims terminated thread resources
// ─────────────────────────────────────────────────────────────

static void
KiReapTerminatedThreads(void)
{
    while (TRUE)
    {
        LINKED_LIST_TAG *entry = NULL;
        KE_CRITICAL_SECTION criticalSection = {0};
        KeEnterCriticalSection(&criticalSection);

        if (!LinkedListIsEmpty(&gTerminatedList))
        {
            entry = gTerminatedList.Flink;
            LinkedListRemove(entry);
        }

        KeLeaveCriticalSection(&criticalSection);
        if (!entry)
            return;

        KTHREAD *thread = CONTAINING_RECORD(entry, KTHREAD, ReadyLink);

        // Free stack pages
        if (thread->StackPhys != 0)
        {
            KePmmFreePages(thread->StackPhys, KE_THREAD_STACK_PAGES);
        }

        // Return KTHREAD to pool
        KePoolFree(&gKThreadPool, thread);
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

// ─────────────────────────────────────────────────────────────
// Queue depth counter
// ─────────────────────────────────────────────────────────────

static uint32_t
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

    memset(out, 0, sizeof(*out));

    out->SchedulerEnabled = gSchedulerEnabled;

    if (!gSchedulerEnabled)
        return EC_SUCCESS;

    out->CurrentThreadId = gCurrentThread ? gCurrentThread->ThreadId : 0;
    out->IdleThreadId = gIdleThread ? gIdleThread->ThreadId : 0;
    out->ReadyQueueDepth = KiCountQueueDepth(&gReadyQueue);
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

    return EC_SUCCESS;
}
