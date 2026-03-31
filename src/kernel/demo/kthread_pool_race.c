/**
 * HimuOperatingSystem
 *
 * File: demo/kthread_pool_race.c
 * Description: Regression suite for KTHREAD pool synchronization and lifecycle safety.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"
#include <libc/string.h>

#define POOL_RACE_WORKER_COUNT              2U
#define POOL_RACE_ALLOCATIONS_PER_WORKER    24U
#define CREATE_RACE_CREATOR_COUNT           2U
#define CREATE_RACE_CHILDREN_PER_CREATOR    6U
#define CREATE_RACE_TOTAL_CHILDREN          (CREATE_RACE_CREATOR_COUNT * CREATE_RACE_CHILDREN_PER_CREATOR)
#define REAP_RACE_CHILD_COUNT               10U
#define POOL_SETTLE_EXPECTED_USED_SLOTS     2U
#define POOL_SETTLE_MAX_RETRIES             64U
#define POOL_SETTLE_SLEEP_NS                1000000ULL

typedef struct KI_POOL_RACE_WORKER_CONTEXT
{
    KE_POOL *Pool;
    KEVENT *StartEvent;
    KSEMAPHORE *DoneSemaphore;
    uint32_t AllocationCount;
    uint8_t FillPattern;
} KI_POOL_RACE_WORKER_CONTEXT;

typedef struct KI_CREATE_RACE_CREATOR_CONTEXT
{
    KEVENT *StartEvent;
    KSEMAPHORE *DoneSemaphore;
    KSEMAPHORE *ChildExitSemaphore;
    uint32_t *RecordedIds;
    uint32_t Count;
    BOOL SleepBetweenCreates;
} KI_CREATE_RACE_CREATOR_CONTEXT;

static KE_POOL gPoolRaceRegressionPool;
static KEVENT gPoolRaceStartEvent;
static KSEMAPHORE gPoolRaceDoneSemaphore;
static KI_POOL_RACE_WORKER_CONTEXT gPoolRaceWorkerContexts[POOL_RACE_WORKER_COUNT];

static KEVENT gCreateRaceStartEvent;
static KSEMAPHORE gCreateRaceDoneSemaphore;
static KSEMAPHORE gCreateRaceChildExitSemaphore;
static uint32_t gCreateRaceRecordedIds[CREATE_RACE_TOTAL_CHILDREN];
static KI_CREATE_RACE_CREATOR_CONTEXT gCreateRaceContexts[CREATE_RACE_CREATOR_COUNT];

static KEVENT gReapRaceStartEvent;
static KSEMAPHORE gReapRaceDoneSemaphore;
static KSEMAPHORE gReapRaceChildExitSemaphore;
static uint32_t gReapRaceRecordedIds[REAP_RACE_CHILD_COUNT];
static KI_CREATE_RACE_CREATOR_CONTEXT gReapRaceContext;

static void KiWaitForSemaphorePermits(KSEMAPHORE *semaphore, uint32_t count, const char *reason);
static void KiReadPoolAccounting(KE_POOL *pool, uint32_t *usedSlots, uint32_t *totalSlots);
static uint32_t KiCountPoolFreeNodes(KE_POOL *pool);
static void KiWaitForPoolToSettle(KE_POOL *pool, uint32_t expectedUsedSlots, const char *reason);
static void KiVerifyDistinctThreadIds(const uint32_t *ids, uint32_t count, const char *reason);
static void KiRunPoolInterleavingRegression(void);
static void KiRunThreadIdRegression(void);
static void KiRunCreateReapRegression(void);
static void KthreadPoolRaceControllerThread(void *arg);
static void PoolRaceWorkerThread(void *arg);
static void KthreadPoolRecordedWorkerThread(void *arg);
static void ThreadIdCreatorThread(void *arg);
void KiReapTerminatedThreads(void);

void
RunKthreadPoolRaceDemo(void)
{
    KTHREAD *controller = NULL;
    HO_STATUS status = KeThreadCreate(&controller, KthreadPoolRaceControllerThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create KTHREAD pool regression controller");

    status = KeThreadStart(controller);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start KTHREAD pool regression controller");
}

static void
KiWaitForSemaphorePermits(KSEMAPHORE *semaphore, uint32_t count, const char *reason)
{
    for (uint32_t i = 0; i < count; i++)
    {
        HO_STATUS status = KeWaitForSingleObject(semaphore, KE_WAIT_INFINITE);
        if (status != EC_SUCCESS)
        {
            klog(KLOG_LEVEL_ERROR, "[TEST] %s wait failed at permit %u/%u\n", reason, i + 1, count);
            HO_KPANIC(status, "KTHREAD pool regression wait failed");
        }
    }
}

static void
KiReadPoolAccounting(KE_POOL *pool, uint32_t *usedSlots, uint32_t *totalSlots)
{
    KE_CRITICAL_SECTION criticalSection = {0};

    KeEnterCriticalSection(&criticalSection);
    if (usedSlots != NULL)
        *usedSlots = pool->UsedSlots;
    if (totalSlots != NULL)
        *totalSlots = pool->TotalSlots;
    KeLeaveCriticalSection(&criticalSection);
}

static uint32_t
KiCountPoolFreeNodes(KE_POOL *pool)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    uint32_t freeCount = 0;

    KeEnterCriticalSection(&criticalSection);

    for (KE_POOL_FREE_NODE *node = pool->FreeList; node != NULL; node = node->Next)
    {
        freeCount++;
        HO_KASSERT(freeCount <= pool->TotalSlots, EC_INVALID_STATE);
    }

    HO_KASSERT(freeCount + pool->UsedSlots == pool->TotalSlots, EC_INVALID_STATE);
    KeLeaveCriticalSection(&criticalSection);
    return freeCount;
}

static void
KiWaitForPoolToSettle(KE_POOL *pool, uint32_t expectedUsedSlots, const char *reason)
{
    uint32_t usedSlots = 0;
    uint32_t totalSlots = 0;

    for (uint32_t attempt = 0; attempt < POOL_SETTLE_MAX_RETRIES; attempt++)
    {
        KiReapTerminatedThreads();

        KiReadPoolAccounting(pool, &usedSlots, &totalSlots);
        if (usedSlots == expectedUsedSlots)
            return;

        KeSleep(POOL_SETTLE_SLEEP_NS);
    }

    klog(KLOG_LEVEL_ERROR, "[TEST] pool did not settle for %s (used=%u total=%u expected=%u)\n", reason, usedSlots,
         totalSlots, expectedUsedSlots);
    HO_KPANIC(EC_TIMEOUT, "KTHREAD pool regression settle timeout");
}

static void
KiVerifyDistinctThreadIds(const uint32_t *ids, uint32_t count, const char *reason)
{
    for (uint32_t i = 0; i < count; i++)
    {
        HO_KASSERT(ids[i] != 0, EC_INVALID_STATE);
        for (uint32_t j = i + 1; j < count; j++)
        {
            HO_KASSERT(ids[i] != ids[j], EC_INVALID_STATE);
        }
    }

    klog(KLOG_LEVEL_INFO, "[TEST] %s verified %u distinct ThreadId values\n", reason, count);
}

static void
KiRunPoolInterleavingRegression(void)
{
    klog(KLOG_LEVEL_INFO, "[TEST] pool interleaving regression start\n");

    HO_STATUS status = KePoolInit(&gPoolRaceRegressionPool, 256, 4, "pool-race-regression");
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize shared regression pool");

    KeInitializeEvent(&gPoolRaceStartEvent, FALSE);
    status = KeInitializeSemaphore(&gPoolRaceDoneSemaphore, 0, POOL_RACE_WORKER_COUNT);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize pool regression semaphore");

    for (uint32_t i = 0; i < POOL_RACE_WORKER_COUNT; i++)
    {
        gPoolRaceWorkerContexts[i].Pool = &gPoolRaceRegressionPool;
        gPoolRaceWorkerContexts[i].StartEvent = &gPoolRaceStartEvent;
        gPoolRaceWorkerContexts[i].DoneSemaphore = &gPoolRaceDoneSemaphore;
        gPoolRaceWorkerContexts[i].AllocationCount = POOL_RACE_ALLOCATIONS_PER_WORKER;
        gPoolRaceWorkerContexts[i].FillPattern = (uint8_t)(0xA0U + i);

        KTHREAD *worker = NULL;
        status = KeThreadCreate(&worker, PoolRaceWorkerThread, &gPoolRaceWorkerContexts[i]);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Failed to create pool regression worker");

        status = KeThreadStart(worker);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Failed to start pool regression worker");
    }

    KeSetEvent(&gPoolRaceStartEvent);
    KiWaitForSemaphorePermits(&gPoolRaceDoneSemaphore, POOL_RACE_WORKER_COUNT, "pool regression workers");
    KiWaitForPoolToSettle(&gKThreadPool, POOL_SETTLE_EXPECTED_USED_SLOTS, "pool regression worker reap");

    uint32_t usedSlots;
    uint32_t totalSlots;
    uint32_t freeCount = KiCountPoolFreeNodes(&gPoolRaceRegressionPool);
    KiReadPoolAccounting(&gPoolRaceRegressionPool, &usedSlots, &totalSlots);

    HO_KASSERT(usedSlots == 0, EC_INVALID_STATE);
    HO_KASSERT(freeCount == totalSlots, EC_INVALID_STATE);
    HO_KASSERT(totalSlots >= POOL_RACE_WORKER_COUNT * POOL_RACE_ALLOCATIONS_PER_WORKER, EC_INVALID_STATE);

    klog(KLOG_LEVEL_INFO, "[TEST] pool interleaving regression passed (slots=%u)\n", totalSlots);
}

static void
KiRunThreadIdRegression(void)
{
    klog(KLOG_LEVEL_INFO, "[TEST] create/create ThreadId regression start\n");

    memset(gCreateRaceRecordedIds, 0, sizeof(gCreateRaceRecordedIds));

    KeInitializeEvent(&gCreateRaceStartEvent, FALSE);

    HO_STATUS status = KeInitializeSemaphore(&gCreateRaceDoneSemaphore, 0, CREATE_RACE_CREATOR_COUNT);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize creator completion semaphore");

    status = KeInitializeSemaphore(&gCreateRaceChildExitSemaphore, 0, CREATE_RACE_TOTAL_CHILDREN);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize child exit semaphore");

    for (uint32_t i = 0; i < CREATE_RACE_CREATOR_COUNT; i++)
    {
        gCreateRaceContexts[i].StartEvent = &gCreateRaceStartEvent;
        gCreateRaceContexts[i].DoneSemaphore = &gCreateRaceDoneSemaphore;
        gCreateRaceContexts[i].ChildExitSemaphore = &gCreateRaceChildExitSemaphore;
        gCreateRaceContexts[i].RecordedIds = &gCreateRaceRecordedIds[i * CREATE_RACE_CHILDREN_PER_CREATOR];
        gCreateRaceContexts[i].Count = CREATE_RACE_CHILDREN_PER_CREATOR;
        gCreateRaceContexts[i].SleepBetweenCreates = FALSE;

        KTHREAD *creator = NULL;
        status = KeThreadCreate(&creator, ThreadIdCreatorThread, &gCreateRaceContexts[i]);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Failed to create ThreadId regression creator");

        status = KeThreadStart(creator);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Failed to start ThreadId regression creator");
    }

    KeSetEvent(&gCreateRaceStartEvent);
    KiWaitForSemaphorePermits(&gCreateRaceDoneSemaphore, CREATE_RACE_CREATOR_COUNT, "ThreadId creators");
    KiVerifyDistinctThreadIds(gCreateRaceRecordedIds, CREATE_RACE_TOTAL_CHILDREN, "create/create regression");
    KiWaitForSemaphorePermits(&gCreateRaceChildExitSemaphore, CREATE_RACE_TOTAL_CHILDREN,
                              "create/create child exits");
    KiWaitForPoolToSettle(&gKThreadPool, POOL_SETTLE_EXPECTED_USED_SLOTS, "create/create reap");

    klog(KLOG_LEVEL_INFO, "[TEST] create/create ThreadId regression passed\n");
}

static void
KiRunCreateReapRegression(void)
{
    klog(KLOG_LEVEL_INFO, "[TEST] create/reap regression start\n");

    memset(gReapRaceRecordedIds, 0, sizeof(gReapRaceRecordedIds));
    KeInitializeEvent(&gReapRaceStartEvent, FALSE);

    HO_STATUS status = KeInitializeSemaphore(&gReapRaceDoneSemaphore, 0, 1);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize create/reap completion semaphore");

    status = KeInitializeSemaphore(&gReapRaceChildExitSemaphore, 0, REAP_RACE_CHILD_COUNT);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to initialize create/reap child exit semaphore");

    gReapRaceContext.StartEvent = &gReapRaceStartEvent;
    gReapRaceContext.DoneSemaphore = &gReapRaceDoneSemaphore;
    gReapRaceContext.ChildExitSemaphore = &gReapRaceChildExitSemaphore;
    gReapRaceContext.RecordedIds = gReapRaceRecordedIds;
    gReapRaceContext.Count = REAP_RACE_CHILD_COUNT;
    gReapRaceContext.SleepBetweenCreates = TRUE;

    KTHREAD *creator = NULL;
    status = KeThreadCreate(&creator, ThreadIdCreatorThread, &gReapRaceContext);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create create/reap regression creator");

    status = KeThreadStart(creator);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start create/reap regression creator");

    KeSetEvent(&gReapRaceStartEvent);
    KiWaitForSemaphorePermits(&gReapRaceDoneSemaphore, 1, "create/reap creator");
    KiWaitForSemaphorePermits(&gReapRaceChildExitSemaphore, REAP_RACE_CHILD_COUNT, "create/reap child exits");
    KiWaitForPoolToSettle(&gKThreadPool, POOL_SETTLE_EXPECTED_USED_SLOTS, "create/reap reap");

    uint32_t usedSlots;
    uint32_t totalSlots;
    uint32_t freeCount = KiCountPoolFreeNodes(&gKThreadPool);
    KiReadPoolAccounting(&gKThreadPool, &usedSlots, &totalSlots);

    HO_KASSERT(usedSlots == POOL_SETTLE_EXPECTED_USED_SLOTS, EC_INVALID_STATE);
    HO_KASSERT(freeCount + usedSlots == totalSlots, EC_INVALID_STATE);

    klog(KLOG_LEVEL_INFO, "[TEST] create/reap regression passed (used=%u total=%u free=%u)\n", usedSlots,
         totalSlots, freeCount);
}

static void
KiRunOversizedObjectRegression(void)
{
    klog(KLOG_LEVEL_INFO, "[TEST] oversize objectSize regression start\n");

    KE_POOL oversizePool = {0};
    // objectSize = 8192 (2 * PAGE_4KB / 2) exceeds single-page capacity.
    // KePoolInit must reject it with EC_ILLEGAL_ARGUMENT, not underflow.
    HO_STATUS status = KePoolInit(&oversizePool, 8192, 1, "oversize-regression");
    HO_KASSERT(status == EC_ILLEGAL_ARGUMENT, status);
    HO_KASSERT(oversizePool.Magic != KE_POOL_MAGIC_ALIVE, EC_INVALID_STATE);

    // Also verify the exact boundary: objectSize == PAGE_4KB (4096).
    status = KePoolInit(&oversizePool, 4096, 1, "page-boundary-regression");
    HO_KASSERT(status == EC_ILLEGAL_ARGUMENT, status);
    HO_KASSERT(oversizePool.Magic != KE_POOL_MAGIC_ALIVE, EC_INVALID_STATE);

    klog(KLOG_LEVEL_INFO, "[TEST] oversize objectSize regression passed\n");
}

static void
KthreadPoolRaceControllerThread(void *arg)
{
    (void)arg;

    klog(KLOG_LEVEL_INFO, "[TEST] KTHREAD pool race regression controller start\n");
    KiRunOversizedObjectRegression();
    KiRunPoolInterleavingRegression();
    KiRunThreadIdRegression();
    KiRunCreateReapRegression();
    klog(KLOG_LEVEL_INFO, "[TEST] KTHREAD pool race regression suite passed\n");
}

static void
PoolRaceWorkerThread(void *arg)
{
    KI_POOL_RACE_WORKER_CONTEXT *context = (KI_POOL_RACE_WORKER_CONTEXT *)arg;
    void *objects[POOL_RACE_ALLOCATIONS_PER_WORKER] = {0};

    HO_STATUS status = KeWaitForSingleObject(context->StartEvent, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Pool regression worker failed waiting for start event");

    HO_KASSERT(context->AllocationCount <= POOL_RACE_ALLOCATIONS_PER_WORKER, EC_ILLEGAL_ARGUMENT);

    for (uint32_t i = 0; i < context->AllocationCount; i++)
    {
        objects[i] = KePoolAlloc(context->Pool);
        HO_KASSERT(objects[i] != NULL, EC_OUT_OF_RESOURCE);

        memset(objects[i], context->FillPattern, context->Pool->SlotSize);

        if ((i & 1U) == 0)
            KeYield();
    }

    for (uint32_t i = context->AllocationCount; i > 0; i--)
    {
        KePoolFree(context->Pool, objects[i - 1]);

        if ((i & 1U) == 0)
            KeYield();
    }

    status = KeReleaseSemaphore(context->DoneSemaphore, 1);
    HO_KASSERT(status == EC_SUCCESS, status);
}

static void
KthreadPoolRecordedWorkerThread(void *arg)
{
    KSEMAPHORE *childExitSemaphore = (KSEMAPHORE *)arg;

    KeYield();

    HO_STATUS status = KeReleaseSemaphore(childExitSemaphore, 1);
    HO_KASSERT(status == EC_SUCCESS, status);
}

static void
ThreadIdCreatorThread(void *arg)
{
    KI_CREATE_RACE_CREATOR_CONTEXT *context = (KI_CREATE_RACE_CREATOR_CONTEXT *)arg;

    HO_STATUS status = KeWaitForSingleObject(context->StartEvent, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Thread creator failed waiting for start event");

    for (uint32_t i = 0; i < context->Count; i++)
    {
        KTHREAD *child = NULL;

        status = KeThreadCreate(&child, KthreadPoolRecordedWorkerThread, context->ChildExitSemaphore);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Thread creator failed to allocate child thread");

        context->RecordedIds[i] = child->ThreadId;

        status = KeThreadStart(child);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Thread creator failed to start child thread");

        if (context->SleepBetweenCreates)
            KeSleep(POOL_SETTLE_SLEEP_NS);
        else
            KeYield();
    }

    status = KeReleaseSemaphore(context->DoneSemaphore, 1);
    HO_KASSERT(status == EC_SUCCESS, status);
}