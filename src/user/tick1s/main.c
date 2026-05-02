/**
 * HimuOperatingSystem
 *
 * File: user/tick1s/main.c
 * Description: Continuous ticker used to prove background output during foreground work.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gTickLine[] = "TICK!\n";

int
main(void)
{
    for (;;)
    {
        if (HoUserSleepMs(5000U) != 0)
            HoUserAbort();

        if (HoUserWriteStdout(gTickLine, sizeof(gTickLine) - 1U) != (int64_t)(sizeof(gTickLine) - 1U))
            HoUserAbort();
    }

    return 0;
}
