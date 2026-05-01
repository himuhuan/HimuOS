/**
 * HimuOperatingSystem
 *
 * File: demo/user_input.c
 * Description: Demo-shell input regression profile: hsh first, then calc.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_process.h>
#include <kernel/ke/input.h>

typedef struct KI_USER_INPUT_CONTEXT
{
    uint32_t HshPid;
    uint32_t CalcPid;
} KI_USER_INPUT_CONTEXT;

static KI_USER_INPUT_CONTEXT gKiUserInputContext;

static void KiUserInputControllerThread(void *arg);

static void
KiUserInputControllerThread(void *arg)
{
    KI_USER_INPUT_CONTEXT *context = (KI_USER_INPUT_CONTEXT *)arg;
    uint64_t initialReadCount = 0;
    HO_STATUS status = EC_SUCCESS;

    if (context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "user_input context is required");

    initialReadCount = KeInputGetCompletedReadCount();

    status = ExSpawnProgram("hsh", sizeof("hsh") - 1U, EX_USER_SPAWN_FLAG_FOREGROUND, &context->HshPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_input hsh process");
    klog(KLOG_LEVEL_INFO, "[USERINPUT] foreground -> hsh pid=%u\n", context->HshPid);

    status = ExSpawnProgram("calc", sizeof("calc") - 1U, EX_USER_SPAWN_FLAG_NONE, &context->CalcPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_input calc process");

    while (KeInputGetCompletedReadCount() == initialReadCount)
        KeSleep(KE_DEFAULT_QUANTUM_NS);

    status = ExSetForegroundProcess(context->CalcPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to set calc foreground process");
    klog(KLOG_LEVEL_INFO, "[USERINPUT] foreground -> calc pid=%u\n", context->CalcPid);

    status = ExWaitProcess(context->CalcPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait calc process");

    status = ExWaitProcess(context->HshPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait hsh process");

    status = KeInputSetForegroundOwnerThreadId(0U);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to clear foreground owner after calc");
}

void
RunUserInputDemo(void)
{
    KTHREAD *controllerThread = NULL;
    HO_STATUS status = EC_SUCCESS;

    gKiUserInputContext.HshPid = 0;
    gKiUserInputContext.CalcPid = 0;

    status = KeThreadCreate(&controllerThread, KiUserInputControllerThread, &gKiUserInputContext);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_input controller thread");

    status = KeThreadStart(controllerThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_input controller thread");
}
