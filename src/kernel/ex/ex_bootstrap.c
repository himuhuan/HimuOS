/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap.c
 * Description: Ex-owned bootstrap runtime init and launch facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_bootstrap.h>

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>
#include <libc/string.h>

EX_PROCESS *gExBootstrapProcess = NULL;
EX_THREAD *gExBootstrapThread = NULL;

#define EX_PRIVATE_HANDLE_INDEX_BITS       8u
#define EX_PRIVATE_HANDLE_INDEX_MASK       ((EX_PRIVATE_HANDLE)0x000000FFu)
#define EX_PRIVATE_HANDLE_GENERATION_MASK  ((uint32_t)0x00FFFFFFu)

#if EX_PRIVATE_HANDLE_TABLE_CAPACITY > 0xFFu
#error EX_PRIVATE_HANDLE_TABLE_CAPACITY exceeds the internal handle encoding range.
#endif

static void KiInitializeStdoutServiceObject(EX_PROCESS *process);
static HO_STATUS KiReleaseStdoutServiceOwner(EX_PROCESS *process);
static HO_STATUS KiBuildInitialBootstrapConstBytes(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params,
                                                   uint8_t **outConstBytes,
                                                   uint64_t *outConstLength);
static HO_STATUS KiPatchBootstrapCapabilitySeed(EX_PROCESS *process, EX_THREAD *thread);

static void
KiInitializeObjectHeader(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE type)
{
    if (header == NULL)
        return;

    header->Type = type;
    header->ReferenceCount = 1;
}

static HO_STATUS
KiRetainObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType)
{
    if (header == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount++;
    return EC_SUCCESS;
}

static HO_STATUS
KiReleaseObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType, uint32_t *remainingReferences)
{
    if (header == NULL || remainingReferences == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (header->Type != expectedType || header->ReferenceCount == 0)
        return EC_INVALID_STATE;

    header->ReferenceCount--;
    *remainingReferences = header->ReferenceCount;
    return EC_SUCCESS;
}

static HO_STATUS
KiRestoreImportedRootForProcessTeardown(const EX_PROCESS *process)
{
    HO_PHYSICAL_ADDRESS activeRoot = 0;

    if (process == NULL || !process->AddressSpace.Initialized)
        return EC_SUCCESS;

    if (KeQueryActiveRootPageTable(&activeRoot) != EC_SUCCESS ||
        activeRoot != process->AddressSpace.RootPageTablePhys)
    {
        return EC_SUCCESS;
    }

    const KE_KERNEL_ADDRESS_SPACE *kernelSpace = KeGetKernelAddressSpace();
    if (kernelSpace == NULL || !kernelSpace->Initialized || kernelSpace->RootPageTablePhys == 0)
        return EC_INVALID_STATE;

    return KeSwitchAddressSpace(kernelSpace->RootPageTablePhys);
}

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
        objectHeader->Type != EX_OBJECT_TYPE_STDOUT_SERVICE)
        return EC_INVALID_STATE;

    return KiRetainObject(objectHeader, objectHeader->Type);
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
    case EX_OBJECT_TYPE_STDOUT_SERVICE:
    {
        EX_STDOUT_SERVICE *stdoutService = CONTAINING_RECORD(objectHeader, EX_STDOUT_SERVICE, Header);
        uint32_t remainingReferences = 0;

        (void)stdoutService;
        return KiReleaseObject(objectHeader, EX_OBJECT_TYPE_STDOUT_SERVICE, &remainingReferences);
    }
    default:
        return EC_INVALID_STATE;
    }
}

static BOOL
KiIsRuntimeAliasObject(const EX_OBJECT_HEADER *objectHeader)
{
    if (objectHeader == NULL)
        return FALSE;

    switch (objectHeader->Type)
    {
    case EX_OBJECT_TYPE_PROCESS:
        return gExBootstrapProcess == CONTAINING_RECORD(objectHeader, EX_PROCESS, Header);
    case EX_OBJECT_TYPE_THREAD:
        return gExBootstrapThread == CONTAINING_RECORD(objectHeader, EX_THREAD, Header);
    default:
        return FALSE;
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
    if (KiIsRuntimeAliasObject(objectHeader))
        return EC_INVALID_STATE;

    if (objectHeader->Type != EX_OBJECT_TYPE_PROCESS && objectHeader->Type != EX_OBJECT_TYPE_THREAD &&
        objectHeader->Type != EX_OBJECT_TYPE_STDOUT_SERVICE)
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
    case EX_OBJECT_TYPE_PROCESS:
    {
        EX_PROCESS *process = CONTAINING_RECORD(objectHeader, EX_PROCESS, Header);
        if (process->SelfHandle == handle)
            process->SelfHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_THREAD:
    {
        EX_THREAD *thread = CONTAINING_RECORD(objectHeader, EX_THREAD, Header);
        if (thread->SelfHandle == handle)
            thread->SelfHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    case EX_OBJECT_TYPE_STDOUT_SERVICE:
    {
        EX_STDOUT_SERVICE *stdoutService = CONTAINING_RECORD(objectHeader, EX_STDOUT_SERVICE, Header);
        if (stdoutService->Owner != NULL && stdoutService->Owner->StdoutHandle == handle)
            stdoutService->Owner->StdoutHandle = EX_PRIVATE_HANDLE_INVALID;
        break;
    }
    default:
        break;
    }
}

static void
KiInitializeStdoutServiceObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    KiInitializeObjectHeader(&process->StdoutService.Header, EX_OBJECT_TYPE_STDOUT_SERVICE);
    process->StdoutService.Owner = process;
}

static HO_STATUS
KiReleaseStdoutServiceOwner(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = KiReleaseObject(&process->StdoutService.Header,
                                       EX_OBJECT_TYPE_STDOUT_SERVICE,
                                       &remainingReferences);
    if (status != EC_SUCCESS)
        return status;

    return remainingReferences == 0 ? EC_SUCCESS : EC_INVALID_STATE;
}

static HO_STATUS
KiBuildInitialBootstrapConstBytes(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params,
                                  uint8_t **outConstBytes,
                                  uint64_t *outConstLength)
{
    if (params == NULL || outConstBytes == NULL || outConstLength == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outConstBytes = NULL;
    *outConstLength = 0;

    if ((params->ConstBytes == NULL) != (params->ConstLength == 0))
        return EC_ILLEGAL_ARGUMENT;

    uint64_t totalConstLength = KE_USER_BOOTSTRAP_CONST_PAYLOAD_OFFSET + params->ConstLength;
    if (totalConstLength < params->ConstLength || totalConstLength > KE_USER_BOOTSTRAP_PAGE_SIZE)
        return EC_ILLEGAL_ARGUMENT;

    uint8_t *constBytes = (uint8_t *)kzalloc((size_t)totalConstLength);
    if (constBytes == NULL)
        return EC_OUT_OF_RESOURCE;

    KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK seed = {
        .Version = KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION,
        .Size = KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE,
        .ProcessSelf = KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE,
        .ThreadSelf = KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE,
        .Stdout = KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE,
        .WaitObject = KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE,
    };

    memcpy(constBytes, &seed, sizeof(seed));

    if (params->ConstLength != 0)
    {
        memcpy(constBytes + KE_USER_BOOTSTRAP_CONST_PAYLOAD_OFFSET,
               params->ConstBytes,
               (size_t)params->ConstLength);
    }

    *outConstBytes = constBytes;
    *outConstLength = totalConstLength;
    return EC_SUCCESS;
}

static HO_STATUS
KiPatchBootstrapCapabilitySeed(EX_PROCESS *process, EX_THREAD *thread)
{
    if (process == NULL || thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (process->Staging == NULL || thread->Process != process)
        return EC_INVALID_STATE;

    if (process->SelfHandle == EX_PRIVATE_HANDLE_INVALID || process->StdoutHandle == EX_PRIVATE_HANDLE_INVALID ||
        thread->SelfHandle == EX_PRIVATE_HANDLE_INVALID)
    {
        return EC_INVALID_STATE;
    }

    KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK seed = {
        .Version = KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION,
        .Size = KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE,
        .ProcessSelf = process->SelfHandle,
        .ThreadSelf = thread->SelfHandle,
        .Stdout = process->StdoutHandle,
        .WaitObject = KE_USER_BOOTSTRAP_CAPABILITY_INVALID_HANDLE,
    };

    return KeUserBootstrapPatchConstBytes(process->Staging, 0, &seed, sizeof(seed));
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

void
ExBootstrapInitializeProcessObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    KiInitializeObjectHeader(&process->Header, EX_OBJECT_TYPE_PROCESS);
    memset(&process->AddressSpace, 0, sizeof(process->AddressSpace));
    process->Staging = NULL;
    process->SelfHandle = EX_PRIVATE_HANDLE_INVALID;
    process->StdoutHandle = EX_PRIVATE_HANDLE_INVALID;
    KiInitializeStdoutServiceObject(process);
    ExBootstrapInitializePrivateHandleTable(&process->HandleTable);
}

void
ExBootstrapInitializeThreadObject(EX_THREAD *thread)
{
    if (thread == NULL)
        return;

    KiInitializeObjectHeader(&thread->Header, EX_OBJECT_TYPE_THREAD);
    thread->Thread = NULL;
    thread->Process = NULL;
    thread->SelfHandle = EX_PRIVATE_HANDLE_INVALID;
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
        expectedType != EX_OBJECT_TYPE_STDOUT_SERVICE)
        return EC_ILLEGAL_ARGUMENT;

    slot = KiLookupPrivateHandleSlot(process, handle, &generation);
    if (slot == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (slot->Object == NULL || slot->Generation != generation)
        return EC_INVALID_STATE;

    if ((slot->Rights & desiredRights) != desiredRights)
        return EC_INVALID_STATE;

    HO_STATUS status = KiRetainObject(slot->Object, expectedType);
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

    KiInvalidateObjectSelfHandleIfMatch(objectHeader, localHandle);
    KiClearPrivateHandleSlot(slot);
    *handle = EX_PRIVATE_HANDLE_INVALID;
    status = KiReleaseHandleObject(objectHeader);

Exit:
    {
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

        KiInvalidateObjectSelfHandleIfMatch(objectHeader, handle);
        KiClearPrivateHandleSlot(slot);

        HO_STATUS status = KiReleaseHandleObject(objectHeader);
        if (firstError == EC_SUCCESS)
            firstError = status;
    }

Exit:
    ownerStatus = ExBootstrapReleaseProcess(ownerReference);
    if (firstError == EC_SUCCESS)
        firstError = ownerStatus;

    return firstError;
}

BOOL
ExBootstrapHasRuntimeAlias(void)
{
    return gExBootstrapProcess != NULL || gExBootstrapThread != NULL;
}

BOOL
ExBootstrapRuntimeAliasMatchesProcess(const EX_PROCESS *process)
{
    return process != NULL && gExBootstrapProcess == process;
}

EX_THREAD *
ExBootstrapLookupRuntimeThread(const KTHREAD *thread)
{
    if (thread == NULL || gExBootstrapThread == NULL || gExBootstrapThread->Thread != thread)
        return NULL;

    return gExBootstrapThread;
}

EX_PROCESS *
ExBootstrapLookupRuntimeProcess(const KTHREAD *thread)
{
    EX_THREAD *runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread == NULL)
        return NULL;

    return runtimeThread->Process;
}

HO_STATUS
ExBootstrapPublishRuntimeAlias(EX_PROCESS *process, EX_THREAD *thread)
{
    if (process == NULL || thread == NULL || thread->Thread == NULL || thread->Process != process)
        return EC_ILLEGAL_ARGUMENT;

    if (ExBootstrapHasRuntimeAlias())
        return EC_INVALID_STATE;

    gExBootstrapProcess = process;
    gExBootstrapThread = thread;
    return EC_SUCCESS;
}

void
ExBootstrapUnpublishRuntimeAlias(EX_THREAD **outThread, EX_PROCESS **outProcess)
{
    EX_THREAD *thread = gExBootstrapThread;
    EX_PROCESS *process = gExBootstrapProcess;

    gExBootstrapThread = NULL;
    gExBootstrapProcess = NULL;

    if (outThread != NULL)
        *outThread = thread;

    if (outProcess != NULL)
        *outProcess = process;
}

HO_STATUS
ExBootstrapTeardownProcessPayload(EX_PROCESS *process)
{
    HO_STATUS status = KiRestoreImportedRootForProcessTeardown(process);

    if (process == NULL)
        return status;

    if (process->Staging != NULL)
    {
        HO_STATUS stagingStatus = KeUserBootstrapDestroyStaging(process->Staging);

        /* Destroy consumes the staging object even when teardown reports an error. */
        process->Staging = NULL;

        if (status == EC_SUCCESS)
            status = stagingStatus;
    }

    if (process->AddressSpace.Initialized)
    {
        HO_STATUS addrStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        if (status == EC_SUCCESS)
            status = addrStatus;
    }

    return status;
}

EX_PROCESS *
ExBootstrapRetainProcess(EX_PROCESS *process)
{
    if (process == NULL)
        return NULL;

    if (KiRetainObject(&process->Header, EX_OBJECT_TYPE_PROCESS) != EC_SUCCESS)
        return NULL;

    return process;
}

HO_STATUS
ExBootstrapReleaseProcess(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;
    HO_STATUS status = EC_SUCCESS;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    status = KiReleaseObject(&process->Header, EX_OBJECT_TYPE_PROCESS, &remainingReferences);
    if (status != EC_SUCCESS || remainingReferences != 0)
        return status;

    status = KiReleaseStdoutServiceOwner(process);
    if (status != EC_SUCCESS)
        return status;

    kfree(process);
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapReleaseThread(EX_THREAD *thread)
{
    EX_PROCESS *process = NULL;
    uint32_t remainingReferences = 0;
    HO_STATUS status = EC_SUCCESS;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    status = KiReleaseObject(&thread->Header, EX_OBJECT_TYPE_THREAD, &remainingReferences);
    if (status != EC_SUCCESS || remainingReferences != 0)
        return status;

    process = thread->Process;
    thread->Thread = NULL;
    thread->Process = NULL;
    kfree(thread);

    if (process != NULL)
        return ExBootstrapReleaseProcess(process);

    return EC_SUCCESS;
}

static HO_STATUS
KiDestroyNewThread(KTHREAD *thread)
{
    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if ((thread->Flags & KTHREAD_FLAG_IDLE) != 0 || thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    if (thread->StackOwnedByKva)
    {
        HO_STATUS status = KeKvaReleaseRangeHandle(&thread->StackRange);
        if (status != EC_SUCCESS)
            return status;
    }

    KePoolFree(&gKThreadPool, thread);
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapInit(void)
{
    HO_STATUS status = KeUserBootstrapRawSyscallInit();
    if (status != EC_SUCCESS)
        return status;

    return ExBootstrapAdapterInit();
}

HO_STATUS
ExBootstrapCreateProcess(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params, EX_PROCESS **outProcess)
{
    KE_USER_BOOTSTRAP_STAGING *staging = NULL;
    EX_PROCESS *process = NULL;
    KE_USER_BOOTSTRAP_CREATE_PARAMS keParams = {0};
    uint8_t *initialConstBytes = NULL;
    uint64_t initialConstLength = 0;
    const EX_PRIVATE_HANDLE_RIGHTS processSelfRights = EX_PRIVATE_HANDLE_RIGHT_QUERY |
                                                       EX_PRIVATE_HANDLE_RIGHT_CLOSE |
                                                       EX_PRIVATE_HANDLE_RIGHT_PROCESS_SELF;
    const EX_PRIVATE_HANDLE_RIGHTS stdoutRights = EX_PRIVATE_HANDLE_RIGHT_WRITE |
                                                  EX_PRIVATE_HANDLE_RIGHT_CLOSE;

    if (outProcess == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outProcess = NULL;

    if (params == NULL)
        return EC_ILLEGAL_ARGUMENT;

    keParams.CodeBytes = params->CodeBytes;
    keParams.CodeLength = params->CodeLength;
    keParams.EntryOffset = params->EntryOffset;

    process = (EX_PROCESS *)kzalloc(sizeof(*process));
    if (process == NULL)
        return EC_OUT_OF_RESOURCE;

    ExBootstrapInitializeProcessObject(process);

    HO_STATUS status = KeCreateProcessAddressSpace(&process->AddressSpace);
    if (status != EC_SUCCESS)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
        if (releaseStatus != EC_SUCCESS)
            return releaseStatus;

        return status;
    }

    status = KiBuildInitialBootstrapConstBytes(params, &initialConstBytes, &initialConstLength);
    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    keParams.ConstBytes = initialConstBytes;
    keParams.ConstLength = initialConstLength;

    status = KeUserBootstrapCreateStaging(&keParams, &process->AddressSpace, &staging);
    if (initialConstBytes != NULL)
    {
        kfree(initialConstBytes);
        initialConstBytes = NULL;
    }

    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = KeDestroyProcessAddressSpace(&process->AddressSpace);
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    process->Staging = staging;
    status = ExBootstrapInsertPrivateHandle(process, &process->Header, processSelfRights, &process->SelfHandle);
    if (status != EC_SUCCESS)
    {
        HO_STATUS teardownStatus = ExBootstrapTeardownProcessPayload(process);
        HO_STATUS closeStatus = ExBootstrapCloseAllPrivateHandles(process);
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);

        if (teardownStatus != EC_SUCCESS)
            return teardownStatus;

        if (closeStatus != EC_SUCCESS)
            return closeStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    status = ExBootstrapInsertPrivateHandle(process, &process->StdoutService.Header, stdoutRights, &process->StdoutHandle);
    if (status != EC_SUCCESS)
    {
        HO_STATUS teardownStatus = ExBootstrapTeardownProcessPayload(process);
        HO_STATUS closeStatus = ExBootstrapCloseAllPrivateHandles(process);
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);

        if (teardownStatus != EC_SUCCESS)
            return teardownStatus;

        if (closeStatus != EC_SUCCESS)
            return closeStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    *outProcess = process;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapDestroyProcess(EX_PROCESS *process)
{
    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (ExBootstrapRuntimeAliasMatchesProcess(process))
        return EC_INVALID_STATE;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);

    HO_STATUS closeStatus = ExBootstrapCloseAllPrivateHandles(process);
    if (status == EC_SUCCESS)
        status = closeStatus;

    HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
    if (status == EC_SUCCESS)
        status = releaseStatus;

    return status;
}

HO_STATUS
ExBootstrapCreateThread(EX_PROCESS **processHandle,
                        const EX_BOOTSTRAP_THREAD_CREATE_PARAMS *params,
                        EX_THREAD **outThread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;
    KTHREAD *kernelThread = NULL;
    const EX_PRIVATE_HANDLE_RIGHTS threadSelfRights = EX_PRIVATE_HANDLE_RIGHT_QUERY |
                                                      EX_PRIVATE_HANDLE_RIGHT_CLOSE |
                                                      EX_PRIVATE_HANDLE_RIGHT_THREAD_SELF;

    if (processHandle == NULL || outThread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outThread = NULL;
    process = *processHandle;

    if (process == NULL || params == NULL || params->EntryPoint == NULL || process->Staging == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (ExBootstrapHasRuntimeAlias())
        return EC_INVALID_STATE;

    thread = (EX_THREAD *)kzalloc(sizeof(*thread));
    if (thread == NULL)
        return EC_OUT_OF_RESOURCE;

    ExBootstrapInitializeThreadObject(thread);

    HO_STATUS status = KeThreadCreate(&kernelThread, (KTHREAD_ENTRY)params->EntryPoint, params->EntryArg);
    if (status != EC_SUCCESS)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);
        if (releaseStatus != EC_SUCCESS)
            return releaseStatus;

        return status;
    }

    status = KeUserBootstrapAttachThread(kernelThread, process->Staging);
    if (status != EC_SUCCESS)
    {
        HO_STATUS destroyStatus = KiDestroyNewThread(kernelThread);
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    thread->Thread = kernelThread;
    thread->Process = process;

    status = ExBootstrapInsertPrivateHandle(process, &thread->Header, threadSelfRights, &thread->SelfHandle);
    if (status != EC_SUCCESS)
        goto FailThreadRuntimeSetup;

    status = KiPatchBootstrapCapabilitySeed(process, thread);
    if (status != EC_SUCCESS)
        goto FailThreadRuntimeSetup;

    status = ExBootstrapPublishRuntimeAlias(process, thread);
    if (status != EC_SUCCESS)
    {
        goto FailThreadRuntimeSetup;
    }

    *outThread = thread;
    *processHandle = NULL;
    return EC_SUCCESS;

FailThreadRuntimeSetup:
    {
        HO_STATUS detachStatus = KeUserBootstrapDetachThread(kernelThread, process->Staging);
        HO_STATUS destroyStatus = KiDestroyNewThread(kernelThread);
        HO_STATUS closeStatus = ExBootstrapClosePrivateHandle(process, &thread->SelfHandle);

        thread->Thread = NULL;
        thread->Process = NULL;

        HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);

        if (detachStatus != EC_SUCCESS)
            return detachStatus;

        if (destroyStatus != EC_SUCCESS)
            return destroyStatus;

        if (closeStatus != EC_SUCCESS)
            return closeStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }
}

HO_STATUS
ExBootstrapStartThread(EX_THREAD **threadHandle)
{
    EX_THREAD *thread = NULL;

    if (threadHandle == NULL)
        return EC_ILLEGAL_ARGUMENT;

    thread = *threadHandle;

    if (thread == NULL || thread->Thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = KeThreadStart(thread->Thread);
    if (status != EC_SUCCESS)
        return status;

    *threadHandle = NULL;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapTeardownThread(EX_THREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL || thread->Thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (ExBootstrapLookupRuntimeThread(thread->Thread) != thread)
        return EC_INVALID_STATE;

    if (thread->Thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;

    process = ExBootstrapLookupRuntimeProcess(thread->Thread);

    HO_STATUS firstError = ExBootstrapTeardownProcessPayload(process);

    HO_STATUS threadStatus = KiDestroyNewThread(thread->Thread);
    if (firstError == EC_SUCCESS)
        firstError = threadStatus;

    ExBootstrapUnpublishRuntimeAlias(NULL, NULL);

    HO_STATUS closeStatus = ExBootstrapCloseAllPrivateHandles(process);
    if (firstError == EC_SUCCESS)
        firstError = closeStatus;

    HO_STATUS releaseStatus = ExBootstrapReleaseThread(thread);
    if (firstError == EC_SUCCESS)
        firstError = releaseStatus;

    return firstError;
}