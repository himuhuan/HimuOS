/**
 * HimuOperatingSystem
 *
 * File: demo/user_counter_artifact_bridge.c
 * Description: Bridge the generated userspace bootstrap bins into the kernel
 *              link for the user_counter payload.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiUserCounterCodeBytesStart[];
extern const uint8_t gKiUserCounterCodeBytesEnd[];
extern const uint8_t gKiUserCounterConstBytesStart[];
extern const uint8_t gKiUserCounterConstBytesEnd[];

static uint64_t
KiUserCounterArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded user_counter artifact range is invalid");

    return endAddress - startAddress;
}

void
KiUserCounterGetEmbeddedArtifacts(KI_USER_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "User counter artifact sink is required");

    artifacts->CodeBytes = gKiUserCounterCodeBytesStart;
    artifacts->CodeLength = KiUserCounterArtifactLength(gKiUserCounterCodeBytesStart, gKiUserCounterCodeBytesEnd);
    artifacts->ConstBytes = gKiUserCounterConstBytesStart;
    artifacts->ConstLength = KiUserCounterArtifactLength(gKiUserCounterConstBytesStart, gKiUserCounterConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded user_counter artifacts are empty");
}
