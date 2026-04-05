/**
 * HimuOperatingSystem
 *
 * File: demo/demo_internal.h
 * Description: Private declarations shared by kernel demo modules.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/demo.h>
#include <kernel/ex/ex_bootstrap_abi.h>
#include <kernel/hodbg.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/irql.h>
#include <kernel/ke/event.h>
#include <kernel/ke/mutex.h>
#include <kernel/ke/semaphore.h>

#define HO_DEMO_TEST_NONE              0
#define HO_DEMO_TEST_SCHEDULE          1
#define HO_DEMO_TEST_THREAD            2
#define HO_DEMO_TEST_EVENT             3
#define HO_DEMO_TEST_SEMAPHORE         4
#define HO_DEMO_TEST_MUTEX             5
#define HO_DEMO_TEST_GUARD_WAIT        6
#define HO_DEMO_TEST_OWNED_EXIT        7
#define HO_DEMO_TEST_IRQL_WAIT         8
#define HO_DEMO_TEST_IRQL_SLEEP        9
#define HO_DEMO_TEST_IRQL_YIELD        10
#define HO_DEMO_TEST_IRQL_EXIT         11
#define HO_DEMO_TEST_PF_IMPORTED       12
#define HO_DEMO_TEST_PF_GUARD          13
#define HO_DEMO_TEST_PF_FIXMAP         14
#define HO_DEMO_TEST_PF_HEAP           15
#define HO_DEMO_TEST_KTHREAD_POOL_RACE 16
#define HO_DEMO_TEST_USER_HELLO        17
#define HO_DEMO_TEST_USER_CAPS         18
#define HO_DEMO_TEST_USER_DUAL         19

#ifndef HO_DEMO_TEST_SELECTION
#define HO_DEMO_TEST_SELECTION HO_DEMO_TEST_NONE
#endif

#ifndef HO_DEMO_TEST_SELECTION_NAME
#define HO_DEMO_TEST_SELECTION_NAME "none"
#endif

extern KEVENT gTestEvent;
extern KSEMAPHORE gTestSemaphore;
extern KMUTEX gTestMutex;
extern KEVENT gCriticalSectionGuardEvent;
extern KEVENT gDispatchGuardEvent;
extern KMUTEX gOwnedExitMutex;

typedef struct _KI_USER_EMBEDDED_ARTIFACTS
{
    const uint8_t *CodeBytes;
    uint64_t CodeLength;
    const uint8_t *ConstBytes;
    uint64_t ConstLength;
} KI_USER_EMBEDDED_ARTIFACTS;

void TestThreadA(void *arg);
void TestThreadB(void *arg);
void EventProducerThread(void *arg);
void EventConsumerThread(void *arg);
void EventPollThread(void *arg);
void SemaphoreImmediateThread(void *arg);
void SemaphorePollThread(void *arg);
void SemaphoreWaiterOneThread(void *arg);
void SemaphoreWaiterTwoThread(void *arg);
void SemaphoreTimeoutThread(void *arg);
void SemaphoreDelayedWaiterThread(void *arg);
void SemaphoreReleaserThread(void *arg);
void MutexOwnerThread(void *arg);
void MutexPollThread(void *arg);
void MutexWaiterOneThread(void *arg);
void MutexWaiterTwoThread(void *arg);
void MutexNonOwnerReleaseThread(void *arg);
void CriticalSectionWaitViolationThread(void *arg);
void OwnedMutexExitViolationThread(void *arg);
void DispatchGuardWaitViolationThread(void *arg);
void DispatchGuardSleepViolationThread(void *arg);
void DispatchGuardYieldViolationThread(void *arg);
void DispatchGuardExitViolationThread(void *arg);
void PageFaultImportedThread(void *arg);
void PageFaultGuardThread(void *arg);
void PageFaultFixmapThread(void *arg);
void PageFaultHeapThread(void *arg);

void RunIrqlSelfTest(void);
void RunPriorityReadyQueueSmokeDemo(void);
void RunScheduleDemo(void);
void RunThreadDemo(void);
void RunEventDemo(void);
void RunSemaphoreDemo(void);
void RunMutexDemo(void);
void RunGuardWaitDemo(void);
void RunOwnedExitDemo(void);
void RunDispatchGuardWaitDemo(void);
void RunDispatchGuardSleepDemo(void);
void RunDispatchGuardYieldDemo(void);
void RunDispatchGuardExitDemo(void);
void RunPageFaultImportedDemo(void);
void RunPageFaultGuardDemo(void);
void RunPageFaultFixmapDemo(void);
void RunPageFaultHeapDemo(void);
void RunKthreadPoolRaceDemo(void);
void KiUserHelloGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts);
void KiUserCounterGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts);
void RunUserHelloDemo(void);
void RunUserCapsDemo(void);
void RunUserDualDemo(void);
