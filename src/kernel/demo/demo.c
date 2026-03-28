/**
 * HimuOperatingSystem
 *
 * File: demo/demo.c
 * Description: Kernel demo routines for testing various components.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

KEVENT gTestEvent;
KSEMAPHORE gTestSemaphore;
KMUTEX gTestMutex;
KEVENT gCriticalSectionGuardEvent;
KEVENT gDispatchGuardEvent;
KMUTEX gOwnedExitMutex;

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

void
RunScheduleDemo(void)
{
    RunIrqlSelfTest();
    RunThreadDemo();
    RunEventDemo();
    RunSemaphoreDemo();
    RunMutexDemo();
}
