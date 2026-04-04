/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler/timer.c
 * Description: Timer, timeout, and deadline programming logic for the scheduler.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "scheduler_internal.h"

#include <kernel/ke/bootstrap_callbacks.h>

uint64_t
KiNowNs(void)
{
    return KeGetSystemUpRealTime() * 1000ULL;
}

// ─────────────────────────────────────────────────────────────
// Timer ISR — scheduler entry from interrupt
// ─────────────────────────────────────────────────────────────

void
KiSchedulerTimerISR(void *frame, void *context)
{
    (void)context;

    INTERRUPT_FRAME *interruptFrame = (INTERRUPT_FRAME *)frame;
    BOOL interruptedFromUserMode = interruptFrame != NULL && ((interruptFrame->CS & 0x3ULL) == 0x3ULL);

    KeClockEventOnInterrupt(); // EOI + InterruptCount

    if (!gSchedulerEnabled)
        return;

    KiAssertDispatchLevel();

    if (interruptedFromUserMode)
    {
        KE_BOOTSTRAP_TIMER_OBSERVE_FN timerObserveFn = KiGetBootstrapTimerObserveCallback();
        if (timerObserveFn != NULL)
        {
            KTHREAD *current = KeGetCurrentThread();
            if (current != NULL && current->UserBootstrapContext != NULL)
            {
                timerObserveFn(current);
            }
        }
    }

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

// Internal: insert wait block into timeout queue (sorted)
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

// Internal: process timed-out wait blocks
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

// Internal: arm clock event with clamping
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

// Internal: compute and arm next event for a thread
void
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
