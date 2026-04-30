/**
 * HimuOperatingSystem
 *
 * File: ex/bootstrap_compat.c
 * Description: Temporary Ex bootstrap callback compatibility bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ex/user_regression_anchors.h>
#include <kernel/ke/bootstrap_callbacks.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>

static HO_STATUS KiDestroyBootstrapWrapperObjects(KTHREAD *thread);
static HO_STATUS KiValidateBootstrapTerminationHandoff(KTHREAD *thread,
                                                       EX_THREAD **outRuntimeThread,
                                                       EX_PROCESS **outProcess);
static const char *KiGetBootstrapUserExceptionShortName(uint8_t vectorNumber);
static void KiLogBootstrapPageFaultBits(uint32_t errorCode);
static void KiLogBootstrapUserExceptionEvidence(KTHREAD *thread,
                                                const EX_PROCESS *process,
                                                const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context);
static HO_NORETURN void ExBootstrapUserExceptionCallback(KTHREAD *thread,
                                                         const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context);

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
    return ExBootstrapAdapterFinalizeThread(thread);
}

static void
ExBootstrapTimerObserveCallback(KTHREAD *thread)
{
    (void)thread;
    KeUserBootstrapObserveCurrentThreadUserTimerPreemption();
}

static HO_NORETURN void
ExBootstrapUserExceptionCallback(KTHREAD *thread, const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL || context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Bootstrap user-fault handoff requires thread and context");

    HO_STATUS status = KiValidateBootstrapTerminationHandoff(thread, NULL, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Bootstrap user-fault handoff validation failed");

    KiLogBootstrapUserExceptionEvidence(thread, process, context);

    status = ExBootstrapAdapterHandleExit(thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Bootstrap user-fault exit handoff validation failed");

    KeThreadExit();
}

HO_STATUS
ExBootstrapAdapterInit(void)
{
    return KeRegisterBootstrapCallbacks(ExBootstrapEnterCallback, ExBootstrapThreadOwnershipQueryCallback,
                                        ExBootstrapThreadRootQueryCallback, ExBootstrapFinalizeCallback,
                                        ExBootstrapTimerObserveCallback, ExBootstrapUserExceptionCallback);
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
    if (process != NULL)
        process->State = EX_PROCESS_STATE_TERMINATED;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);
    if (status == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_TEARDOWN_COMPLETE " thread=%u\n", thread->ThreadId);
    }

    HO_STATUS releaseStatus = KiDestroyBootstrapWrapperObjects(thread);
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
ExBootstrapAdapterHandleExit(KTHREAD *thread)
{
    return KiValidateBootstrapTerminationHandoff(thread, NULL, NULL);
}

static HO_STATUS
KiDestroyBootstrapWrapperObjects(KTHREAD *thread)
{
    EX_THREAD *exThread = NULL;
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    ExBootstrapUnpublishRuntimeAlias(thread, &exThread, &process);

    if (exThread != NULL && process == NULL)
        process = exThread->Process;

    if (process != NULL)
        status = ExHandleCloseAll(process);

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

static HO_STATUS
KiValidateBootstrapTerminationHandoff(KTHREAD *thread, EX_THREAD **outRuntimeThread, EX_PROCESS **outProcess)
{
    EX_THREAD *runtimeThread = NULL;
    EX_PROCESS *process = NULL;
    KE_USER_BOOTSTRAP_LAYOUT layout = {0};

    if (outRuntimeThread != NULL)
        *outRuntimeThread = NULL;
    if (outProcess != NULL)
        *outProcess = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread == NULL || runtimeThread->Thread != thread)
        return EC_INVALID_STATE;

    process = runtimeThread->Process;
    if (process == NULL || process->Staging == NULL || !process->AddressSpace.Initialized ||
        process->AddressSpace.RootPageTablePhys == 0)
    {
        return EC_INVALID_STATE;
    }

    HO_STATUS status = KeUserBootstrapQueryCurrentThreadLayout(&layout);
    if (status != EC_SUCCESS)
        return status;

    if (layout.OwnerRootPageTablePhys == 0 || layout.OwnerRootPageTablePhys != process->AddressSpace.RootPageTablePhys)
        return EC_INVALID_STATE;

    if (outRuntimeThread != NULL)
        *outRuntimeThread = runtimeThread;
    if (outProcess != NULL)
        *outProcess = process;

    return EC_SUCCESS;
}

static const char *
KiGetBootstrapUserExceptionShortName(uint8_t vectorNumber)
{
    switch (vectorNumber)
    {
    case 0U:
        return "#DE";
    case 14U:
        return "#PF";
    default:
        return "#??";
    }
}

static void
KiLogBootstrapPageFaultBits(uint32_t errorCode)
{
    klog(KLOG_LEVEL_ERROR, "[USERFAULT] PFERR: P=%u W=%u U=%u RSVD=%u I=%u PK=%u SS=%u SGX=%u\n", errorCode & 0x1U,
         (errorCode >> 1) & 0x1U, (errorCode >> 2) & 0x1U, (errorCode >> 3) & 0x1U, (errorCode >> 4) & 0x1U,
         (errorCode >> 5) & 0x1U, (errorCode >> 6) & 0x1U, (errorCode >> 15) & 0x1U);
}

static void
KiLogBootstrapUserExceptionEvidence(KTHREAD *thread,
                                    const EX_PROCESS *process,
                                    const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context)
{
    if (thread == NULL || process == NULL || context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Bootstrap user-fault evidence requires live runtime state");

    klog(KLOG_LEVEL_ERROR, "[USERFAULT] %s thread=%u program=%u rip=%p\n",
         KiGetBootstrapUserExceptionShortName(context->VectorNumber), thread->ThreadId, process->ProgramId,
         (void *)(uint64_t)context->InstructionPointer);

    if (context->HasFaultAddress)
    {
        klog(KLOG_LEVEL_ERROR, "[USERFAULT] CR2=%p PFERR=%p safe=%u\n", (void *)(uint64_t)context->FaultAddress,
             (void *)(uint64_t)context->PageFaultErrorCode, context->IsSafePageFaultContext);
        KiLogBootstrapPageFaultBits(context->PageFaultErrorCode);
    }
}
