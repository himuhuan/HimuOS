/**
 * HimuOperatingSystem
 *
 * File: demo/user_input.c
 * Description: Foreground handoff regression profile for bounded input probes.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_process.h>
#include <kernel/ke/input.h>

typedef struct KI_USER_INPUT_CONTEXT
{
    uint32_t InputProbePid;
    uint32_t LineEchoPid;
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

    status = ExSpawnProgram("input_probe", sizeof("input_probe") - 1U, EX_USER_SPAWN_FLAG_FOREGROUND,
                            &context->InputProbePid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_input input_probe process");
    klog(KLOG_LEVEL_INFO, "[USERINPUT] foreground -> input_probe pid=%u\n", context->InputProbePid);

    while (KeInputGetCompletedReadCount() == initialReadCount)
        KeSleep(KE_DEFAULT_QUANTUM_NS);

    status = ExSpawnProgram("line_echo", sizeof("line_echo") - 1U, EX_USER_SPAWN_FLAG_NONE, &context->LineEchoPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_input line_echo process");

    status = ExSetForegroundProcess(context->LineEchoPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to set line_echo foreground process");
    klog(KLOG_LEVEL_INFO, "[USERINPUT] foreground -> line_echo pid=%u\n", context->LineEchoPid);

    status = ExWaitProcess(context->LineEchoPid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait line_echo process");

    status = ExWaitProcess(context->InputProbePid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to wait input_probe process");

    status = KeInputSetForegroundOwnerThreadId(0U);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to clear foreground owner after line_echo");
}

void
RunUserInputDemo(void)
{
    KTHREAD *controllerThread = NULL;
    HO_STATUS status = EC_SUCCESS;

    gKiUserInputContext.InputProbePid = 0;
    gKiUserInputContext.LineEchoPid = 0;

    status = KeThreadCreate(&controllerThread, KiUserInputControllerThread, &gKiUserInputContext);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_input controller thread");

    status = KeThreadStart(controllerThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_input controller thread");
}
