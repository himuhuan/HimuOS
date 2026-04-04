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
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>

static int64_t KiEncodeCapabilitySyscallStatus(HO_STATUS status);
static int64_t KiRejectCapabilitySyscall(const char *operation,
                                         uint64_t syscallNumber,
                                         EX_PRIVATE_HANDLE handle,
                                         HO_STATUS status);
static int64_t KiHandleCapabilityWrite(EX_PROCESS *process,
                                       EX_PRIVATE_HANDLE handle,
                                       uint64_t userBuffer,
                                       uint64_t length);
static int64_t KiHandleCapabilityClose(EX_PROCESS *process, EX_PRIVATE_HANDLE handle);
static HO_STATUS KiDestroyBootstrapWrapperObjects(void);

static int64_t
KiEncodeCapabilitySyscallStatus(HO_STATUS status)
{
    return status == EC_SUCCESS ? 0 : -(int64_t)status;
}

static int64_t
KiRejectCapabilitySyscall(const char *operation,
                         uint64_t syscallNumber,
                         EX_PRIVATE_HANDLE handle,
                         HO_STATUS status)
{
    KTHREAD *thread = KeGetCurrentThread();

    klog(KLOG_LEVEL_WARNING,
         KE_USER_BOOTSTRAP_LOG_CAP_REJECTED " op=%s nr=%lu handle=%u thread=%u status=%s (%d)\n",
         operation,
         (unsigned long)syscallNumber,
         handle,
         thread ? thread->ThreadId : 0U,
         KrGetStatusMessage(status),
         status);

    return KiEncodeCapabilitySyscallStatus(status);
}

static int64_t
KiHandleCapabilityWrite(EX_PROCESS *process, EX_PRIVATE_HANDLE handle, uint64_t userBuffer, uint64_t length)
{
    EX_OBJECT_HEADER *objectHeader = NULL;
    KTHREAD *thread = KeGetCurrentThread();
    uint64_t written = 0;
    char scratch[KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH];

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, EC_INVALID_STATE);

    if (length > KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH)
    {
        klog(KLOG_LEVEL_WARNING,
             KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " cap=write addr=%p len=%lu exceeds=%u\n",
             (void *)(uint64_t)userBuffer,
             (unsigned long)length,
             KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH);
        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, EC_ILLEGAL_ARGUMENT);
    }

    HO_STATUS status = ExBootstrapResolvePrivateHandle(process,
                                                       handle,
                                                       EX_OBJECT_TYPE_STDOUT_SERVICE,
                                                       EX_PRIVATE_HANDLE_RIGHT_WRITE,
                                                       &objectHeader);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, status);

    if (length != 0)
    {
        status = KeUserBootstrapCopyInBytes(scratch, (HO_VIRTUAL_ADDRESS)userBuffer, length);
        if (status == EC_SUCCESS)
            status = KeUserBootstrapWriteConsoleBytes(scratch, length, &written);
    }

    HO_STATUS releaseStatus = ExBootstrapReleaseResolvedObject(objectHeader);
    if (status == EC_SUCCESS && releaseStatus != EC_SUCCESS)
        status = releaseStatus;

    if (status != EC_SUCCESS)
    {
        if (status == EC_FAILURE)
        {
            klog(KLOG_LEVEL_ERROR,
                 "[USERCAP] SYS_WRITE console emit failed index=%lu thread=%u handle=%u\n",
                 (unsigned long)written,
                 thread ? thread->ThreadId : 0U,
                 handle);
            return KiEncodeCapabilitySyscallStatus(status);
        }

        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, status);
    }

    klog(KLOG_LEVEL_INFO,
         KE_USER_BOOTSTRAP_LOG_CAP_WRITE_SUCCEEDED " bytes=%lu thread=%u handle=%u\n",
         (unsigned long)written,
         thread ? thread->ThreadId : 0U,
         handle);

    return (int64_t)written;
}

static int64_t
KiHandleCapabilityClose(EX_PROCESS *process, EX_PRIVATE_HANDLE handle)
{
    KTHREAD *thread = KeGetCurrentThread();

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_CLOSE", SYS_CLOSE, handle, EC_INVALID_STATE);

    if (handle == EX_PRIVATE_HANDLE_INVALID)
        return KiRejectCapabilitySyscall("SYS_CLOSE", SYS_CLOSE, handle, EC_ILLEGAL_ARGUMENT);

    EX_PRIVATE_HANDLE localHandle = handle;
    HO_STATUS status = ExBootstrapClosePrivateHandle(process, &localHandle);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_CLOSE", SYS_CLOSE, handle, status);

    klog(KLOG_LEVEL_INFO,
         KE_USER_BOOTSTRAP_LOG_CAP_CLOSE_SUCCEEDED " handle=%u thread=%u\n",
         handle,
         thread ? thread->ThreadId : 0U);

    return 0;
}

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

HO_KERNEL_API HO_NODISCARD int64_t
ExBootstrapAdapterDispatchCapabilitySyscall(uint64_t syscallNumber,
                                            uint64_t arg0,
                                            uint64_t arg1,
                                            uint64_t arg2)
{
    KTHREAD *thread = KeGetCurrentThread();
    EX_PROCESS *process = ExBootstrapLookupRuntimeProcess(thread);

    if (thread == NULL || process == NULL || process->Staging == NULL)
    {
        klog(KLOG_LEVEL_ERROR,
             KE_USER_BOOTSTRAP_LOG_INVALID_CAP_SYSCALL " nr=%lu thread=%u missing bootstrap staging\n",
             (unsigned long)syscallNumber,
             thread ? thread->ThreadId : 0U);
        return KiEncodeCapabilitySyscallStatus(EC_INVALID_STATE);
    }

    switch (syscallNumber)
    {
    case SYS_WRITE:
        return KiHandleCapabilityWrite(process, (EX_PRIVATE_HANDLE)arg0, arg1, arg2);
    case SYS_CLOSE:
        return KiHandleCapabilityClose(process, (EX_PRIVATE_HANDLE)arg0);
    default:
        klog(KLOG_LEVEL_WARNING,
             KE_USER_BOOTSTRAP_LOG_INVALID_CAP_SYSCALL " nr=%lu thread=%u args=(%p,%p,%p)\n",
             (unsigned long)syscallNumber,
             thread->ThreadId,
             (void *)(uint64_t)arg0,
             (void *)(uint64_t)arg1,
             (void *)(uint64_t)arg2);
        return KiEncodeCapabilitySyscallStatus(EC_NOT_SUPPORTED);
    }
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
