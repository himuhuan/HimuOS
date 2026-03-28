/**
 * HimuOperatingSystem
 *
 * File: demo/mutex.c
 * Description: Mutex demo routines and worker threads.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

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

void
MutexPollThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(10000000ULL); // 10 ms — owner should already hold the mutex

    klog(KLOG_LEVEL_INFO, "[MUTPOLL-%u] zero-timeout poll (expect TIMEOUT)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestMutex, 0);
    klog(KLOG_LEVEL_INFO, "[MUTPOLL-%u] poll result: 0x%x\n", id, result);
}

void
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

void
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

void
MutexNonOwnerReleaseThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(40000000ULL); // 40 ms — owner should still hold the mutex

    klog(KLOG_LEVEL_INFO, "[MUTFAIL-%u] non-owner release (expect EC_INVALID_STATE)\n", id);
    HO_STATUS result = KeReleaseMutex(&gTestMutex);
    klog(KLOG_LEVEL_INFO, "[MUTFAIL-%u] release result: 0x%x\n", id, result);
}
