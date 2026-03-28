/**
 * HimuOperatingSystem
 *
 * File: demo/guard.c
 * Description: Guard-violation demo routines and worker threads.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

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

void
RunDispatchGuardWaitDemo(void)
{
    HO_STATUS status;
    KTHREAD *violator = NULL;

    KeInitializeEvent(&gDispatchGuardEvent, TRUE);

    status = KeThreadCreate(&violator, DispatchGuardWaitViolationThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create dispatch-guard wait violation thread");

    status = KeThreadStart(violator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start dispatch-guard wait violation thread");
}

void
RunDispatchGuardSleepDemo(void)
{
    HO_STATUS status;
    KTHREAD *violator = NULL;

    status = KeThreadCreate(&violator, DispatchGuardSleepViolationThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create dispatch-guard sleep violation thread");

    status = KeThreadStart(violator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start dispatch-guard sleep violation thread");
}

void
RunDispatchGuardYieldDemo(void)
{
    HO_STATUS status;
    KTHREAD *violator = NULL;

    status = KeThreadCreate(&violator, DispatchGuardYieldViolationThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create dispatch-guard yield violation thread");

    status = KeThreadStart(violator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start dispatch-guard yield violation thread");
}

void
RunDispatchGuardExitDemo(void)
{
    HO_STATUS status;
    KTHREAD *violator = NULL;

    status = KeThreadCreate(&violator, DispatchGuardExitViolationThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create dispatch-guard exit violation thread");

    status = KeThreadStart(violator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start dispatch-guard exit violation thread");
}

void
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

void
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

void
DispatchGuardWaitViolationThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;
    KE_IRQL_GUARD irqlGuard = {0};

    klog(KLOG_LEVEL_INFO, "[IRQLWAIT-%u] entering DISPATCH_LEVEL before wait (expect panic)\n", id);
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);

    (void)KeWaitForSingleObject(&gDispatchGuardEvent, 0);
    HO_KPANIC(EC_INVALID_STATE, "Dispatch-guard wait legality check did not fire");
}

void
DispatchGuardSleepViolationThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;
    KE_IRQL_GUARD irqlGuard = {0};

    klog(KLOG_LEVEL_INFO, "[IRQLSLEEP-%u] entering DISPATCH_LEVEL before sleep (expect panic)\n", id);
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);

    KeSleep(1000000ULL);
    HO_KPANIC(EC_INVALID_STATE, "Dispatch-guard sleep legality check did not fire");
}

void
DispatchGuardYieldViolationThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;
    KE_IRQL_GUARD irqlGuard = {0};

    klog(KLOG_LEVEL_INFO, "[IRQLYIELD-%u] entering DISPATCH_LEVEL before yield (expect panic)\n", id);
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);

    KeYield();
    HO_KPANIC(EC_INVALID_STATE, "Dispatch-guard yield legality check did not fire");
}

void
DispatchGuardExitViolationThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;
    KE_IRQL_GUARD irqlGuard = {0};

    klog(KLOG_LEVEL_INFO, "[IRQLEXIT-%u] entering DISPATCH_LEVEL before exit (expect panic)\n", id);
    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);

    KeThreadExit();
    HO_KPANIC(EC_INVALID_STATE, "Dispatch-guard thread-exit legality check did not fire");
}
