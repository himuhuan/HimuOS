/**
 * HimuOperatingSystem
 *
 * File: ke/thread/scheduler_internal.h
 * Description: Shared state and internal declarations for the scheduler subsystem.
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
#include <kernel/ke/time_source.h>
#include <kernel/ke/mm.h>
#include <kernel/hodefs.h>
#include <kernel/hodbg.h>
#include <kernel/init.h>
#include <arch/amd64/idt.h>
#include <boot/boot_capsule.h>
#include <libc/string.h>

// ─────────────────────────────────────────────────────────────
// Shared scheduler globals (defined in scheduler.c)
// ─────────────────────────────────────────────────────────────

extern LINKED_LIST_TAG gReadyQueue;
extern LINKED_LIST_TAG gTimeoutQueue;
extern LINKED_LIST_TAG gTerminatedList;

extern KTHREAD *gCurrentThread;
extern KTHREAD *gIdleThread;
extern BOOL gSchedulerEnabled;

extern KE_SCHEDULER_STATS gStats;

extern uint64_t gQuantumDeadlineNs;
extern uint64_t gNextProgrammedDeadlineNs;

// ─────────────────────────────────────────────────────────────
// Shared inline helpers
// ─────────────────────────────────────────────────────────────

static inline uint64_t
KiNowNs(void)
{
    return KeGetSystemUpRealTime() * 1000ULL;
}

// ─────────────────────────────────────────────────────────────
// Functions from scheduler.c
// ─────────────────────────────────────────────────────────────

void KiSchedule(void);
void KiAssertDispatchContextAllowed(void);

// ─────────────────────────────────────────────────────────────
// Functions from scheduler_wait.c
// ─────────────────────────────────────────────────────────────

void KiInitWaitBlock(KWAIT_BLOCK *block);
void KiInsertTimeoutQueue(KWAIT_BLOCK *block);
void KiCompleteWait(KWAIT_BLOCK *block, HO_STATUS status);
void KiWakeTimeouts(uint64_t nowNs);
void KiArmClockEvent(uint64_t deltaNs);
void KiArmForNextEvent(uint64_t nowNs, KTHREAD *next);

// ─────────────────────────────────────────────────────────────
// Functions from scheduler_sync.c
// ─────────────────────────────────────────────────────────────

HO_STATUS KiValidateDispatcherHeader(const KDISPATCHER_HEADER *header);
HO_STATUS KiTryAcquireDispatcherObject(KDISPATCHER_HEADER *header, KTHREAD *thread, BOOL *acquired);
uint32_t KiCountQueueDepth(LINKED_LIST_TAG *head);
