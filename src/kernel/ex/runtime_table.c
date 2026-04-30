/**
 * HimuOperatingSystem
 *
 * File: ex/runtime_table.c
 * Description: Ex runtime process and thread tables.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ex/program.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/scheduler.h>
#include <libc/string.h>

#define EX_RUNTIME_PROCESS_TABLE_CAPACITY EX_MAX_PROCESSES
#define EX_RUNTIME_THREAD_TABLE_CAPACITY  (EX_MAX_PROCESSES * EX_MAX_THREADS_PER_PROCESS)

#if EX_MAX_PROCESSES > EX_SYSINFO_PROCESS_LIST_MAX_ENTRIES
#error EX_MAX_PROCESSES exceeds the process sysinfo ABI entry capacity.
#endif

#if EX_RUNTIME_THREAD_TABLE_CAPACITY > EX_SYSINFO_THREAD_LIST_MAX_ENTRIES
#error EX runtime thread capacity exceeds the thread sysinfo ABI entry capacity.
#endif

typedef struct EX_RUNTIME_PROCESS_TABLE_ENTRY
{
    BOOL Active;
    EX_PROCESS *Process;
} EX_RUNTIME_PROCESS_TABLE_ENTRY;

typedef struct EX_RUNTIME_THREAD_TABLE_ENTRY
{
    BOOL Active;
    EX_THREAD *Thread;
} EX_RUNTIME_THREAD_TABLE_ENTRY;

static EX_RUNTIME_PROCESS_TABLE_ENTRY gExRuntimeProcessTable[EX_RUNTIME_PROCESS_TABLE_CAPACITY] = {0};
static EX_RUNTIME_THREAD_TABLE_ENTRY gExRuntimeThreadTable[EX_RUNTIME_THREAD_TABLE_CAPACITY] = {0};

static void KiCopyRuntimeProgramName(char *destination, size_t destinationSize, const char *source);
static const char *KiGetRuntimeProgramName(uint32_t programId);
static uint32_t KiMapRuntimeThreadState(const KTHREAD *thread);
static uint32_t KiMapRuntimeProcessState(const EX_PROCESS *process, const KTHREAD *thread);
static void KiSortThreadEntries(EX_SYSINFO_THREAD_LIST *threadList);
static void KiSortProcessEntries(EX_SYSINFO_PROCESS_LIST *processList);
static uint32_t KiFindProcessSlotByProcess(const EX_PROCESS *process);
static uint32_t KiFindProcessSlotByPid(uint32_t processId);
static uint32_t KiFindFreeProcessSlot(void);
static uint32_t KiFindThreadSlotByThreadObject(const EX_THREAD *thread);
static uint32_t KiFindThreadSlotByThreadId(uint32_t threadId);
static uint32_t KiFindThreadSlotByKernelThread(const KTHREAD *thread);
static uint32_t KiFindFreeThreadSlot(void);
static EX_THREAD *KiLookupThreadByTidLocked(uint32_t threadId);

static void
KiCopyRuntimeProgramName(char *destination, size_t destinationSize, const char *source)
{
    size_t copyLength = 0;

    if (destination == NULL || destinationSize == 0)
        return;

    if (source != NULL)
    {
        copyLength = strlen(source);
        if (copyLength >= destinationSize)
            copyLength = destinationSize - 1;
        memcpy(destination, source, copyLength);
    }

    destination[copyLength] = '\0';
}

static const char *
KiGetRuntimeProgramName(uint32_t programId)
{
    const EX_USER_IMAGE *image = NULL;

    if (ExLookupProgramImageById(programId, &image) != EC_SUCCESS || image == NULL || image->Name == NULL)
        return "user";

    return image->Name;
}

static uint32_t
KiMapRuntimeThreadState(const KTHREAD *thread)
{
    if (thread == NULL)
        return EX_SYSINFO_THREAD_STATE_INVALID;

    switch (thread->State)
    {
    case KTHREAD_STATE_READY:
        return EX_SYSINFO_THREAD_STATE_READY;
    case KTHREAD_STATE_RUNNING:
        return EX_SYSINFO_THREAD_STATE_RUNNING;
    case KTHREAD_STATE_BLOCKED:
        if (thread->WaitBlock.Dispatcher == NULL && thread->WaitBlock.DeadlineNs != 0)
            return EX_SYSINFO_THREAD_STATE_SLEEPING;
        return EX_SYSINFO_THREAD_STATE_BLOCKED;
    case KTHREAD_STATE_TERMINATED:
        return EX_SYSINFO_THREAD_STATE_TERMINATED;
    default:
        return EX_SYSINFO_THREAD_STATE_INVALID;
    }
}

static uint32_t
KiMapRuntimeProcessState(const EX_PROCESS *process, const KTHREAD *thread)
{
    if (process == NULL)
        return EX_SYSINFO_PROCESS_STATE_INVALID;

    if (thread == NULL)
    {
        switch (process->State)
        {
        case EX_PROCESS_STATE_CREATED:
            return EX_SYSINFO_PROCESS_STATE_CREATED;
        case EX_PROCESS_STATE_READY:
            return EX_SYSINFO_PROCESS_STATE_READY;
        case EX_PROCESS_STATE_TERMINATED:
            return EX_SYSINFO_PROCESS_STATE_TERMINATED;
        default:
            return EX_SYSINFO_PROCESS_STATE_INVALID;
        }
    }

    switch (thread->State)
    {
    case KTHREAD_STATE_NEW:
        return EX_SYSINFO_PROCESS_STATE_CREATED;
    case KTHREAD_STATE_READY:
        return EX_SYSINFO_PROCESS_STATE_READY;
    case KTHREAD_STATE_RUNNING:
        return EX_SYSINFO_PROCESS_STATE_RUNNING;
    case KTHREAD_STATE_BLOCKED:
        if (thread->WaitBlock.Dispatcher == NULL && thread->WaitBlock.DeadlineNs != 0)
            return EX_SYSINFO_PROCESS_STATE_SLEEPING;
        return EX_SYSINFO_PROCESS_STATE_BLOCKED;
    case KTHREAD_STATE_TERMINATED:
        return EX_SYSINFO_PROCESS_STATE_TERMINATED;
    default:
        return EX_SYSINFO_PROCESS_STATE_INVALID;
    }
}

static void
KiSortThreadEntries(EX_SYSINFO_THREAD_LIST *threadList)
{
    if (threadList == NULL || threadList->ReturnedCount < 2U)
        return;

    for (uint32_t index = 1; index < threadList->ReturnedCount; ++index)
    {
        EX_SYSINFO_THREAD_ENTRY entry = threadList->Entries[index];
        uint32_t insertIndex = index;

        while (insertIndex != 0U && threadList->Entries[insertIndex - 1U].ThreadId > entry.ThreadId)
        {
            threadList->Entries[insertIndex] = threadList->Entries[insertIndex - 1U];
            --insertIndex;
        }

        threadList->Entries[insertIndex] = entry;
    }
}

static void
KiSortProcessEntries(EX_SYSINFO_PROCESS_LIST *processList)
{
    if (processList == NULL || processList->ReturnedCount < 2U)
        return;

    for (uint32_t index = 1; index < processList->ReturnedCount; ++index)
    {
        EX_SYSINFO_PROCESS_ENTRY entry = processList->Entries[index];
        uint32_t insertIndex = index;

        while (insertIndex != 0U && processList->Entries[insertIndex - 1U].ProcessId > entry.ProcessId)
        {
            processList->Entries[insertIndex] = processList->Entries[insertIndex - 1U];
            --insertIndex;
        }

        processList->Entries[insertIndex] = entry;
    }
}

static uint32_t
KiFindProcessSlotByProcess(const EX_PROCESS *process)
{
    if (process == NULL)
        return EX_RUNTIME_PROCESS_TABLE_CAPACITY;

    for (uint32_t index = 0; index < EX_RUNTIME_PROCESS_TABLE_CAPACITY; ++index)
    {
        if (gExRuntimeProcessTable[index].Active && gExRuntimeProcessTable[index].Process == process)
            return index;
    }

    return EX_RUNTIME_PROCESS_TABLE_CAPACITY;
}

static uint32_t
KiFindProcessSlotByPid(uint32_t processId)
{
    if (processId == 0)
        return EX_RUNTIME_PROCESS_TABLE_CAPACITY;

    for (uint32_t index = 0; index < EX_RUNTIME_PROCESS_TABLE_CAPACITY; ++index)
    {
        EX_PROCESS *process = gExRuntimeProcessTable[index].Process;
        if (gExRuntimeProcessTable[index].Active && process != NULL && process->ProcessId == processId)
            return index;
    }

    return EX_RUNTIME_PROCESS_TABLE_CAPACITY;
}

static uint32_t
KiFindFreeProcessSlot(void)
{
    for (uint32_t index = 0; index < EX_RUNTIME_PROCESS_TABLE_CAPACITY; ++index)
    {
        if (!gExRuntimeProcessTable[index].Active)
            return index;
    }

    return EX_RUNTIME_PROCESS_TABLE_CAPACITY;
}

static uint32_t
KiFindThreadSlotByThreadObject(const EX_THREAD *thread)
{
    if (thread == NULL)
        return EX_RUNTIME_THREAD_TABLE_CAPACITY;

    for (uint32_t index = 0; index < EX_RUNTIME_THREAD_TABLE_CAPACITY; ++index)
    {
        if (gExRuntimeThreadTable[index].Active && gExRuntimeThreadTable[index].Thread == thread)
            return index;
    }

    return EX_RUNTIME_THREAD_TABLE_CAPACITY;
}

static uint32_t
KiFindThreadSlotByThreadId(uint32_t threadId)
{
    if (threadId == 0)
        return EX_RUNTIME_THREAD_TABLE_CAPACITY;

    for (uint32_t index = 0; index < EX_RUNTIME_THREAD_TABLE_CAPACITY; ++index)
    {
        EX_THREAD *thread = gExRuntimeThreadTable[index].Thread;
        if (gExRuntimeThreadTable[index].Active && thread != NULL && thread->ThreadId == threadId)
            return index;
    }

    return EX_RUNTIME_THREAD_TABLE_CAPACITY;
}

static uint32_t
KiFindThreadSlotByKernelThread(const KTHREAD *thread)
{
    if (thread == NULL)
        return EX_RUNTIME_THREAD_TABLE_CAPACITY;

    for (uint32_t index = 0; index < EX_RUNTIME_THREAD_TABLE_CAPACITY; ++index)
    {
        EX_THREAD *runtimeThread = gExRuntimeThreadTable[index].Thread;
        if (gExRuntimeThreadTable[index].Active && runtimeThread != NULL && runtimeThread->Thread == thread)
            return index;
    }

    return EX_RUNTIME_THREAD_TABLE_CAPACITY;
}

static uint32_t
KiFindFreeThreadSlot(void)
{
    for (uint32_t index = 0; index < EX_RUNTIME_THREAD_TABLE_CAPACITY; ++index)
    {
        if (!gExRuntimeThreadTable[index].Active)
            return index;
    }

    return EX_RUNTIME_THREAD_TABLE_CAPACITY;
}

BOOL
ExRuntimeIsPublishedObject(const EX_OBJECT_HEADER *objectHeader)
{
    BOOL isPublished = FALSE;
    KE_CRITICAL_SECTION guard = {0};

    if (objectHeader == NULL)
        return FALSE;

    KeEnterCriticalSection(&guard);

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS:
        isPublished = KiFindProcessSlotByProcess(CONTAINING_RECORD(objectHeader, EX_PROCESS, Header)) <
                      EX_RUNTIME_PROCESS_TABLE_CAPACITY;
        break;
    case EX_OBJECT_TYPE_THREAD:
        isPublished = KiFindThreadSlotByThreadObject(CONTAINING_RECORD(objectHeader, EX_THREAD, Header)) <
                      EX_RUNTIME_THREAD_TABLE_CAPACITY;
        break;
    default:
        isPublished = FALSE;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return isPublished;
}

BOOL
ExRuntimeIsProcessPublished(const EX_PROCESS *process)
{
    BOOL isPublished = FALSE;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    isPublished = KiFindProcessSlotByProcess(process) < EX_RUNTIME_PROCESS_TABLE_CAPACITY;
    KeLeaveCriticalSection(&guard);

    return isPublished;
}

HO_STATUS
ExRuntimeCaptureThreadList(EX_SYSINFO_THREAD_LIST *outThreadList)
{
    KE_CRITICAL_SECTION guard = {0};

    if (outThreadList == NULL)
        return EC_ILLEGAL_ARGUMENT;

    memset(outThreadList, 0, sizeof(*outThreadList));
    outThreadList->Version = EX_SYSINFO_THREAD_LIST_VERSION;
    outThreadList->Size = sizeof(*outThreadList);

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < EX_RUNTIME_THREAD_TABLE_CAPACITY; ++index)
    {
        const EX_RUNTIME_THREAD_TABLE_ENTRY *slot = NULL;
        const EX_THREAD *thread = NULL;
        EX_SYSINFO_THREAD_ENTRY *entry = NULL;

        if (!gExRuntimeThreadTable[index].Active)
            continue;

        slot = &gExRuntimeThreadTable[index];
        thread = slot->Thread;
        if (thread == NULL || thread->Thread == NULL || thread->Process == NULL)
            continue;

        outThreadList->TotalCount++;
        if (outThreadList->ReturnedCount >= EX_SYSINFO_THREAD_LIST_MAX_ENTRIES)
        {
            outThreadList->Truncated = TRUE;
            continue;
        }

        entry = &outThreadList->Entries[outThreadList->ReturnedCount++];
        entry->ThreadId = thread->ThreadId;
        entry->State = KiMapRuntimeThreadState(thread->Thread);
        entry->Priority = thread->Thread->Priority;
        KiCopyRuntimeProgramName(entry->Name, sizeof(entry->Name), KiGetRuntimeProgramName(thread->Process->ProgramId));
    }

    KeLeaveCriticalSection(&guard);
    KiSortThreadEntries(outThreadList);
    return EC_SUCCESS;
}

HO_STATUS
ExRuntimeCaptureProcessList(EX_SYSINFO_PROCESS_LIST *outProcessList)
{
    KE_CRITICAL_SECTION guard = {0};

    if (outProcessList == NULL)
        return EC_ILLEGAL_ARGUMENT;

    memset(outProcessList, 0, sizeof(*outProcessList));
    outProcessList->Version = EX_SYSINFO_PROCESS_LIST_VERSION;
    outProcessList->Size = sizeof(*outProcessList);

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < EX_RUNTIME_PROCESS_TABLE_CAPACITY; ++index)
    {
        const EX_RUNTIME_PROCESS_TABLE_ENTRY *slot = NULL;
        const EX_PROCESS *process = NULL;
        const EX_THREAD *mainThread = NULL;
        EX_SYSINFO_PROCESS_ENTRY *entry = NULL;

        if (!gExRuntimeProcessTable[index].Active)
            continue;

        slot = &gExRuntimeProcessTable[index];
        process = slot->Process;
        if (process == NULL)
            continue;

        mainThread = KiLookupThreadByTidLocked(process->MainThreadId);

        outProcessList->TotalCount++;
        if (outProcessList->ReturnedCount >= EX_SYSINFO_PROCESS_LIST_MAX_ENTRIES)
        {
            outProcessList->Truncated = TRUE;
            continue;
        }

        entry = &outProcessList->Entries[outProcessList->ReturnedCount++];
        entry->ProcessId = process->ProcessId;
        entry->ParentProcessId = process->ParentProcessId;
        entry->MainThreadId = process->MainThreadId;
        entry->State = KiMapRuntimeProcessState(process, mainThread != NULL ? mainThread->Thread : NULL);
        KiCopyRuntimeProgramName(entry->Name, sizeof(entry->Name), KiGetRuntimeProgramName(process->ProgramId));
    }

    KeLeaveCriticalSection(&guard);
    KiSortProcessEntries(outProcessList);
    return EC_SUCCESS;
}

HO_STATUS
ExRuntimePublishThread(EX_PROCESS *process, EX_THREAD *thread)
{
    uint32_t processSlot = 0;
    uint32_t threadSlot = 0;
    HO_STATUS status = EC_SUCCESS;
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL || thread == NULL || thread->Thread == NULL || thread->Process != process ||
        process->ProcessId == 0 || thread->ThreadId == 0)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    KeEnterCriticalSection(&guard);

    if (KiFindProcessSlotByProcess(process) < EX_RUNTIME_PROCESS_TABLE_CAPACITY ||
        KiFindProcessSlotByPid(process->ProcessId) < EX_RUNTIME_PROCESS_TABLE_CAPACITY ||
        KiFindThreadSlotByThreadObject(thread) < EX_RUNTIME_THREAD_TABLE_CAPACITY ||
        KiFindThreadSlotByThreadId(thread->ThreadId) < EX_RUNTIME_THREAD_TABLE_CAPACITY ||
        KiFindThreadSlotByKernelThread(thread->Thread) < EX_RUNTIME_THREAD_TABLE_CAPACITY)
    {
        status = EC_INVALID_STATE;
        goto Exit;
    }

    processSlot = KiFindFreeProcessSlot();
    threadSlot = KiFindFreeThreadSlot();
    if (processSlot >= EX_RUNTIME_PROCESS_TABLE_CAPACITY || threadSlot >= EX_RUNTIME_THREAD_TABLE_CAPACITY)
    {
        status = EC_OUT_OF_RESOURCE;
        goto Exit;
    }

    gExRuntimeProcessTable[processSlot].Active = TRUE;
    gExRuntimeProcessTable[processSlot].Process = process;
    gExRuntimeThreadTable[threadSlot].Active = TRUE;
    gExRuntimeThreadTable[threadSlot].Thread = thread;
    process->MainThreadId = thread->ThreadId;

Exit:
    KeLeaveCriticalSection(&guard);
    return status;
}

EX_THREAD *
ExRuntimeLookupThreadByKernelThread(const KTHREAD *thread)
{
    uint32_t slotIndex = 0;
    EX_THREAD *runtimeThread = NULL;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindThreadSlotByKernelThread(thread);
    if (slotIndex < EX_RUNTIME_THREAD_TABLE_CAPACITY)
        runtimeThread = gExRuntimeThreadTable[slotIndex].Thread;
    KeLeaveCriticalSection(&guard);

    return runtimeThread;
}

EX_PROCESS *
ExRuntimeLookupProcessByKernelThread(const KTHREAD *thread)
{
    EX_THREAD *runtimeThread = ExRuntimeLookupThreadByKernelThread(thread);
    return runtimeThread != NULL ? runtimeThread->Process : NULL;
}

EX_PROCESS *
ExRuntimeLookupProcessByPid(uint32_t processId)
{
    uint32_t slotIndex = 0;
    EX_PROCESS *process = NULL;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindProcessSlotByPid(processId);
    if (slotIndex < EX_RUNTIME_PROCESS_TABLE_CAPACITY)
        process = gExRuntimeProcessTable[slotIndex].Process;
    KeLeaveCriticalSection(&guard);

    return process;
}

HO_STATUS
ExRuntimeRetainChildProcess(uint32_t parentProcessId, uint32_t childProcessId, EX_PROCESS **outProcess)
{
    uint32_t slotIndex = 0;
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_INVALID_STATE;
    KE_CRITICAL_SECTION guard = {0};

    if (outProcess == NULL || childProcessId == 0U)
        return EC_ILLEGAL_ARGUMENT;

    *outProcess = NULL;

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindProcessSlotByPid(childProcessId);
    if (slotIndex < EX_RUNTIME_PROCESS_TABLE_CAPACITY)
    {
        process = gExRuntimeProcessTable[slotIndex].Process;
        if (process != NULL && process->ParentProcessId == parentProcessId &&
            ExObjectRetain(&process->Header, EX_OBJECT_TYPE_PROCESS) == EC_SUCCESS)
        {
            *outProcess = process;
            status = EC_SUCCESS;
        }
    }
    KeLeaveCriticalSection(&guard);

    return status;
}

HO_STATUS
ExRuntimeBorrowProcessMainKernelThread(EX_PROCESS *process, KTHREAD **outThread)
{
    uint32_t slotIndex = 0;
    KTHREAD *kernelThread = NULL;
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL || outThread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outThread = NULL;

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindThreadSlotByThreadId(process->MainThreadId);
    if (slotIndex < EX_RUNTIME_THREAD_TABLE_CAPACITY && gExRuntimeThreadTable[slotIndex].Thread != NULL)
        kernelThread = gExRuntimeThreadTable[slotIndex].Thread->Thread;
    KeLeaveCriticalSection(&guard);

    if (kernelThread == NULL)
        return EC_INVALID_STATE;

    *outThread = kernelThread;
    return EC_SUCCESS;
}

HO_STATUS
ExRuntimeQueryCurrentProcessId(uint32_t *outProcessId)
{
    KTHREAD *thread = KeGetCurrentThread();
    EX_PROCESS *process = NULL;

    if (outProcessId == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outProcessId = 0;

    if (thread == NULL)
        return EC_INVALID_STATE;

    process = ExRuntimeLookupProcessByKernelThread(thread);
    return ExBootstrapQueryProcessId(process, outProcessId);
}

HO_STATUS
ExRuntimeRequestProcessKill(EX_PROCESS *process)
{
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    KeEnterCriticalSection(&guard);
    if (process->KillRequested)
    {
        KeLeaveCriticalSection(&guard);
        return EC_INVALID_STATE;
    }

    process->KillRequested = TRUE;
    KeLeaveCriticalSection(&guard);
    return EC_SUCCESS;
}

BOOL
ExRuntimeShouldTerminateCurrentProcess(uint32_t *outProgramId)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_PROCESS *process = NULL;
    BOOL killRequested = FALSE;
    KE_CRITICAL_SECTION guard = {0};

    if (outProgramId != NULL)
        *outProgramId = EX_PROGRAM_ID_NONE;

    if (currentThread == NULL)
        return FALSE;

    process = ExRuntimeLookupProcessByKernelThread(currentThread);
    if (process == NULL)
        return FALSE;

    KeEnterCriticalSection(&guard);
    killRequested = process->KillRequested;
    if (killRequested && outProgramId != NULL)
        *outProgramId = process->ProgramId;
    KeLeaveCriticalSection(&guard);

    return killRequested;
}

HO_STATUS
ExRuntimeMarkProcessControl(EX_PROCESS *process, BOOL foreground, uint32_t restoreForegroundOwnerThreadId)
{
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    KeEnterCriticalSection(&guard);
    process->Foreground = foreground;
    process->RestoreForegroundOwnerThreadId = restoreForegroundOwnerThreadId;
    KeLeaveCriticalSection(&guard);
    return EC_SUCCESS;
}

HO_STATUS
ExRuntimeMarkCurrentProcessTerminating(EX_PROCESS_TERMINATION_REASON reason, uint64_t exitStatus)
{
    KTHREAD *currentThread = KeGetCurrentThread();
    EX_PROCESS *process = NULL;

    if (currentThread == NULL || reason == EX_PROCESS_TERMINATION_REASON_NONE)
        return EC_ILLEGAL_ARGUMENT;

    process = ExRuntimeLookupProcessByKernelThread(currentThread);
    if (process == NULL)
        return EC_INVALID_STATE;

    return ExRuntimeMarkProcessTerminating(process, reason, exitStatus);
}

HO_STATUS
ExRuntimeMarkProcessTerminating(EX_PROCESS *process, EX_PROCESS_TERMINATION_REASON reason, uint64_t exitStatus)
{
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL || reason == EX_PROCESS_TERMINATION_REASON_NONE)
        return EC_ILLEGAL_ARGUMENT;

    KeEnterCriticalSection(&guard);
    if (process->TerminationReason == EX_PROCESS_TERMINATION_REASON_NONE)
    {
        process->TerminationReason = reason;
        process->ExitStatus = exitStatus;
    }
    KeLeaveCriticalSection(&guard);
    return EC_SUCCESS;
}

HO_STATUS
ExRuntimeMarkProcessTerminated(EX_PROCESS *process)
{
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    KeEnterCriticalSection(&guard);
    if (process->TerminationReason == EX_PROCESS_TERMINATION_REASON_NONE)
    {
        process->TerminationReason = EX_PROCESS_TERMINATION_REASON_EXIT;
        process->ExitStatus = 0;
    }
    process->State = EX_PROCESS_STATE_TERMINATED;
    KeLeaveCriticalSection(&guard);
    return EC_SUCCESS;
}

void
ExRuntimeUnpublishByKernelThread(const KTHREAD *thread, EX_THREAD **outThread, EX_PROCESS **outProcess)
{
    uint32_t threadSlot = 0;
    uint32_t processSlot = 0;
    EX_THREAD *runtimeThread = NULL;
    EX_PROCESS *process = NULL;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    threadSlot = KiFindThreadSlotByKernelThread(thread);
    if (threadSlot < EX_RUNTIME_THREAD_TABLE_CAPACITY)
    {
        runtimeThread = gExRuntimeThreadTable[threadSlot].Thread;
        gExRuntimeThreadTable[threadSlot].Active = FALSE;
        gExRuntimeThreadTable[threadSlot].Thread = NULL;
    }

    if (runtimeThread != NULL)
        process = runtimeThread->Process;

    processSlot = KiFindProcessSlotByProcess(process);
    if (processSlot < EX_RUNTIME_PROCESS_TABLE_CAPACITY)
    {
        gExRuntimeProcessTable[processSlot].Active = FALSE;
        gExRuntimeProcessTable[processSlot].Process = NULL;
    }

    KeLeaveCriticalSection(&guard);

    if (outThread != NULL)
        *outThread = runtimeThread;

    if (outProcess != NULL)
        *outProcess = process;
}

static EX_THREAD *
KiLookupThreadByTidLocked(uint32_t threadId)
{
    uint32_t slotIndex = KiFindThreadSlotByThreadId(threadId);
    if (slotIndex >= EX_RUNTIME_THREAD_TABLE_CAPACITY)
        return NULL;

    return gExRuntimeThreadTable[slotIndex].Thread;
}

HO_STATUS
ExBootstrapQueryCurrentProcessId(uint32_t *outProcessId)
{
    return ExRuntimeQueryCurrentProcessId(outProcessId);
}
