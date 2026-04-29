/**
 * HimuOperatingSystem
 *
 * File: demo/user_dual.c
 * Description: Dual compiled-userspace bring-up profile that launches both the
 *              user_hello and user_counter bootstrap payloads in one session.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ex/program.h>

static void KiUnexpectedUserDualHelloKernelEntry(void *arg);
static void KiUnexpectedUserDualCounterKernelEntry(void *arg);

static void
KiUnexpectedUserDualHelloKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "user_dual user_hello bootstrap thread unexpectedly executed the kernel entry point");
}

static void
KiUnexpectedUserDualCounterKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "user_dual user_counter bootstrap thread unexpectedly executed the kernel entry point");
}

void
RunUserDualDemo(void)
{
    const EX_USER_IMAGE *helloImage = NULL;
    const EX_USER_IMAGE *counterImage = NULL;
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS helloCreateParams = {0};
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS counterCreateParams = {0};
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS helloThreadParams = {
        .EntryPoint = KiUnexpectedUserDualHelloKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_NONE,
    };
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS counterThreadParams = {
        .EntryPoint = KiUnexpectedUserDualCounterKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_NONE,
    };
    EX_PROCESS *helloProcess = NULL;
    EX_PROCESS *counterProcess = NULL;
    EX_THREAD *helloThread = NULL;
    EX_THREAD *counterThread = NULL;
    HO_STATUS status = EC_SUCCESS;

    status = ExLookupProgramImageByName("user_hello", sizeof("user_hello") - 1U, &helloImage);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to resolve user_dual user_hello image");

    status = ExLookupProgramImageByName("user_counter", sizeof("user_counter") - 1U, &counterImage);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to resolve user_dual user_counter image");

    status = ExProgramBuildBootstrapCreateParams(helloImage, 0, &helloCreateParams);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to build user_dual user_hello params");

    status = ExProgramBuildBootstrapCreateParams(counterImage, 0, &counterCreateParams);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to build user_dual user_counter params");

    status = ExBootstrapCreateProcess(&helloCreateParams, &helloProcess);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_dual user_hello process");

    status = ExBootstrapCreateThread(&helloProcess, &helloThreadParams, &helloThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_dual user_hello bootstrap thread");

    status = ExBootstrapCreateProcess(&counterCreateParams, &counterProcess);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_dual user_counter process");

    status = ExBootstrapCreateThread(&counterProcess, &counterThreadParams, &counterThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_dual user_counter bootstrap thread");

    status = ExBootstrapStartThread(&helloThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_dual user_hello bootstrap thread");

    status = ExBootstrapStartThread(&counterThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_dual user_counter bootstrap thread");
}
