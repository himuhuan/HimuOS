/**
 * HimuOperatingSystem
 *
 * File: demo/demo_shell.c
 * Description: P2 demo-shell regression profile that launches hsh as the
 *              userspace-owned entry point.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/demo_shell.h>
#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ke/input.h>

enum
{
    KI_DEMO_SHELL_ENTRY_OFFSET = 0U,
};

typedef struct KI_DEMO_SHELL_CONTEXT
{
    EX_THREAD *HshThread;
    KTHREAD *HshKernelThread;
    uint32_t HshThreadId;
} KI_DEMO_SHELL_CONTEXT;

static KI_DEMO_SHELL_CONTEXT gKiDemoShellContext;

static void KiUnexpectedDemoShellProfileKernelEntry(void *arg);
static void KiDemoShellControllerThread(void *arg);

static void
KiUnexpectedDemoShellProfileKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "demo_shell hsh bootstrap thread unexpectedly executed the kernel entry point");
}

static void
KiDemoShellControllerThread(void *arg)
{
    KI_DEMO_SHELL_CONTEXT *context = (KI_DEMO_SHELL_CONTEXT *)arg;
    HO_STATUS status = EC_SUCCESS;

    KeDemoShellResetControlPlane();

    status = KeInputSetForegroundOwnerThreadId(context->HshThreadId);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to set demo_shell foreground owner");

    status = ExBootstrapStartThread(&context->HshThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start demo_shell hsh thread");

    status = KeThreadJoin(context->HshKernelThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to join demo_shell hsh thread");

    status = KeInputSetForegroundOwnerThreadId(0U);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to clear demo_shell foreground owner");
}

void
RunDemoShellDemo(void)
{
    KI_USER_EMBEDDED_ARTIFACTS hshArtifacts = {0};
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS createParams = {0};
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedDemoShellProfileKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_JOINABLE,
    };
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;
    KTHREAD *kernelThread = NULL;
    KTHREAD *controllerThread = NULL;
    uint32_t hshThreadId = 0;
    HO_STATUS status = EC_SUCCESS;

    KiHshGetEmbeddedArtifacts(&hshArtifacts);

    createParams.CodeBytes = hshArtifacts.CodeBytes;
    createParams.CodeLength = hshArtifacts.CodeLength;
    createParams.EntryOffset = KI_DEMO_SHELL_ENTRY_OFFSET;
    createParams.ConstBytes = hshArtifacts.ConstBytes;
    createParams.ConstLength = hshArtifacts.ConstLength;

    status = ExBootstrapCreateProcess(&createParams, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create demo_shell hsh process");

    status = ExBootstrapCreateThread(&process, &threadParams, &thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create demo_shell hsh thread");

    status = ExBootstrapBorrowKernelThread(thread, &kernelThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to borrow demo_shell hsh kernel thread");

    status = ExBootstrapQueryThreadId(thread, &hshThreadId);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to query demo_shell hsh thread id");

    gKiDemoShellContext.HshThread = thread;
    gKiDemoShellContext.HshKernelThread = kernelThread;
    gKiDemoShellContext.HshThreadId = hshThreadId;

    status = KeThreadCreate(&controllerThread, KiDemoShellControllerThread, &gKiDemoShellContext);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create demo_shell controller thread");

    status = KeThreadStart(controllerThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start demo_shell controller thread");
}
