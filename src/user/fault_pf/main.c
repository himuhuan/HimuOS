/**
 * HimuOperatingSystem
 *
 * File: user/fault_pf/main.c
 * Description: Demo-shell foreground payload that intentionally triggers a
 *              user-mode page fault so the shell can verify crash recovery.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gFaultPfLine[] = "[FAULTPF] triggering #PF\n";

static HO_NORETURN void
HoFaultPfTrigger(void)
{
    volatile const uint8_t *guard = (volatile const uint8_t *)HoUserImageStackGuardBase();
    volatile uint8_t value = *guard;

    (void)value;
    HoUserAbort();
    __builtin_unreachable();
}

int
main(void)
{
    if (HoUserWriteStdout(gFaultPfLine, sizeof(gFaultPfLine) - 1U) != (int64_t)(sizeof(gFaultPfLine) - 1U))
        HoUserAbort();

    HoFaultPfTrigger();
}
