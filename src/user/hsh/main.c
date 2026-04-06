/**
 * HimuOperatingSystem
 *
 * File: user/hsh/main.c
 * Description: Minimal P1 demo-shell hsh skeleton: prompt, readline, echo.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gHshPrompt[] = "hsh> ";
static const char gHshPrefix[] = "[HSH] ";
static const char gHshHandoff[] = "[HSH] handoff\n";
static const char gNewLine[] = "\n";

int
main(void)
{
    char line[KE_USER_BOOTSTRAP_READLINE_MAX_LENGTH];
    int64_t status = HoUserWriteStdout(gHshPrompt, sizeof(gHshPrompt) - 1U);
    if (status != (int64_t)(sizeof(gHshPrompt) - 1U))
        HoUserAbort();

    status = HoUserReadLine(line, sizeof(line));
    if (status < 0)
        HoUserAbort();

    if (HoUserWriteStdout(gHshPrefix, sizeof(gHshPrefix) - 1U) != (int64_t)(sizeof(gHshPrefix) - 1U))
        HoUserAbort();

    if (status != 0 && HoUserWriteStdout(line, (uint64_t)status) != status)
        HoUserAbort();

    if (HoUserWriteStdout(gNewLine, sizeof(gNewLine) - 1U) != (int64_t)(sizeof(gNewLine) - 1U))
        HoUserAbort();

    status = HoUserReadLine(line, sizeof(line));
    if (status != -(int64_t)EC_INVALID_STATE)
        HoUserAbort();

    if (HoUserWriteStdout(gHshHandoff, sizeof(gHshHandoff) - 1U) != (int64_t)(sizeof(gHshHandoff) - 1U))
        HoUserAbort();

    HoUserExit(0);
}
