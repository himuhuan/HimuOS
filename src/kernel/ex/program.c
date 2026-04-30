/**
 * HimuOperatingSystem
 *
 * File: ex/program.c
 * Description: Builtin user program registry backed by embedded split images.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/program.h>

#include <libc/string.h>

extern const uint8_t gExBuiltinProgram_user_hello_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_user_hello_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_user_hello_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_user_hello_ConstBytesEnd[];
extern const uint8_t gExBuiltinProgram_user_counter_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_user_counter_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_user_counter_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_user_counter_ConstBytesEnd[];
extern const uint8_t gExBuiltinProgram_hsh_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_hsh_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_hsh_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_hsh_ConstBytesEnd[];
extern const uint8_t gExBuiltinProgram_calc_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_calc_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_calc_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_calc_ConstBytesEnd[];
extern const uint8_t gExBuiltinProgram_tick1s_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_tick1s_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_tick1s_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_tick1s_ConstBytesEnd[];
extern const uint8_t gExBuiltinProgram_fault_de_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_fault_de_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_fault_de_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_fault_de_ConstBytesEnd[];
extern const uint8_t gExBuiltinProgram_fault_pf_CodeBytesStart[];
extern const uint8_t gExBuiltinProgram_fault_pf_CodeBytesEnd[];
extern const uint8_t gExBuiltinProgram_fault_pf_ConstBytesStart[];
extern const uint8_t gExBuiltinProgram_fault_pf_ConstBytesEnd[];

typedef struct EX_PROGRAM_REGISTRY_ENTRY
{
    EX_USER_IMAGE Image;
    const uint8_t *CodeEnd;
    const uint8_t *ConstEnd;
} EX_PROGRAM_REGISTRY_ENTRY;

#define EX_PROGRAM_REGISTRY_ENTRY(symbol, name, id)                                                                    \
    {                                                                                                                  \
        .Image = {                                                                                                     \
            .Name = (name),                                                                                            \
            .NameLength = sizeof(name) - 1U,                                                                           \
            .ProgramId = (id),                                                                                         \
            .Kind = EX_USER_IMAGE_KIND_EMBEDDED_SPLIT,                                                                 \
            .CodeBytes = gExBuiltinProgram_##symbol##_CodeBytesStart,                                                  \
            .CodeLength = 0,                                                                                           \
            .ConstBytes = gExBuiltinProgram_##symbol##_ConstBytesStart,                                                \
            .ConstLength = 0,                                                                                          \
            .EntryOffset = 0,                                                                                          \
            .DefaultStackSize = EX_USER_IMAGE_PAGE_SIZE,                                                           \
            .RequestedCapabilities = 0,                                                                                \
        },                                                                                                             \
        .CodeEnd = gExBuiltinProgram_##symbol##_CodeBytesEnd,                                                          \
        .ConstEnd = gExBuiltinProgram_##symbol##_ConstBytesEnd,                                                        \
    }

static EX_PROGRAM_REGISTRY_ENTRY gExBuiltinPrograms[] = {
    EX_PROGRAM_REGISTRY_ENTRY(hsh, "hsh", EX_PROGRAM_ID_HSH),
    EX_PROGRAM_REGISTRY_ENTRY(calc, "calc", EX_PROGRAM_ID_CALC),
    EX_PROGRAM_REGISTRY_ENTRY(tick1s, "tick1s", EX_PROGRAM_ID_TICK1S),
    EX_PROGRAM_REGISTRY_ENTRY(fault_de, "fault_de", EX_PROGRAM_ID_FAULT_DE),
    EX_PROGRAM_REGISTRY_ENTRY(fault_pf, "fault_pf", EX_PROGRAM_ID_FAULT_PF),
    EX_PROGRAM_REGISTRY_ENTRY(user_hello, "user_hello", EX_PROGRAM_ID_USER_HELLO),
    EX_PROGRAM_REGISTRY_ENTRY(user_counter, "user_counter", EX_PROGRAM_ID_USER_COUNTER),
};

static BOOL gExProgramRegistryValidated;

static HO_STATUS KiValidateProgramRegistryEntry(EX_PROGRAM_REGISTRY_ENTRY *entry);
static BOOL KiProgramNameEquals(const EX_USER_IMAGE *image, const char *name, uint64_t nameLength);

static HO_STATUS
KiValidateProgramRegistryEntry(EX_PROGRAM_REGISTRY_ENTRY *entry)
{
    uint64_t codeStart = 0;
    uint64_t codeEnd = 0;
    uint64_t constStart = 0;
    uint64_t constEnd = 0;

    if (entry == NULL || entry->Image.Name == NULL || entry->Image.CodeBytes == NULL || entry->CodeEnd == NULL ||
        entry->Image.ConstBytes == NULL || entry->ConstEnd == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    if (entry->Image.NameLength == 0 || entry->Image.NameLength >= EX_PROGRAM_NAME_MAX_LENGTH ||
        entry->Image.Kind != EX_USER_IMAGE_KIND_EMBEDDED_SPLIT || entry->Image.ProgramId == EX_PROGRAM_ID_NONE)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    codeStart = (uint64_t)(const void *)entry->Image.CodeBytes;
    codeEnd = (uint64_t)(const void *)entry->CodeEnd;
    constStart = (uint64_t)(const void *)entry->Image.ConstBytes;
    constEnd = (uint64_t)(const void *)entry->ConstEnd;

    if (codeEnd <= codeStart || constEnd <= constStart)
        return EC_INVALID_STATE;

    entry->Image.CodeLength = codeEnd - codeStart;
    entry->Image.ConstLength = constEnd - constStart;
    return EC_SUCCESS;
}

static BOOL
KiProgramNameEquals(const EX_USER_IMAGE *image, const char *name, uint64_t nameLength)
{
    if (image == NULL || name == NULL)
        return FALSE;

    if (image->NameLength != nameLength)
        return FALSE;

    return memcmp(image->Name, name, (size_t)nameLength) == 0;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
ExProgramValidateBuiltins(void)
{
    for (uint32_t index = 0; index < (sizeof(gExBuiltinPrograms) / sizeof(gExBuiltinPrograms[0])); ++index)
    {
        HO_STATUS status = KiValidateProgramRegistryEntry(&gExBuiltinPrograms[index]);
        if (status != EC_SUCCESS)
            return status;
    }

    gExProgramRegistryValidated = TRUE;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
ExLookupProgramImageByName(const char *name, uint64_t nameLength, const EX_USER_IMAGE **outImage)
{
    if (outImage == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outImage = NULL;

    if (!gExProgramRegistryValidated)
        return EC_INVALID_STATE;

    if (name == NULL || nameLength == 0 || nameLength >= EX_PROGRAM_NAME_MAX_LENGTH)
        return EC_ILLEGAL_ARGUMENT;

    for (uint32_t index = 0; index < (sizeof(gExBuiltinPrograms) / sizeof(gExBuiltinPrograms[0])); ++index)
    {
        if (!KiProgramNameEquals(&gExBuiltinPrograms[index].Image, name, nameLength))
            continue;

        *outImage = &gExBuiltinPrograms[index].Image;
        return EC_SUCCESS;
    }

    return EC_ILLEGAL_ARGUMENT;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
ExLookupProgramImageById(uint32_t programId, const EX_USER_IMAGE **outImage)
{
    if (outImage == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outImage = NULL;

    if (!gExProgramRegistryValidated)
        return EC_INVALID_STATE;

    if (programId == EX_PROGRAM_ID_NONE)
        return EC_ILLEGAL_ARGUMENT;

    for (uint32_t index = 0; index < (sizeof(gExBuiltinPrograms) / sizeof(gExBuiltinPrograms[0])); ++index)
    {
        if (gExBuiltinPrograms[index].Image.ProgramId != programId)
            continue;

        *outImage = &gExBuiltinPrograms[index].Image;
        return EC_SUCCESS;
    }

    return EC_ILLEGAL_ARGUMENT;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
ExProgramBuildBootstrapCreateParams(const EX_USER_IMAGE *image,
                                    uint32_t parentProcessId,
                                    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *outParams)
{
    if (image == NULL || outParams == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!gExProgramRegistryValidated || image->Kind != EX_USER_IMAGE_KIND_EMBEDDED_SPLIT || image->CodeBytes == NULL ||
        image->CodeLength == 0 || image->ConstBytes == NULL || image->ConstLength == 0)
    {
        return EC_INVALID_STATE;
    }

    memset(outParams, 0, sizeof(*outParams));
    outParams->CodeBytes = image->CodeBytes;
    outParams->CodeLength = image->CodeLength;
    outParams->EntryOffset = image->EntryOffset;
    outParams->ConstBytes = image->ConstBytes;
    outParams->ConstLength = image->ConstLength;
    outParams->ProgramId = image->ProgramId;
    outParams->ParentProcessId = parentProcessId;
    return EC_SUCCESS;
}

#undef EX_PROGRAM_REGISTRY_ENTRY
