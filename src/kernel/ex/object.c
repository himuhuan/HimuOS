/**
 * HimuOperatingSystem
 *
 * File: ex/object.c
 * Description: Executive Lite object headers and embedded service objects.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

static HO_STATUS KiDestroyStdoutServiceObject(EX_OBJECT_HEADER *objectHeader);

BOOL
ExObjectIsValidType(EX_OBJECT_TYPE type)
{
    switch (type)
    {
    case EX_OBJECT_TYPE_PROCESS:
    case EX_OBJECT_TYPE_THREAD:
    case EX_OBJECT_TYPE_CONSOLE:
        return TRUE;
    default:
        return FALSE;
    }
}

void
ExObjectInitializeHeader(EX_OBJECT_HEADER *header,
                         EX_OBJECT_TYPE type,
                         EX_OBJECT_FLAGS flags,
                         EX_OBJECT_DESTROY_ROUTINE destroyRoutine)
{
    if (header == NULL)
        return;

    header->Type = type;
    header->ReferenceCount = 1;
    header->Flags = flags;
    header->DestroyRoutine = destroyRoutine;
}

HO_STATUS
ExObjectRetain(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType)
{
    if (header == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!ExObjectIsValidType(expectedType) || header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount++;
    return EC_SUCCESS;
}

HO_STATUS
ExObjectRelease(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType, uint32_t *remainingReferences)
{
    if (header == NULL || remainingReferences == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!ExObjectIsValidType(expectedType) || header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount--;
    *remainingReferences = header->ReferenceCount;
    if (header->ReferenceCount == 0 && header->DestroyRoutine != NULL)
        return header->DestroyRoutine(header);

    return EC_SUCCESS;
}

void
ExBootstrapInitializeObjectHeader(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE type)
{
    ExObjectInitializeHeader(header, type, EX_OBJECT_FLAG_NONE, NULL);
}

HO_STATUS
ExBootstrapRetainObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType)
{
    return ExObjectRetain(header, expectedType);
}

HO_STATUS
ExBootstrapReleaseObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType, uint32_t *remainingReferences)
{
    return ExObjectRelease(header, expectedType, remainingReferences);
}

void
ExBootstrapInitializeStdoutServiceObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    ExObjectInitializeHeader(&process->StdoutService.Header, EX_OBJECT_TYPE_CONSOLE, EX_OBJECT_FLAG_NONE,
                             KiDestroyStdoutServiceObject);
    process->StdoutService.Owner = process;
}

HO_STATUS
ExBootstrapReleaseStdoutServiceOwner(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = ExObjectRelease(&process->StdoutService.Header, EX_OBJECT_TYPE_CONSOLE, &remainingReferences);
    if (status != EC_SUCCESS)
        return status;

    return remainingReferences == 0 ? EC_SUCCESS : EC_INVALID_STATE;
}

static HO_STATUS
KiDestroyStdoutServiceObject(EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL || objectHeader->Type != EX_OBJECT_TYPE_CONSOLE)
        return EC_ILLEGAL_ARGUMENT;

    return EC_SUCCESS;
}
