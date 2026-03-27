/**
 * HimuOperatingSystem
 *
 * File: demo_event.c
 * Description: KEVENT demo — producer/consumer/poller threads.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"
#include <kernel/hodbg.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/event.h>

static KEVENT gTestEvent;

static void EventProducerThread(void *arg);
static void EventConsumerThread(void *arg);
static void EventPollThread(void *arg);

void
RunEventDemo(void)
{
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
EventProducerThread(void *arg)
{
    (void)arg;
    uint32_t id = KeGetCurrentThread()->ThreadId;

    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] sleeping 100ms before signaling event\n", id);
    KeSleep(100000000ULL); // 100 ms

    klog(KLOG_LEVEL_INFO, "[PRODUCER-%u] >>> KeSetEvent\n", id);
    KeSetEvent(&gTestEvent);

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

    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] waiting for event (infinite)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestEvent, KE_WAIT_INFINITE);
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] event wait returned: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] waiting for event again (expect immediate SUCCESS)\n", id);
    result = KeWaitForSingleObject(&gTestEvent, 200000000ULL);
    klog(KLOG_LEVEL_INFO, "[CONSUMER-%u] event wait returned: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

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

    klog(KLOG_LEVEL_INFO, "[POLLER-%u] zero-timeout poll (expect TIMEOUT)\n", id);
    HO_STATUS result = KeWaitForSingleObject(&gTestEvent, 0);
    klog(KLOG_LEVEL_INFO, "[POLLER-%u] poll result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    klog(KLOG_LEVEL_INFO, "[POLLER-%u] finite timeout wait 30ms (expect TIMEOUT)\n", id);
    result = KeWaitForSingleObject(&gTestEvent, 30000000ULL);
    klog(KLOG_LEVEL_INFO, "[POLLER-%u] finite wait result: %s\n", id, result == EC_SUCCESS ? "SUCCESS" : "TIMEOUT");

    klog(KLOG_LEVEL_INFO, "[POLLER-%u] done\n", id);
}
