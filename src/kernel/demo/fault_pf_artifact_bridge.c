/**
 * HimuOperatingSystem
 *
 * File: demo/fault_pf_artifact_bridge.c
 * Description: Bridge the generated fault_pf userspace artifacts into the kernel.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiFaultPfCodeBytesStart[];
extern const uint8_t gKiFaultPfCodeBytesEnd[];
extern const uint8_t gKiFaultPfConstBytesStart[];
extern const uint8_t gKiFaultPfConstBytesEnd[];

static uint64_t
KiFaultPfArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded fault_pf artifact range is invalid");

    return endAddress - startAddress;
}

void
KiFaultPfGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Fault_pf artifact sink is required");

    artifacts->CodeBytes = gKiFaultPfCodeBytesStart;
    artifacts->CodeLength = KiFaultPfArtifactLength(gKiFaultPfCodeBytesStart, gKiFaultPfCodeBytesEnd);
    artifacts->ConstBytes = gKiFaultPfConstBytesStart;
    artifacts->ConstLength = KiFaultPfArtifactLength(gKiFaultPfConstBytesStart, gKiFaultPfConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded fault_pf artifacts are empty");
}
