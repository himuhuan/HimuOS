/**
 * HimuOperatingSystem
 *
 * File: demo/user_hello_artifact_bridge.c
 * Description: Bridge the generated userspace bootstrap bins into the kernel
 *              link for the user_hello demo.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

extern const uint8_t gKiUserHelloCodeBytesStart[];
extern const uint8_t gKiUserHelloCodeBytesEnd[];
extern const uint8_t gKiUserHelloConstBytesStart[];
extern const uint8_t gKiUserHelloConstBytesEnd[];

static uint64_t
KiUserHelloArtifactLength(const uint8_t *start, const uint8_t *end)
{
    uint64_t startAddress = (uint64_t)(const void *)start;
    uint64_t endAddress = (uint64_t)(const void *)end;

    if (endAddress < startAddress)
        HO_KPANIC(EC_INVALID_STATE, "Embedded user_hello artifact range is invalid");

    return endAddress - startAddress;
}

void
KiUserHelloGetEmbeddedArtifacts(KI_USER_HELLO_EMBEDDED_ARTIFACTS *artifacts)
{
    if (artifacts == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "User hello artifact sink is required");

    artifacts->CodeBytes = gKiUserHelloCodeBytesStart;
    artifacts->CodeLength = KiUserHelloArtifactLength(gKiUserHelloCodeBytesStart, gKiUserHelloCodeBytesEnd);
    artifacts->ConstBytes = gKiUserHelloConstBytesStart;
    artifacts->ConstLength = KiUserHelloArtifactLength(gKiUserHelloConstBytesStart, gKiUserHelloConstBytesEnd);

    if (artifacts->CodeLength == 0 || artifacts->ConstLength == 0)
        HO_KPANIC(EC_INVALID_STATE, "Embedded user_hello artifacts are empty");
}
