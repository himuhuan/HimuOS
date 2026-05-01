/**
 * HimuOperatingSystem
 *
 * File: ex/user_runtime_bridge.c
 * Description: Ex implementation of the Ke user-runtime hook contract.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "runtime_internal.h"

#include <kernel/ex/ex_user_runtime.h>
#include <kernel/ex/user_regression_anchors.h>
#include <kernel/ke/user_runtime_hooks.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_mode.h>
#include <kernel/hodbg.h>

static HO_STATUS KiDestroyUserRuntimeWrapperObjects(KTHREAD *thread);
static HO_STATUS KiValidateUserRuntimeTerminationHandoff(KTHREAD *thread,
                                                         EX_THREAD **outRuntimeThread,
                                                         EX_PROCESS **outProcess);
static const char *KiGetUserRuntimeFaultShortName(uint8_t vectorNumber);
static void KiLogUserRuntimePageFaultBits(uint32_t errorCode);
static void KiLogUserRuntimeFaultEvidence(KTHREAD *thread,
                                          const EX_PROCESS *process,
                                          const KE_USER_RUNTIME_FAULT_CONTEXT *context);
static HO_NORETURN void ExUserRuntimeFaultHook(KTHREAD *thread, const KE_USER_RUNTIME_FAULT_CONTEXT *context);

static HO_NORETURN void
ExUserRuntimeEnterHook(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    HO_STATUS status = ExUserRuntimeWrapThread(thread);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Failed to wrap user-runtime thread in Ex adapter");
    }

    process = ExRuntimeLookupProcessByKernelThread(thread);
    if (process == NULL || !process->AddressSpace.Initialized)
    {
        HO_KPANIC(EC_INVALID_STATE, "User-runtime enter: process root not initialized");
    }

    if (process->AddressSpace.RootPageTablePhys == 0)
    {
        HO_KPANIC(EC_INVALID_STATE, "User-runtime enter: process root missing");
    }

    HO_PHYSICAL_ADDRESS activeRoot = 0;
    status = KeQueryActiveRootPageTable(&activeRoot);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "User-runtime enter: failed to query active root");
    }

    if (activeRoot != process->AddressSpace.RootPageTablePhys)
    {
        HO_KPANIC(EC_INVALID_STATE, "User-runtime enter: dispatch root not installed");
    }

    KeUserModeEnterCurrentThread();
}

static BOOL
ExUserRuntimeOwnsThreadHook(const KTHREAD *thread)
{
    return ExUserRuntimeHasWrapper(thread);
}

static HO_STATUS
ExUserRuntimeResolveRootHook(const KTHREAD *thread, HO_PHYSICAL_ADDRESS *outRootPageTablePhys)
{
    const KE_KERNEL_ADDRESS_SPACE *kernelSpace = KeGetKernelAddressSpace();
    EX_THREAD *runtimeThread = NULL;

    if (thread == NULL || outRootPageTablePhys == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (kernelSpace == NULL || !kernelSpace->Initialized || kernelSpace->RootPageTablePhys == 0)
        return EC_INVALID_STATE;

    *outRootPageTablePhys = kernelSpace->RootPageTablePhys;

    runtimeThread = ExRuntimeLookupThreadByKernelThread(thread);
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
ExUserRuntimeFinalizeThreadHook(KTHREAD *thread)
{
    return ExUserRuntimeFinalizeThread(thread);
}

static void
ExUserRuntimeObserveTimerHook(KTHREAD *thread)
{
    (void)thread;
    KeUserModeObserveCurrentThreadUserTimerPreemption();
}

static HO_NORETURN void
ExUserRuntimeFaultHook(KTHREAD *thread, const KE_USER_RUNTIME_FAULT_CONTEXT *context)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL || context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "User-runtime fault handoff requires thread and context");

    HO_STATUS status = KiValidateUserRuntimeTerminationHandoff(thread, NULL, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "User-runtime fault handoff validation failed");

    KiLogUserRuntimeFaultEvidence(thread, process, context);

    status = ExRuntimeMarkProcessTerminating(process, EX_PROCESS_TERMINATION_REASON_FAULT, context->VectorNumber);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "User-runtime fault termination marking failed");

    status = ExUserRuntimeHandleExit(thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "User-runtime fault exit handoff validation failed");

    KeThreadExit();
}

HO_STATUS
ExUserRuntimeInit(void)
{
    return KeRegisterUserRuntimeHooks(ExUserRuntimeEnterHook, ExUserRuntimeOwnsThreadHook, ExUserRuntimeResolveRootHook,
                                      ExUserRuntimeFinalizeThreadHook, ExUserRuntimeObserveTimerHook,
                                      ExUserRuntimeFaultHook);
}

HO_STATUS
ExUserRuntimeWrapThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    process = ExRuntimeLookupProcessByKernelThread(thread);
    if (process == NULL)
        return EC_INVALID_STATE;

    if (process->Staging == NULL)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

HO_STATUS
ExUserRuntimeFinalizeThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *runtimeThread = NULL;

    runtimeThread = ExRuntimeLookupThreadByKernelThread(thread);
    if (runtimeThread == NULL)
        return EC_SUCCESS;

    process = runtimeThread->Process;
    if (process != NULL)
    {
        HO_STATUS markStatus = ExRuntimeMarkProcessTerminated(process);
        if (markStatus != EC_SUCCESS)
            return markStatus;
    }

    HO_STATUS threadSignalStatus = ExRuntimeSignalThreadCompletion(runtimeThread);
    if (threadSignalStatus != EC_SUCCESS)
        return threadSignalStatus;

    HO_STATUS status = ExRuntimeTeardownProcessPayload(process);
    if (status == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_TEARDOWN_COMPLETE " thread=%u\n", thread->ThreadId);
    }

    HO_STATUS releaseStatus = KiDestroyUserRuntimeWrapperObjects(thread);
    if (status == EC_SUCCESS)
        status = releaseStatus;

    if (status == EC_SUCCESS && process != NULL)
        status = ExRuntimeSignalProcessCompletion(process);

    return status;
}

BOOL
ExUserRuntimeHasWrapper(const KTHREAD *thread)
{
    return ExRuntimeLookupThreadByKernelThread(thread) != NULL;
}

struct KE_USER_MODE_STAGING *
ExUserRuntimeQueryThreadStaging(const KTHREAD *thread)
{
    EX_PROCESS *process = ExRuntimeLookupProcessByKernelThread(thread);

    if (process == NULL)
        return NULL;

    return process->Staging;
}

HO_STATUS
ExUserRuntimeHandleExit(KTHREAD *thread)
{
    return KiValidateUserRuntimeTerminationHandoff(thread, NULL, NULL);
}

static HO_STATUS
KiDestroyUserRuntimeWrapperObjects(KTHREAD *thread)
{
    EX_THREAD *exThread = NULL;
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    ExRuntimeUnpublishThreadByKernelThread(thread, &exThread, &process);

    if (exThread != NULL && process == NULL)
        process = exThread->Process;

    if (process != NULL)
        status = ExHandleCloseAllForTeardown(process);

    if (exThread != NULL)
    {
        HO_STATUS releaseStatus = ExRuntimeReleaseThread(exThread);
        return status == EC_SUCCESS ? releaseStatus : status;
    }

    if (process != NULL)
    {
        HO_STATUS releaseStatus = ExRuntimeReleaseProcess(process);
        return status == EC_SUCCESS ? releaseStatus : status;
    }

    return status;
}

static HO_STATUS
KiValidateUserRuntimeTerminationHandoff(KTHREAD *thread, EX_THREAD **outRuntimeThread, EX_PROCESS **outProcess)
{
    EX_THREAD *runtimeThread = NULL;
    EX_PROCESS *process = NULL;
    KE_USER_MODE_LAYOUT layout = {0};

    if (outRuntimeThread != NULL)
        *outRuntimeThread = NULL;
    if (outProcess != NULL)
        *outProcess = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    runtimeThread = ExRuntimeLookupThreadByKernelThread(thread);
    if (runtimeThread == NULL || runtimeThread->Thread != thread)
        return EC_INVALID_STATE;

    process = runtimeThread->Process;
    if (process == NULL || process->Staging == NULL || !process->AddressSpace.Initialized ||
        process->AddressSpace.RootPageTablePhys == 0)
    {
        return EC_INVALID_STATE;
    }

    HO_STATUS status = KeUserModeQueryCurrentThreadLayout(&layout);
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
KiGetUserRuntimeFaultShortName(uint8_t vectorNumber)
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
KiLogUserRuntimePageFaultBits(uint32_t errorCode)
{
    klog(KLOG_LEVEL_ERROR, "[USERFAULT] PFERR: P=%u W=%u U=%u RSVD=%u I=%u PK=%u SS=%u SGX=%u\n", errorCode & 0x1U,
         (errorCode >> 1) & 0x1U, (errorCode >> 2) & 0x1U, (errorCode >> 3) & 0x1U, (errorCode >> 4) & 0x1U,
         (errorCode >> 5) & 0x1U, (errorCode >> 6) & 0x1U, (errorCode >> 15) & 0x1U);
}

static void
KiLogUserRuntimeFaultEvidence(KTHREAD *thread, const EX_PROCESS *process, const KE_USER_RUNTIME_FAULT_CONTEXT *context)
{
    if (thread == NULL || process == NULL || context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "User-runtime fault evidence requires live runtime state");

    klog(KLOG_LEVEL_ERROR, "[USERFAULT] %s thread=%u program=%u rip=%p\n",
         KiGetUserRuntimeFaultShortName(context->VectorNumber), thread->ThreadId, process->ProgramId,
         (void *)(uint64_t)context->InstructionPointer);

    if (context->HasFaultAddress)
    {
        klog(KLOG_LEVEL_ERROR, "[USERFAULT] CR2=%p PFERR=%p safe=%u\n", (void *)(uint64_t)context->FaultAddress,
             (void *)(uint64_t)context->PageFaultErrorCode, context->IsSafePageFaultContext);
        KiLogUserRuntimePageFaultBits(context->PageFaultErrorCode);
    }
}
