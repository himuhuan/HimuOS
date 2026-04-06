/**
 * HimuOperatingSystem
 *
 * File: demo/tick1s_artifact_bridge.c
 * Description: Bridge the generated tick1s userspace artifacts into the kernel.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiTick1sCodeBytesStart[];
extern const uint8_t gKiTick1sCodeBytesEnd[];
extern const uint8_t gKiTick1sConstBytesStart[];
extern const uint8_t gKiTick1sConstBytesEnd[];

static uint64_t
KiTick1sArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded tick1s artifact range is invalid");

    return endAddress - startAddress;
}

void
KiTick1sGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Tick1s artifact sink is required");

    artifacts->CodeBytes = gKiTick1sCodeBytesStart;
    artifacts->CodeLength = KiTick1sArtifactLength(gKiTick1sCodeBytesStart, gKiTick1sCodeBytesEnd);
    artifacts->ConstBytes = gKiTick1sConstBytesStart;
    artifacts->ConstLength = KiTick1sArtifactLength(gKiTick1sConstBytesStart, gKiTick1sConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded tick1s artifacts are empty");
}
