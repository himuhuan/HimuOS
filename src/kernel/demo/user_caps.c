/**
 * HimuOperatingSystem
 *
 * File: demo/user_caps.c
 * Description: Capability regression profile covering the versioned seed block,
 *              stdout capability write, process wait-handle timeout/close,
 *              stale-handle rejection after close, and formal clean exit.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/program.h>

void
RunUserCapsDemo(void)
{
    uint32_t pid = 0;
    HO_STATUS status = ExSpawnProgram("user_caps", sizeof("user_caps") - 1U, EX_USER_SPAWN_FLAG_NONE, &pid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_caps process");
}
