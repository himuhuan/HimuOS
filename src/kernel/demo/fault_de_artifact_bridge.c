/**
 * HimuOperatingSystem
 *
 * File: demo/fault_de_artifact_bridge.c
 * Description: Bridge the generated fault_de userspace artifacts into the kernel.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiFaultDeCodeBytesStart[];
extern const uint8_t gKiFaultDeCodeBytesEnd[];
extern const uint8_t gKiFaultDeConstBytesStart[];
extern const uint8_t gKiFaultDeConstBytesEnd[];

static uint64_t
KiFaultDeArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded fault_de artifact range is invalid");

    return endAddress - startAddress;
}

void
KiFaultDeGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Fault_de artifact sink is required");

    artifacts->CodeBytes = gKiFaultDeCodeBytesStart;
    artifacts->CodeLength = KiFaultDeArtifactLength(gKiFaultDeCodeBytesStart, gKiFaultDeCodeBytesEnd);
    artifacts->ConstBytes = gKiFaultDeConstBytesStart;
    artifacts->ConstLength = KiFaultDeArtifactLength(gKiFaultDeConstBytesStart, gKiFaultDeConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded fault_de artifacts are empty");
}
