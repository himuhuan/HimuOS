/**
 * HimuOperatingSystem
 *
 * File: ex/handle.c
 * Description: Executive Lite process-owned handle table and rights checks.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "runtime_internal.h"

#include <libc/string.h>

#define EX_HANDLE_INDEX_BITS      8u
#define EX_HANDLE_INDEX_MASK      ((EX_HANDLE)0x000000FFu)
#define EX_HANDLE_GENERATION_MASK ((uint32_t)0x00FFFFFFu)

#if EX_MAX_HANDLES_PER_PROCESS > 0xFFu
#error EX_MAX_HANDLES_PER_PROCESS exceeds the internal handle encoding range.
#endif

static uint32_t
KiAdvanceHandleGeneration(uint32_t generation)
{
    generation = (generation + 1u) & EX_HANDLE_GENERATION_MASK;
    if (generation == 0)
        generation = 1u;

    return generation;
}

static EX_HANDLE
KiEncodeHandle(uint32_t slotIndex, uint32_t generation)
{
    if (slotIndex >= EX_MAX_HANDLES_PER_PROCESS || generation == 0)
        return EX_HANDLE_INVALID;

    return (EX_HANDLE)((((EX_HANDLE)generation) << EX_HANDLE_INDEX_BITS) | (EX_HANDLE)(slotIndex + 1u));
}

static BOOL
KiDecodeHandle(EX_HANDLE handle, uint32_t *slotIndex, uint32_t *generation)
{
    EX_HANDLE encodedIndex = 0;
    uint32_t decodedGeneration = 0;

    if (slotIndex == NULL || generation == NULL || handle == EX_HANDLE_INVALID)
        return FALSE;

    encodedIndex = handle & EX_HANDLE_INDEX_MASK;
    decodedGeneration = (uint32_t)(handle >> EX_HANDLE_INDEX_BITS) & EX_HANDLE_GENERATION_MASK;

    if (encodedIndex == 0 || decodedGeneration == 0)
        return FALSE;

    encodedIndex--;
    if (encodedIndex >= EX_MAX_HANDLES_PER_PROCESS)
        return FALSE;

    *slotIndex = (uint32_t)encodedIndex;
    *generation = decodedGeneration;
    return TRUE;
}

static EX_HANDLE_SLOT *
KiLookupHandleSlot(EX_PROCESS *process, EX_HANDLE handle, uint32_t *generation)
{
    uint32_t slotIndex = 0;

    if (process == NULL || generation == NULL)
        return NULL;

    if (!KiDecodeHandle(handle, &slotIndex, generation))
        return NULL;

    return &process->HandleTable.Slots[slotIndex];
}

static HO_STATUS
KiRetainHandleObject(EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!ExObjectIsValidType(objectHeader->Type))
        return EC_INVALID_STATE;

    return ExObjectRetain(objectHeader, objectHeader->Type);
}

static HO_STATUS
KiReleaseHandleObject(EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS:
        return ExRuntimeReleaseProcess(CONTAINING_RECORD(objectHeader, EX_PROCESS, Header));
    case EX_OBJECT_TYPE_THREAD:
        return ExRuntimeReleaseThread(CONTAINING_RECORD(objectHeader, EX_THREAD, Header));
    case EX_OBJECT_TYPE_CONSOLE: {
        uint32_t remainingReferences = 0;

        return ExObjectRelease(objectHeader, EX_OBJECT_TYPE_CONSOLE, &remainingReferences);
    }
    default:
        return EC_INVALID_STATE;
    }
}

static HO_STATUS
KiValidateCloseHandleSlot(const EX_HANDLE_SLOT *slot, BOOL allowPublishedObjectClose)
{
    EX_OBJECT_HEADER *objectHeader = NULL;
    const EX_HANDLE_RIGHTS identityRights = EX_HANDLE_RIGHT_PROCESS_SELF | EX_HANDLE_RIGHT_THREAD_SELF;

    if (slot == NULL || slot->Object == NULL)
        return EC_INVALID_STATE;

    if ((slot->Rights & EX_HANDLE_RIGHT_CLOSE) != EX_HANDLE_RIGHT_CLOSE)
        return EC_INVALID_STATE;

    objectHeader = slot->Object;
    if (!allowPublishedObjectClose && ExRuntimeIsPublishedObject(objectHeader) &&
        (slot->Rights & identityRights) != EX_HANDLE_RIGHT_NONE)
    {
        return EC_INVALID_STATE;
    }

    if (!ExObjectIsValidType(objectHeader->Type))
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

static void
KiInvalidateObjectSelfHandleIfMatch(EX_OBJECT_HEADER *objectHeader, EX_HANDLE handle)
{
    if (objectHeader == NULL || handle == EX_HANDLE_INVALID)
        return;

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS: {
        EX_PROCESS *process = CONTAINING_RECORD(objectHeader, EX_PROCESS, Header);
        if (process->SelfHandle == handle)
            process->SelfHandle = EX_HANDLE_INVALID;
        if (process->WaitHandle == handle)
            process->WaitHandle = EX_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_THREAD: {
        EX_THREAD *thread = CONTAINING_RECORD(objectHeader, EX_THREAD, Header);
        if (thread->SelfHandle == handle)
            thread->SelfHandle = EX_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_CONSOLE: {
        EX_STDOUT_SERVICE *stdoutService = CONTAINING_RECORD(objectHeader, EX_STDOUT_SERVICE, Header);
        if (stdoutService->Owner != NULL && stdoutService->Owner->StdoutHandle == handle)
            stdoutService->Owner->StdoutHandle = EX_HANDLE_INVALID;
        break;
    }
    default:
        break;
    }
}

static BOOL
KiObjectTypeIsWaitable(EX_OBJECT_TYPE type)
{
    return type == EX_OBJECT_TYPE_PROCESS || type == EX_OBJECT_TYPE_THREAD;
}

static void
KiClearHandleSlot(EX_HANDLE_SLOT *slot)
{
    if (slot == NULL)
        return;

    slot->Object = NULL;
    slot->Rights = EX_HANDLE_RIGHT_NONE;
    slot->Generation = KiAdvanceHandleGeneration(slot->Generation);
}

void
ExHandleInitializeTable(EX_HANDLE_TABLE *table)
{
    if (table == NULL)
        return;

    memset(table, 0, sizeof(*table));
}

HO_STATUS
ExHandleInsert(EX_PROCESS *process, EX_OBJECT_HEADER *objectHeader, EX_HANDLE_RIGHTS rights, EX_HANDLE *outHandle)
{
    EX_HANDLE_SLOT *slot = NULL;
    uint32_t slotIndex = 0;

    if (process == NULL || objectHeader == NULL || outHandle == NULL || rights == EX_HANDLE_RIGHT_NONE)
        return EC_ILLEGAL_ARGUMENT;

    *outHandle = EX_HANDLE_INVALID;

    for (slotIndex = 0; slotIndex < EX_MAX_HANDLES_PER_PROCESS; slotIndex++)
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
        slot->Generation = KiAdvanceHandleGeneration(0);

    slot->Object = objectHeader;
    slot->Rights = rights;
    *outHandle = KiEncodeHandle(slotIndex, slot->Generation);
    return *outHandle == EX_HANDLE_INVALID ? EC_INVALID_STATE : EC_SUCCESS;
}

HO_STATUS
ExHandleResolve(EX_PROCESS *process,
                EX_HANDLE handle,
                EX_OBJECT_TYPE expectedType,
                EX_HANDLE_RIGHTS desiredRights,
                EX_OBJECT_HEADER **outObjectHeader)
{
    EX_HANDLE_SLOT *slot = NULL;
    uint32_t generation = 0;

    if (process == NULL || outObjectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outObjectHeader = NULL;

    if (!ExObjectIsValidType(expectedType))
        return EC_ILLEGAL_ARGUMENT;

    slot = KiLookupHandleSlot(process, handle, &generation);
    if (slot == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (slot->Object == NULL || slot->Generation != generation)
        return EC_INVALID_STATE;

    if ((slot->Rights & desiredRights) != desiredRights)
        return EC_INVALID_STATE;

    HO_STATUS status = ExObjectRetain(slot->Object, expectedType);
    if (status != EC_SUCCESS)
        return status;

    *outObjectHeader = slot->Object;
    return EC_SUCCESS;
}

HO_STATUS
ExHandleResolveWaitable(EX_PROCESS *process,
                        EX_HANDLE handle,
                        EX_HANDLE_RIGHTS desiredRights,
                        EX_OBJECT_HEADER **outObjectHeader)
{
    EX_HANDLE_SLOT *slot = NULL;
    uint32_t generation = 0;

    if (process == NULL || outObjectHeader == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outObjectHeader = NULL;

    slot = KiLookupHandleSlot(process, handle, &generation);
    if (slot == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (slot->Object == NULL || slot->Generation != generation)
        return EC_INVALID_STATE;

    if ((slot->Rights & desiredRights) != desiredRights)
        return EC_INVALID_STATE;

    if (!KiObjectTypeIsWaitable(slot->Object->Type))
        return EC_INVALID_STATE;

    HO_STATUS status = ExObjectRetain(slot->Object, slot->Object->Type);
    if (status != EC_SUCCESS)
        return status;

    *outObjectHeader = slot->Object;
    return EC_SUCCESS;
}

HO_STATUS
ExHandleReleaseResolvedObject(EX_OBJECT_HEADER *objectHeader)
{
    return KiReleaseHandleObject(objectHeader);
}

HO_STATUS
ExHandleClose(EX_PROCESS *process, EX_HANDLE *handle)
{
    EX_PROCESS *ownerReference = NULL;
    EX_HANDLE localHandle = EX_HANDLE_INVALID;
    EX_HANDLE_SLOT *slot = NULL;
    EX_OBJECT_HEADER *objectHeader = NULL;
    uint32_t generation = 0;
    HO_STATUS status = EC_SUCCESS;

    if (process == NULL || handle == NULL)
        return EC_ILLEGAL_ARGUMENT;

    localHandle = *handle;
    if (localHandle == EX_HANDLE_INVALID)
        return EC_SUCCESS;

    ownerReference = ExRuntimeRetainProcess(process);
    if (ownerReference == NULL)
        return EC_INVALID_STATE;

    slot = KiLookupHandleSlot(process, localHandle, &generation);
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
    status = KiValidateCloseHandleSlot(slot, FALSE);
    if (status != EC_SUCCESS)
        goto Exit;

    KiInvalidateObjectSelfHandleIfMatch(objectHeader, localHandle);
    KiClearHandleSlot(slot);
    *handle = EX_HANDLE_INVALID;
    status = KiReleaseHandleObject(objectHeader);

Exit: {
    HO_STATUS ownerStatus = ExRuntimeReleaseProcess(ownerReference);
    if (status == EC_SUCCESS)
        status = ownerStatus;
}

    return status;
}

static HO_STATUS
KiHandleCloseAll(EX_PROCESS *process, BOOL allowPublishedObjectClose)
{
    EX_PROCESS *ownerReference = NULL;
    HO_STATUS firstError = EC_SUCCESS;
    HO_STATUS ownerStatus = EC_SUCCESS;
    uint32_t slotIndex = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    ownerReference = ExRuntimeRetainProcess(process);
    if (ownerReference == NULL)
        return EC_INVALID_STATE;

    for (slotIndex = 0; slotIndex < EX_MAX_HANDLES_PER_PROCESS; slotIndex++)
    {
        EX_HANDLE_SLOT *slot = &process->HandleTable.Slots[slotIndex];

        if (slot->Object == NULL)
            continue;

        firstError = KiValidateCloseHandleSlot(slot, allowPublishedObjectClose);
        if (firstError != EC_SUCCESS)
            goto Exit;
    }

    for (slotIndex = 0; slotIndex < EX_MAX_HANDLES_PER_PROCESS; slotIndex++)
    {
        EX_HANDLE_SLOT *slot = &process->HandleTable.Slots[slotIndex];
        EX_OBJECT_HEADER *objectHeader = slot->Object;
        EX_HANDLE handle = KiEncodeHandle(slotIndex, slot->Generation);

        if (objectHeader == NULL)
            continue;

        KiInvalidateObjectSelfHandleIfMatch(objectHeader, handle);
        KiClearHandleSlot(slot);

        HO_STATUS status = KiReleaseHandleObject(objectHeader);
        if (firstError == EC_SUCCESS)
            firstError = status;
    }

Exit:
    ownerStatus = ExRuntimeReleaseProcess(ownerReference);
    if (firstError == EC_SUCCESS)
        firstError = ownerStatus;

    return firstError;
}

HO_STATUS
ExHandleCloseAll(EX_PROCESS *process)
{
    return KiHandleCloseAll(process, FALSE);
}

HO_STATUS
ExHandleCloseAllForTeardown(EX_PROCESS *process)
{
    return KiHandleCloseAll(process, TRUE);
}

void
ExRuntimeInitializePrivateHandleTable(EX_PRIVATE_HANDLE_TABLE *table)
{
    ExHandleInitializeTable(table);
}

HO_STATUS
ExRuntimeInsertPrivateHandle(EX_PROCESS *process,
                               EX_OBJECT_HEADER *objectHeader,
                               EX_PRIVATE_HANDLE_RIGHTS rights,
                               EX_PRIVATE_HANDLE *outHandle)
{
    return ExHandleInsert(process, objectHeader, rights, outHandle);
}

HO_STATUS
ExRuntimeResolvePrivateHandle(EX_PROCESS *process,
                                EX_PRIVATE_HANDLE handle,
                                EX_OBJECT_TYPE expectedType,
                                EX_PRIVATE_HANDLE_RIGHTS desiredRights,
                                EX_OBJECT_HEADER **outObjectHeader)
{
    return ExHandleResolve(process, handle, expectedType, desiredRights, outObjectHeader);
}

HO_STATUS
ExRuntimeReleaseResolvedObject(EX_OBJECT_HEADER *objectHeader)
{
    return ExHandleReleaseResolvedObject(objectHeader);
}

HO_STATUS
ExRuntimeClosePrivateHandle(EX_PROCESS *process, EX_PRIVATE_HANDLE *handle)
{
    return ExHandleClose(process, handle);
}

HO_STATUS
ExRuntimeCloseAllPrivateHandles(EX_PROCESS *process)
{
    return ExHandleCloseAll(process);
}
