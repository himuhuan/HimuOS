/**
 * HimuOperatingSystem
 *
 * File: demo/demo_shell_runtime.c
 * Description: Narrow demo-shell P2 runtime helper for builtin spawn/wait.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/demo_shell.h>
#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ke/input.h>
#include <kernel/ke/mm.h>

enum
{
    KE_DEMO_SHELL_ENTRY_OFFSET = 0U,
    KE_DEMO_SHELL_CHILD_TABLE_CAPACITY = 4U,
};

typedef struct KE_DEMO_SHELL_CHILD_ENTRY
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
} KE_DEMO_SHELL_CHILD_ENTRY;

typedef struct KE_DEMO_SHELL_SPAWN_WORK
{
    uint32_t ProgramId;
    uint32_t Flags;
    uint32_t ParentProcessId;
    uint32_t ParentThreadId;
    uint32_t SlotIndex;
    uint32_t PreviousForegroundOwnerThreadId;
    uint32_t ChildProcessId;
    uint32_t ChildThreadId;
    KTHREAD *ChildKernelThread;
    HO_STATUS Status;
} KE_DEMO_SHELL_SPAWN_WORK;

static KE_DEMO_SHELL_CHILD_ENTRY gKeDemoShellChildTable[KE_DEMO_SHELL_CHILD_TABLE_CAPACITY];

static void KiUnexpectedDemoShellKernelEntry(void *arg);
static void KiDemoShellSpawnWorkerThread(void *arg);
static HO_STATUS KiResolveBuiltinArtifacts(uint32_t programId, KI_USER_EMBEDDED_ARTIFACTS *artifacts);
static uint32_t KiReserveChildSlot(void);
static void KiReleaseChildSlot(uint32_t slotIndex);
static HO_STATUS KiLookupChildSlot(uint32_t parentProcessId,
                                   uint32_t childProcessId,
                                   uint32_t *outSlotIndex,
                                   KE_DEMO_SHELL_CHILD_ENTRY *outEntry);
static HO_STATUS KiLookupChildSlotByThreadId(uint32_t childThreadId,
                                             uint32_t *outSlotIndex,
                                             KE_DEMO_SHELL_CHILD_ENTRY *outEntry);

static void
KiUnexpectedDemoShellKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "demo-shell bootstrap thread unexpectedly executed the kernel entry point");
}

static void
KiDemoShellSpawnWorkerThread(void *arg)
{
    KE_DEMO_SHELL_SPAWN_WORK *work = (KE_DEMO_SHELL_SPAWN_WORK *)arg;
    KI_USER_EMBEDDED_ARTIFACTS artifacts = {0};
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS createParams = {0};
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedDemoShellKernelEntry,
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
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "demo-shell spawn work is required");

    work->Status = EC_INVALID_STATE;
    work->ChildProcessId = 0;
    work->ChildThreadId = 0;
    work->ChildKernelThread = NULL;

    status = KiResolveBuiltinArtifacts(work->ProgramId, &artifacts);
    if (status != EC_SUCCESS)
        goto Cleanup;

    createParams.CodeBytes = artifacts.CodeBytes;
    createParams.CodeLength = artifacts.CodeLength;
    createParams.EntryOffset = KE_DEMO_SHELL_ENTRY_OFFSET;
    createParams.ConstBytes = artifacts.ConstBytes;
    createParams.ConstLength = artifacts.ConstLength;
    createParams.ProgramId = work->ProgramId;
    createParams.ParentProcessId = work->ParentProcessId;

    foregroundRequested = (work->Flags & KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND) != 0U;

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
        gKeDemoShellChildTable[work->SlotIndex].Active = TRUE;
        gKeDemoShellChildTable[work->SlotIndex].ParentProcessId = work->ParentProcessId;
        gKeDemoShellChildTable[work->SlotIndex].ChildProcessId = childProcessId;
        gKeDemoShellChildTable[work->SlotIndex].ChildThreadId = childThreadId;
        gKeDemoShellChildTable[work->SlotIndex].ProgramId = work->ProgramId;
        gKeDemoShellChildTable[work->SlotIndex].Foreground = foregroundRequested;
        gKeDemoShellChildTable[work->SlotIndex].RestoreForegroundOwnerThreadId = work->ParentThreadId;
        gKeDemoShellChildTable[work->SlotIndex].KernelThread = kernelThread;
        KeLeaveCriticalSection(&guard);
    }

    work->ChildProcessId = childProcessId;
    work->ChildThreadId = childThreadId;
    work->ChildKernelThread = kernelThread;
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
KiResolveBuiltinArtifacts(uint32_t programId, KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        return EC_ILLEGAL_ARGUMENT;

    memset(artifacts, 0, sizeof(*artifacts));

    switch (programId)
    {
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_HSH:
        KiHshGetEmbeddedArtifacts(artifacts);
        return EC_SUCCESS;
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_CALC:
        KiCalcGetEmbeddedArtifacts(artifacts);
        return EC_SUCCESS;
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_TICK1S:
        KiTick1sGetEmbeddedArtifacts(artifacts);
        return EC_SUCCESS;
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_DE:
        KiFaultDeGetEmbeddedArtifacts(artifacts);
        return EC_SUCCESS;
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_PF:
        KiFaultPfGetEmbeddedArtifacts(artifacts);
        return EC_SUCCESS;
    default:
        return EC_ILLEGAL_ARGUMENT;
    }
}

static uint32_t
KiReserveChildSlot(void)
{
    KE_CRITICAL_SECTION guard = {0};
    uint32_t slotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < KE_DEMO_SHELL_CHILD_TABLE_CAPACITY; ++index)
    {
        if (gKeDemoShellChildTable[index].Active)
            continue;

        memset(&gKeDemoShellChildTable[index], 0, sizeof(gKeDemoShellChildTable[index]));
        gKeDemoShellChildTable[index].Active = TRUE;
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

    if (slotIndex >= KE_DEMO_SHELL_CHILD_TABLE_CAPACITY)
        return;

    KeEnterCriticalSection(&guard);
    memset(&gKeDemoShellChildTable[slotIndex], 0, sizeof(gKeDemoShellChildTable[slotIndex]));
    KeLeaveCriticalSection(&guard);
}

static HO_STATUS
KiLookupChildSlot(uint32_t parentProcessId,
                  uint32_t childProcessId,
                  uint32_t *outSlotIndex,
                  KE_DEMO_SHELL_CHILD_ENTRY *outEntry)
{
    KE_CRITICAL_SECTION guard = {0};
    HO_STATUS status = EC_INVALID_STATE;

    if (outSlotIndex == NULL || outEntry == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outSlotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;
    memset(outEntry, 0, sizeof(*outEntry));

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < KE_DEMO_SHELL_CHILD_TABLE_CAPACITY; ++index)
    {
        if (!gKeDemoShellChildTable[index].Active)
            continue;

        if (gKeDemoShellChildTable[index].ParentProcessId != parentProcessId ||
            gKeDemoShellChildTable[index].ChildProcessId != childProcessId)
        {
            continue;
        }

        *outSlotIndex = index;
        *outEntry = gKeDemoShellChildTable[index];
        status = EC_SUCCESS;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return status;
}

static HO_STATUS
KiLookupChildSlotByThreadId(uint32_t childThreadId, uint32_t *outSlotIndex, KE_DEMO_SHELL_CHILD_ENTRY *outEntry)
{
    KE_CRITICAL_SECTION guard = {0};
    HO_STATUS status = EC_INVALID_STATE;

    if (outSlotIndex == NULL || outEntry == NULL || childThreadId == 0U)
        return EC_ILLEGAL_ARGUMENT;

    *outSlotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;
    memset(outEntry, 0, sizeof(*outEntry));

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < KE_DEMO_SHELL_CHILD_TABLE_CAPACITY; ++index)
    {
        if (!gKeDemoShellChildTable[index].Active)
            continue;

        if (gKeDemoShellChildTable[index].ChildThreadId != childThreadId)
            continue;

        *outSlotIndex = index;
        *outEntry = gKeDemoShellChildTable[index];
        status = EC_SUCCESS;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return status;
}

HO_KERNEL_API void
KeDemoShellResetControlPlane(void)
{
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    memset(gKeDemoShellChildTable, 0, sizeof(gKeDemoShellChildTable));
    KeLeaveCriticalSection(&guard);
}

HO_KERNEL_API HO_STATUS
KeDemoShellSpawnBuiltin(uint32_t programId, uint32_t flags, uint32_t *outPid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    KE_DEMO_SHELL_SPAWN_WORK work = {0};
    KTHREAD *workerThread = NULL;
    uint32_t slotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (outPid == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outPid = 0;

    if (currentThread == NULL)
        return EC_INVALID_STATE;

    if ((flags & ~KE_USER_BOOTSTRAP_SPAWN_FLAG_FOREGROUND) != 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = ExBootstrapQueryCurrentProcessId(&parentProcessId);
    if (status != EC_SUCCESS)
        return status;

    slotIndex = KiReserveChildSlot();
    if (slotIndex >= KE_DEMO_SHELL_CHILD_TABLE_CAPACITY)
        return EC_OUT_OF_RESOURCE;

    work.ProgramId = programId;
    work.Flags = flags;
    work.ParentProcessId = parentProcessId;
    work.ParentThreadId = currentThread->ThreadId;
    work.SlotIndex = slotIndex;
    work.PreviousForegroundOwnerThreadId = KeInputGetForegroundOwnerThreadId();
    work.Status = EC_INVALID_STATE;

    status = KeThreadCreateJoinable(&workerThread, KiDemoShellSpawnWorkerThread, &work);
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
KeDemoShellWaitPid(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    KE_DEMO_SHELL_CHILD_ENTRY childEntry = {0};
    uint32_t slotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = ExBootstrapQueryCurrentProcessId(&parentProcessId);
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

    if (childEntry.Foreground && childEntry.RestoreForegroundOwnerThreadId != 0U)
    {
        status = KeInputSetForegroundOwnerThreadId(childEntry.RestoreForegroundOwnerThreadId);
        if (status == EC_SUCCESS)
        {
            klog(KLOG_LEVEL_INFO, "[DEMOSHELL] foreground restored pid=%u owner=%u\n", pid,
                 childEntry.RestoreForegroundOwnerThreadId);
        }
    }

    KiReleaseChildSlot(slotIndex);
    return status;
}

HO_KERNEL_API HO_STATUS
KeDemoShellKillPid(uint32_t pid)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    KE_DEMO_SHELL_CHILD_ENTRY childEntry = {0};
    uint32_t slotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;
    uint32_t parentProcessId = 0;
    HO_STATUS status = EC_SUCCESS;

    if (currentThread == NULL || pid == 0U)
        return EC_ILLEGAL_ARGUMENT;

    status = ExBootstrapQueryCurrentProcessId(&parentProcessId);
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
        if (!gKeDemoShellChildTable[slotIndex].Active ||
            gKeDemoShellChildTable[slotIndex].ParentProcessId != parentProcessId ||
            gKeDemoShellChildTable[slotIndex].ChildProcessId != pid || gKeDemoShellChildTable[slotIndex].KillRequested)
        {
            KeLeaveCriticalSection(&guard);
            return EC_INVALID_STATE;
        }

        gKeDemoShellChildTable[slotIndex].KillRequested = TRUE;
        KeLeaveCriticalSection(&guard);
    }

    status = KeThreadJoin(childEntry.KernelThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        return status;

    if (childEntry.Foreground && childEntry.RestoreForegroundOwnerThreadId != 0U)
    {
        status = KeInputSetForegroundOwnerThreadId(childEntry.RestoreForegroundOwnerThreadId);
        if (status != EC_SUCCESS)
            return status;

        klog(KLOG_LEVEL_INFO, "[DEMOSHELL] foreground restored pid=%u owner=%u\n", pid,
             childEntry.RestoreForegroundOwnerThreadId);
    }

    KiReleaseChildSlot(slotIndex);
    return EC_SUCCESS;
}

HO_KERNEL_API BOOL
KeDemoShellShouldTerminateCurrentThread(uint32_t *outProgramId)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    KE_DEMO_SHELL_CHILD_ENTRY childEntry = {0};
    uint32_t slotIndex = KE_DEMO_SHELL_CHILD_TABLE_CAPACITY;
    HO_STATUS status = EC_SUCCESS;

    if (outProgramId != NULL)
        *outProgramId = KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_NONE;

    if (currentThread == NULL)
        return FALSE;

    status = KiLookupChildSlotByThreadId(currentThread->ThreadId, &slotIndex, &childEntry);
    if (status != EC_SUCCESS || !childEntry.KillRequested)
        return FALSE;

    if (outProgramId != NULL)
        *outProgramId = childEntry.ProgramId;

    return TRUE;
}
