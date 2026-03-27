/**
 * HimuOperatingSystem
 *
 * File: demo.c
 * Description: Kernel demo entry point — routes to per-subsystem demo modules.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/demo.h>
#include <kernel/hodbg.h>
#include "demo_internal.h"

#define HO_DEMO_TEST_NONE       0
#define HO_DEMO_TEST_SCHEDULE   1
#define HO_DEMO_TEST_THREAD     2
#define HO_DEMO_TEST_EVENT      3
#define HO_DEMO_TEST_SEMAPHORE  4
#define HO_DEMO_TEST_MUTEX      5
#define HO_DEMO_TEST_GUARD_WAIT 6
#define HO_DEMO_TEST_OWNED_EXIT 7

#ifndef HO_DEMO_TEST_SELECTION
#define HO_DEMO_TEST_SELECTION HO_DEMO_TEST_NONE
#endif

#ifndef HO_DEMO_TEST_SELECTION_NAME
#define HO_DEMO_TEST_SELECTION_NAME "none"
#endif

static void RunScheduleDemo(void);

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
}

static void
RunScheduleDemo(void)
{
    RunThreadDemo();
    RunEventDemo();
    RunSemaphoreDemo();
    RunMutexDemo();
}
