/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler/scheduler_internal.h
 * Description: Private declarations shared by scheduler module files.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/event.h>
#include <kernel/ke/mutex.h>
#include <kernel/ke/semaphore.h>
#include <kernel/ke/clock_event.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/irql.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/mm.h>
#include <kernel/hodefs.h>
#include <kernel/hodbg.h>
#include <kernel/init.h>
#include <arch/amd64/idt.h>
#include <boot/boot_capsule.h>
#include <libc/string.h>

extern LINKED_LIST_TAG gReadyQueues[KTHREAD_PRIORITY_COUNT];
extern LINKED_LIST_TAG gTimeoutQueue;
extern LINKED_LIST_TAG gTerminatedList;

extern KTHREAD *gCurrentThread;
extern KTHREAD *gIdleThread;
extern BOOL gSchedulerEnabled;

extern KE_SCHEDULER_STATS gStats;

extern uint64_t gQuantumDeadlineNs;
extern uint64_t gNextProgrammedDeadlineNs;

static inline BOOL
KiIsValidThreadPriority(uint8_t priority)
{
    return priority < (uint8_t)KTHREAD_PRIORITY_COUNT;
}

static inline LINKED_LIST_TAG *
KiGetReadyQueueForPriority(uint8_t priority)
{
    HO_KASSERT(KiIsValidThreadPriority(priority), EC_ILLEGAL_ARGUMENT);
    return &gReadyQueues[priority];
}

static inline LINKED_LIST_TAG *
KiGetReadyQueueForThread(const KTHREAD *thread)
{
    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    return KiGetReadyQueueForPriority(thread->Priority);
}

static inline LINKED_LIST_TAG *
KiGetDefaultReadyQueue(void)
{
    return KiGetReadyQueueForPriority(KTHREAD_DEFAULT_PRIORITY);
}

static inline void
KiInitReadyQueues(void)
{
    uint32_t priority;

    for (priority = 0; priority < (uint32_t)KTHREAD_PRIORITY_COUNT; priority++)
    {
        LinkedListInit(&gReadyQueues[priority]);
    }
}

static inline BOOL
KiHasAnyReadyThread(void)
{
    uint32_t priority;

    for (priority = 0; priority < (uint32_t)KTHREAD_PRIORITY_COUNT; priority++)
    {
        if (!LinkedListIsEmpty(&gReadyQueues[priority]))
            return TRUE;
    }

    return FALSE;
}

static inline LINKED_LIST_TAG *
KiGetHighestPriorityReadyQueue(void)
{
    int priority;

    for (priority = (int)KTHREAD_PRIORITY_HIGH; priority >= (int)KTHREAD_PRIORITY_LOW; priority--)
    {
        if (!LinkedListIsEmpty(&gReadyQueues[priority]))
            return &gReadyQueues[priority];
    }

    return NULL;
}

static inline uint32_t
KiCountAllReadyThreads(void)
{
    uint32_t count = 0;
    uint32_t priority;

    for (priority = 0; priority < (uint32_t)KTHREAD_PRIORITY_COUNT; priority++)
    {
        for (LINKED_LIST_TAG *entry = gReadyQueues[priority].Flink; entry != &gReadyQueues[priority]; entry = entry->Flink)
            count++;
    }

    return count;
}

void KiSchedule(void);
void KiSchedulerTimerISR(void *frame, void *context);
void KiWakeTimeouts(uint64_t nowNs);
void KiArmClockEvent(uint64_t deltaNs);
void KiArmForNextEvent(uint64_t nowNs, KTHREAD *next);
void KiFinalizeThread(KTHREAD *thread);
void KiReapTerminatedThreads(void);
uint32_t KiCountQueueDepth(LINKED_LIST_TAG *head);
void KiCompleteWait(KWAIT_BLOCK *block, HO_STATUS status);
void KiInsertTimeoutQueue(KWAIT_BLOCK *block);
void KiInitWaitBlock(KWAIT_BLOCK *block);
void KiAssertBlockingAllowed(void);
void KiAssertDispatchLevel(void);
HO_STATUS KiValidateDispatcherHeader(const KDISPATCHER_HEADER *header);
void KiAssertMutexState(const KMUTEX *mutex);
void KiAssertSemaphoreState(const KSEMAPHORE *semaphore);
void KiIncrementOwnedMutexCount(KTHREAD *thread);
void KiDecrementOwnedMutexCount(KTHREAD *thread);
void KiAcquireMutexOwnership(KMUTEX *mutex, KTHREAD *thread);
void KiReleaseMutexOwnership(KMUTEX *mutex);
void KiHandOffMutexOwnership(KMUTEX *mutex, KTHREAD *thread);
HO_STATUS KiTryAcquireDispatcherObject(KDISPATCHER_HEADER *header, KTHREAD *thread, BOOL *acquired);
void KiThreadTrampoline(void);
uint64_t KiNowNs(void);
