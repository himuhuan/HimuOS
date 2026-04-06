/**
 * HimuOperatingSystem
 *
 * File: demo/calc_artifact_bridge.c
 * Description: Bridge the generated calc userspace artifacts into the kernel.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiCalcCodeBytesStart[];
extern const uint8_t gKiCalcCodeBytesEnd[];
extern const uint8_t gKiCalcConstBytesStart[];
extern const uint8_t gKiCalcConstBytesEnd[];

static uint64_t
KiCalcArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded calc artifact range is invalid");

    return endAddress - startAddress;
}

void
KiCalcGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Calc artifact sink is required");

    artifacts->CodeBytes = gKiCalcCodeBytesStart;
    artifacts->CodeLength = KiCalcArtifactLength(gKiCalcCodeBytesStart, gKiCalcCodeBytesEnd);
    artifacts->ConstBytes = gKiCalcConstBytesStart;
    artifacts->ConstLength = KiCalcArtifactLength(gKiCalcConstBytesStart, gKiCalcConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded calc artifacts are empty");
}
