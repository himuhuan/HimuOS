/**
 * HimuOperatingSystem
 *
 * File: demo/user_input.c
 * Description: P1 demo-shell input regression profile: hsh first, then calc.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ke/input.h>
#include <kernel/ke/scheduler.h>

enum
{
    KI_USER_INPUT_ENTRY_OFFSET = 0U,
};

typedef struct KI_USER_INPUT_CONTEXT
{
    EX_THREAD *HshThread;
    EX_THREAD *CalcThread;
    KTHREAD *HshKernelThread;
    KTHREAD *CalcKernelThread;
    uint32_t HshThreadId;
    uint32_t CalcThreadId;
} KI_USER_INPUT_CONTEXT;

static KI_USER_INPUT_CONTEXT gKiUserInputContext;

static void KiUnexpectedHshKernelEntry(void *arg);
static void KiUnexpectedCalcKernelEntry(void *arg);
static void KiUserInputControllerThread(void *arg);

static void
KiUnexpectedHshKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "hsh bootstrap thread unexpectedly executed the kernel entry point");
}

static void
KiUnexpectedCalcKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "calc bootstrap thread unexpectedly executed the kernel entry point");
}

static void
KiUserInputControllerThread(void *arg)
{
    KI_USER_INPUT_CONTEXT *context = (KI_USER_INPUT_CONTEXT *)arg;
    uint64_t initialReadCount = KeInputGetCompletedReadCount();
    HO_STATUS status = EC_SUCCESS;

    status = KeInputSetForegroundOwnerThreadId(context->HshThreadId);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to set hsh foreground owner");
    klog(KLOG_LEVEL_INFO, "[USERINPUT] foreground -> hsh thread=%u\n", context->HshThreadId);

    status = ExBootstrapStartThread(&context->HshThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start hsh bootstrap thread");

    status = ExBootstrapStartThread(&context->CalcThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start calc bootstrap thread");

    while (KeInputGetCompletedReadCount() == initialReadCount)
        KeSleep(KE_DEFAULT_QUANTUM_NS);

    status = KeInputSetForegroundOwnerThreadId(context->CalcThreadId);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to set calc foreground owner");
    klog(KLOG_LEVEL_INFO, "[USERINPUT] foreground -> calc thread=%u\n", context->CalcThreadId);

    status = KeThreadJoin(context->HshKernelThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to join hsh bootstrap thread");

    status = KeThreadJoin(context->CalcKernelThread, KE_WAIT_INFINITE);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to join calc bootstrap thread");

    status = KeInputSetForegroundOwnerThreadId(0U);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to clear foreground owner after calc");
}

void
RunUserInputDemo(void)
{
    KI_USER_EMBEDDED_ARTIFACTS hshArtifacts = {0};
    KI_USER_EMBEDDED_ARTIFACTS calcArtifacts = {0};
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS hshCreateParams = {0};
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS calcCreateParams = {0};
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS hshThreadParams = {
        .EntryPoint = KiUnexpectedHshKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_NONE,
    };
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS calcThreadParams = {
        .EntryPoint = KiUnexpectedCalcKernelEntry,
        .EntryArg = NULL,
        .Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_NONE,
    };
    EX_PROCESS *hshProcess = NULL;
    EX_PROCESS *calcProcess = NULL;
    EX_THREAD *hshThread = NULL;
    EX_THREAD *calcThread = NULL;
    KTHREAD *hshKernelThread = NULL;
    KTHREAD *calcKernelThread = NULL;
    KTHREAD *controllerThread = NULL;
    uint32_t hshThreadId = 0;
    uint32_t calcThreadId = 0;
    HO_STATUS status = EC_SUCCESS;

    KiHshGetEmbeddedArtifacts(&hshArtifacts);
    KiCalcGetEmbeddedArtifacts(&calcArtifacts);

    hshCreateParams.CodeBytes = hshArtifacts.CodeBytes;
    hshCreateParams.CodeLength = hshArtifacts.CodeLength;
    hshCreateParams.EntryOffset = KI_USER_INPUT_ENTRY_OFFSET;
    hshCreateParams.ConstBytes = hshArtifacts.ConstBytes;
    hshCreateParams.ConstLength = hshArtifacts.ConstLength;

    calcCreateParams.CodeBytes = calcArtifacts.CodeBytes;
    calcCreateParams.CodeLength = calcArtifacts.CodeLength;
    calcCreateParams.EntryOffset = KI_USER_INPUT_ENTRY_OFFSET;
    calcCreateParams.ConstBytes = calcArtifacts.ConstBytes;
    calcCreateParams.ConstLength = calcArtifacts.ConstLength;

    status = ExBootstrapCreateProcess(&hshCreateParams, &hshProcess);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create hsh bootstrap process");

    hshThreadParams.Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_JOINABLE;
    status = ExBootstrapCreateThread(&hshProcess, &hshThreadParams, &hshThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create hsh bootstrap thread");

    status = ExBootstrapCreateProcess(&calcCreateParams, &calcProcess);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create calc bootstrap process");

    calcThreadParams.Flags = EX_BOOTSTRAP_THREAD_CREATE_FLAG_JOINABLE;
    status = ExBootstrapCreateThread(&calcProcess, &calcThreadParams, &calcThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create calc bootstrap thread");

    status = ExBootstrapBorrowKernelThread(hshThread, &hshKernelThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to borrow hsh kernel thread");

    status = ExBootstrapBorrowKernelThread(calcThread, &calcKernelThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to borrow calc kernel thread");

    status = ExBootstrapQueryThreadId(hshThread, &hshThreadId);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to query hsh thread id");

    status = ExBootstrapQueryThreadId(calcThread, &calcThreadId);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to query calc thread id");

    gKiUserInputContext.HshThread = hshThread;
    gKiUserInputContext.CalcThread = calcThread;
    gKiUserInputContext.HshKernelThread = hshKernelThread;
    gKiUserInputContext.CalcKernelThread = calcKernelThread;
    gKiUserInputContext.HshThreadId = hshThreadId;
    gKiUserInputContext.CalcThreadId = calcThreadId;

    status = KeThreadCreate(&controllerThread, KiUserInputControllerThread, &gKiUserInputContext);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_input controller thread");

    status = KeThreadStart(controllerThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_input controller thread");
}
