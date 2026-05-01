/**
 * HimuOperatingSystem
 *
 * File: demo/user_hello.c
 * Description: Minimal compiled user_hello artifact bridge covering P1 gate wiring,
 *              P2 raw-syscall self-check, and P3 exit-reap termination-before-teardown
 *              evidence for the userspace path built from src/user/.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

/*
 * Refactor anchor: keep the user_hello clean-pass evidence chain fixed as
 * first entry, timer round-trip, rejected raw write, hello write,
 * EX_USER_BRINGUP_SYS_RAW_EXIT, thread terminated, runtime teardown complete, and
 * idle/reaper reclaim.
 * This change only permits boundary refactoring around ownership and
 * registration seams for the compiled userspace bring-up path.
 * It must not change the profile's logs, ordering contract, or pass/fail behavior.
 */

#include "demo_internal.h"

#include <kernel/ex/program.h>

void
RunUserHelloDemo(void)
{
    uint32_t pid = 0;
    HO_STATUS status = ExSpawnProgram("user_hello", sizeof("user_hello") - 1U, EX_USER_SPAWN_FLAG_NONE, &pid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_hello sentinel");
}
