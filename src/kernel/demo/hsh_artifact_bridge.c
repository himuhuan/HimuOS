/**
 * HimuOperatingSystem
 *
 * File: demo/hsh_artifact_bridge.c
 * Description: Bridge the generated hsh userspace artifacts into the kernel.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiHshCodeBytesStart[];
extern const uint8_t gKiHshCodeBytesEnd[];
extern const uint8_t gKiHshConstBytesStart[];
extern const uint8_t gKiHshConstBytesEnd[];

static uint64_t
KiHshArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded hsh artifact range is invalid");

    return endAddress - startAddress;
}

void
KiHshGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Hsh artifact sink is required");

    artifacts->CodeBytes = gKiHshCodeBytesStart;
    artifacts->CodeLength = KiHshArtifactLength(gKiHshCodeBytesStart, gKiHshCodeBytesEnd);
    artifacts->ConstBytes = gKiHshConstBytesStart;
    artifacts->ConstLength = KiHshArtifactLength(gKiHshConstBytesStart, gKiHshConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded hsh artifacts are empty");
}
