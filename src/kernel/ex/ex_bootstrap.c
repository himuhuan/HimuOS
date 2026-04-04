/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap.c
 * Description: Ex-owned bootstrap runtime init and launch facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_bootstrap.h>

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>

EX_PROCESS *gExBootstrapProcess = NULL;
EX_THREAD *gExBootstrapThread = NULL;

static void
KiInitializeObjectHeader(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE type)
{
    if (header == NULL)
        return;

    header->Type = type;
    header->ReferenceCount = 1;
}

static HO_STATUS
KiRetainObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType)
{
    if (header == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount++;
    return EC_SUCCESS;
}

static HO_STATUS
KiReleaseObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType, uint32_t *remainingReferences)
{
    if (header == NULL || remainingReferences == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount--;
    *remainingReferences = header->ReferenceCount;
    return EC_SUCCESS;
}

static HO_STATUS
KiRestoreImportedRootForProcessTeardown(const EX_PROCESS *process)
{
    HO_PHYSICAL_ADDRESS activeRoot = 0;

    if (process == NULL || !process->AddressSpace.Initialized)
        return EC_SUCCESS;

    if (KeQueryActiveRootPageTable(&activeRoot) != EC_SUCCESS ||
        activeRoot != process->AddressSpace.RootPageTablePhys)
    {
        return EC_SUCCESS;
    }

    const KE_KERNEL_ADDRESS_SPACE *kernelSpace = KeGetKernelAddressSpace();
    if (kernelSpace == NULL || !kernelSpace->Initialized || kernelSpace->RootPageTablePhys == 0)
        return EC_INVALID_STATE;

    return KeSwitchAddressSpace(kernelSpace->RootPageTablePhys);
}

void
ExBootstrapInitializeProcessObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    KiInitializeObjectHeader(&process->Header, EX_OBJECT_TYPE_PROCESS);
}

void
ExBootstrapInitializeThreadObject(EX_THREAD *thread)
{
    if (thread == NULL)
        return;

    KiInitializeObjectHeader(&thread->Header, EX_OBJECT_TYPE_THREAD);
}

HO_STATUS
ExBootstrapTeardownProcessPayload(EX_PROCESS *process)
{
    HO_STATUS status = KiRestoreImportedRootForProcessTeardown(process);

    if (process == NULL)
        return status;

    if (process->Staging != NULL)
    {
        HO_STATUS stagingStatus = KeUserBootstrapDestroyStaging(process->Staging);

        /* Destroy consumes the staging object even when teardown reports an error. */
        process->Staging = NULL;

        if (status == EC_SUCCESS)
            status = stagingStatus;
    }

    if (process->AddressSpace.Initialized)
    {
        HO_STATUS addrStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        if (status == EC_SUCCESS)
            status = addrStatus;
    }

    return status;
}

EX_PROCESS *
ExBootstrapRetainProcess(EX_PROCESS *process)
{
    if (process == NULL)
        return NULL;

    if (KiRetainObject(&process->Header, EX_OBJECT_TYPE_PROCESS) != EC_SUCCESS)
        return NULL;

    return process;
}

HO_STATUS
ExBootstrapReleaseProcess(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;
    HO_STATUS status = EC_SUCCESS;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    status = KiReleaseObject(&process->Header, EX_OBJECT_TYPE_PROCESS, &remainingReferences);
    if (status != EC_SUCCESS || remainingReferences != 0)
        return status;

    kfree(process);
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapReleaseThread(EX_THREAD *thread)
{
    EX_PROCESS *process = NULL;
    uint32_t remainingReferences = 0;
    HO_STATUS status = EC_SUCCESS;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    status = KiReleaseObject(&thread->Header, EX_OBJECT_TYPE_THREAD, &remainingReferences);
    if (status != EC_SUCCESS || remainingReferences != 0)
        return status;

    process = thread->Process;
    thread->Thread = NULL;
    thread->Process = NULL;
    kfree(thread);

    if (process != NULL)
        return ExBootstrapReleaseProcess(process);

    return EC_SUCCESS;
}

static HO_STATUS
KiDestroyNewThread(KTHREAD *thread)
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
ExBootstrapInit(void)
{
    HO_STATUS status = KeUserBootstrapRawSyscallInit();
    if (status != EC_SUCCESS)
        return status;

    return ExBootstrapAdapterInit();
}

HO_STATUS
ExBootstrapCreateProcess(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params, EX_PROCESS **outProcess)
{
    KE_USER_BOOTSTRAP_STAGING *staging = NULL;
    EX_PROCESS *process = NULL;
    KE_USER_BOOTSTRAP_CREATE_PARAMS keParams = {0};

    if (outProcess == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outProcess = NULL;

    if (params == NULL)
        return EC_ILLEGAL_ARGUMENT;

    keParams.CodeBytes = params->CodeBytes;
    keParams.CodeLength = params->CodeLength;
    keParams.EntryOffset = params->EntryOffset;
    keParams.ConstBytes = params->ConstBytes;
    keParams.ConstLength = params->ConstLength;

    process = (EX_PROCESS *)kzalloc(sizeof(*process));
    if (process == NULL)
        return EC_OUT_OF_RESOURCE;

    ExBootstrapInitializeProcessObject(process);

    HO_STATUS status = KeCreateProcessAddressSpace(&process->AddressSpace);
    if (status != EC_SUCCESS)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
        if (releaseStatus != EC_SUCCESS)
            return releaseStatus;

        return status;
    }

    status = KeUserBootstrapCreateStaging(&keParams, &process->AddressSpace, &staging);
    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    process->Staging = staging;
    *outProcess = process;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapDestroyProcess(EX_PROCESS *process)
{
    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (process == gExBootstrapProcess)
        return EC_INVALID_STATE;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);

    HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
    if (status == EC_SUCCESS)
        status = releaseStatus;

    return status;
}

HO_STATUS
ExBootstrapCreateThread(EX_PROCESS **processHandle,
                        const EX_BOOTSTRAP_THREAD_CREATE_PARAMS *params,
                        EX_THREAD **outThread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;
    KTHREAD *kernelThread = NULL;

    if (processHandle == NULL || outThread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outThread = NULL;
    process = *processHandle;

    if (process == NULL || params == NULL || params->EntryPoint == NULL || process->Staging == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (gExBootstrapProcess != NULL || gExBootstrapThread != NULL)
        return EC_INVALID_STATE;

    thread = (EX_THREAD *)kzalloc(sizeof(*thread));
    if (thread == NULL)
        return EC_OUT_OF_RESOURCE;

    ExBootstrapInitializeThreadObject(thread);

    HO_STATUS status = KeThreadCreate(&kernelThread, (KTHREAD_ENTRY)params->EntryPoint, params->EntryArg);
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
        HO_STATUS destroyStatus = KiDestroyNewThread(kernelThread);
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    thread->Thread = kernelThread;
    thread->Process = process;

    gExBootstrapProcess = process;
    gExBootstrapThread = thread;

    *outThread = thread;
    *processHandle = NULL;
    return EC_SUCCESS;
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

    *threadHandle = NULL;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapTeardownThread(EX_THREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL || thread->Thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (gExBootstrapThread != thread)
        return EC_INVALID_STATE;

    if (thread->Thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    process = thread->Process;

    HO_STATUS firstError = ExBootstrapTeardownProcessPayload(process);

    HO_STATUS threadStatus = KiDestroyNewThread(thread->Thread);
    if (firstError == EC_SUCCESS)
        firstError = threadStatus;

    gExBootstrapThread = NULL;
    gExBootstrapProcess = NULL;

    HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);
    if (firstError == EC_SUCCESS)
        firstError = releaseStatus;

    return firstError;
}