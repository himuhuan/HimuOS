/**
 * HimuOperatingSystem
 *
 * File: demo.c
 * Description: Kernel demo routines for testing various components.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/demo.h>
#include <kernel/hodbg.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/irql.h>
#include <kernel/ke/event.h>
#include <kernel/ke/mutex.h>
#include <kernel/ke/semaphore.h>

#define HO_DEMO_TEST_NONE       0
#define HO_DEMO_TEST_SCHEDULE   1
#define HO_DEMO_TEST_THREAD     2
#define HO_DEMO_TEST_EVENT      3
#define HO_DEMO_TEST_SEMAPHORE  4
#define HO_DEMO_TEST_MUTEX      5
#define HO_DEMO_TEST_GUARD_WAIT 6
#define HO_DEMO_TEST_OWNED_EXIT 7
#define HO_DEMO_TEST_IRQL_WAIT  8
#define HO_DEMO_TEST_IRQL_SLEEP 9
#define HO_DEMO_TEST_IRQL_YIELD 10
#define HO_DEMO_TEST_IRQL_EXIT  11

#ifndef HO_DEMO_TEST_SELECTION
#define HO_DEMO_TEST_SELECTION HO_DEMO_TEST_NONE
#endif

#ifndef HO_DEMO_TEST_SELECTION_NAME
#define HO_DEMO_TEST_SELECTION_NAME "none"
#endif

static void TestThreadA(void *arg);
static void TestThreadB(void *arg);
static void EventProducerThread(void *arg);
static void EventConsumerThread(void *arg);
static void EventPollThread(void *arg);
static void SemaphoreImmediateThread(void *arg);
static void SemaphorePollThread(void *arg);
static void SemaphoreWaiterOneThread(void *arg);
static void SemaphoreWaiterTwoThread(void *arg);
static void SemaphoreTimeoutThread(void *arg);
static void SemaphoreDelayedWaiterThread(void *arg);
static void SemaphoreReleaserThread(void *arg);
static void MutexOwnerThread(void *arg);
static void MutexPollThread(void *arg);
static void MutexWaiterOneThread(void *arg);
static void MutexWaiterTwoThread(void *arg);
static void MutexNonOwnerReleaseThread(void *arg);
static void CriticalSectionWaitViolationThread(void *arg);
static void OwnedMutexExitViolationThread(void *arg);
static void DispatchGuardWaitViolationThread(void *arg);
static void DispatchGuardSleepViolationThread(void *arg);
static void DispatchGuardYieldViolationThread(void *arg);
static void DispatchGuardExitViolationThread(void *arg);

static KEVENT gTestEvent;
static KSEMAPHORE gTestSemaphore;
static KMUTEX gTestMutex;
static KEVENT gCriticalSectionGuardEvent;
static KEVENT gDispatchGuardEvent;
static KMUTEX gOwnedExitMutex;

static void RunIrqlSelfTest(void);
static void RunScheduleDemo(void);
static void RunThreadDemo(void);
static void RunEventDemo(void);
static void RunSemaphoreDemo(void);
static void RunMutexDemo(void);
static void RunGuardWaitDemo(void);
static void RunOwnedExitDemo(void);
static void RunDispatchGuardWaitDemo(void);
static void RunDispatchGuardSleepDemo(void);
static void RunDispatchGuardYieldDemo(void);
static void RunDispatchGuardExitDemo(void);

void
RunKernelDemos(void)
{
    klog(KLOG_LEVEL_INFO, "[DEMO] Selected profile: %s\n", HO_DEMO_TEST_SELECTION_NAME);

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_NONE)
    {
        return;
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_SCHEDULE)
    {
        RunScheduleDemo();
        return;
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_THREAD)
    {
        RunThreadDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_EVENT)
    {
        RunEventDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_SEMAPHORE)
    {
        RunSemaphoreDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_MUTEX)
    {
        RunMutexDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_GUARD_WAIT)
    {
        RunGuardWaitDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_OWNED_EXIT)
    {
        RunOwnedExitDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_IRQL_WAIT)
    {
        RunDispatchGuardWaitDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_IRQL_SLEEP)
    {
        RunDispatchGuardSleepDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_IRQL_YIELD)
    {
        RunDispatchGuardYieldDemo();
    }

    if (HO_DEMO_TEST_SELECTION == HO_DEMO_TEST_IRQL_EXIT)
    {
        RunDispatchGuardExitDemo();
    }
}

static void
RunScheduleDemo(void)
{
    RunIrqlSelfTest();
    RunThreadDemo();
    RunEventDemo();
    RunSemaphoreDemo();
    RunMutexDemo();
}

static void
RunIrqlSelfTest(void)
{
    KE_IRQL_GUARD outerGuard = {0};
    KE_IRQL_GUARD innerGuard = {0};
    KE_CRITICAL_SECTION outerCriticalSection = {0};
    KE_CRITICAL_SECTION innerCriticalSection = {0};

    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeAcquireIrqlGuard(&outerGuard, KE_IRQL_DISPATCH_LEVEL);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(!KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeAcquireIrqlGuard(&innerGuard, KE_IRQL_DISPATCH_LEVEL);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(!KeIsBlockingAllowed(), EC_INVALID_STATE);
    KeReleaseIrqlGuard(&innerGuard);

    KeEnterCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    KeLeaveCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);

    KeReleaseIrqlGuard(&outerGuard);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeEnterCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(!KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeEnterCriticalSection(&innerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    KeLeaveCriticalSection(&innerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);

    KeLeaveCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);

    klog(KLOG_LEVEL_INFO, "[DEMO] IRQL/critical-section self-test passed\n");
}

static void
RunThreadDemo(void)
{
    // Create test kernel threads (KeSleep compatibility)
    KTHREAD *threadA = NULL;
    KTHREAD *threadB = NULL;

    HO_STATUS status;
    status = KeThreadCreate(&threadA, TestThreadA, (void *)0xAAAA);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create thread A");

    status = KeThreadCreate(&threadB, TestThreadB, (void *)0xBBBB);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create thread B");

    status = KeThreadStart(threadA);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start thread A");

    status = KeThreadStart(threadB);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start thread B");
}

static void
RunEventDemo(void)
{
    // ── KEVENT demo ──────────────────────────────────────────
    HO_STATUS status;
    KeInitializeEvent(&gTestEvent, FALSE);

    KTHREAD *producer = NULL;
    KTHREAD *consumer = NULL;
    KTHREAD *poller = NULL;

    status = KeThreadCreate(&producer, EventProducerThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create producer thread");

    status = KeThreadCreate(&consumer, EventConsumerThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create consumer thread");

    status = KeThreadCreate(&poller, EventPollThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create poller thread");

    status = KeThreadStart(consumer);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start consumer thread");

    status = KeThreadStart(poller);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start poller thread");

    status = KeThreadStart(producer);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start producer thread");
}

static void
RunSemaphoreDemo(void)
{
    // ── KSEMAPHORE demo ──────────────────────────────────────
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

static void
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

static void
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

static void
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

static void
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

static void
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

static void
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

// ─────────────────────────────────────────────────────────────
// Test kernel threads
// ─────────────────────────────────────────────────────────────

static void
TestThreadA(void *arg)
{
    uint32_t id = KeGetCurrentThread()->ThreadId;
    for (uint32_t i = 0; i < 5; i++)
    {
        klog(KLOG_LEVEL_INFO, "[THREAD-%u] tick %u (arg=%p)\n", id, i, arg);
        KeSleep(50000000ULL); // 50 ms
    }
    klog(KLOG_LEVEL_INFO, "[THREAD-%u] done, exiting\n", id);
}

static void
TestThreadB(void *arg)
{
    uint32_t id = KeGetCurrentThread()->ThreadId;
    for (uint32_t i = 0; i < 5; i++)
    {
        klog(KLOG_LEVEL_INFO, "[THREAD-%u] tick %u (arg=%p)\n", id, i, arg);
        KeYield();
        KeSleep(30000000ULL); // 30 ms
    }
    klog(KLOG_LEVEL_INFO, "[THREAD-%u] done, exiting\n", id);
}

// ─────────────────────────────────────────────────────────────
// KEVENT demo threads
// ─────────────────────────────────────────────────────────────

static void
EventProducerThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    // Wait 100ms then signal the event
    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] sleeping 100ms before signaling event\n", id);
    KeSleep(100000000ULL); // 100 ms

    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] >>> KeSetEvent\n", id);
    KeSetEvent(&gTestEvent);

    // Wait a bit, then reset and re-signal
    KeSleep(50000000ULL); // 50 ms
    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] >>> KeResetEvent\n", id);
    KeResetEvent(&gTestEvent);

    KeSleep(50000000ULL); // 50 ms
    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] >>> KeSetEvent (second)\n", id);
    KeSetEvent(&gTestEvent);

    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] done\n", id);
}

static void
EventConsumerThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    // Infinite wait — should block until producer signals
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] waiting for event (infinite)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestEvent, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] event wait returned: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    // Immediate satisfy while the manual-reset event remains signaled
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] waiting for event again (expect immediate SUCCESS)\n", id);
    result = KeWaitForSingleObject(&gTestEvent, 200000000ULL);
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] event wait returned: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    // Wait again after reset so the second signal drives a blocking wakeup
    KeSleep(80000000ULL); // 80ms — event should be reset by now
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] waiting for event after reset (200ms timeout)\n", id);
    result = KeWaitForSingleObject(&gTestEvent, 200000000ULL);
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] event wait returned: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] done\n", id);
}

static void
EventPollThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    // Zero-timeout poll — should return TIMEOUT immediately since event not signaled
    klog(KLOG_LEVEL_INFO, "[POLLER-%u] zero-timeout poll (expect TIMEOUT)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestEvent, 0);
    klog(KLOG_LEVEL_INFO, "[POLLER-%u] poll result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    // Finite timeout wait — should timeout before producer signals
    klog(KLOG_LEVEL_INFO, "[POLLER-%u] finite timeout wait 30ms (expect TIMEOUT)\n", id);
    result = KeWaitForSingleObject(&gTestEvent, 30000000ULL);
    klog(KLOG_LEVEL_INFO, "[POLLER-%u] finite wait result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    klog(KLOG_LEVEL_INFO, "[POLLER-%u] done\n", id);
}

// ─────────────────────────────────────────────────────────────
// KSEMAPHORE demo threads
// ─────────────────────────────────────────────────────────────

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

static void
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

static void
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

static void
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

static void
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
