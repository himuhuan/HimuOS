/**
 * HimuOperatingSystem
 *
 * File: ex/object.c
 * Description: Ex bootstrap object headers and embedded pilot objects.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

void
ExBootstrapInitializeObjectHeader(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE type)
{
    if (header == NULL)
        return;

    header->Type = type;
    header->ReferenceCount = 1;
}

HO_STATUS
ExBootstrapRetainObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType)
{
    if (header == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount++;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapReleaseObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType, uint32_t *remainingReferences)
{
    if (header == NULL || remainingReferences == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount--;
    *remainingReferences = header->ReferenceCount;
    return EC_SUCCESS;
}

void
ExBootstrapInitializeStdoutServiceObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    ExBootstrapInitializeObjectHeader(&process->StdoutService.Header, EX_OBJECT_TYPE_STDOUT_SERVICE);
    process->StdoutService.Owner = process;
}

HO_STATUS
ExBootstrapReleaseStdoutServiceOwner(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status =
        ExBootstrapReleaseObject(&process->StdoutService.Header, EX_OBJECT_TYPE_STDOUT_SERVICE, &remainingReferences);
    if (status != EC_SUCCESS)
        return status;

    return remainingReferences == 0 ? EC_SUCCESS : EC_INVALID_STATE;
}

void
ExBootstrapInitializeWaitableObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    ExBootstrapInitializeObjectHeader(&process->WaitObject.Header, EX_OBJECT_TYPE_WAITABLE);
    process->WaitObject.Owner = process;
    process->WaitObject.Dispatcher = NULL;
    process->WaitObject.CompanionThread = NULL;
}

HO_STATUS
ExBootstrapReleaseWaitableObjectOwner(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status =
        ExBootstrapReleaseObject(&process->WaitObject.Header, EX_OBJECT_TYPE_WAITABLE, &remainingReferences);
    if (status != EC_SUCCESS)
        return status;

    return remainingReferences == 0 ? EC_SUCCESS : EC_INVALID_STATE;
}
