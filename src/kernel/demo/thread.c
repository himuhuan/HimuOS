/**
 * HimuOperatingSystem
 *
 * File: demo/thread.c
 * Description: Thread demo routines and worker threads.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

void
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

void
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

void
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
