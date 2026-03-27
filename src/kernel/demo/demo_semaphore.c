/**
 * HimuOperatingSystem
 *
 * File: demo_semaphore.c
 * Description: KSEMAPHORE demo — counting semaphore acquire/release threads.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"
#include <kernel/hodbg.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/semaphore.h>

static KSEMAPHORE gTestSemaphore;

static void SemaphoreImmediateThread(void *arg);
static void SemaphorePollThread(void *arg);
static void SemaphoreWaiterOneThread(void *arg);
static void SemaphoreWaiterTwoThread(void *arg);
static void SemaphoreTimeoutThread(void *arg);
static void SemaphoreDelayedWaiterThread(void *arg);
static void SemaphoreReleaserThread(void *arg);

void
RunSemaphoreDemo(void)
{
    HO_STATUS status;
    status = KeInitializeSemaphore(&gTestSemaphore, 1, 4);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize test semaphore");

    KTHREAD *semImmediate = NULL;
    KTHREAD *semPoll = NULL;
    KTHREAD *semWaiterOne = NULL;
    KTHREAD *semWaiterTwo = NULL;
    KTHREAD *semTimeout = NULL;
    KTHREAD *semDelayedWaiter = NULL;
    KTHREAD *semReleaser = NULL;

    status = KeThreadCreate(&semImmediate, SemaphoreImmediateThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore immediate thread");

    status = KeThreadCreate(&semPoll, SemaphorePollThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore poll thread");

    status = KeThreadCreate(&semWaiterOne, SemaphoreWaiterOneThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore waiter one thread");

    status = KeThreadCreate(&semWaiterTwo, SemaphoreWaiterTwoThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore waiter two thread");

    status = KeThreadCreate(&semTimeout, SemaphoreTimeoutThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore timeout thread");

    status = KeThreadCreate(&semDelayedWaiter, SemaphoreDelayedWaiterThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore delayed waiter thread");

    status = KeThreadCreate(&semReleaser, SemaphoreReleaserThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create semaphore releaser thread");

    status = KeThreadStart(semImmediate);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore immediate thread");

    status = KeThreadStart(semPoll);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore poll thread");

    status = KeThreadStart(semWaiterOne);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore waiter one thread");

    status = KeThreadStart(semWaiterTwo);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore waiter two thread");

    status = KeThreadStart(semTimeout);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore timeout thread");

    status = KeThreadStart(semDelayedWaiter);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore delayed waiter thread");

    status = KeThreadStart(semReleaser);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start semaphore releaser thread");
}

static void
SemaphoreImmediateThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    klog(KLOG_LEVEL_INFO, "[SEMI-%u] immediate acquire (expect SUCCESS)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestSemaphore, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[SEMI-%u] acquire result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");
}

static void
SemaphorePollThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(10000000ULL); // 10 ms — allow the initial permit to be consumed first

    klog(KLOG_LEVEL_INFO, "[SEMPOLL-%u] zero-timeout poll (expect TIMEOUT)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestSemaphore, 0);
    klog(KLOG_LEVEL_INFO, "[SEMPOLL-%u] poll result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");
}

static void
SemaphoreWaiterOneThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(10000000ULL); // 10 ms
    klog(KLOG_LEVEL_INFO, "[SEMWAIT1-%u] blocking wait (expect release #1)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestSemaphore, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[SEMWAIT1-%u] wait result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");
}

static void
SemaphoreWaiterTwoThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(10000000ULL); // 10 ms
    klog(KLOG_LEVEL_INFO, "[SEMWAIT2-%u] blocking wait (expect release #2)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestSemaphore, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[SEMWAIT2-%u] wait result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");
}

static void
SemaphoreTimeoutThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(10000000ULL); // 10 ms
    klog(KLOG_LEVEL_INFO, "[SEMTIME-%u] finite wait 30ms (expect TIMEOUT)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestSemaphore, 30000000ULL);
    klog(KLOG_LEVEL_INFO, "[SEMTIME-%u] wait result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");
}

static void
SemaphoreDelayedWaiterThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(350000000ULL); // 350 ms — join after release #1, before release #2

    klog(KLOG_LEVEL_INFO, "[SEMWAIT3-%u] delayed blocking wait (expect release #2)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestSemaphore, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[SEMWAIT3-%u] wait result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");
}

static void
SemaphoreReleaserThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    KeSleep(200000000ULL); // 200 ms
    klog(KLOG_LEVEL_INFO, "[SEMREL-%u] >>> KeReleaseSemaphore(1)\n", id);
    HO_STATUS status = KeReleaseSemaphore(&gTestSemaphore, 1);
    klog(KLOG_LEVEL_INFO, "[SEMREL-%u] release #1 status: 0x%x\n", id, status);

    KeSleep(200000000ULL); // 200 ms
    klog(KLOG_LEVEL_INFO, "[SEMREL-%u] >>> KeReleaseSemaphore(3)\n", id);
    status = KeReleaseSemaphore(&gTestSemaphore, 3);
    klog(KLOG_LEVEL_INFO, "[SEMREL-%u] release #2 status: 0x%x\n", id, status);
}
