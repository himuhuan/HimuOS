/**
 * HimuOperatingSystem
 *
 * File: ex/thread.c
 * Description: Ex bootstrap thread object lifecycle and KTHREAD binding.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_bootstrap.h>

#include "ex_bootstrap_internal.h"

#include <kernel/ke/kthread.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>

static HO_STATUS KiCreateWaitablePilotHandle(EX_PROCESS *process);
static HO_STATUS KiDestroyThreadObject(EX_OBJECT_HEADER *objectHeader);
static HO_STATUS ExBootstrapDestroyNewKernelThread(KTHREAD *thread);

static void
KiBootstrapWaitableCompanionThreadEntry(void *arg)
{
    (void)arg;
}

HO_STATUS
ExBootstrapCleanupWaitableBacking(EX_WAITABLE_OBJECT *waitObject)
{
    KTHREAD *companionThread = NULL;

    if (waitObject == NULL)
        return EC_ILLEGAL_ARGUMENT;

    companionThread = waitObject->CompanionThread;
    if (companionThread == NULL)
        return waitObject->Dispatcher == NULL ? EC_SUCCESS : EC_INVALID_STATE;

    if (waitObject->Dispatcher == NULL)
        return EC_INVALID_STATE;

    HO_STATUS status = companionThread->State == KTHREAD_STATE_NEW ? ExBootstrapDestroyNewKernelThread(companionThread)
                                                                   : KeThreadDetach(companionThread);
    if (status != EC_SUCCESS)
        return status;

    waitObject->Dispatcher = NULL;
    waitObject->CompanionThread = NULL;
    return EC_SUCCESS;
}

void
ExBootstrapInitializeThreadObject(EX_THREAD *thread)
{
    if (thread == NULL)
        return;

    ExObjectInitializeHeader(&thread->Header, EX_OBJECT_TYPE_THREAD, EX_OBJECT_FLAG_NONE, KiDestroyThreadObject);
    thread->Thread = NULL;
    thread->Process = NULL;
    thread->SelfHandle = EX_HANDLE_INVALID;
    thread->ThreadId = 0;
}

HO_STATUS
ExBootstrapReleaseThread(EX_THREAD *thread)
{
    uint32_t remainingReferences = 0;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    return ExObjectRelease(&thread->Header, EX_OBJECT_TYPE_THREAD, &remainingReferences);
}

static HO_STATUS
KiDestroyThreadObject(EX_OBJECT_HEADER *objectHeader)
{
    EX_THREAD *thread = NULL;
    EX_PROCESS *process = NULL;

    if (objectHeader == NULL || objectHeader->Type != EX_OBJECT_TYPE_THREAD)
        return EC_ILLEGAL_ARGUMENT;

    thread = CONTAINING_RECORD(objectHeader, EX_THREAD, Header);
    process = thread->Process;
    thread->Thread = NULL;
    thread->Process = NULL;
    kfree(thread);

    if (process != NULL)
        return ExBootstrapReleaseProcess(process);

    return EC_SUCCESS;
}

static HO_STATUS
KiCreateWaitablePilotHandle(EX_PROCESS *process)
{
    KTHREAD *companionThread = NULL;
    const EX_HANDLE_RIGHTS waitRights = EX_HANDLE_RIGHT_WAIT | EX_HANDLE_RIGHT_CLOSE;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (process->WaitHandle != EX_HANDLE_INVALID || process->WaitObject.CompanionThread != NULL ||
        process->WaitObject.Dispatcher != NULL)
    {
        return EC_INVALID_STATE;
    }

    HO_STATUS status = KeThreadCreateJoinable(&companionThread, KiBootstrapWaitableCompanionThreadEntry, NULL);
    if (status != EC_SUCCESS)
        return status;

    process->WaitObject.Dispatcher = &companionThread->TerminationCompletion;
    process->WaitObject.CompanionThread = companionThread;

    status = ExHandleInsert(process, &process->WaitObject.Header, waitRights, &process->WaitHandle);
    if (status != EC_SUCCESS)
    {
        HO_STATUS cleanupStatus = ExBootstrapCleanupWaitableBacking(&process->WaitObject);
        return cleanupStatus == EC_SUCCESS ? status : cleanupStatus;
    }

    status = KeThreadStart(companionThread);
    if (status != EC_SUCCESS)
    {
        HO_STATUS closeStatus = ExHandleClose(process, &process->WaitHandle);
        return closeStatus == EC_SUCCESS ? status : closeStatus;
    }

    return EC_SUCCESS;
}

static HO_STATUS
ExBootstrapDestroyNewKernelThread(KTHREAD *thread)
{
    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if ((thread->Flags & KTHREAD_FLAG_IDLE) != 0 || thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    if (thread->StackOwnedByKva)
    {
        HO_STATUS status = KeKvaReleaseRangeHandle(&thread->StackRange);
        if (status != EC_SUCCESS)
            return status;
    }

    KePoolFree(&gKThreadPool, thread);
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapCreateThread(EX_PROCESS **processHandle,
                        const EX_BOOTSTRAP_THREAD_CREATE_PARAMS *params,
                        EX_THREAD **outThread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;
    KTHREAD *kernelThread = NULL;
    const EX_HANDLE_RIGHTS threadSelfRights =
        EX_HANDLE_RIGHT_QUERY | EX_HANDLE_RIGHT_CLOSE | EX_HANDLE_RIGHT_THREAD_SELF;

    if (processHandle == NULL || outThread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outThread = NULL;
    process = *processHandle;

    if (process == NULL || params == NULL || params->EntryPoint == NULL || process->Staging == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if ((params->Flags &
         ~(EX_BOOTSTRAP_THREAD_CREATE_FLAG_SEED_WAIT_OBJECT | EX_BOOTSTRAP_THREAD_CREATE_FLAG_JOINABLE)) != 0)
        return EC_ILLEGAL_ARGUMENT;

    thread = (EX_THREAD *)kzalloc(sizeof(*thread));
    if (thread == NULL)
        return EC_OUT_OF_RESOURCE;

    ExBootstrapInitializeThreadObject(thread);

    HO_STATUS status = (params->Flags & EX_BOOTSTRAP_THREAD_CREATE_FLAG_JOINABLE) != 0
                           ? KeThreadCreateJoinable(&kernelThread, (KTHREAD_ENTRY)params->EntryPoint, params->EntryArg)
                           : KeThreadCreate(&kernelThread, (KTHREAD_ENTRY)params->EntryPoint, params->EntryArg);
    if (status != EC_SUCCESS)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);
        if (releaseStatus != EC_SUCCESS)
            return releaseStatus;

        return status;
    }

    status = KeUserBootstrapAttachThread(kernelThread, process->Staging);
    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = ExBootstrapDestroyNewKernelThread(kernelThread);
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    thread->Thread = kernelThread;
    thread->Process = process;
    thread->ThreadId = kernelThread->ThreadId;

    status = ExHandleInsert(process, &thread->Header, threadSelfRights, &thread->SelfHandle);
    if (status != EC_SUCCESS)
        goto FailThreadRuntimeSetup;

    if ((params->Flags & EX_BOOTSTRAP_THREAD_CREATE_FLAG_SEED_WAIT_OBJECT) != 0)
    {
        status = KiCreateWaitablePilotHandle(process);
        if (status != EC_SUCCESS)
            goto FailThreadRuntimeSetup;
    }

    status = ExBootstrapPatchCapabilitySeed(process, thread);
    if (status != EC_SUCCESS)
        goto FailThreadRuntimeSetup;

    status = ExRuntimePublishThread(process, thread);
    if (status != EC_SUCCESS)
    {
        goto FailThreadRuntimeSetup;
    }

    *outThread = thread;
    *processHandle = NULL;
    return EC_SUCCESS;

FailThreadRuntimeSetup: {
    HO_STATUS detachStatus = KeUserBootstrapDetachThread(kernelThread, process->Staging);
    HO_STATUS destroyStatus = ExBootstrapDestroyNewKernelThread(kernelThread);
    HO_STATUS waitCloseStatus = ExHandleClose(process, &process->WaitHandle);
    HO_STATUS closeStatus = ExHandleClose(process, &thread->SelfHandle);

    thread->Thread = NULL;
    thread->Process = NULL;

    HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);

    if (detachStatus != EC_SUCCESS)
        return detachStatus;

    if (destroyStatus != EC_SUCCESS)
        return destroyStatus;

    if (waitCloseStatus != EC_SUCCESS)
        return waitCloseStatus;

    if (closeStatus != EC_SUCCESS)
        return closeStatus;

    return releaseStatus == EC_SUCCESS ? status : releaseStatus;
}
}

HO_STATUS
ExBootstrapStartThread(EX_THREAD **threadHandle)
{
    EX_THREAD *thread = NULL;

    if (threadHandle == NULL)
        return EC_ILLEGAL_ARGUMENT;

    thread = *threadHandle;

    if (thread == NULL || thread->Thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = KeThreadStart(thread->Thread);
    if (status != EC_SUCCESS)
        return status;

    if (thread->Process != NULL)
        thread->Process->State = EX_PROCESS_STATE_READY;

    *threadHandle = NULL;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapQueryThreadId(const EX_THREAD *thread, uint32_t *outThreadId)
{
    if (thread == NULL || outThreadId == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (thread->Thread == NULL)
        return EC_INVALID_STATE;

    *outThreadId = thread->ThreadId;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapBorrowKernelThread(EX_THREAD *thread, KTHREAD **outThread)
{
    if (thread == NULL || outThread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (thread->Thread == NULL)
        return EC_INVALID_STATE;

    *outThread = thread->Thread;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapTeardownThread(EX_THREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL || thread->Thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (ExRuntimeLookupThreadByKernelThread(thread->Thread) != thread)
        return EC_INVALID_STATE;

    if (thread->Thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    process = ExRuntimeLookupProcessByKernelThread(thread->Thread);

    HO_STATUS firstError = ExBootstrapTeardownProcessPayload(process);

    if (process != NULL)
        process->State = EX_PROCESS_STATE_TERMINATED;

    ExRuntimeUnpublishByKernelThread(thread->Thread, NULL, NULL);

    HO_STATUS threadStatus = ExBootstrapDestroyNewKernelThread(thread->Thread);
    if (firstError == EC_SUCCESS)
        firstError = threadStatus;

    HO_STATUS closeStatus = ExHandleCloseAll(process);
    if (firstError == EC_SUCCESS)
        firstError = closeStatus;

    HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);
    if (firstError == EC_SUCCESS)
        firstError = releaseStatus;

    return firstError;
}
