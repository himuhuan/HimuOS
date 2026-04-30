/**
 * HimuOperatingSystem
 *
 * File: ex/process_control.c
 * Description: Ex-owned bootstrap process lifecycle control for spawn/wait/kill.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap.h>

#include <kernel/ex/program.h>
#include <kernel/ex/user_syscall_abi.h>
#include <kernel/hodbg.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/input.h>
#include <kernel/ke/scheduler.h>
#include <libc/string.h>

typedef struct EX_CHILD_PROCESS_ENTRY
{
    BOOL Active;
    uint32_t ParentProcessId;
    uint32_t ChildProcessId;
    uint32_t ChildThreadId;
    uint32_t ProgramId;
    BOOL Foreground;
    uint32_t RestoreForegroundOwnerThreadId;
    KTHREAD *KernelThread;
    BOOL KillRequested;
} EX_CHILD_PROCESS_ENTRY;

typedef struct EX_PROCESS_SPAWN_WORK
{
    const EX_USER_IMAGE *Image;
    uint32_t Flags;
    uint32_t ParentProcessId;
    uint32_t SlotIndex;
    uint32_t PreviousForegroundOwnerThreadId;
    uint32_t ChildProcessId;
    uint32_t ChildThreadId;
    HO_STATUS Status;
} EX_PROCESS_SPAWN_WORK;

static EX_CHILD_PROCESS_ENTRY gExChildProcessTable[EX_MAX_PROCESSES];

static void KiUnexpectedExProcessControlKernelEntry(void *arg);
static void KiExSpawnWorkerThread(void *arg);
static HO_STATUS KiResolveCallerParentProcessId(uint32_t *outParentProcessId);
static uint32_t KiReserveChildSlot(void);
static void KiReleaseChildSlot(uint32_t slotIndex);
static HO_STATUS KiLookupChildSlot(uint32_t parentProcessId,
                                   uint32_t childProcessId,
                                   uint32_t *outSlotIndex,
                                   EX_CHILD_PROCESS_ENTRY *outEntry);
static HO_STATUS KiLookupChildSlotByThreadId(uint32_t childThreadId,
                                             uint32_t *outSlotIndex,
                                             EX_CHILD_PROCESS_ENTRY *outEntry);
static HO_STATUS KiRestoreForegroundOwner(const EX_CHILD_PROCESS_ENTRY *childEntry);

static void
KiUnexpectedExProcessControlKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "Ex process-control bootstrap thread unexpectedly executed the kernel entry point");
}

static void
KiExSpawnWorkerThread(void *arg)
{
    EX_PROCESS_SPAWN_WORK *work = (EX_PROCESS_SPAWN_WORK *)arg;
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS createParams = {0};
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedExProcessControlKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_JOINABLE,
    };
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;
    KTHREAD *kernelThread = NULL;
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

    status = ExProgramBuildBootstrapCreateParams(work->Image, work->ParentProcessId, &createParams);
    if (status != EC_SUCCESS)
        goto Cleanup;

    foregroundRequested = (work->Flags & EX_USER_SPAWN_FLAG_FOREGROUND) != 0U;

    status = ExBootstrapCreateProcess(&createParams, &process);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExBootstrapQueryProcessId(process, &childProcessId);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExBootstrapCreateThread(&process, &threadParams, &thread);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExBootstrapQueryThreadId(thread, &childThreadId);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = ExBootstrapBorrowKernelThread(thread, &kernelThread);
    if (status != EC_SUCCESS)
        goto Cleanup;

    if (foregroundRequested)
    {
        status = KeInputSetForegroundOwnerThreadId(childThreadId);
        if (status != EC_SUCCESS)
            goto Cleanup;

        foregroundOwnerChanged = TRUE;
    }

    status = ExBootstrapStartThread(&thread);
    if (status != EC_SUCCESS)
        goto Cleanup;

    {
        KE_CRITICAL_SECTION guard = {0};
        KeEnterCriticalSection(&guard);
        gExChildProcessTable[work->SlotIndex].Active = TRUE;
        gExChildProcessTable[work->SlotIndex].ParentProcessId = work->ParentProcessId;
        gExChildProcessTable[work->SlotIndex].ChildProcessId = childProcessId;
        gExChildProcessTable[work->SlotIndex].ChildThreadId = childThreadId;
        gExChildProcessTable[work->SlotIndex].ProgramId = work->Image->ProgramId;
        gExChildProcessTable[work->SlotIndex].Foreground = foregroundRequested;
        gExChildProcessTable[work->SlotIndex].RestoreForegroundOwnerThreadId = work->PreviousForegroundOwnerThreadId;
        gExChildProcessTable[work->SlotIndex].KernelThread = kernelThread;
        KeLeaveCriticalSection(&guard);
    }

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
        HO_STATUS teardownStatus = ExBootstrapTeardownThread(thread);
        if (status == EC_SUCCESS)
            status = teardownStatus;
    }
    else if (process != NULL)
    {
        HO_STATUS destroyStatus = ExBootstrapDestroyProcess(process);
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

    process = ExBootstrapLookupRuntimeProcess(currentThread);
    if (process == NULL)
        return EC_SUCCESS;

    status = ExBootstrapQueryProcessId(process, outParentProcessId);

    return status;
}

static uint32_t
KiReserveChildSlot(void)
{
    KE_CRITICAL_SECTION guard = {0};
    uint32_t slotIndex = EX_MAX_PROCESSES;

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < EX_MAX_PROCESSES; ++index)
    {
        if (gExChildProcessTable[index].Active)
            continue;

        memset(&gExChildProcessTable[index], 0, sizeof(gExChildProcessTable[index]));
        gExChildProcessTable[index].Active = TRUE;
        slotIndex = index;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return slotIndex;
}

static void
KiReleaseChildSlot(uint32_t slotIndex)
{
    KE_CRITICAL_SECTION guard = {0};

    if (slotIndex >= EX_MAX_PROCESSES)
        return;

    KeEnterCriticalSection(&guard);
    memset(&gExChildProcessTable[slotIndex], 0, sizeof(gExChildProcessTable[slotIndex]));
    KeLeaveCriticalSection(&guard);
}

static HO_STATUS
KiLookupChildSlot(uint32_t parentProcessId,
                  uint32_t childProcessId,
                  uint32_t *outSlotIndex,
                  EX_CHILD_PROCESS_ENTRY *outEntry)
{
    KE_CRITICAL_SECTION guard = {0};
    HO_STATUS status = EC_INVALID_STATE;

    if (outSlotIndex == NULL || outEntry == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outSlotIndex = EX_MAX_PROCESSES;
    memset(outEntry, 0, sizeof(*outEntry));

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < EX_MAX_PROCESSES; ++index)
    {
        if (!gExChildProcessTable[index].Active)
            continue;

        if (gExChildProcessTable[index].ParentProcessId != parentProcessId ||
            gExChildProcessTable[index].ChildProcessId != childProcessId)
        {
            continue;
        }

        *outSlotIndex = index;
        *outEntry = gExChildProcessTable[index];
        status = EC_SUCCESS;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return status;
}

static HO_STATUS
KiLookupChildSlotByThreadId(uint32_t childThreadId, uint32_t *outSlotIndex, EX_CHILD_PROCESS_ENTRY *outEntry)
{
    KE_CRITICAL_SECTION guard = {0};
    HO_STATUS status = EC_INVALID_STATE;

    if (outSlotIndex == NULL || outEntry == NULL || childThreadId == 0U)
        return EC_ILLEGAL_ARGUMENT;

    *outSlotIndex = EX_MAX_PROCESSES;
    memset(outEntry, 0, sizeof(*outEntry));

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < EX_MAX_PROCESSES; ++index)
    {
        if (!gExChildProcessTable[index].Active)
            continue;

        if (gExChildProcessTable[index].ChildThreadId != childThreadId)
            continue;

        *outSlotIndex = index;
        *outEntry = gExChildProcessTable[index];
        status = EC_SUCCESS;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return status;
}

static HO_STATUS
KiRestoreForegroundOwner(const EX_CHILD_PROCESS_ENTRY *childEntry)
{
    if (childEntry == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!childEntry->Foreground)
        return EC_SUCCESS;

    HO_STATUS status = KeInputSetForegroundOwnerThreadId(childEntry->RestoreForegroundOwnerThreadId);
    if (status == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, "[DEMOSHELL] foreground restored pid=%u owner=%u\n", childEntry->ChildProcessId,
             childEntry->RestoreForegroundOwnerThreadId);
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
    uint32_t slotIndex = EX_MAX_PROCESSES;
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

    slotIndex = KiReserveChildSlot();
    if (slotIndex >= EX_MAX_PROCESSES)
        return EC_OUT_OF_RESOURCE;

    work.Image = image;
    work.Flags = flags;
    work.ParentProcessId = parentProcessId;
    work.SlotIndex = slotIndex;
    work.PreviousForegroundOwnerThreadId = KeInputGetForegroundOwnerThreadId();
    work.Status = EC_INVALID_STATE;

    status = KeThreadCreateJoinable(&workerThread, KiExSpawnWorkerThread, &work);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = KeThreadStart(workerThread);
    if (status != EC_SUCCESS)
        goto Cleanup;

    status = KeThreadJoin(workerThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        goto Cleanup;

    if (work.Status != EC_SUCCESS)
    {
        status = work.Status;
        goto Cleanup;
    }

    *outPid = work.ChildProcessId;
    return EC_SUCCESS;

Cleanup:
    KiReleaseChildSlot(slotIndex);
    return status;
}

HO_KERNEL_API HO_STATUS
ExWaitProcess(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_CHILD_PROCESS_ENTRY childEntry = {0};
    uint32_t slotIndex = EX_MAX_PROCESSES;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = KiResolveCallerParentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    status = KiLookupChildSlot(parentProcessId, pid, &slotIndex, &childEntry);
    if (status != EC_SUCCESS)
        return status;

    if (childEntry.KernelThread == NULL)
        return EC_INVALID_STATE;

    status = KeThreadJoin(childEntry.KernelThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        return status;

    status = KiRestoreForegroundOwner(&childEntry);
    KiReleaseChildSlot(slotIndex);
    return status;
}

HO_KERNEL_API HO_STATUS
ExKillProcess(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_CHILD_PROCESS_ENTRY childEntry = {0};
    uint32_t slotIndex = EX_MAX_PROCESSES;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = KiResolveCallerParentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    status = KiLookupChildSlot(parentProcessId, pid, &slotIndex, &childEntry);
    if (status != EC_SUCCESS)
        return status;

    if (childEntry.KernelThread == NULL || childEntry.KillRequested)
        return EC_INVALID_STATE;

    {
        KE_CRITICAL_SECTION guard = {0};
        KeEnterCriticalSection(&guard);
        if (!gExChildProcessTable[slotIndex].Active ||
            gExChildProcessTable[slotIndex].ParentProcessId != parentProcessId ||
            gExChildProcessTable[slotIndex].ChildProcessId != pid || gExChildProcessTable[slotIndex].KillRequested)
        {
            KeLeaveCriticalSection(&guard);
            return EC_INVALID_STATE;
        }

        gExChildProcessTable[slotIndex].KillRequested = TRUE;
        KeLeaveCriticalSection(&guard);
    }

    status = KeThreadJoin(childEntry.KernelThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        return status;

    status = KiRestoreForegroundOwner(&childEntry);
    KiReleaseChildSlot(slotIndex);
    return status;
}

HO_KERNEL_API BOOL
ExShouldTerminateCurrentProcess(uint32_t *outProgramId)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_CHILD_PROCESS_ENTRY childEntry = {0};
    uint32_t slotIndex = EX_MAX_PROCESSES;
    HO_STATUS status = EC_SUCCESS;

    if (outProgramId != NULL)
        *outProgramId = EX_PROGRAM_ID_NONE;

    if (currentThread == NULL)
        return FALSE;

    status = KiLookupChildSlotByThreadId(currentThread->ThreadId, &slotIndex, &childEntry);
    if (status != EC_SUCCESS || !childEntry.KillRequested)
        return FALSE;

    if (outProgramId != NULL)
        *outProgramId = childEntry.ProgramId;

    return TRUE;
}
