/**
 * HimuOperatingSystem
 *
 * File: demo/user_dual.c
 * Description: Dual compiled-userspace runtime profile that launches both the
 *              formal-ABI user_hello and user_counter payloads through Ex
 *              process control.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_process.h>

static void KiUserDualControllerThread(void *arg);

static void
KiUserDualControllerThread(void *arg)
{
    (void)arg;

    uint32_t helloPid = 0;
    uint32_t counterPid = 0;
    HO_STATUS status = EC_SUCCESS;

    status = ExSpawnProgram("user_hello", sizeof("user_hello") - 1U, EX_USER_SPAWN_FLAG_NONE, &helloPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_dual user_hello process");

    status = ExSpawnProgram("user_counter", sizeof("user_counter") - 1U, EX_USER_SPAWN_FLAG_NONE, &counterPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_dual user_counter process");

    status = ExWaitProcess(helloPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait user_dual user_hello process");

    status = ExWaitProcess(counterPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait user_dual user_counter process");
}

void
RunUserDualDemo(void)
{
    KTHREAD *controllerThread = NULL;
    HO_STATUS status = EC_SUCCESS;

    status = KeThreadCreate(&controllerThread, KiUserDualControllerThread, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_dual controller thread");

    status = KeThreadStart(controllerThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_dual controller thread");
}
