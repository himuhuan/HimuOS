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
 * SYS_RAW_EXIT, thread terminated, bootstrap teardown complete, and
 * idle/reaper reclaim.
 * This change only permits boundary refactoring around ownership and
 * registration seams for the compiled userspace bring-up path.
 * It must not change the profile's logs, ordering contract, or pass/fail behavior.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ex/program.h>

static void KiUnexpectedUserHelloKernelEntry(void *arg);

static void
KiUnexpectedUserHelloKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "user_hello bootstrap thread unexpectedly executed the kernel entry point");
}

void
RunUserHelloDemo(void)
{
    const EX_USER_IMAGE *image = NULL;
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS createParams = {0};
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedUserHelloKernelEntry,
        .EntryArg = NULL,
    };
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;

    HO_STATUS status = ExLookupProgramImageByName("user_hello", sizeof("user_hello") - 1U, &image);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to resolve user_hello image");

    status = ExProgramBuildBootstrapCreateParams(image, 0, &createParams);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to build user_hello create params");

    status = ExBootstrapCreateProcess(&createParams, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create staged user_hello payload");

    status = ExBootstrapCreateThread(&process, &threadParams, &thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_hello bootstrap thread");

    status = ExBootstrapStartThread(&thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_hello bootstrap thread");
}
