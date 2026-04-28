/**
 * HimuOperatingSystem
 *
 * File: ex/handle.c
 * Description: Ex bootstrap private handle table and rights checks.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <libc/string.h>

#define EX_PRIVATE_HANDLE_INDEX_BITS      8u
#define EX_PRIVATE_HANDLE_INDEX_MASK      ((EX_PRIVATE_HANDLE)0x000000FFu)
#define EX_PRIVATE_HANDLE_GENERATION_MASK ((uint32_t)0x00FFFFFFu)

#if EX_PRIVATE_HANDLE_TABLE_CAPACITY > 0xFFu
#error EX_PRIVATE_HANDLE_TABLE_CAPACITY exceeds the internal handle encoding range.
#endif

static uint32_t
KiAdvancePrivateHandleGeneration(uint32_t generation)
{
    generation = (generation + 1u) & EX_PRIVATE_HANDLE_GENERATION_MASK;
    if (generation == 0)
        generation = 1u;

    return generation;
}

static EX_PRIVATE_HANDLE
KiEncodePrivateHandle(uint32_t slotIndex, uint32_t generation)
{
    if (slotIndex >= EX_PRIVATE_HANDLE_TABLE_CAPACITY || generation == 0)
        return EX_PRIVATE_HANDLE_INVALID;

    return (EX_PRIVATE_HANDLE)((((EX_PRIVATE_HANDLE)generation) << EX_PRIVATE_HANDLE_INDEX_BITS) |
                               (EX_PRIVATE_HANDLE)(slotIndex + 1u));
}

static BOOL
KiDecodePrivateHandle(EX_PRIVATE_HANDLE handle, uint32_t *slotIndex, uint32_t *generation)
{
    EX_PRIVATE_HANDLE encodedIndex = 0;
    uint32_t decodedGeneration = 0;

    if (slotIndex == NULL || generation == NULL || handle == EX_PRIVATE_HANDLE_INVALID)
        return FALSE;

    encodedIndex = handle & EX_PRIVATE_HANDLE_INDEX_MASK;
    decodedGeneration = (uint32_t)(handle >> EX_PRIVATE_HANDLE_INDEX_BITS) & EX_PRIVATE_HANDLE_GENERATION_MASK;

    if (encodedIndex == 0 || decodedGeneration == 0)
        return FALSE;

    encodedIndex--;
    if (encodedIndex >= EX_PRIVATE_HANDLE_TABLE_CAPACITY)
        return FALSE;

    *slotIndex = (uint32_t)encodedIndex;
    *generation = decodedGeneration;
    return TRUE;
}

static EX_PRIVATE_HANDLE_SLOT *
KiLookupPrivateHandleSlot(EX_PROCESS *process, EX_PRIVATE_HANDLE handle, uint32_t *generation)
{
    uint32_t slotIndex = 0;

    if (process == NULL || generation == NULL)
        return NULL;

    if (!KiDecodePrivateHandle(handle, &slotIndex, generation))
        return NULL;

    return &process->HandleTable.Slots[slotIndex];
}

static HO_STATUS
KiRetainHandleObject(EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (objectHeader->Type != EX_OBJECT_TYPE_PROCESS && objectHeader->Type != EX_OBJECT_TYPE_THREAD &&
        objectHeader->Type != EX_OBJECT_TYPE_STDOUT_SERVICE && objectHeader->Type != EX_OBJECT_TYPE_WAITABLE)
        return EC_INVALID_STATE;

    return ExBootstrapRetainObject(objectHeader, objectHeader->Type);
}

static HO_STATUS
KiReleaseHandleObject(EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS:
        return ExBootstrapReleaseProcess(CONTAINING_RECORD(objectHeader, EX_PROCESS, Header));
    case EX_OBJECT_TYPE_THREAD:
        return ExBootstrapReleaseThread(CONTAINING_RECORD(objectHeader, EX_THREAD, Header));
    case EX_OBJECT_TYPE_STDOUT_SERVICE: {
        EX_STDOUT_SERVICE *stdoutService = CONTAINING_RECORD(objectHeader, EX_STDOUT_SERVICE, Header);
        uint32_t remainingReferences = 0;

        (void)stdoutService;
        return ExBootstrapReleaseObject(objectHeader, EX_OBJECT_TYPE_STDOUT_SERVICE, &remainingReferences);
    }
    case EX_OBJECT_TYPE_WAITABLE: {
        uint32_t remainingReferences = 0;

        return ExBootstrapReleaseObject(objectHeader, EX_OBJECT_TYPE_WAITABLE, &remainingReferences);
    }
    default:
        return EC_INVALID_STATE;
    }
}

static HO_STATUS
KiValidateClosePrivateHandleSlot(const EX_PRIVATE_HANDLE_SLOT *slot)
{
    EX_OBJECT_HEADER *objectHeader = NULL;

    if (slot == NULL || slot->Object == NULL)
        return EC_INVALID_STATE;

    if ((slot->Rights & EX_PRIVATE_HANDLE_RIGHT_CLOSE) != EX_PRIVATE_HANDLE_RIGHT_CLOSE)
        return EC_INVALID_STATE;

    objectHeader = slot->Object;
    if (ExBootstrapIsRuntimeAliasObject(objectHeader))
        return EC_INVALID_STATE;

    if (objectHeader->Type != EX_OBJECT_TYPE_PROCESS && objectHeader->Type != EX_OBJECT_TYPE_THREAD &&
        objectHeader->Type != EX_OBJECT_TYPE_STDOUT_SERVICE && objectHeader->Type != EX_OBJECT_TYPE_WAITABLE)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

static void
KiInvalidateObjectSelfHandleIfMatch(EX_OBJECT_HEADER *objectHeader, EX_PRIVATE_HANDLE handle)
{
    if (objectHeader == NULL || handle == EX_PRIVATE_HANDLE_INVALID)
        return;

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS: {
        EX_PROCESS *process = CONTAINING_RECORD(objectHeader, EX_PROCESS, Header);
        if (process->SelfHandle == handle)
            process->SelfHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_THREAD: {
        EX_THREAD *thread = CONTAINING_RECORD(objectHeader, EX_THREAD, Header);
        if (thread->SelfHandle == handle)
            thread->SelfHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_STDOUT_SERVICE: {
        EX_STDOUT_SERVICE *stdoutService = CONTAINING_RECORD(objectHeader, EX_STDOUT_SERVICE, Header);
        if (stdoutService->Owner != NULL && stdoutService->Owner->StdoutHandle == handle)
            stdoutService->Owner->StdoutHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_WAITABLE: {
        EX_WAITABLE_OBJECT *waitObject = CONTAINING_RECORD(objectHeader, EX_WAITABLE_OBJECT, Header);
        if (waitObject->Owner != NULL && waitObject->Owner->WaitHandle == handle)
            waitObject->Owner->WaitHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    default:
        break;
    }
}

static HO_STATUS
KiCleanupPrivateHandleBacking(EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (objectHeader->Type != EX_OBJECT_TYPE_WAITABLE)
        return EC_SUCCESS;

    return ExBootstrapCleanupWaitableBacking(CONTAINING_RECORD(objectHeader, EX_WAITABLE_OBJECT, Header));
}

static void
KiClearPrivateHandleSlot(EX_PRIVATE_HANDLE_SLOT *slot)
{
    if (slot == NULL)
        return;

    slot->Object = NULL;
    slot->Rights = EX_PRIVATE_HANDLE_RIGHT_NONE;
    slot->Generation = KiAdvancePrivateHandleGeneration(slot->Generation);
}

void
ExBootstrapInitializePrivateHandleTable(EX_PRIVATE_HANDLE_TABLE *table)
{
    if (table == NULL)
        return;

    memset(table, 0, sizeof(*table));
}

HO_STATUS
ExBootstrapInsertPrivateHandle(EX_PROCESS *process,
                               EX_OBJECT_HEADER *objectHeader,
                               EX_PRIVATE_HANDLE_RIGHTS rights,
                               EX_PRIVATE_HANDLE *outHandle)
{
    EX_PRIVATE_HANDLE_SLOT *slot = NULL;
    uint32_t slotIndex = 0;

    if (process == NULL || objectHeader == NULL || outHandle == NULL || rights == EX_PRIVATE_HANDLE_RIGHT_NONE)
        return EC_ILLEGAL_ARGUMENT;

    *outHandle = EX_PRIVATE_HANDLE_INVALID;

    for (slotIndex = 0; slotIndex < EX_PRIVATE_HANDLE_TABLE_CAPACITY; slotIndex++)
    {
        if (process->HandleTable.Slots[slotIndex].Object == NULL)
        {
            slot = &process->HandleTable.Slots[slotIndex];
            break;
        }
    }

    if (slot == NULL)
        return EC_OUT_OF_RESOURCE;

    HO_STATUS status = KiRetainHandleObject(objectHeader);
    if (status != EC_SUCCESS)
        return status;

    if (slot->Generation == 0)
        slot->Generation = KiAdvancePrivateHandleGeneration(0);

    slot->Object = objectHeader;
    slot->Rights = rights;
    *outHandle = KiEncodePrivateHandle(slotIndex, slot->Generation);
    return *outHandle == EX_PRIVATE_HANDLE_INVALID ? EC_INVALID_STATE : EC_SUCCESS;
}

HO_STATUS
ExBootstrapResolvePrivateHandle(EX_PROCESS *process,
                                EX_PRIVATE_HANDLE handle,
                                EX_OBJECT_TYPE expectedType,
                                EX_PRIVATE_HANDLE_RIGHTS desiredRights,
                                EX_OBJECT_HEADER **outObjectHeader)
{
    EX_PRIVATE_HANDLE_SLOT *slot = NULL;
    uint32_t generation = 0;

    if (process == NULL || outObjectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outObjectHeader = NULL;

    if (expectedType != EX_OBJECT_TYPE_PROCESS && expectedType != EX_OBJECT_TYPE_THREAD &&
        expectedType != EX_OBJECT_TYPE_STDOUT_SERVICE && expectedType != EX_OBJECT_TYPE_WAITABLE)
        return EC_ILLEGAL_ARGUMENT;

    slot = KiLookupPrivateHandleSlot(process, handle, &generation);
    if (slot == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (slot->Object == NULL || slot->Generation != generation)
        return EC_INVALID_STATE;

    if ((slot->Rights & desiredRights) != desiredRights)
        return EC_INVALID_STATE;

    HO_STATUS status = ExBootstrapRetainObject(slot->Object, expectedType);
    if (status != EC_SUCCESS)
        return status;

    *outObjectHeader = slot->Object;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapReleaseResolvedObject(EX_OBJECT_HEADER *objectHeader)
{
    return KiReleaseHandleObject(objectHeader);
}

HO_STATUS
ExBootstrapClosePrivateHandle(EX_PROCESS *process, EX_PRIVATE_HANDLE *handle)
{
    EX_PROCESS *ownerReference = NULL;
    EX_PRIVATE_HANDLE localHandle = EX_PRIVATE_HANDLE_INVALID;
    EX_PRIVATE_HANDLE_SLOT *slot = NULL;
    EX_OBJECT_HEADER *objectHeader = NULL;
    uint32_t generation = 0;
    HO_STATUS status = EC_SUCCESS;

    if (process == NULL || handle == NULL)
        return EC_ILLEGAL_ARGUMENT;

    localHandle = *handle;
    if (localHandle == EX_PRIVATE_HANDLE_INVALID)
        return EC_SUCCESS;

    ownerReference = ExBootstrapRetainProcess(process);
    if (ownerReference == NULL)
        return EC_INVALID_STATE;

    slot = KiLookupPrivateHandleSlot(process, localHandle, &generation);
    if (slot == NULL)
    {
        status = EC_ILLEGAL_ARGUMENT;
        goto Exit;
    }

    if (slot->Object == NULL || slot->Generation != generation)
    {
        status = EC_INVALID_STATE;
        goto Exit;
    }

    objectHeader = slot->Object;
    status = KiValidateClosePrivateHandleSlot(slot);
    if (status != EC_SUCCESS)
        goto Exit;

    status = KiCleanupPrivateHandleBacking(objectHeader);
    if (status != EC_SUCCESS)
        goto Exit;

    KiInvalidateObjectSelfHandleIfMatch(objectHeader, localHandle);
    KiClearPrivateHandleSlot(slot);
    *handle = EX_PRIVATE_HANDLE_INVALID;
    status = KiReleaseHandleObject(objectHeader);

Exit: {
    HO_STATUS ownerStatus = ExBootstrapReleaseProcess(ownerReference);
    if (status == EC_SUCCESS)
        status = ownerStatus;
}

    return status;
}

HO_STATUS
ExBootstrapCloseAllPrivateHandles(EX_PROCESS *process)
{
    EX_PROCESS *ownerReference = NULL;
    HO_STATUS firstError = EC_SUCCESS;
    HO_STATUS ownerStatus = EC_SUCCESS;
    uint32_t slotIndex = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    ownerReference = ExBootstrapRetainProcess(process);
    if (ownerReference == NULL)
        return EC_INVALID_STATE;

    for (slotIndex = 0; slotIndex < EX_PRIVATE_HANDLE_TABLE_CAPACITY; slotIndex++)
    {
        EX_PRIVATE_HANDLE_SLOT *slot = &process->HandleTable.Slots[slotIndex];

        if (slot->Object == NULL)
            continue;

        firstError = KiValidateClosePrivateHandleSlot(slot);
        if (firstError != EC_SUCCESS)
            goto Exit;
    }

    for (slotIndex = 0; slotIndex < EX_PRIVATE_HANDLE_TABLE_CAPACITY; slotIndex++)
    {
        EX_PRIVATE_HANDLE_SLOT *slot = &process->HandleTable.Slots[slotIndex];
        EX_OBJECT_HEADER *objectHeader = slot->Object;
        EX_PRIVATE_HANDLE handle = KiEncodePrivateHandle(slotIndex, slot->Generation);

        if (objectHeader == NULL)
            continue;

        HO_STATUS status = KiCleanupPrivateHandleBacking(objectHeader);
        if (status != EC_SUCCESS)
        {
            firstError = status;
            goto Exit;
        }

        KiInvalidateObjectSelfHandleIfMatch(objectHeader, handle);
        KiClearPrivateHandleSlot(slot);

        status = KiReleaseHandleObject(objectHeader);
        if (firstError == EC_SUCCESS)
            firstError = status;
    }

Exit:
    ownerStatus = ExBootstrapReleaseProcess(ownerReference);
    if (firstError == EC_SUCCESS)
        firstError = ownerStatus;

    return firstError;
}
