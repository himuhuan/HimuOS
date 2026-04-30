/**
 * HimuOperatingSystem
 *
 * File: ex/image.c
 * Description: Bootstrap image const payload and capability seed helpers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/user_capability_abi.h>
#include <kernel/ex/user_image_abi.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/user_bootstrap.h>
#include <libc/string.h>

HO_STATUS
ExBootstrapBuildInitialConstBytes(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params,
                                  uint8_t **outConstBytes,
                                  uint64_t *outConstLength)
{
    if (params == NULL || outConstBytes == NULL || outConstLength == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outConstBytes = NULL;
    *outConstLength = 0;

    if ((params->ConstBytes == NULL) != (params->ConstLength == 0))
        return EC_ILLEGAL_ARGUMENT;

    uint64_t totalConstLength = EX_USER_IMAGE_CONST_PAYLOAD_OFFSET + params->ConstLength;
    if (totalConstLength < params->ConstLength || totalConstLength > EX_USER_IMAGE_PAGE_SIZE)
        return EC_ILLEGAL_ARGUMENT;

    uint8_t *constBytes = (uint8_t *)kzalloc((size_t)totalConstLength);
    if (constBytes == NULL)
        return EC_OUT_OF_RESOURCE;

    EX_USER_CAPABILITY_SEED_BLOCK seed = {
        .Version = EX_USER_CAPABILITY_SEED_VERSION,
        .Size = EX_USER_CAPABILITY_SEED_BLOCK_SIZE,
        .ProcessSelf = EX_USER_CAPABILITY_INVALID_HANDLE,
        .ThreadSelf = EX_USER_CAPABILITY_INVALID_HANDLE,
        .Stdout = EX_USER_CAPABILITY_INVALID_HANDLE,
        .WaitObject = EX_USER_CAPABILITY_INVALID_HANDLE,
    };

    memcpy(constBytes, &seed, sizeof(seed));

    if (params->ConstLength != 0)
    {
        memcpy(constBytes + EX_USER_IMAGE_CONST_PAYLOAD_OFFSET, params->ConstBytes, (size_t)params->ConstLength);
    }

    *outConstBytes = constBytes;
    *outConstLength = totalConstLength;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapPatchCapabilitySeed(EX_PROCESS *process, EX_THREAD *thread)
{
    if (process == NULL || thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (process->Staging == NULL || thread->Process != process)
        return EC_INVALID_STATE;

    if (process->SelfHandle == EX_HANDLE_INVALID || process->StdoutHandle == EX_HANDLE_INVALID ||
        thread->SelfHandle == EX_HANDLE_INVALID)
    {
        return EC_INVALID_STATE;
    }

    EX_USER_CAPABILITY_SEED_BLOCK seed = {
        .Version = EX_USER_CAPABILITY_SEED_VERSION,
        .Size = EX_USER_CAPABILITY_SEED_BLOCK_SIZE,
        .ProcessSelf = process->SelfHandle,
        .ThreadSelf = thread->SelfHandle,
        .Stdout = process->StdoutHandle,
        .WaitObject = process->WaitHandle,
    };

    return KeUserBootstrapPatchConstBytes(process->Staging, 0, &seed, sizeof(seed));
}
