/**
 * HimuOperatingSystem
 *
 * File: ex/process.c
 * Description: Ex bootstrap process object lifecycle and staging ownership.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_bootstrap.h>

#include "ex_bootstrap_internal.h"

#include <kernel/ke/critical_section.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

static uint32_t gNextBootstrapProcessId = 1;

static uint32_t KiAllocateBootstrapProcessId(void);
static HO_STATUS KiDestroyProcessObject(EX_OBJECT_HEADER *objectHeader);

static uint32_t
KiAllocateBootstrapProcessId(void)
{
    KE_CRITICAL_SECTION guard = {0};
    uint32_t processId = 0;

    KeEnterCriticalSection(&guard);
    HO_KASSERT(gNextBootstrapProcessId != 0, EC_OUT_OF_RESOURCE);
    processId = gNextBootstrapProcessId++;
    KeLeaveCriticalSection(&guard);

    return processId;
}

static HO_STATUS
KiRestoreImportedRootForProcessTeardown(const EX_PROCESS *process)
{
    HO_PHYSICAL_ADDRESS activeRoot = 0;

    if (process == NULL || !process->AddressSpace.Initialized)
        return EC_SUCCESS;

    if (KeQueryActiveRootPageTable(&activeRoot) != EC_SUCCESS || activeRoot != process->AddressSpace.RootPageTablePhys)
    {
        return EC_SUCCESS;
    }

    const KE_KERNEL_ADDRESS_SPACE *kernelSpace = KeGetKernelAddressSpace();
    if (kernelSpace == NULL || !kernelSpace->Initialized || kernelSpace->RootPageTablePhys == 0)
        return EC_INVALID_STATE;

    return KeSwitchAddressSpace(kernelSpace->RootPageTablePhys);
}

void
ExBootstrapInitializeProcessObject(EX_PROCESS *process)
{
    if (process == NULL)
        return;

    ExObjectInitializeHeader(&process->Header, EX_OBJECT_TYPE_PROCESS, EX_OBJECT_FLAG_NONE, KiDestroyProcessObject);
    memset(&process->AddressSpace, 0, sizeof(process->AddressSpace));
    process->Staging = NULL;
    process->SelfHandle = EX_HANDLE_INVALID;
    process->StdoutHandle = EX_HANDLE_INVALID;
    process->WaitHandle = EX_HANDLE_INVALID;
    process->ProcessId = 0;
    process->ParentProcessId = 0;
    process->MainThreadId = 0;
    process->State = EX_PROCESS_STATE_CREATED;
    process->ExitStatus = 0;
    process->TerminationReason = EX_PROCESS_TERMINATION_REASON_NONE;
    process->KillRequested = FALSE;
    process->Foreground = FALSE;
    process->RestoreForegroundOwnerThreadId = 0;
    ExBootstrapInitializeStdoutServiceObject(process);
    ExBootstrapInitializeWaitableObject(process);
    ExHandleInitializeTable(&process->HandleTable);
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

    if (ExObjectRetain(&process->Header, EX_OBJECT_TYPE_PROCESS) != EC_SUCCESS)
        return NULL;

    return process;
}

HO_STATUS
ExBootstrapReleaseProcess(EX_PROCESS *process)
{
    uint32_t remainingReferences = 0;

    if (process == NULL)
        return EC_ILLEGAL_ARGUMENT;

    return ExObjectRelease(&process->Header, EX_OBJECT_TYPE_PROCESS, &remainingReferences);
}

static HO_STATUS
KiDestroyProcessObject(EX_OBJECT_HEADER *objectHeader)
{
    EX_PROCESS *process = NULL;

    if (objectHeader == NULL || objectHeader->Type != EX_OBJECT_TYPE_PROCESS)
        return EC_ILLEGAL_ARGUMENT;

    process = CONTAINING_RECORD(objectHeader, EX_PROCESS, Header);

    HO_STATUS status = ExBootstrapReleaseWaitableObjectOwner(process);
    if (status != EC_SUCCESS)
        return status;

    status = ExBootstrapReleaseStdoutServiceOwner(process);
    if (status != EC_SUCCESS)
        return status;

    kfree(process);
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapCreateProcess(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params, EX_PROCESS **outProcess)
{
    KE_USER_BOOTSTRAP_STAGING *staging = NULL;
    EX_PROCESS *process = NULL;
    KE_USER_BOOTSTRAP_CREATE_PARAMS keParams = {0};
    uint8_t *initialConstBytes = NULL;
    uint64_t initialConstLength = 0;
    const EX_HANDLE_RIGHTS processSelfRights =
        EX_HANDLE_RIGHT_QUERY | EX_HANDLE_RIGHT_CLOSE | EX_HANDLE_RIGHT_PROCESS_SELF;
    const EX_HANDLE_RIGHTS stdoutRights = EX_HANDLE_RIGHT_WRITE | EX_HANDLE_RIGHT_CLOSE;

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
    process->ProcessId = KiAllocateBootstrapProcessId();
    process->ParentProcessId = params->ParentProcessId;
    process->ProgramId = params->ProgramId;

    HO_STATUS status = KeCreateProcessAddressSpace(&process->AddressSpace);
    if (status != EC_SUCCESS)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
        if (releaseStatus != EC_SUCCESS)
            return releaseStatus;

        return status;
    }

    status = ExBootstrapBuildInitialConstBytes(params, &initialConstBytes, &initialConstLength);
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
    status = ExHandleInsert(process, &process->Header, processSelfRights, &process->SelfHandle);
    if (status != EC_SUCCESS)
    {
        HO_STATUS teardownStatus = ExBootstrapTeardownProcessPayload(process);
        HO_STATUS closeStatus = ExHandleCloseAll(process);
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);

        if (teardownStatus != EC_SUCCESS)
            return teardownStatus;

        if (closeStatus != EC_SUCCESS)
            return closeStatus;

        return releaseStatus == EC_SUCCESS ? status : releaseStatus;
    }

    status = ExHandleInsert(process, &process->StdoutService.Header, stdoutRights, &process->StdoutHandle);
    if (status != EC_SUCCESS)
    {
        HO_STATUS teardownStatus = ExBootstrapTeardownProcessPayload(process);
        HO_STATUS closeStatus = ExHandleCloseAll(process);
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

    if (ExRuntimeIsProcessPublished(process))
        return EC_INVALID_STATE;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);

    HO_STATUS closeStatus = ExHandleCloseAll(process);
    if (status == EC_SUCCESS)
        status = closeStatus;

    HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
    if (status == EC_SUCCESS)
        status = releaseStatus;

    return status;
}

HO_STATUS
ExBootstrapQueryProcessId(const EX_PROCESS *process, uint32_t *outProcessId)
{
    if (process == NULL || outProcessId == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (process->ProcessId == 0)
        return EC_INVALID_STATE;

    *outProcessId = process->ProcessId;
    return EC_SUCCESS;
}
