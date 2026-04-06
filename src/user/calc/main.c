/**
 * HimuOperatingSystem
 *
 * File: user/calc/main.c
 * Description: Minimal P1 demo-shell calc skeleton: prompt, readline, echo.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gCalcPrompt[] = "calc> ";
static const char gCalcPrefix[] = "[CALC] ";
static const char gNewLine[] = "\n";

int
main(void)
{
    char line[KE_USER_BOOTSTRAP_READLINE_MAX_LENGTH];
    int64_t status = HoUserWriteStdout(gCalcPrompt, sizeof(gCalcPrompt) - 1U);
    if (status != (int64_t)(sizeof(gCalcPrompt) - 1U))
        HoUserAbort();

    for (;;)
    {
        status = HoUserReadLine(line, sizeof(line));
        if (status >= 0)
            break;

        if (status != -(int64_t)EC_INVALID_STATE)
            HoUserAbort();
    }

    if (HoUserWriteStdout(gCalcPrefix, sizeof(gCalcPrefix) - 1U) != (int64_t)(sizeof(gCalcPrefix) - 1U))
        HoUserAbort();

    if (status != 0 && HoUserWriteStdout(line, (uint64_t)status) != status)
        HoUserAbort();

    if (HoUserWriteStdout(gNewLine, sizeof(gNewLine) - 1U) != (int64_t)(sizeof(gNewLine) - 1U))
        HoUserAbort();

    HoUserExit(0);
}
