/**
 * HimuOperatingSystem
 *
 * File: ex/program.h
 * Description: Ex-owned builtin user program registry.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#include <kernel/ex/ex_process.h>
#include <kernel/ex/user_image_abi.h>

#define EX_PROGRAM_NAME_MAX_LENGTH 16U

typedef enum EX_PROGRAM_ID
{
    EX_PROGRAM_ID_NONE = 0,
    EX_PROGRAM_ID_HSH = 1,
    EX_PROGRAM_ID_CALC = 2,
    EX_PROGRAM_ID_TICK1S = 3,
    EX_PROGRAM_ID_FAULT_DE = 4,
    EX_PROGRAM_ID_FAULT_PF = 5,
    EX_PROGRAM_ID_USER_HELLO = 6,
    EX_PROGRAM_ID_USER_COUNTER = 7,
} EX_PROGRAM_ID;

typedef enum EX_USER_IMAGE_KIND
{
    EX_USER_IMAGE_KIND_INVALID = 0,
    EX_USER_IMAGE_KIND_EMBEDDED_SPLIT = 1,
} EX_USER_IMAGE_KIND;

typedef struct EX_USER_IMAGE
{
    const char *Name;
    uint32_t NameLength;
    uint32_t ProgramId;
    EX_USER_IMAGE_KIND Kind;
    const uint8_t *CodeBytes;
    uint64_t CodeLength;
    const uint8_t *ConstBytes;
    uint64_t ConstLength;
    uint64_t EntryOffset;
    uint64_t DefaultStackSize;
    uint64_t RequestedCapabilities;
} EX_USER_IMAGE;

HO_KERNEL_API HO_NODISCARD HO_STATUS ExProgramValidateBuiltins(void);
HO_KERNEL_API HO_NODISCARD HO_STATUS ExLookupProgramImageByName(const char *name,
                                                                uint64_t nameLength,
                                                                const EX_USER_IMAGE **outImage);
HO_KERNEL_API HO_NODISCARD HO_STATUS ExLookupProgramImageById(uint32_t programId,
                                                              const EX_USER_IMAGE **outImage);
HO_KERNEL_API HO_NODISCARD HO_STATUS ExProgramBuildBootstrapCreateParams(
    const EX_USER_IMAGE *image,
    uint32_t parentProcessId,
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *outParams);
