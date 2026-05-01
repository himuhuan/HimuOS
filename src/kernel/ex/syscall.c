/**
 * HimuOperatingSystem
 *
 * File: ex/syscall.c
 * Description: Ex bootstrap syscall dispatch.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ex/ex_syscall.h>
#include <kernel/ex/program.h>
#include <kernel/ex/user_bringup_sentinel_abi.h>
#include <kernel/ex/user_regression_anchors.h>
#include <kernel/ex/user_syscall_abi.h>
#include <kernel/hodbg.h>
#include <kernel/ke/input.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>

typedef char KI_INPUT_LINE_CAPACITY_MATCHES_USER_ABI[(KE_INPUT_LINE_CAPACITY == EX_USER_READLINE_MAX_LENGTH) ? 1 : -1];

static int64_t KiEncodeSyscallStatus(HO_STATUS status);
static void KiSetReturnResult(EX_SYSCALL_DISPATCH_RESULT *result, int64_t returnValue);
static void KiSetExitResult(EX_SYSCALL_DISPATCH_RESULT *result);
static HO_NORETURN void KiAbortBootstrapExit(
    KTHREAD *thread, uint64_t exitCode, HO_STATUS status, const char *exitKind, const char *reason);
static HO_STATUS KiPrepareBootstrapExit(uint64_t exitCode,
                                        const char *successLog,
                                        const char *exitKind,
                                        EX_SYSCALL_DISPATCH_RESULT *result);
static HO_STATUS KiPrepareBootstrapKillExit(uint32_t programId, EX_SYSCALL_DISPATCH_RESULT *result);
static int64_t KiRejectRawWrite(uint64_t userBuffer, uint64_t length, HO_STATUS status);
static int64_t KiHandleRawWrite(uint64_t userBuffer, uint64_t length);
static HO_STATUS KiDispatchRawSyscall(const EX_SYSCALL_ARGUMENTS *args, EX_SYSCALL_DISPATCH_RESULT *result);
static int64_t KiRejectCapabilitySyscall(const char *operation,
                                         uint64_t syscallNumber,
                                         EX_HANDLE handle,
                                         HO_STATUS status);
static int64_t KiHandleCapabilityWrite(EX_PROCESS *process, EX_HANDLE handle, uint64_t userBuffer, uint64_t length);
static int64_t KiHandleCapabilityClose(EX_PROCESS *process, EX_HANDLE handle);
static HO_STATUS KiDecodeCapabilityWaitTimeoutNs(uint64_t timeoutMsRaw, uint64_t reserved, uint64_t *outTimeoutNs);
static int64_t KiHandleCapabilityWaitOne(EX_PROCESS *process,
                                         EX_HANDLE handle,
                                         uint64_t timeoutMsRaw,
                                         uint64_t reserved);
static int64_t KiDispatchCapabilitySyscall(uint64_t syscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2);
static int64_t KiHandleReadLine(uint64_t userBuffer, uint64_t capacity, uint64_t reserved);
static int64_t KiHandleSpawnProgram(uint64_t userName, uint64_t nameLength, uint64_t flags);
static int64_t KiHandleWaitPid(uint64_t pid, uint64_t reserved0, uint64_t reserved1);
static int64_t KiHandleSleepMs(uint64_t milliseconds, uint64_t reserved0, uint64_t reserved1);
static int64_t KiHandleKillPid(uint64_t pid, uint64_t reserved0, uint64_t reserved1);
static HO_STATUS KiDispatchFormalSyscall(const EX_SYSCALL_ARGUMENTS *args, EX_SYSCALL_DISPATCH_RESULT *result);
static HO_STATUS KiObserveKillRequest(EX_SYSCALL_DISPATCH_RESULT *result);

static int64_t
KiEncodeSyscallStatus(HO_STATUS status)
{
    return status == EC_SUCCESS ? 0 : -(int64_t)status;
}

static void
KiSetReturnResult(EX_SYSCALL_DISPATCH_RESULT *result, int64_t returnValue)
{
    result->Disposition = EX_SYSCALL_DISPOSITION_RETURN_TO_USER;
    result->ReturnValue = returnValue;
}

static void
KiSetExitResult(EX_SYSCALL_DISPATCH_RESULT *result)
{
    result->Disposition = EX_SYSCALL_DISPOSITION_EXIT_CURRENT_THREAD;
    result->ReturnValue = 0;
}

static HO_NORETURN void
KiAbortBootstrapExit(KTHREAD *thread, uint64_t exitCode, HO_STATUS status, const char *exitKind, const char *reason)
{
    klog(KLOG_LEVEL_ERROR,
         EX_USER_REGRESSION_LOG_TEARDOWN_FAILED " exit=%s unrecoverable thread=%u code=%lu reason=%s status=%s (%d)\n",
         exitKind, thread ? thread->ThreadId : 0U, (unsigned long)exitCode, reason, KrGetStatusMessage(status), status);
    HO_KPANIC(status, reason);
}

static HO_STATUS
KiPrepareBootstrapExit(uint64_t exitCode,
                       const char *successLog,
                       const char *exitKind,
                       EX_SYSCALL_DISPATCH_RESULT *result)
{
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread || ExBootstrapAdapterQueryThreadStaging(thread) == NULL)
        KiAbortBootstrapExit(thread, exitCode, EC_INVALID_STATE, exitKind, "Bootstrap exit missing staging");

    HO_STATUS status = ExRuntimeMarkCurrentProcessTerminating(EX_PROCESS_TERMINATION_REASON_EXIT, (uint64_t)exitCode);
    if (status != EC_SUCCESS)
        KiAbortBootstrapExit(thread, exitCode, status, exitKind, "Failed to mark process exit");

    klog(KLOG_LEVEL_INFO, "%s code=%lu thread=%u\n", successLog, (unsigned long)exitCode, thread->ThreadId);

    status = ExBootstrapAdapterHandleExit(thread);
    if (status != EC_SUCCESS)
        KiAbortBootstrapExit(thread, exitCode, status, exitKind,
                             "Bootstrap exit handoff validation failed after no-return transition");

    KiSetExitResult(result);
    return EC_SUCCESS;
}

static HO_STATUS
KiPrepareBootstrapKillExit(uint32_t programId, EX_SYSCALL_DISPATCH_RESULT *result)
{
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread || ExBootstrapAdapterQueryThreadStaging(thread) == NULL)
        KiAbortBootstrapExit(thread, 0, EC_INVALID_STATE, "kill", "Bootstrap kill exit missing staging");

    HO_STATUS status = ExRuntimeMarkCurrentProcessTerminating(EX_PROCESS_TERMINATION_REASON_KILL, 0);
    if (status != EC_SUCCESS)
        KiAbortBootstrapExit(thread, 0, status, "kill", "Failed to mark process kill exit");

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_KILL_EXIT " thread=%u program=%u\n", thread->ThreadId, programId);

    status = ExBootstrapAdapterHandleExit(thread);
    if (status != EC_SUCCESS)
        KiAbortBootstrapExit(thread, 0, status, "kill", "Bootstrap kill exit handoff validation failed");

    KiSetExitResult(result);
    return EC_SUCCESS;
}

static int64_t
KiRejectRawWrite(uint64_t userBuffer, uint64_t length, HO_STATUS status)
{
    klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_INVALID_RAW_WRITE " addr=%p len=%lu status=%s (%d)\n",
         (void *)(uint64_t)userBuffer, (unsigned long)length, KrGetStatusMessage(status), status);
    return KiEncodeSyscallStatus(status);
}

static int64_t
KiHandleRawWrite(uint64_t userBuffer, uint64_t length)
{
    if (length > EX_USER_SYSCALL_WRITE_MAX_LENGTH)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " raw=write addr=%p len=%lu exceeds=%u\n",
             (void *)(uint64_t)userBuffer, (unsigned long)length, EX_USER_SYSCALL_WRITE_MAX_LENGTH);
        return KiRejectRawWrite(userBuffer, length, EC_ILLEGAL_ARGUMENT);
    }

    if (length == 0)
        return 0;

    char scratch[EX_USER_SYSCALL_WRITE_MAX_LENGTH];
    HO_STATUS status = KeUserBootstrapCopyInBytes(scratch, (HO_VIRTUAL_ADDRESS)userBuffer, length);
    if (status != EC_SUCCESS)
        return KiRejectRawWrite(userBuffer, length, status);

    KTHREAD *thread = KeGetCurrentThread();
    uint64_t written = 0;
    status = KeUserBootstrapWriteConsoleBytes(scratch, length, &written);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[USERBOOT] SYS_RAW_WRITE console emit failed index=%lu thread=%u\n",
             (unsigned long)written, thread ? thread->ThreadId : 0U);
        return KiEncodeSyscallStatus(status);
    }

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_HELLO_WRITE_SUCCEEDED " bytes=%lu thread=%u\n", (unsigned long)written,
         thread ? thread->ThreadId : 0U);

    return (int64_t)written;
}

static HO_STATUS
KiDispatchRawSyscall(const EX_SYSCALL_ARGUMENTS *args, EX_SYSCALL_DISPATCH_RESULT *result)
{
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread || ExBootstrapAdapterQueryThreadStaging(thread) == NULL)
    {
        if (args->Number == EX_USER_BRINGUP_SYS_RAW_EXIT)
            KiAbortBootstrapExit(thread, args->Arg0, EC_INVALID_STATE, "raw", "Bootstrap raw exit missing staging");

        klog(KLOG_LEVEL_ERROR, EX_USER_REGRESSION_LOG_INVALID_SYSCALL " nr=%lu thread=%u missing bootstrap staging\n",
             (unsigned long)args->Number, thread ? thread->ThreadId : 0U);
        KiSetReturnResult(result, KiEncodeSyscallStatus(EC_INVALID_STATE));
        return EC_SUCCESS;
    }

    switch (args->Number)
    {
    case EX_USER_BRINGUP_SYS_RAW_WRITE:
        KiSetReturnResult(result, KiHandleRawWrite(args->Arg0, args->Arg1));
        return EC_SUCCESS;
    case EX_USER_BRINGUP_SYS_RAW_EXIT:
        return KiPrepareBootstrapExit(args->Arg0, EX_USER_REGRESSION_LOG_SYS_RAW_EXIT, "raw", result);
    default:
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_INVALID_SYSCALL " nr=%lu thread=%u args=(%p,%p,%p)\n",
             (unsigned long)args->Number, thread->ThreadId, (void *)(uint64_t)args->Arg0, (void *)(uint64_t)args->Arg1,
             (void *)(uint64_t)args->Arg2);
        KiSetReturnResult(result, KiEncodeSyscallStatus(EC_NOT_SUPPORTED));
        return EC_SUCCESS;
    }
}

static int64_t
KiRejectCapabilitySyscall(const char *operation, uint64_t syscallNumber, EX_HANDLE handle, HO_STATUS status)
{
    KTHREAD *thread = KeGetCurrentThread();

    klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_CAP_REJECTED " op=%s nr=%lu handle=%u thread=%u status=%s (%d)\n",
         operation, (unsigned long)syscallNumber, handle, thread ? thread->ThreadId : 0U, KrGetStatusMessage(status),
         status);

    return KiEncodeSyscallStatus(status);
}

static int64_t
KiHandleCapabilityWrite(EX_PROCESS *process, EX_HANDLE handle, uint64_t userBuffer, uint64_t length)
{
    EX_OBJECT_HEADER *objectHeader = NULL;
    KTHREAD *thread = KeGetCurrentThread();
    uint64_t written = 0;
    char scratch[EX_USER_SYSCALL_WRITE_MAX_LENGTH];

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_WRITE", EX_USER_SYS_WRITE, handle, EC_INVALID_STATE);

    if (length > EX_USER_SYSCALL_WRITE_MAX_LENGTH)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " cap=write addr=%p len=%lu exceeds=%u\n",
             (void *)(uint64_t)userBuffer, (unsigned long)length, EX_USER_SYSCALL_WRITE_MAX_LENGTH);
        return KiRejectCapabilitySyscall("SYS_WRITE", EX_USER_SYS_WRITE, handle, EC_ILLEGAL_ARGUMENT);
    }

    HO_STATUS status = ExHandleResolve(process, handle, EX_OBJECT_TYPE_CONSOLE, EX_HANDLE_RIGHT_WRITE, &objectHeader);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WRITE", EX_USER_SYS_WRITE, handle, status);

    if (length != 0)
    {
        status = KeUserBootstrapCopyInBytes(scratch, (HO_VIRTUAL_ADDRESS)userBuffer, length);
        if (status == EC_SUCCESS)
            status = KeUserBootstrapWriteConsoleBytes(scratch, length, &written);
    }

    HO_STATUS releaseStatus = ExHandleReleaseResolvedObject(objectHeader);
    if (status == EC_SUCCESS && releaseStatus != EC_SUCCESS)
        status = releaseStatus;

    if (status != EC_SUCCESS)
    {
        if (status == EC_FAILURE)
        {
            klog(KLOG_LEVEL_ERROR, "[USERCAP] SYS_WRITE console emit failed index=%lu thread=%u handle=%u\n",
                 (unsigned long)written, thread ? thread->ThreadId : 0U, handle);
            return KiEncodeSyscallStatus(status);
        }

        return KiRejectCapabilitySyscall("SYS_WRITE", EX_USER_SYS_WRITE, handle, status);
    }

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_CAP_WRITE_SUCCEEDED " bytes=%lu thread=%u handle=%u\n",
         (unsigned long)written, thread ? thread->ThreadId : 0U, handle);

    return (int64_t)written;
}

static int64_t
KiHandleCapabilityClose(EX_PROCESS *process, EX_HANDLE handle)
{
    KTHREAD *thread = KeGetCurrentThread();

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_CLOSE", EX_USER_SYS_CLOSE, handle, EC_INVALID_STATE);

    if (handle == EX_HANDLE_INVALID)
        return KiRejectCapabilitySyscall("SYS_CLOSE", EX_USER_SYS_CLOSE, handle, EC_ILLEGAL_ARGUMENT);

    EX_HANDLE localHandle = handle;
    HO_STATUS status = ExHandleClose(process, &localHandle);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_CLOSE", EX_USER_SYS_CLOSE, handle, status);

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_CAP_CLOSE_SUCCEEDED " handle=%u thread=%u\n", handle,
         thread ? thread->ThreadId : 0U);

    return 0;
}

static HO_STATUS
KiDecodeCapabilityWaitTimeoutNs(uint64_t timeoutMsRaw, uint64_t reserved, uint64_t *outTimeoutNs)
{
    if (outTimeoutNs == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (reserved != 0 || timeoutMsRaw > EX_USER_WAIT_ONE_TIMEOUT_MAX_MS)
        return EC_ILLEGAL_ARGUMENT;

    *outTimeoutNs = timeoutMsRaw * EX_USER_WAIT_ONE_TIMEOUT_NS_PER_MS;
    return EC_SUCCESS;
}

static int64_t
KiHandleCapabilityWaitOne(EX_PROCESS *process, EX_HANDLE handle, uint64_t timeoutMsRaw, uint64_t reserved)
{
    EX_OBJECT_HEADER *objectHeader = NULL;
    KTHREAD *thread = KeGetCurrentThread();
    uint64_t timeoutNs = 0;

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", EX_USER_SYS_WAIT_ONE, handle, EC_INVALID_STATE);

    if (handle == EX_HANDLE_INVALID)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", EX_USER_SYS_WAIT_ONE, handle, EC_ILLEGAL_ARGUMENT);

    HO_STATUS status = KiDecodeCapabilityWaitTimeoutNs(timeoutMsRaw, reserved, &timeoutNs);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", EX_USER_SYS_WAIT_ONE, handle, status);

    status = ExHandleResolveWaitable(process, handle, EX_HANDLE_RIGHT_WAIT, &objectHeader);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", EX_USER_SYS_WAIT_ONE, handle, status);

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS:
        status = ExRuntimeWaitForProcessCompletion(CONTAINING_RECORD(objectHeader, EX_PROCESS, Header), timeoutNs);
        break;
    case EX_OBJECT_TYPE_THREAD:
        status = ExRuntimeWaitForThreadCompletion(CONTAINING_RECORD(objectHeader, EX_THREAD, Header), timeoutNs);
        break;
    default:
        status = EC_INVALID_STATE;
        break;
    }

    HO_STATUS releaseStatus = ExHandleReleaseResolvedObject(objectHeader);
    if ((status == EC_SUCCESS || status == EC_TIMEOUT) && releaseStatus != EC_SUCCESS)
        status = releaseStatus;

    if (status != EC_SUCCESS)
    {
        if (status == EC_TIMEOUT)
        {
            klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_CAP_WAIT_TIMED_OUT " handle=%u thread=%u timeout_ms=%lu\n",
                 handle, thread ? thread->ThreadId : 0U, (unsigned long)timeoutMsRaw);
            return KiEncodeSyscallStatus(status);
        }

        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", EX_USER_SYS_WAIT_ONE, handle, status);
    }

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_CAP_WAIT_SUCCEEDED " handle=%u thread=%u timeout_ms=%lu\n", handle,
         thread ? thread->ThreadId : 0U, (unsigned long)timeoutMsRaw);

    return 0;
}

static int64_t
KiDispatchCapabilitySyscall(uint64_t syscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    KTHREAD *thread = KeGetCurrentThread();
    EX_PROCESS *process = ExRuntimeLookupProcessByKernelThread(thread);

    if (thread == NULL || process == NULL || process->Staging == NULL)
    {
        klog(KLOG_LEVEL_ERROR,
             EX_USER_REGRESSION_LOG_INVALID_CAP_SYSCALL " nr=%lu thread=%u missing bootstrap staging\n",
             (unsigned long)syscallNumber, thread ? thread->ThreadId : 0U);
        return KiEncodeSyscallStatus(EC_INVALID_STATE);
    }

    switch (syscallNumber)
    {
    case EX_USER_SYS_WRITE:
        return KiHandleCapabilityWrite(process, (EX_HANDLE)arg0, arg1, arg2);
    case EX_USER_SYS_CLOSE:
        return KiHandleCapabilityClose(process, (EX_HANDLE)arg0);
    case EX_USER_SYS_WAIT_ONE:
        return KiHandleCapabilityWaitOne(process, (EX_HANDLE)arg0, arg1, arg2);
    case EX_USER_SYS_QUERY_SYSINFO:
        return ExBootstrapHandleQuerySysinfo(process, arg0, arg1, arg2);
    default:
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_INVALID_CAP_SYSCALL " nr=%lu thread=%u args=(%p,%p,%p)\n",
             (unsigned long)syscallNumber, thread->ThreadId, (void *)(uint64_t)arg0, (void *)(uint64_t)arg1,
             (void *)(uint64_t)arg2);
        return KiEncodeSyscallStatus(EC_NOT_SUPPORTED);
    }
}

static int64_t
KiHandleReadLine(uint64_t userBuffer, uint64_t capacity, uint64_t reserved)
{
    KTHREAD *thread = KeGetCurrentThread();
    char scratch[KE_INPUT_LINE_CAPACITY];
    uint32_t copiedLength = 0;

    if (reserved != 0 || capacity == 0 || capacity > KE_INPUT_LINE_CAPACITY)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_READLINE_REJECTED " thread=%u addr=%p cap=%lu reserved=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (void *)(uint64_t)userBuffer, (unsigned long)capacity,
             (unsigned long)reserved, KrGetStatusMessage(EC_ILLEGAL_ARGUMENT), EC_ILLEGAL_ARGUMENT);
        return KiEncodeSyscallStatus(EC_ILLEGAL_ARGUMENT);
    }

    HO_STATUS status = KeInputWaitForForegroundLine();
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_READLINE_REJECTED " thread=%u addr=%p cap=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (void *)(uint64_t)userBuffer, (unsigned long)capacity,
             KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    status = KeInputCopyCompletedLineForCurrentThread(scratch, (uint32_t)capacity, &copiedLength);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_READLINE_REJECTED " thread=%u addr=%p cap=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (void *)(uint64_t)userBuffer, (unsigned long)capacity,
             KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, scratch, copiedLength);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_READLINE_REJECTED " thread=%u addr=%p cap=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (void *)(uint64_t)userBuffer, (unsigned long)capacity,
             KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    status = KeInputConsumeCompletedLineForCurrentThread();
    if (status != EC_SUCCESS)
        return KiEncodeSyscallStatus(status);

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_READLINE_SUCCEEDED " bytes=%u thread=%u\n", copiedLength,
         thread ? thread->ThreadId : 0U);
    return (int64_t)copiedLength;
}

static int64_t
KiHandleSpawnProgram(uint64_t userName, uint64_t nameLength, uint64_t flags)
{
    KTHREAD *thread = KeGetCurrentThread();
    uint32_t pid = 0;
    char programName[EX_PROGRAM_NAME_MAX_LENGTH];

    if (userName == 0 || nameLength == 0 || nameLength >= EX_PROGRAM_NAME_MAX_LENGTH || flags > 0xFFFFFFFFULL)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_SPAWN_PROGRAM_REJECTED " thread=%u name_len=%lu flags=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (unsigned long)nameLength, (unsigned long)flags,
             KrGetStatusMessage(EC_ILLEGAL_ARGUMENT), EC_ILLEGAL_ARGUMENT);
        return KiEncodeSyscallStatus(EC_ILLEGAL_ARGUMENT);
    }

    memset(programName, 0, sizeof(programName));

    HO_STATUS status = KeUserBootstrapCopyInBytes(programName, (HO_VIRTUAL_ADDRESS)userName, nameLength);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_SPAWN_PROGRAM_REJECTED
             " thread=%u name_addr=%p name_len=%lu flags=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (void *)(uint64_t)userName, (unsigned long)nameLength,
             (unsigned long)flags, KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    status = ExSpawnProgram(programName, (uint32_t)nameLength, (uint32_t)flags, &pid);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_SPAWN_PROGRAM_REJECTED " thread=%u name=%s flags=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, programName, (unsigned long)flags, KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_SPAWN_PROGRAM_SUCCEEDED " thread=%u name=%s flags=%lu pid=%u\n",
         thread ? thread->ThreadId : 0U, programName, (unsigned long)flags, pid);
    return (int64_t)pid;
}

static int64_t
KiHandleWaitPid(uint64_t pid, uint64_t reserved0, uint64_t reserved1)
{
    KTHREAD *thread = KeGetCurrentThread();

    if (reserved0 != 0 || reserved1 != 0 || pid == 0 || pid > 0xFFFFFFFFULL)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_WAIT_PID_REJECTED " thread=%u pid=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (unsigned long)pid, KrGetStatusMessage(EC_ILLEGAL_ARGUMENT),
             EC_ILLEGAL_ARGUMENT);
        return KiEncodeSyscallStatus(EC_ILLEGAL_ARGUMENT);
    }

    HO_STATUS status = ExWaitProcess((uint32_t)pid);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_WAIT_PID_REJECTED " thread=%u pid=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (unsigned long)pid, KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_WAIT_PID_SUCCEEDED " thread=%u pid=%lu\n",
         thread ? thread->ThreadId : 0U, (unsigned long)pid);
    return 0;
}

static int64_t
KiHandleSleepMs(uint64_t milliseconds, uint64_t reserved0, uint64_t reserved1)
{
    KTHREAD *thread = KeGetCurrentThread();

    if (reserved0 != 0 || reserved1 != 0 || milliseconds > EX_USER_SLEEP_MS_MAX)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_SLEEP_MS_REJECTED " thread=%u milliseconds=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (unsigned long)milliseconds, KrGetStatusMessage(EC_ILLEGAL_ARGUMENT),
             EC_ILLEGAL_ARGUMENT);
        return KiEncodeSyscallStatus(EC_ILLEGAL_ARGUMENT);
    }

    KeSleep(milliseconds * EX_USER_SLEEP_NS_PER_MS);
    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_SLEEP_MS_SUCCEEDED " thread=%u milliseconds=%lu\n",
         thread ? thread->ThreadId : 0U, (unsigned long)milliseconds);
    return 0;
}

static int64_t
KiHandleKillPid(uint64_t pid, uint64_t reserved0, uint64_t reserved1)
{
    KTHREAD *thread = KeGetCurrentThread();

    if (reserved0 != 0 || reserved1 != 0 || pid == 0 || pid > 0xFFFFFFFFULL)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_KILL_PID_REJECTED " thread=%u pid=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (unsigned long)pid, KrGetStatusMessage(EC_ILLEGAL_ARGUMENT),
             EC_ILLEGAL_ARGUMENT);
        return KiEncodeSyscallStatus(EC_ILLEGAL_ARGUMENT);
    }

    HO_STATUS status = ExKillProcess((uint32_t)pid);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING, EX_USER_REGRESSION_LOG_KILL_PID_REJECTED " thread=%u pid=%lu status=%s (%d)\n",
             thread ? thread->ThreadId : 0U, (unsigned long)pid, KrGetStatusMessage(status), status);
        return KiEncodeSyscallStatus(status);
    }

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_KILL_PID_SUCCEEDED " pid=%lu thread=%u\n", (unsigned long)pid,
         thread ? thread->ThreadId : 0U);
    return 0;
}

static HO_STATUS
KiDispatchFormalSyscall(const EX_SYSCALL_ARGUMENTS *args, EX_SYSCALL_DISPATCH_RESULT *result)
{
    switch (args->Number)
    {
    case EX_USER_SYS_EXIT:
        if (args->Arg1 != 0 || args->Arg2 != 0)
        {
            KiSetReturnResult(result, KiEncodeSyscallStatus(EC_ILLEGAL_ARGUMENT));
            return EC_SUCCESS;
        }

        return KiPrepareBootstrapExit(args->Arg0, EX_USER_REGRESSION_LOG_SYS_EXIT, "formal", result);
    case EX_USER_SYS_READLINE:
        KiSetReturnResult(result, KiHandleReadLine(args->Arg0, args->Arg1, args->Arg2));
        return EC_SUCCESS;
    case EX_USER_SYS_SPAWN_PROGRAM:
        KiSetReturnResult(result, KiHandleSpawnProgram(args->Arg0, args->Arg1, args->Arg2));
        return EC_SUCCESS;
    case EX_USER_SYS_WAIT_PID:
        KiSetReturnResult(result, KiHandleWaitPid(args->Arg0, args->Arg1, args->Arg2));
        return EC_SUCCESS;
    case EX_USER_SYS_SLEEP_MS:
        KiSetReturnResult(result, KiHandleSleepMs(args->Arg0, args->Arg1, args->Arg2));
        return EC_SUCCESS;
    case EX_USER_SYS_KILL_PID:
        KiSetReturnResult(result, KiHandleKillPid(args->Arg0, args->Arg1, args->Arg2));
        return EC_SUCCESS;
    default:
        KiSetReturnResult(result, KiDispatchCapabilitySyscall(args->Number, args->Arg0, args->Arg1, args->Arg2));
        return EC_SUCCESS;
    }
}

static HO_STATUS
KiObserveKillRequest(EX_SYSCALL_DISPATCH_RESULT *result)
{
    uint32_t killedProgramId = EX_PROGRAM_ID_NONE;
    if (ExRuntimeShouldTerminateCurrentProcess(&killedProgramId))
        return KiPrepareBootstrapKillExit(killedProgramId, result);

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
ExDispatchSyscall(const EX_SYSCALL_ARGUMENTS *args, EX_SYSCALL_DISPATCH_RESULT *outResult)
{
    if (args == NULL || outResult == NULL)
        return EC_ILLEGAL_ARGUMENT;

    KiSetReturnResult(outResult, 0);

    HO_STATUS status;
    if (args->Number < EX_USER_SYSCALL_BASE)
    {
        status = KiDispatchRawSyscall(args, outResult);
    }
    else
    {
        status = KiDispatchFormalSyscall(args, outResult);
    }

    if (status != EC_SUCCESS)
        return status;

    if (outResult->Disposition == EX_SYSCALL_DISPOSITION_RETURN_TO_USER)
        return KiObserveKillRequest(outResult);

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD int64_t
ExBootstrapAdapterDispatchSyscall(uint64_t syscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    return KiDispatchCapabilitySyscall(syscallNumber, arg0, arg1, arg2);
}
