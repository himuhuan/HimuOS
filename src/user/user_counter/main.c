/**
 * HimuOperatingSystem
 *
 * File: user/user_counter/main.c
 * Description: Minimal compiled userspace counter payload for later dual-user
 *              bring-up.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gKiUserCounterLine0[] = "[USERCOUNTER] count=0\n";
static const char gKiUserCounterLine1[] = "[USERCOUNTER] count=1\n";
static const char gKiUserCounterLine2[] = "[USERCOUNTER] count=2\n";

int
main(void)
{
    int64_t status = 0;

    HoUserWaitForP1Gate();

    if (!HoUserCurrentCapabilitySeedBlockIsValid())
        HoUserAbort();

    status = HoUserWriteStdout(gKiUserCounterLine0, sizeof(gKiUserCounterLine0) - 1U);
    if (status != (int64_t)(sizeof(gKiUserCounterLine0) - 1U))
        HoUserAbort();

    status = HoUserWriteStdout(gKiUserCounterLine1, sizeof(gKiUserCounterLine1) - 1U);
    if (status != (int64_t)(sizeof(gKiUserCounterLine1) - 1U))
        HoUserAbort();

    status = HoUserWriteStdout(gKiUserCounterLine2, sizeof(gKiUserCounterLine2) - 1U);
    if (status != (int64_t)(sizeof(gKiUserCounterLine2) - 1U))
        HoUserAbort();

    HoUserExit(0);
}
