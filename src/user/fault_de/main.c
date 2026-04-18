/**
 * HimuOperatingSystem
 *
 * File: user/fault_de/main.c
 * Description: Demo-shell foreground payload that intentionally triggers a
 *              user-mode divide error so the shell can verify crash recovery.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

static const char gFaultDeLine[] = "[FAULTDE] triggering #DE\n";

static HO_NORETURN void
HoFaultDeTrigger(void)
{
    __asm__ volatile("xor %%rdx, %%rdx\n"
                     "mov $1, %%rax\n"
                     "xor %%rcx, %%rcx\n"
                     "idivq %%rcx\n"
                     :
                     :
                     : "rax", "rcx", "rdx", "cc", "memory");

    HoUserAbort();
    __builtin_unreachable();
}

int
main(void)
{
    HoUserWaitForP1Gate();

    if (HoUserWriteStdout(gFaultDeLine, sizeof(gFaultDeLine) - 1U) != (int64_t)(sizeof(gFaultDeLine) - 1U))
        HoUserAbort();

    HoFaultDeTrigger();
}
