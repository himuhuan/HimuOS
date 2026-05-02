/**
 * HimuOperatingSystem
 *
 * File: demo/user_hello.c
 * Description: Minimal compiled user_hello artifact bridge covering formal
 *              ABI write, invalid-buffer rejection, and clean exit evidence
 *              for the userspace path built from src/user/.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

/*
 * Refactor anchor: keep the user_hello clean-pass evidence chain fixed as
 * first entry, rejected formal write, hello write, EX_USER_SYS_EXIT, thread
 * terminated, runtime teardown complete, and idle/reaper reclaim.
 * This change only permits boundary refactoring around ownership and
 * registration surfaces for the compiled userspace path.
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
        HO_KPANIC(status, "Failed to spawn user_hello process");
}
