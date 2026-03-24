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
static LINKED_LIST_TAG gSleepQueue;
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
static void KiWakeSleepers(uint64_t nowNs);
static void KiArmClockEvent(uint64_t deltaNs);
static void KiArmForNextEvent(uint64_t nowNs, KTHREAD *next);
static void KiReapTerminatedThreads(void);
static uint32_t KiCountQueueDepth(LINKED_LIST_TAG *head);

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
    LinkedListInit(&gSleepQueue);
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
    gIdleThread->WakeDeadline = 0;
    LinkedListInit(&gIdleThread->ReadyLink);
    LinkedListInit(&gIdleThread->SleepLink);
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
    gCurrentThread->WakeDeadline = nowNs + durationNs;
    gCurrentThread->State = KTHREAD_STATE_BLOCKED;

    klog(KLOG_LEVEL_DEBUG, "[SCHED] Thread %u sleep %lu ns\n", gCurrentThread->ThreadId, (unsigned long)durationNs);

    // Insert into sleep queue sorted by WakeDeadline (ascending)
    LINKED_LIST_TAG *pos;
    for (pos = gSleepQueue.Flink; pos != &gSleepQueue; pos = pos->Flink)
    {
        KTHREAD *t = CONTAINING_RECORD(pos, KTHREAD, SleepLink);
        if (t->WakeDeadline > gCurrentThread->WakeDeadline)
            break;
    }
    // Insert before pos (i.e., at the tail of all entries with earlier deadlines)
    gCurrentThread->SleepLink.Flink = pos;
    gCurrentThread->SleepLink.Blink = pos->Blink;
    pos->Blink->Flink = &gCurrentThread->SleepLink;
    pos->Blink = &gCurrentThread->SleepLink;

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

    // Wake sleeping threads whose deadlines have passed
    KiWakeSleepers(nowNs);

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

// ─────────────────────────────────────────────────────────────
// Internal: wake sleeping threads
// ─────────────────────────────────────────────────────────────

static void
KiWakeSleepers(uint64_t nowNs)
{
    while (!LinkedListIsEmpty(&gSleepQueue))
    {
        KTHREAD *sleeper = CONTAINING_RECORD(gSleepQueue.Flink, KTHREAD, SleepLink);
        if (sleeper->WakeDeadline > nowNs)
            break;

        LinkedListRemove(&sleeper->SleepLink);
        sleeper->WakeDeadline = 0;
        sleeper->State = KTHREAD_STATE_READY;
        LinkedListInsertTail(&gReadyQueue, &sleeper->ReadyLink);
        gStats.SleepWakeCount++;

        klog(KLOG_LEVEL_DEBUG, "[SCHED] Thread %u woke up\n", sleeper->ThreadId);
    }
}

// ─────────────────────────────────────────────────────────────
// Internal: arm clock event with clamping
// ─────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────
// Internal: compute and arm next event for a thread
// ─────────────────────────────────────────────────────────────

static void
KiArmForNextEvent(uint64_t nowNs, KTHREAD *next)
{
    if (next == gIdleThread)
    {
        // IdleThread: arm for earliest sleep deadline only
        if (!LinkedListIsEmpty(&gSleepQueue))
        {
            KTHREAD *sleeper = CONTAINING_RECORD(gSleepQueue.Flink, KTHREAD, SleepLink);
            uint64_t delta = sleeper->WakeDeadline > nowNs ? sleeper->WakeDeadline - nowNs : 1;
            gNextProgrammedDeadlineNs = sleeper->WakeDeadline;
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

        if (!LinkedListIsEmpty(&gSleepQueue))
        {
            KTHREAD *sleeper = CONTAINING_RECORD(gSleepQueue.Flink, KTHREAD, SleepLink);
            if (sleeper->WakeDeadline < targetDeadline)
                targetDeadline = sleeper->WakeDeadline;
        }

        uint64_t delta = targetDeadline > nowNs ? targetDeadline - nowNs : 1;
        gNextProgrammedDeadlineNs = targetDeadline;
        KiArmClockEvent(delta);
    }
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
    out->SleepQueueDepth = KiCountQueueDepth(&gSleepQueue);

    if (!LinkedListIsEmpty(&gSleepQueue))
    {
        KTHREAD *sleeper = CONTAINING_RECORD(gSleepQueue.Flink, KTHREAD, SleepLink);
        out->EarliestWakeDeadline = sleeper->WakeDeadline;
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
