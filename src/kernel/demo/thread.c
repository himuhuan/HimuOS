/**
 * HimuOperatingSystem
 *
 * File: demo/thread.c
 * Description: Thread demo routines and worker threads.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#define THREAD_DEMO_JOIN_DELAY_NS   30000000ULL
#define THREAD_DEMO_JOIN_TIMEOUT_NS 20000000ULL
#define THREAD_DEMO_POLL_RETRIES    128U
#define THREAD_DEMO_POLL_SLEEP_NS   1000000ULL

typedef struct KI_THREAD_DEMO_WORKER_CONTEXT
{
    KEVENT *StartEvent;
    KSEMAPHORE *ReadySemaphore;
    KSEMAPHORE *ExitSemaphore;
    uint64_t DelayNs;
} KI_THREAD_DEMO_WORKER_CONTEXT;

typedef struct KI_THREAD_DEMO_JOINER_CONTEXT
{
    KTHREAD *TargetThread;
    KSEMAPHORE *ReadySemaphore;
    KSEMAPHORE *DoneSemaphore;
    uint64_t TimeoutNs;
    HO_STATUS JoinStatus;
} KI_THREAD_DEMO_JOINER_CONTEXT;

typedef struct KI_THREAD_DEMO_STATUS_CONTEXT
{
    KSEMAPHORE *DoneSemaphore;
    HO_STATUS Status;
} KI_THREAD_DEMO_STATUS_CONTEXT;

void KiFinalizeThread(KTHREAD *thread);

static void KiAssertThreadDemoStatus(HO_STATUS actual, HO_STATUS expected, const char *reason);
static void KiWaitForThreadDemoSemaphore(KSEMAPHORE *semaphore, const char *reason);
static KTHREAD_STATE KiReadThreadDemoState(const KTHREAD *thread);
static KTHREAD_TERMINATION_CLAIM_STATE KiReadThreadDemoClaimState(const KTHREAD *thread);
static BOOL KiIsThreadDemoTerminationCompletionPublished(const KTHREAD *thread);
static void KiWaitForThreadDemoState(const KTHREAD *thread, KTHREAD_STATE expectedState, const char *reason);
static void KiWaitForThreadDemoTerminationCompletion(const KTHREAD *thread, const char *reason);
static void KiWaitForThreadDemoClaimState(const KTHREAD *thread,
                                          KTHREAD_TERMINATION_CLAIM_STATE expectedState,
                                          const char *reason);
static void KiMarkThreadDemoTerminationConsumed(KTHREAD *thread, const char *reason);
static void KiFinalizeConsumedThreadDemoWorker(KTHREAD **threadStorage, const char *reason);
static void KiRunJoinBeforeTerminationScenario(void);
static void KiRunLateJoinScenario(void);
static void KiRunTimeoutJoinScenario(void);
static void KiRunConsumedJoinRejectionScenario(void);
static void KiRunJoinClaimConflictScenario(void);
static void KiRunSelfJoinScenario(void);
static void KiRunDetachedThreadLifecycleScenario(void);
static void ThreadDemoControllerThread(void *arg);
static void ThreadDemoControlledWorkerThread(void *arg);
static void ThreadDemoJoinerThread(void *arg);
static void ThreadDemoSelfJoinThread(void *arg);

void
RunThreadDemo(void)
{
    // Create test kernel threads (KeSleep compatibility)
    KTHREAD *threadA = NULL;
    KTHREAD *threadB = NULL;
    KTHREAD *controller = NULL;

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

    status = KeThreadCreate(&controller, ThreadDemoControllerThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create thread demo controller");

    status = KeThreadStart(controller);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start thread demo controller");
}

static void
KiAssertThreadDemoStatus(HO_STATUS actual, HO_STATUS expected, const char *reason)
{
    if (actual == expected)
    {
        return;
    }

    klog(KLOG_LEVEL_ERROR, "[TEST] %s failed (expected=%d actual=%d)\n", reason, expected, actual);
    HO_KPANIC(actual, "Thread demo regression assertion failed");
}

static void
KiWaitForThreadDemoSemaphore(KSEMAPHORE *semaphore, const char *reason)
{
    HO_STATUS status = KeWaitForSingleObject(semaphore, KE_WAIT_INFINITE);
    if (status == EC_SUCCESS)
    {
        return;
    }

    klog(KLOG_LEVEL_ERROR, "[TEST] %s wait failed (status=%d)\n", reason, status);
    HO_KPANIC(status, "Thread demo semaphore wait failed");
}

static KTHREAD_STATE
KiReadThreadDemoState(const KTHREAD *thread)
{
    KE_IRQL_GUARD irqlGuard = {0};
    KTHREAD_STATE state;
    KE_CRITICAL_SECTION criticalSection = {0};

    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);

    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KeEnterCriticalSection(&criticalSection);
    state = thread->State;
    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);
    return state;
}

static KTHREAD_TERMINATION_CLAIM_STATE
KiReadThreadDemoClaimState(const KTHREAD *thread)
{
    KE_IRQL_GUARD irqlGuard = {0};
    KTHREAD_TERMINATION_CLAIM_STATE claimState;
    KE_CRITICAL_SECTION criticalSection = {0};

    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);

    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KeEnterCriticalSection(&criticalSection);
    claimState = thread->TerminationClaimState;
    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);
    return claimState;
}

static BOOL
KiIsThreadDemoTerminationCompletionPublished(const KTHREAD *thread)
{
    KE_IRQL_GUARD irqlGuard = {0};
    BOOL published;
    KE_CRITICAL_SECTION criticalSection = {0};

    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);

    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KeEnterCriticalSection(&criticalSection);
    published = thread->TerminationCompletion.Header.SignalState != 0;
    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);
    return published;
}

static void
KiWaitForThreadDemoState(const KTHREAD *thread, KTHREAD_STATE expectedState, const char *reason)
{
    for (uint32_t attempt = 0; attempt < THREAD_DEMO_POLL_RETRIES; attempt++)
    {
        if (KiReadThreadDemoState(thread) == expectedState)
        {
            return;
        }

        KeSleep(THREAD_DEMO_POLL_SLEEP_NS);
    }

    klog(KLOG_LEVEL_ERROR, "[TEST] %s did not reach state %u\n", reason, expectedState);
    HO_KPANIC(EC_TIMEOUT, "Thread demo state wait timed out");
}

static void
KiWaitForThreadDemoTerminationCompletion(const KTHREAD *thread, const char *reason)
{
    for (uint32_t attempt = 0; attempt < THREAD_DEMO_POLL_RETRIES; attempt++)
    {
        if (KiIsThreadDemoTerminationCompletionPublished(thread))
        {
            return;
        }

        KeSleep(THREAD_DEMO_POLL_SLEEP_NS);
    }

    klog(KLOG_LEVEL_ERROR, "[TEST] %s did not publish termination completion\n", reason);
    HO_KPANIC(EC_TIMEOUT, "Thread demo termination completion wait timed out");
}

static void
KiWaitForThreadDemoClaimState(const KTHREAD *thread, KTHREAD_TERMINATION_CLAIM_STATE expectedState, const char *reason)
{
    for (uint32_t attempt = 0; attempt < THREAD_DEMO_POLL_RETRIES; attempt++)
    {
        if (KiReadThreadDemoClaimState(thread) == expectedState)
        {
            return;
        }

        KeSleep(THREAD_DEMO_POLL_SLEEP_NS);
    }

    klog(KLOG_LEVEL_ERROR, "[TEST] %s did not reach claim state %u\n", reason, expectedState);
    HO_KPANIC(EC_TIMEOUT, "Thread demo claim wait timed out");
}

static void
KiMarkThreadDemoTerminationConsumed(KTHREAD *thread, const char *reason)
{
    KE_IRQL_GUARD irqlGuard = {0};
    KE_CRITICAL_SECTION criticalSection = {0};

    HO_KASSERT(thread != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(reason != NULL, EC_ILLEGAL_ARGUMENT);

    KeAcquireIrqlGuard(&irqlGuard, KE_IRQL_DISPATCH_LEVEL);
    KeEnterCriticalSection(&criticalSection);

    if (thread->State != KTHREAD_STATE_TERMINATED || thread->TerminationMode != KTHREAD_TERMINATION_MODE_JOINABLE ||
        thread->TerminationClaimState != KTHREAD_TERMINATION_CLAIM_STATE_UNCLAIMED ||
        thread->TerminationCompletion.Header.SignalState == 0)
    {
        KTHREAD_STATE state = thread->State;
        KTHREAD_TERMINATION_MODE mode = thread->TerminationMode;
        KTHREAD_TERMINATION_CLAIM_STATE claimState = thread->TerminationClaimState;
        int completionPublished = thread->TerminationCompletion.Header.SignalState;

        KeLeaveCriticalSection(&criticalSection);
        KeReleaseIrqlGuard(&irqlGuard);

        klog(KLOG_LEVEL_ERROR, "[TEST] %s invalid consume precondition (state=%u mode=%u claim=%u completion=%d)\n",
             reason, state, mode, claimState, completionPublished);
        HO_KPANIC(EC_INVALID_STATE, "Thread demo consume precondition failed");
    }

    thread->TerminationClaimState = KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED;

    KeLeaveCriticalSection(&criticalSection);
    KeReleaseIrqlGuard(&irqlGuard);
}

static void
KiFinalizeConsumedThreadDemoWorker(KTHREAD **threadStorage, const char *reason)
{
    HO_KASSERT(threadStorage != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(*threadStorage != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(reason != NULL, EC_ILLEGAL_ARGUMENT);

    KiWaitForThreadDemoState(*threadStorage, KTHREAD_STATE_TERMINATED, reason);
    KiWaitForThreadDemoClaimState(*threadStorage, KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED, reason);
    KiFinalizeThread(*threadStorage);
    *threadStorage = NULL;
}

static void
KiRunJoinBeforeTerminationScenario(void)
{
    KI_THREAD_DEMO_WORKER_CONTEXT workerContext = {0};
    KTHREAD *worker = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] join before termination start\n");

    workerContext.DelayNs = THREAD_DEMO_JOIN_DELAY_NS;

    HO_STATUS status = KeThreadCreateJoinable(&worker, ThreadDemoControlledWorkerThread, &workerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create join-before-termination worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start join-before-termination worker");

    status = KeThreadJoin(worker, KE_WAIT_INFINITE);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "join before termination");

    klog(KLOG_LEVEL_INFO, "[TEST] join before termination passed\n");
}

static void
KiRunLateJoinScenario(void)
{
    KEVENT startEvent;
    KSEMAPHORE exitSemaphore;
    KI_THREAD_DEMO_WORKER_CONTEXT workerContext = {0};
    KTHREAD *worker = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] late join start\n");

    KeInitializeEvent(&startEvent, FALSE);

    HO_STATUS status = KeInitializeSemaphore(&exitSemaphore, 0, 1);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "initialize late-join exit semaphore");

    workerContext.StartEvent = &startEvent;
    workerContext.ExitSemaphore = &exitSemaphore;

    status = KeThreadCreateJoinable(&worker, ThreadDemoControlledWorkerThread, &workerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create late-join worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start late-join worker");

    KeSetEvent(&startEvent);
    KiWaitForThreadDemoSemaphore(&exitSemaphore, "late-join worker exit semaphore");
    KiWaitForThreadDemoState(worker, KTHREAD_STATE_TERMINATED, "late-join worker termination");

    status = KeThreadJoin(worker, 0);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "late join");

    klog(KLOG_LEVEL_INFO, "[TEST] late join passed\n");
}

static void
KiRunTimeoutJoinScenario(void)
{
    KEVENT startEvent;
    KI_THREAD_DEMO_WORKER_CONTEXT workerContext = {0};
    KTHREAD *worker = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] timeout join start\n");

    KeInitializeEvent(&startEvent, FALSE);
    workerContext.StartEvent = &startEvent;

    HO_STATUS status = KeThreadCreateJoinable(&worker, ThreadDemoControlledWorkerThread, &workerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create timeout-join worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start timeout-join worker");

    status = KeThreadJoin(worker, THREAD_DEMO_JOIN_TIMEOUT_NS);
    KiAssertThreadDemoStatus(status, EC_TIMEOUT, "timeout join initial wait");
    KiWaitForThreadDemoClaimState(worker, KTHREAD_TERMINATION_CLAIM_STATE_UNCLAIMED, "timeout join claim release");

    KeSetEvent(&startEvent);

    status = KeThreadJoin(worker, KE_WAIT_INFINITE);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "timeout join retry");

    klog(KLOG_LEVEL_INFO, "[TEST] timeout join passed\n");
}

static void
KiRunConsumedJoinRejectionScenario(void)
{
    KEVENT startEvent;
    KSEMAPHORE exitSemaphore;
    KI_THREAD_DEMO_WORKER_CONTEXT workerContext = {0};
    KTHREAD *worker = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] termination-consumed join rejection start\n");

    KeInitializeEvent(&startEvent, FALSE);

    HO_STATUS status = KeInitializeSemaphore(&exitSemaphore, 0, 1);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "initialize consumed-join exit semaphore");

    workerContext.StartEvent = &startEvent;
    workerContext.ExitSemaphore = &exitSemaphore;

    status = KeThreadCreateJoinable(&worker, ThreadDemoControlledWorkerThread, &workerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create consumed-join worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start consumed-join worker");

    KeSetEvent(&startEvent);
    KiWaitForThreadDemoSemaphore(&exitSemaphore, "consumed-join worker exit semaphore");
    KiWaitForThreadDemoState(worker, KTHREAD_STATE_TERMINATED, "consumed-join worker termination");
    KiWaitForThreadDemoTerminationCompletion(worker, "consumed-join worker completion publication");

    KiMarkThreadDemoTerminationConsumed(worker, "consume terminated joinable worker for rejection");

    status = KeThreadJoin(worker, 0);
    KiAssertThreadDemoStatus(status, EC_INVALID_STATE, "join after termination already consumed");

    KiFinalizeConsumedThreadDemoWorker(&worker, "consumed-join worker cleanup");

    klog(KLOG_LEVEL_INFO, "[TEST] termination-consumed join rejection passed\n");
}

static void
KiRunJoinClaimConflictScenario(void)
{
    KEVENT startEvent;
    KSEMAPHORE joinerReadySemaphore;
    KSEMAPHORE joinerDoneSemaphore;
    KI_THREAD_DEMO_WORKER_CONTEXT workerContext = {0};
    KI_THREAD_DEMO_JOINER_CONTEXT joinerContext = {0};
    KTHREAD *worker = NULL;
    KTHREAD *joiner = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] join-claim conflict start\n");

    KeInitializeEvent(&startEvent, FALSE);

    HO_STATUS status = KeInitializeSemaphore(&joinerReadySemaphore, 0, 1);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "initialize joiner-ready semaphore");

    status = KeInitializeSemaphore(&joinerDoneSemaphore, 0, 1);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "initialize joiner-done semaphore");

    workerContext.StartEvent = &startEvent;

    status = KeThreadCreateJoinable(&worker, ThreadDemoControlledWorkerThread, &workerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create join-claim worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start join-claim worker");

    joinerContext.TargetThread = worker;
    joinerContext.ReadySemaphore = &joinerReadySemaphore;
    joinerContext.DoneSemaphore = &joinerDoneSemaphore;
    joinerContext.TimeoutNs = KE_WAIT_INFINITE;
    joinerContext.JoinStatus = EC_FAILURE;

    status = KeThreadCreate(&joiner, ThreadDemoJoinerThread, &joinerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create primary joiner");

    status = KeThreadStart(joiner);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start primary joiner");

    KiWaitForThreadDemoSemaphore(&joinerReadySemaphore, "primary joiner ready");
    KiWaitForThreadDemoClaimState(worker, KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS,
                                  "primary joiner claim state");

    status = KeThreadJoin(worker, 0);
    KiAssertThreadDemoStatus(status, EC_INVALID_STATE, "second join while claim is active");

    status = KeThreadDetach(worker);
    KiAssertThreadDemoStatus(status, EC_INVALID_STATE, "detach while join claim is active");

    KeSetEvent(&startEvent);
    KiWaitForThreadDemoSemaphore(&joinerDoneSemaphore, "primary joiner completion");
    KiAssertThreadDemoStatus(joinerContext.JoinStatus, EC_SUCCESS, "primary joiner result");

    klog(KLOG_LEVEL_INFO, "[TEST] join-claim conflict passed\n");
}

static void
KiRunSelfJoinScenario(void)
{
    KSEMAPHORE doneSemaphore;
    KI_THREAD_DEMO_STATUS_CONTEXT statusContext = {0};
    KTHREAD *worker = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] self-join rejection start\n");

    HO_STATUS status = KeInitializeSemaphore(&doneSemaphore, 0, 1);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "initialize self-join completion semaphore");

    statusContext.DoneSemaphore = &doneSemaphore;
    statusContext.Status = EC_SUCCESS;

    status = KeThreadCreate(&worker, ThreadDemoSelfJoinThread, &statusContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create self-join worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start self-join worker");

    KiWaitForThreadDemoSemaphore(&doneSemaphore, "self-join completion");
    KiAssertThreadDemoStatus(statusContext.Status, EC_INVALID_STATE, "self-join rejection");

    klog(KLOG_LEVEL_INFO, "[TEST] self-join rejection passed\n");
}

static void
KiRunDetachedThreadLifecycleScenario(void)
{
    KEVENT startEvent;
    KSEMAPHORE exitSemaphore;
    KI_THREAD_DEMO_WORKER_CONTEXT workerContext = {0};
    KTHREAD *worker = NULL;

    klog(KLOG_LEVEL_INFO, "[TEST] detached lifecycle compatibility start\n");

    KeInitializeEvent(&startEvent, FALSE);

    HO_STATUS status = KeInitializeSemaphore(&exitSemaphore, 0, 1);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "initialize detached exit semaphore");

    workerContext.StartEvent = &startEvent;
    workerContext.ExitSemaphore = &exitSemaphore;

    status = KeThreadCreate(&worker, ThreadDemoControlledWorkerThread, &workerContext);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "create detached worker");

    status = KeThreadStart(worker);
    KiAssertThreadDemoStatus(status, EC_SUCCESS, "start detached worker");

    status = KeThreadJoin(worker, 0);
    KiAssertThreadDemoStatus(status, EC_INVALID_STATE, "join detached thread rejection");

    status = KeThreadDetach(worker);
    KiAssertThreadDemoStatus(status, EC_INVALID_STATE, "detach already detached thread rejection");

    KeSetEvent(&startEvent);
    KiWaitForThreadDemoSemaphore(&exitSemaphore, "detached worker exit semaphore");

    klog(KLOG_LEVEL_INFO, "[TEST] detached lifecycle compatibility passed\n");
}

static void
ThreadDemoControllerThread(void *arg)
{
    (void)arg;

    klog(KLOG_LEVEL_INFO, "[TEST] thread termination collaboration controller start\n");
    KiRunJoinBeforeTerminationScenario();
    KiRunLateJoinScenario();
    KiRunTimeoutJoinScenario();
    KiRunConsumedJoinRejectionScenario();
    KiRunJoinClaimConflictScenario();
    KiRunSelfJoinScenario();
    KiRunDetachedThreadLifecycleScenario();
    klog(KLOG_LEVEL_INFO, "[TEST] thread termination collaboration demo passed\n");
}

static void
ThreadDemoControlledWorkerThread(void *arg)
{
    KI_THREAD_DEMO_WORKER_CONTEXT *context = (KI_THREAD_DEMO_WORKER_CONTEXT *)arg;

    if (context->ReadySemaphore != NULL)
    {
        HO_STATUS status = KeReleaseSemaphore(context->ReadySemaphore, 1);
        HO_KASSERT(status == EC_SUCCESS, status);
    }

    if (context->StartEvent != NULL)
    {
        HO_STATUS status = KeWaitForSingleObject(context->StartEvent, KE_WAIT_INFINITE);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "Thread demo worker failed waiting for start event");
    }

    if (context->DelayNs != 0)
    {
        KeSleep(context->DelayNs);
    }

    if (context->ExitSemaphore != NULL)
    {
        HO_STATUS status = KeReleaseSemaphore(context->ExitSemaphore, 1);
        HO_KASSERT(status == EC_SUCCESS, status);
    }
}

static void
ThreadDemoJoinerThread(void *arg)
{
    KI_THREAD_DEMO_JOINER_CONTEXT *context = (KI_THREAD_DEMO_JOINER_CONTEXT *)arg;

    if (context->ReadySemaphore != NULL)
    {
        HO_STATUS status = KeReleaseSemaphore(context->ReadySemaphore, 1);
        HO_KASSERT(status == EC_SUCCESS, status);
    }

    context->JoinStatus = KeThreadJoin(context->TargetThread, context->TimeoutNs);

    if (context->DoneSemaphore != NULL)
    {
        HO_STATUS status = KeReleaseSemaphore(context->DoneSemaphore, 1);
        HO_KASSERT(status == EC_SUCCESS, status);
    }
}

static void
ThreadDemoSelfJoinThread(void *arg)
{
    KI_THREAD_DEMO_STATUS_CONTEXT *context = (KI_THREAD_DEMO_STATUS_CONTEXT *)arg;

    context->Status = KeThreadJoin(KeGetCurrentThread(), THREAD_DEMO_JOIN_TIMEOUT_NS);

    HO_STATUS status = KeReleaseSemaphore(context->DoneSemaphore, 1);
    HO_KASSERT(status == EC_SUCCESS, status);
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
