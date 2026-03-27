/**
 * HimuOperatingSystem
 *
 * File: demo_thread.c
 * Description: Thread lifecycle demo — basic KeSleep / KeYield test threads.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"
#include <kernel/hodbg.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>

static void TestThreadA(void *arg);
static void TestThreadB(void *arg);

void
RunThreadDemo(void)
{
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
