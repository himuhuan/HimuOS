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

    HO_STATUS status = KeCreateProcessAddressSpace(&process->AddressSpace);
    if (status != EC_SUCCESS)
    {
        kfree(process);
        return status;
    }

    status = KeUserBootstrapCreateStaging(&keParams, &process->AddressSpace, &staging);
    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        kfree(process);
        return destroyStatus == EC_SUCCESS ? status : destroyStatus;
    }

    process->Staging = staging;
    *outProcess = process;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapDestroyProcess(EX_PROCESS *process)
{
    HO_STATUS status = EC_SUCCESS;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (process == gExBootstrapProcess)
        return EC_INVALID_STATE;

    if (process->Staging != NULL)
    {
        status = KeUserBootstrapDestroyStaging(process->Staging);

        /* Destroy consumes the staging object even when teardown reports an error. */
        process->Staging = NULL;
    }

    if (process->AddressSpace.Initialized)
    {
        HO_STATUS addrStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        if (addrStatus != EC_SUCCESS && status == EC_SUCCESS)
        {
            status = addrStatus;
        }
    }

    kfree(process);
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

    HO_STATUS status = KeThreadCreate(&kernelThread, (KTHREAD_ENTRY)params->EntryPoint, params->EntryArg);
    if (status != EC_SUCCESS)
    {
        kfree(thread);
        return status;
    }

    status = KeUserBootstrapAttachThread(kernelThread, process->Staging);
    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = KiDestroyNewThread(kernelThread);
        kfree(thread);
        return destroyStatus == EC_SUCCESS ? status : destroyStatus;
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
    HO_STATUS firstError = EC_SUCCESS;

    if (thread == NULL || thread->Thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (gExBootstrapThread != thread)
        return EC_INVALID_STATE;

    if (thread->Thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    process = thread->Process;

    if (process != NULL && process->Staging != NULL)
    {
        firstError = KeUserBootstrapDestroyStaging(process->Staging);

        /* Destroy consumes the staging object even when teardown reports an error. */
        process->Staging = NULL;
    }

    if (process != NULL && process->AddressSpace.Initialized)
    {
        HO_STATUS addrStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        if (firstError == EC_SUCCESS)
        {
            firstError = addrStatus;
        }
    }

    HO_STATUS threadStatus = KiDestroyNewThread(thread->Thread);
    if (firstError == EC_SUCCESS)
        firstError = threadStatus;

    gExBootstrapThread = NULL;
    gExBootstrapProcess = NULL;

    thread->Thread = NULL;
    thread->Process = NULL;
    kfree(thread);

    if (process != NULL)
        kfree(process);

    return firstError;
}