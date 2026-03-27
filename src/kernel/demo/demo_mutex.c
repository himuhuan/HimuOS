/**
 * HimuOperatingSystem
 *
 * File: demo_mutex.c
 * Description: KMUTEX demo + safety regression demos (guard-wait, owned-exit).
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"
#include <kernel/hodbg.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/event.h>
#include <kernel/ke/mutex.h>

static KMUTEX gTestMutex;
static KEVENT gCriticalSectionGuardEvent;
static KMUTEX gOwnedExitMutex;

static void MutexOwnerThread(void *arg);
static void MutexPollThread(void *arg);
static void MutexWaiterOneThread(void *arg);
static void MutexWaiterTwoThread(void *arg);
static void MutexNonOwnerReleaseThread(void *arg);
static void CriticalSectionWaitViolationThread(void *arg);
static void OwnedMutexExitViolationThread(void *arg);

void
RunMutexDemo(void)
{
    HO_STATUS status;
    KTHREAD *mutexOwner = NULL;
    KTHREAD *mutexPoll = NULL;
    KTHREAD *mutexWaiterOne = NULL;
    KTHREAD *mutexWaiterTwo = NULL;
    KTHREAD *mutexNonOwner = NULL;

    KeInitializeMutex(&gTestMutex);

    status = KeThreadCreate(&mutexOwner, MutexOwnerThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create mutex owner thread");

    status = KeThreadCreate(&mutexPoll, MutexPollThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create mutex poll thread");

    status = KeThreadCreate(&mutexWaiterOne, MutexWaiterOneThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create mutex waiter one thread");

    status = KeThreadCreate(&mutexWaiterTwo, MutexWaiterTwoThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create mutex waiter two thread");

    status = KeThreadCreate(&mutexNonOwner, MutexNonOwnerReleaseThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create mutex non-owner thread");

    status = KeThreadStart(mutexOwner);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start mutex owner thread");

    status = KeThreadStart(mutexPoll);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start mutex poll thread");

    status = KeThreadStart(mutexWaiterOne);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start mutex waiter one thread");

    status = KeThreadStart(mutexWaiterTwo);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start mutex waiter two thread");

    status = KeThreadStart(mutexNonOwner);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start mutex non-owner thread");
}

void
RunGuardWaitDemo(void)
{
    HO_STATUS status;
    KTHREAD *violator = NULL;

    KeInitializeEvent(&gCriticalSectionGuardEvent, TRUE);

    status = KeThreadCreate(&violator, CriticalSectionWaitViolationThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create guard-wait violation thread");

    status = KeThreadStart(violator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start guard-wait violation thread");
}

void
RunOwnedExitDemo(void)
{
    HO_STATUS status;
    KTHREAD *violator = NULL;

    KeInitializeMutex(&gOwnedExitMutex);

    status = KeThreadCreate(&violator, OwnedMutexExitViolationThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create owned-exit violation thread");

    status = KeThreadStart(violator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start owned-exit violation thread");
}

// ─────────────────────────────────────────────────────────────
// KMUTEX demo threads
// ─────────────────────────────────────────────────────────────

static void
MutexOwnerThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    klog(KLOG_LEVEL_INFO, "[MUTOWNER-%u] immediate acquire (expect SUCCESS)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestMutex, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[MUTOWNER-%u] acquire result: 0x%x\n", id, result);

    klog(KLOG_LEVEL_INFO, "[MUTOWNER-%u] self-acquire (expect EC_INVALID_STATE)\n", id);
    result = KeWaitForSingleObject(&gTestMutex, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[MUTOWNER-%u] self-acquire result: 0x%x\n", id, result);

    KeSleep(150000000ULL); // 150 ms — allow poller/non-owner/waiters to line up

    klog(KLOG_LEVEL_INFO, "[MUTOWNER-%u] >>> KeReleaseMutex\n", id);
    result = KeReleaseMutex(&gTestMutex);
    klog(KLOG_LEVEL_INFO, "[MUTOWNER-%u] release status: 0x%x\n", id, result);
}

static void
MutexPollThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(10000000ULL); // 10 ms — owner should already hold the mutex

    klog(KLOG_LEVEL_INFO, "[MUTPOLL-%u] zero-timeout poll (expect TIMEOUT)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestMutex, 0);
    klog(KLOG_LEVEL_INFO, "[MUTPOLL-%u] poll result: 0x%x\n", id, result);
}

static void
MutexWaiterOneThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(20000000ULL); // 20 ms
    klog(KLOG_LEVEL_INFO, "[MUTWAIT1-%u] blocking wait (expect handoff #1)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestMutex, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[MUTWAIT1-%u] wait result: 0x%x\n", id, result);

    KeSleep(80000000ULL); // 80 ms — keep waiter #2 queued
    klog(KLOG_LEVEL_INFO, "[MUTWAIT1-%u] >>> KeReleaseMutex (expect handoff #2)\n", id);
    result = KeReleaseMutex(&gTestMutex);
    klog(KLOG_LEVEL_INFO, "[MUTWAIT1-%u] release status: 0x%x\n", id, result);
}

static void
MutexWaiterTwoThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(90000000ULL); // 90 ms — queue behind waiter #1 but ahead of owner release
    klog(KLOG_LEVEL_INFO, "[MUTWAIT2-%u] blocking wait (expect handoff #2)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestMutex, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[MUTWAIT2-%u] wait result: 0x%x\n", id, result);

    klog(KLOG_LEVEL_INFO, "[MUTWAIT2-%u] >>> KeReleaseMutex (expect final SUCCESS)\n", id);
    result = KeReleaseMutex(&gTestMutex);
    klog(KLOG_LEVEL_INFO, "[MUTWAIT2-%u] release status: 0x%x\n", id, result);
}

static void
MutexNonOwnerReleaseThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(40000000ULL); // 40 ms — owner should still hold the mutex

    klog(KLOG_LEVEL_INFO, "[MUTFAIL-%u] non-owner release (expect EC_INVALID_STATE)\n", id);
    HO_STATUS result = KeReleaseMutex(&gTestMutex);
    klog(KLOG_LEVEL_INFO, "[MUTFAIL-%u] release result: 0x%x\n", id, result);
}

// ─────────────────────────────────────────────────────────────
// Safety regression demo threads
// ─────────────────────────────────────────────────────────────

static void
CriticalSectionWaitViolationThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;
    KE_CRITICAL_SECTION criticalSection = {0};

    klog(KLOG_LEVEL_INFO, "[GUARDWAIT-%u] entering critical section before wait (expect panic)\n", id);
    KeEnterCriticalSection(&criticalSection);

    (void)KeWaitForSingleObject(&gCriticalSectionGuardEvent, 0);
    HO_KPANIC(EC_INVALID_STATE, "Critical-section wait guard did not fire");
}

static void
OwnedMutexExitViolationThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    klog(KLOG_LEVEL_INFO, "[OWNEDEXIT-%u] acquire mutex before exit (expect SUCCESS)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gOwnedExitMutex, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[OWNEDEXIT-%u] acquire result: 0x%x\n", id, result);

    klog(KLOG_LEVEL_INFO, "[OWNEDEXIT-%u] calling KeThreadExit while owning mutex (expect panic)\n", id);
    KeThreadExit();
}
