/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.c
 * Description: Thin Ex bootstrap adapter callback bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ke/bootstrap_callbacks.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>

static HO_STATUS KiDestroyBootstrapWrapperObjects(void);

static HO_NORETURN void
ExBootstrapEnterCallback(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    HO_STATUS status = ExBootstrapAdapterWrapThread(thread);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Failed to wrap bootstrap thread in Ex adapter");
    }

    process = ExBootstrapLookupRuntimeProcess(thread);
    if (process == NULL || !process->AddressSpace.Initialized)
    {
        HO_KPANIC(EC_INVALID_STATE, "Bootstrap enter: process root not initialized");
    }

    if (process->AddressSpace.RootPageTablePhys == 0)
    {
        HO_KPANIC(EC_INVALID_STATE, "Bootstrap enter: process root missing");
    }

    HO_PHYSICAL_ADDRESS activeRoot = 0;
    status = KeQueryActiveRootPageTable(&activeRoot);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Bootstrap enter: failed to query active root");
    }

    if (activeRoot != process->AddressSpace.RootPageTablePhys)
    {
        HO_KPANIC(EC_INVALID_STATE, "Bootstrap enter: dispatch root not installed");
    }

    KeUserBootstrapEnterCurrentThread();
}

static BOOL
ExBootstrapThreadOwnershipQueryCallback(const KTHREAD *thread)
{
    return ExBootstrapAdapterHasWrapper(thread);
}

static HO_STATUS
ExBootstrapThreadRootQueryCallback(const KTHREAD *thread, HO_PHYSICAL_ADDRESS *outRootPageTablePhys)
{
    const KE_KERNEL_ADDRESS_SPACE *kernelSpace = KeGetKernelAddressSpace();
    EX_THREAD *runtimeThread = NULL;

    if (thread == NULL || outRootPageTablePhys == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (kernelSpace == NULL || !kernelSpace->Initialized || kernelSpace->RootPageTablePhys == 0)
        return EC_INVALID_STATE;

    *outRootPageTablePhys = kernelSpace->RootPageTablePhys;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread != NULL)
    {
        EX_PROCESS *process = runtimeThread->Process;

        if (process == NULL || !process->AddressSpace.Initialized || process->AddressSpace.RootPageTablePhys == 0)
            return EC_INVALID_STATE;

        *outRootPageTablePhys = process->AddressSpace.RootPageTablePhys;
    }

    return EC_SUCCESS;
}

static HO_STATUS
ExBootstrapFinalizeCallback(KTHREAD *thread)
{
    if (thread != NULL && ExBootstrapAdapterQueryThreadStaging(thread) != NULL)
    {
        klog(KLOG_LEVEL_WARNING, "[USERBOOT] fallback staging reclaim in finalizer thread=%u\n", thread->ThreadId);
    }

    return ExBootstrapAdapterFinalizeThread(thread);
}

static void
ExBootstrapTimerObserveCallback(KTHREAD *thread)
{
    (void)thread;
    KeUserBootstrapObserveCurrentThreadUserTimerPreemption();
}

HO_STATUS
ExBootstrapAdapterInit(void)
{
    return KeRegisterBootstrapCallbacks(ExBootstrapEnterCallback,
                                        ExBootstrapThreadOwnershipQueryCallback,
                                        ExBootstrapThreadRootQueryCallback,
                                        ExBootstrapFinalizeCallback,
                                        ExBootstrapTimerObserveCallback);
}

HO_STATUS
ExBootstrapAdapterWrapThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    process = ExBootstrapLookupRuntimeProcess(thread);
    if (process == NULL)
        return EC_INVALID_STATE;

    if (process->Staging == NULL)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapAdapterFinalizeThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *runtimeThread = NULL;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread == NULL)
        return EC_SUCCESS;

    process = runtimeThread->Process;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);

    HO_STATUS releaseStatus = KiDestroyBootstrapWrapperObjects();
    if (status == EC_SUCCESS)
        status = releaseStatus;

    return status;
}

BOOL
ExBootstrapAdapterHasWrapper(const KTHREAD *thread)
{
    return ExBootstrapLookupRuntimeThread(thread) != NULL;
}

struct KE_USER_BOOTSTRAP_STAGING *
ExBootstrapAdapterQueryThreadStaging(const KTHREAD *thread)
{
    EX_PROCESS *process = ExBootstrapLookupRuntimeProcess(thread);

    if (process == NULL)
        return NULL;

    return process->Staging;
}

HO_STATUS
ExBootstrapAdapterHandleRawExit(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *runtimeThread = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread == NULL)
        return EC_INVALID_STATE;

    process = runtimeThread->Process;
    if (process == NULL || process->Staging == NULL)
        return EC_INVALID_STATE;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);
    if (status != EC_SUCCESS)
        (void)KiDestroyBootstrapWrapperObjects();

    return status;
}

static HO_STATUS
KiDestroyBootstrapWrapperObjects(void)
{
    EX_THREAD *exThread = NULL;
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    ExBootstrapUnpublishRuntimeAlias(&exThread, &process);

    if (exThread != NULL && process == NULL)
        process = exThread->Process;

    if (process != NULL)
        status = ExBootstrapCloseAllPrivateHandles(process);

    if (exThread != NULL)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(exThread);
        return status == EC_SUCCESS ? releaseStatus : status;
    }

    if (process != NULL)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
        return status == EC_SUCCESS ? releaseStatus : status;
    }

    return status;
}
