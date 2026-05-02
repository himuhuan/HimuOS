/**
 * HimuOperatingSystem
 *
 * File: user/user_caps/main.c
 * Description: Formal capability and wait-handle userspace regression payload.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gUserCapsMessage[] = "[USERCAP] stdout capability write\n";

enum
{
    HO_USER_CAPS_MESSAGE_LENGTH = sizeof(gUserCapsMessage) - 1U,
};

int
main(void)
{
    const EX_USER_CAPABILITY_SEED_BLOCK *seed = HoUserCapabilitySeedBlock();

    if (!HoUserCapabilitySeedBlockIsValid(seed))
        HoUserAbort();

    if (seed->ProcessSelf == EX_USER_CAPABILITY_INVALID_HANDLE ||
        seed->ThreadSelf == EX_USER_CAPABILITY_INVALID_HANDLE ||
        seed->Stdout == EX_USER_CAPABILITY_INVALID_HANDLE || seed->WaitObject == EX_USER_CAPABILITY_INVALID_HANDLE)
    {
        HoUserAbort();
    }

    int64_t status = HoUserWrite(seed->Stdout, gUserCapsMessage, HO_USER_CAPS_MESSAGE_LENGTH);
    if (status != (int64_t)HO_USER_CAPS_MESSAGE_LENGTH)
        HoUserAbort();

    status = HoUserClose(seed->Stdout);
    if (status != 0)
        HoUserAbort();

    status = HoUserWrite(seed->Stdout, gUserCapsMessage, HO_USER_CAPS_MESSAGE_LENGTH);
    if (status != -(int64_t)EC_INVALID_STATE)
        HoUserAbort();

    status = HoUserWaitOne(seed->WaitObject, 0);
    if (status != -(int64_t)EC_TIMEOUT)
        HoUserAbort();

    status = HoUserClose(seed->WaitObject);
    if (status != 0)
        HoUserAbort();

    HoUserExit(0);
}
