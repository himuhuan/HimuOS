/**
 * HimuOperatingSystem
 *
 * File: demo/demo_shell.c
 * Description: P2 demo-shell regression profile that launches hsh as the
 *              userspace-owned entry point.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_process.h>

typedef struct KI_DEMO_SHELL_CONTEXT
{
    uint32_t HshPid;
} KI_DEMO_SHELL_CONTEXT;

static KI_DEMO_SHELL_CONTEXT gKiDemoShellContext;

static void KiDemoShellControllerThread(void *arg);

static void
KiDemoShellControllerThread(void *arg)
{
    KI_DEMO_SHELL_CONTEXT *context = (KI_DEMO_SHELL_CONTEXT *)arg;
    HO_STATUS status = EC_SUCCESS;

    if (context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "demo_shell context is required");

    status = ExSpawnProgram("hsh", sizeof("hsh") - 1U, EX_USER_SPAWN_FLAG_FOREGROUND, &context->HshPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn demo_shell hsh process");

    status = ExWaitProcess(context->HshPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait demo_shell hsh process");
}

void
RunDemoShellDemo(void)
{
    KTHREAD *controllerThread = NULL;
    HO_STATUS status = EC_SUCCESS;

    gKiDemoShellContext.HshPid = 0;

    status = KeThreadCreate(&controllerThread, KiDemoShellControllerThread, &gKiDemoShellContext);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create demo_shell controller thread");

    status = KeThreadStart(controllerThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start demo_shell controller thread");
}
