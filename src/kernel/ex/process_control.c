/**
 * HimuOperatingSystem
 *
 * File: ex/process_control.c
 * Description: Ex-owned runtime process lifecycle control for spawn/wait/kill.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "runtime_internal.h"

#include <kernel/ex/ex_runtime.h>

#include <kernel/ex/program.h>
#include <kernel/ex/user_syscall_abi.h>
#include <kernel/hodbg.h>
#include <kernel/ke/input.h>
#include <kernel/ke/scheduler.h>

typedef struct EX_PROCESS_SPAWN_WORK
{
    const EX_USER_IMAGE *Image;
    uint32_t Flags;
    uint32_t ParentProcessId;
    uint32_t PreviousForegroundOwnerThreadId;
    uint32_t ChildProcessId;
    uint32_t ChildThreadId;
    HO_STATUS Status;
} EX_PROCESS_SPAWN_WORK;

static void KiUnexpectedExProcessControlKernelEntry(void *arg);
static void KiExSpawnWorkerThread(void *arg);
static HO_STATUS KiResolveCallerParentProcessId(uint32_t *outParentProcessId);
static HO_STATUS KiRestoreForegroundOwner(const EX_PROCESS *process);

static void
KiUnexpectedExProcessControlKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "Ex process-control runtime thread unexpectedly executed the kernel entry point");
}

static void
KiExSpawnWorkerThread(void *arg)
{
    EX_PROCESS_SPAWN_WORK *work = (EX_PROCESS_SPAWN_WORK *)arg;
    EX_RUNTIME_PROCESS_CREATE_PARAMS createParams = {0};
    EX_RUNTIME_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedExProcessControlKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_RUNTIME_THREAD_CREATE_FLAG_NONE,
    };
    EX_PROCESS *process = NULL;
    EX_PROCESS *runtimeProcess = NULL;
    EX_THREAD *thread = NULL;
    uint32_t childProcessId = 0;
    uint32_t childThreadId = 0;
    BOOL foregroundRequested = FALSE;
    BOOL foregroundOwnerChanged = FALSE;
    HO_STATUS status = EC_SUCCESS;

    if (work == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Ex process-control spawn work is required");

    work->Status = EC_INVALID_STATE;
    work->ChildProcessId = 0;
    work->ChildThreadId = 0;

    if (work->Image == NULL)
    {
        status = EC_ILLEGAL_ARGUMENT;
        goto Cleanup;
    }

    status = ExProgramBuildRuntimeCreateParams(work->Image, work->ParentProcessId, &createParams);
    if (status != EC_SUCCESS)
        goto Cleanup;

    foregroundRequested = (work->Flags & EX_USER_SPAWN_FLAG_FOREGROUND) != 0U;

    status = ExRuntimeCreateProcess(&createParams, &process);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExRuntimeQueryProcessId(process, &childProcessId);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExRuntimeCreateThread(&process, &threadParams, &thread);
    if (status != EC_SUCCESS)
        goto Cleanup;

    runtimeProcess = thread->Process;
    if (runtimeProcess == NULL)
    {
        status = EC_INVALID_STATE;
        goto Cleanup;
    }

    status = ExRuntimeQueryThreadId(thread, &childThreadId);
    if (status != EC_SUCCESS)
        goto Cleanup;

    if (foregroundRequested)
    {
        status = KeInputSetForegroundOwnerThreadId(childThreadId);
        if (status != EC_SUCCESS)
            goto Cleanup;

        foregroundOwnerChanged = TRUE;
    }

    status = ExRuntimeMarkProcessControl(runtimeProcess, foregroundRequested, work->PreviousForegroundOwnerThreadId);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExRuntimeStartThread(&thread);
    if (status != EC_SUCCESS)
        goto Cleanup;

    work->ChildProcessId = childProcessId;
    work->ChildThreadId = childThreadId;
    work->Status = EC_SUCCESS;
    return;

Cleanup:
    if (foregroundOwnerChanged)
    {
        HO_STATUS restoreStatus = KeInputSetForegroundOwnerThreadId(work->PreviousForegroundOwnerThreadId);
        if (status == EC_SUCCESS)
            status = restoreStatus;
    }

    if (thread != NULL)
    {
        HO_STATUS teardownStatus = ExRuntimeTeardownThread(thread);
        if (status == EC_SUCCESS)
            status = teardownStatus;
    }
    else if (process != NULL)
    {
        HO_STATUS destroyStatus = ExRuntimeDestroyProcess(process);
        if (status == EC_SUCCESS)
            status = destroyStatus;
    }

    work->Status = status;
}

static HO_STATUS
KiResolveCallerParentProcessId(uint32_t *outParentProcessId)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    if (outParentProcessId == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outParentProcessId = 0;

    if (currentThread == NULL)
        return EC_INVALID_STATE;

    process = ExRuntimeLookupProcessByKernelThread(currentThread);
    if (process == NULL)
        return EC_SUCCESS;

    status = ExRuntimeQueryProcessId(process, outParentProcessId);

    return status;
}

static HO_STATUS
KiRestoreForegroundOwner(const EX_PROCESS *process)
{
    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!process->Foreground)
        return EC_SUCCESS;

    HO_STATUS status = KeInputSetForegroundOwnerThreadId(process->RestoreForegroundOwnerThreadId);
    if (status == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, "[DEMOSHELL] foreground restored pid=%u owner=%u\n", process->ProcessId,
             process->RestoreForegroundOwnerThreadId);
    }

    return status;
}

HO_KERNEL_API HO_STATUS
ExSpawnProgram(const char *name, uint32_t nameLength, uint32_t flags, uint32_t *outPid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    const EX_USER_IMAGE *image = NULL;
    EX_PROCESS_SPAWN_WORK work = {0};
    KTHREAD *workerThread = NULL;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (outPid == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outPid = 0;

    if (currentThread == NULL)
        return EC_INVALID_STATE;

    if ((flags & ~EX_USER_SPAWN_FLAG_FOREGROUND) != 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = ExLookupProgramImageByName(name, nameLength, &image);
    if (status != EC_SUCCESS)
        return status;

    status = KiResolveCallerParentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    work.Image = image;
    work.Flags = flags;
    work.ParentProcessId = parentProcessId;
    work.PreviousForegroundOwnerThreadId = KeInputGetForegroundOwnerThreadId();
    work.Status = EC_INVALID_STATE;

    status = KeThreadCreateJoinable(&workerThread, KiExSpawnWorkerThread, &work);
    if (status != EC_SUCCESS)
        return status;

    status = KeThreadStart(workerThread);
    if (status != EC_SUCCESS)
        return status;

    status = KeThreadJoin(workerThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        return status;

    if (work.Status != EC_SUCCESS)
    {
        status = work.Status;
        return status;
    }

    *outPid = work.ChildProcessId;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
ExWaitProcess(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_PROCESS *childProcess = NULL;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = KiResolveCallerParentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    status = ExRuntimeRetainChildProcess(parentProcessId, pid, &childProcess);
    if (status != EC_SUCCESS)
        return status;

    status = ExRuntimeWaitForProcessCompletion(childProcess, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        goto Exit;

    status = KiRestoreForegroundOwner(childProcess);
    HO_STATUS consumeStatus = ExRuntimeConsumeCompletedProcess(childProcess);
    if (status == EC_SUCCESS)
        status = consumeStatus;

Exit: {
    HO_STATUS releaseStatus = ExRuntimeReleaseProcess(childProcess);
    if (status == EC_SUCCESS)
        status = releaseStatus;
}

    return status;
}

HO_KERNEL_API HO_STATUS
ExKillProcess(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_PROCESS *childProcess = NULL;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = KiResolveCallerParentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    status = ExRuntimeRetainChildProcess(parentProcessId, pid, &childProcess);
    if (status != EC_SUCCESS)
        return status;

    status = ExRuntimeRequestProcessKill(childProcess);
    if (status != EC_SUCCESS)
        goto Exit;

    status = ExRuntimeWaitForProcessCompletion(childProcess, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        goto Exit;

    status = KiRestoreForegroundOwner(childProcess);
    HO_STATUS consumeStatus = ExRuntimeConsumeCompletedProcess(childProcess);
    if (status == EC_SUCCESS)
        status = consumeStatus;

Exit: {
    HO_STATUS releaseStatus = ExRuntimeReleaseProcess(childProcess);
    if (status == EC_SUCCESS)
        status = releaseStatus;
}

    return status;
}

HO_KERNEL_API HO_STATUS
ExSetForegroundProcess(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_PROCESS *childProcess = NULL;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = KiResolveCallerParentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    status = ExRuntimeRetainChildProcess(parentProcessId, pid, &childProcess);
    if (status != EC_SUCCESS)
        return status;

    status = ExRuntimeSetProcessForeground(childProcess, KeInputGetForegroundOwnerThreadId());

    HO_STATUS releaseStatus = ExRuntimeReleaseProcess(childProcess);
    if (status == EC_SUCCESS)
        status = releaseStatus;

    return status;
}

HO_KERNEL_API BOOL
ExShouldTerminateCurrentProcess(uint32_t *outProgramId)
{
    return ExRuntimeShouldTerminateCurrentProcess(outProgramId);
}
