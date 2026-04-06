/**
 * HimuOperatingSystem
 *
 * File: user/tick1s/main.c
 * Description: Bounded P2 ticker used to prove background spawn and sleep_ms.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gTickLine[] = "TICK!\n";

int
main(void)
{
    HoUserWaitForP1Gate();

    for (uint32_t index = 0; index < 5U; ++index)
    {
        if (HoUserSleepMs(3000U) != 0)
            HoUserAbort();

        if (HoUserWriteStdout(gTickLine, sizeof(gTickLine) - 1U) != (int64_t)(sizeof(gTickLine) - 1U))
            HoUserAbort();
    }

    return 0;
}
