/**
 * HimuOperatingSystem
 *
 * File: ex/runtime_alias.c
 * Description: Temporary Ex bootstrap runtime alias registry.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ke/critical_section.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/user_bootstrap.h>
#include <libc/string.h>

#define EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY 4u

typedef struct EX_BOOTSTRAP_RUNTIME_ALIAS
{
    KTHREAD *ThreadKey;
    EX_PROCESS *Process;
    EX_THREAD *Thread;
} EX_BOOTSTRAP_RUNTIME_ALIAS;

static EX_BOOTSTRAP_RUNTIME_ALIAS gExBootstrapRuntimeAliases[EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY] = {0};

static void
KiCopyBootstrapProgramName(char *destination, size_t destinationSize, const char *source)
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
KiGetBootstrapProgramName(uint32_t programId)
{
    switch (programId)
    {
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_HSH:
        return "hsh";
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_CALC:
        return "calc";
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_TICK1S:
        return "tick1s";
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_DE:
        return "fault_de";
    case KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_FAULT_PF:
        return "fault_pf";
    default:
        return "user";
    }
}

static uint32_t
KiMapBootstrapThreadState(const KTHREAD *thread)
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

static BOOL
KiIsRuntimeAliasSlotFree(uint32_t slotIndex)
{
    EX_BOOTSTRAP_RUNTIME_ALIAS *slot = NULL;

    if (slotIndex >= EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY)
        return FALSE;

    slot = &gExBootstrapRuntimeAliases[slotIndex];
    return slot->ThreadKey == NULL && slot->Process == NULL && slot->Thread == NULL;
}

static BOOL
KiIsRuntimeAliasSlotPopulated(uint32_t slotIndex)
{
    return !KiIsRuntimeAliasSlotFree(slotIndex);
}

static uint32_t
KiFindRuntimeAliasSlotByThreadKey(const KTHREAD *thread)
{
    uint32_t index = 0;

    if (thread == NULL)
        return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;

    for (index = 0; index < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY; ++index)
    {
        if (!KiIsRuntimeAliasSlotPopulated(index))
            continue;

        if (gExBootstrapRuntimeAliases[index].ThreadKey == thread)
            return index;
    }

    return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
}

static uint32_t
KiFindRuntimeAliasSlotByProcess(const EX_PROCESS *process)
{
    uint32_t index = 0;

    if (process == NULL)
        return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;

    for (index = 0; index < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY; ++index)
    {
        if (!KiIsRuntimeAliasSlotPopulated(index))
            continue;

        if (gExBootstrapRuntimeAliases[index].Process == process)
            return index;
    }

    return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
}

static uint32_t
KiFindRuntimeAliasSlotByThreadObject(const EX_THREAD *thread)
{
    uint32_t index = 0;

    if (thread == NULL)
        return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;

    for (index = 0; index < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY; ++index)
    {
        if (!KiIsRuntimeAliasSlotPopulated(index))
            continue;

        if (gExBootstrapRuntimeAliases[index].Thread == thread)
            return index;
    }

    return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
}

static uint32_t
KiFindFreeRuntimeAliasSlot(void)
{
    uint32_t index = 0;

    for (index = 0; index < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY; ++index)
    {
        if (KiIsRuntimeAliasSlotFree(index))
            return index;
    }

    return EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
}

BOOL
ExBootstrapIsRuntimeAliasObject(const EX_OBJECT_HEADER *objectHeader)
{
    uint32_t slotIndex = 0;
    BOOL isRuntimeAlias = FALSE;
    KE_CRITICAL_SECTION guard = {0};

    if (objectHeader == NULL)
        return FALSE;

    KeEnterCriticalSection(&guard);

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS:
        slotIndex = KiFindRuntimeAliasSlotByProcess(CONTAINING_RECORD(objectHeader, EX_PROCESS, Header));
        isRuntimeAlias = slotIndex < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
        break;
    case EX_OBJECT_TYPE_THREAD:
        slotIndex = KiFindRuntimeAliasSlotByThreadObject(CONTAINING_RECORD(objectHeader, EX_THREAD, Header));
        isRuntimeAlias = slotIndex < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
        break;
    default:
        isRuntimeAlias = FALSE;
        break;
    }

    KeLeaveCriticalSection(&guard);
    return isRuntimeAlias;
}

BOOL
ExBootstrapHasRuntimeAlias(void)
{
    uint32_t index = 0;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);

    for (index = 0; index < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY; ++index)
    {
        if (KiIsRuntimeAliasSlotPopulated(index))
        {
            KeLeaveCriticalSection(&guard);
            return TRUE;
        }
    }

    KeLeaveCriticalSection(&guard);
    return FALSE;
}

HO_STATUS
ExBootstrapCaptureThreadList(EX_SYSINFO_THREAD_LIST *outThreadList)
{
    KE_CRITICAL_SECTION guard = {0};

    if (outThreadList == NULL)
        return EC_ILLEGAL_ARGUMENT;

    memset(outThreadList, 0, sizeof(*outThreadList));
    outThreadList->Version = EX_SYSINFO_THREAD_LIST_VERSION;
    outThreadList->Size = sizeof(*outThreadList);

    KeEnterCriticalSection(&guard);

    for (uint32_t index = 0; index < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY; ++index)
    {
        const EX_BOOTSTRAP_RUNTIME_ALIAS *slot = NULL;
        EX_SYSINFO_THREAD_ENTRY *entry = NULL;

        if (!KiIsRuntimeAliasSlotPopulated(index))
            continue;

        slot = &gExBootstrapRuntimeAliases[index];
        if (slot->ThreadKey == NULL || slot->Process == NULL)
            continue;

        outThreadList->TotalCount++;
        if (outThreadList->ReturnedCount >= EX_SYSINFO_THREAD_LIST_MAX_ENTRIES)
        {
            outThreadList->Truncated = TRUE;
            continue;
        }

        entry = &outThreadList->Entries[outThreadList->ReturnedCount++];
        entry->ThreadId = slot->ThreadKey->ThreadId;
        entry->State = KiMapBootstrapThreadState(slot->ThreadKey);
        entry->Priority = slot->ThreadKey->Priority;
        KiCopyBootstrapProgramName(entry->Name, sizeof(entry->Name),
                                   KiGetBootstrapProgramName(slot->Process->ProgramId));
    }

    KeLeaveCriticalSection(&guard);
    KiSortThreadEntries(outThreadList);
    return EC_SUCCESS;
}

BOOL
ExBootstrapRuntimeAliasMatchesProcess(const EX_PROCESS *process)
{
    BOOL matches = FALSE;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    matches = KiFindRuntimeAliasSlotByProcess(process) < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY;
    KeLeaveCriticalSection(&guard);

    return matches;
}

EX_THREAD *
ExBootstrapLookupRuntimeThread(const KTHREAD *thread)
{
    uint32_t slotIndex = 0;
    EX_THREAD *runtimeThread = NULL;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindRuntimeAliasSlotByThreadKey(thread);

    if (slotIndex >= EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY)
    {
        KeLeaveCriticalSection(&guard);
        return NULL;
    }

    runtimeThread = gExBootstrapRuntimeAliases[slotIndex].Thread;
    KeLeaveCriticalSection(&guard);

    return runtimeThread;
}

EX_PROCESS *
ExBootstrapLookupRuntimeProcess(const KTHREAD *thread)
{
    uint32_t slotIndex = 0;
    EX_PROCESS *process = NULL;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindRuntimeAliasSlotByThreadKey(thread);
    if (slotIndex < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY)
        process = gExBootstrapRuntimeAliases[slotIndex].Process;
    KeLeaveCriticalSection(&guard);

    return process;
}

HO_STATUS
ExBootstrapPublishRuntimeAlias(EX_PROCESS *process, EX_THREAD *thread)
{
    uint32_t slotIndex = 0;
    HO_STATUS status = EC_SUCCESS;
    KE_CRITICAL_SECTION guard = {0};

    if (process == NULL || thread == NULL || thread->Thread == NULL || thread->Process != process)
        return EC_ILLEGAL_ARGUMENT;

    KeEnterCriticalSection(&guard);

    if (KiFindRuntimeAliasSlotByThreadKey(thread->Thread) < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY ||
        KiFindRuntimeAliasSlotByProcess(process) < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY ||
        KiFindRuntimeAliasSlotByThreadObject(thread) < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY)
    {
        status = EC_INVALID_STATE;
        goto Exit;
    }

    slotIndex = KiFindFreeRuntimeAliasSlot();
    if (slotIndex >= EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY)
    {
        status = EC_OUT_OF_RESOURCE;
        goto Exit;
    }

    gExBootstrapRuntimeAliases[slotIndex].ThreadKey = thread->Thread;
    gExBootstrapRuntimeAliases[slotIndex].Process = process;
    gExBootstrapRuntimeAliases[slotIndex].Thread = thread;

Exit:
    KeLeaveCriticalSection(&guard);
    return status;
}

void
ExBootstrapUnpublishRuntimeAlias(const KTHREAD *thread, EX_THREAD **outThread, EX_PROCESS **outProcess)
{
    uint32_t slotIndex = 0;
    EX_THREAD *runtimeThread = NULL;
    EX_PROCESS *process = NULL;
    KE_CRITICAL_SECTION guard = {0};

    KeEnterCriticalSection(&guard);
    slotIndex = KiFindRuntimeAliasSlotByThreadKey(thread);

    if (slotIndex < EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY)
    {
        runtimeThread = gExBootstrapRuntimeAliases[slotIndex].Thread;
        process = gExBootstrapRuntimeAliases[slotIndex].Process;

        gExBootstrapRuntimeAliases[slotIndex].ThreadKey = NULL;
        gExBootstrapRuntimeAliases[slotIndex].Process = NULL;
        gExBootstrapRuntimeAliases[slotIndex].Thread = NULL;
    }

    KeLeaveCriticalSection(&guard);

    if (outThread != NULL)
        *outThread = runtimeThread;

    if (outProcess != NULL)
        *outProcess = process;
}
