/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.c
 * Description: Thin Ex bootstrap adapter callback bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ke/bootstrap_callbacks.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>

static void KiDestroyBootstrapWrapperObjects(void);

static HO_NORETURN void
ExBootstrapEnterCallback(KTHREAD *thread)
{
    HO_STATUS status = ExBootstrapAdapterWrapThread(thread);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Failed to wrap bootstrap thread in Ex adapter");
    }

    KeUserBootstrapEnterCurrentThread();
}

static BOOL
ExBootstrapThreadOwnershipQueryCallback(const KTHREAD *thread)
{
    return ExBootstrapAdapterHasWrapper(thread);
}

static HO_STATUS
ExBootstrapFinalizeCallback(KTHREAD *thread)
{
    if (thread != NULL && ExBootstrapAdapterQueryThreadStaging(thread) != NULL)
    {
        klog(KLOG_LEVEL_WARNING, "[USERBOOT] fallback staging reclaim in finalizer thread=%u\n", thread->ThreadId);
    }

    return ExBootstrapAdapterFinalizeThread(thread);
}

static void
ExBootstrapTimerObserveCallback(KTHREAD *thread)
{
    (void)thread;
    KeUserBootstrapObserveCurrentThreadUserTimerPreemption();
}

HO_STATUS
ExBootstrapAdapterInit(void)
{
    return KeRegisterBootstrapCallbacks(ExBootstrapEnterCallback,
                                        ExBootstrapThreadOwnershipQueryCallback,
                                        ExBootstrapFinalizeCallback,
                                        ExBootstrapTimerObserveCallback);
}

HO_STATUS
ExBootstrapAdapterWrapThread(KTHREAD *thread)
{
    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (gExBootstrapThread == NULL || gExBootstrapThread->Thread != thread)
        return EC_INVALID_STATE;

    if (gExBootstrapThread->Process == NULL || gExBootstrapThread->Process->Staging == NULL)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapAdapterFinalizeThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    if (gExBootstrapThread == NULL || gExBootstrapThread->Thread != thread)
        return EC_SUCCESS;

    process = gExBootstrapThread->Process;

    if (process != NULL && process->Staging != NULL)
    {
        status = KeUserBootstrapDestroyStaging(process->Staging);

        /* Destroy consumes the staging object even when teardown reports an error. */
        process->Staging = NULL;
    }

    KiDestroyBootstrapWrapperObjects();
    return status;
}

BOOL
ExBootstrapAdapterHasWrapper(const KTHREAD *thread)
{
    return gExBootstrapThread != NULL && gExBootstrapThread->Thread == thread;
}

struct KE_USER_BOOTSTRAP_STAGING *
ExBootstrapAdapterQueryThreadStaging(const KTHREAD *thread)
{
    if (thread == NULL || gExBootstrapThread == NULL || gExBootstrapThread->Thread != thread)
        return NULL;

    if (gExBootstrapThread->Process == NULL)
        return NULL;

    return gExBootstrapThread->Process->Staging;
}

HO_STATUS
ExBootstrapAdapterHandleRawExit(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (gExBootstrapThread == NULL || gExBootstrapThread->Thread != thread)
        return EC_INVALID_STATE;

    process = gExBootstrapThread->Process;
    if (process == NULL || process->Staging == NULL)
        return EC_INVALID_STATE;

    HO_STATUS status = KeUserBootstrapDestroyStaging(process->Staging);

    /* Destroy consumes the staging object even when teardown reports an error. */
    process->Staging = NULL;

    if (status != EC_SUCCESS)
    {
        KiDestroyBootstrapWrapperObjects();
    }

    return status;
}

static void
KiDestroyBootstrapWrapperObjects(void)
{
    EX_THREAD *exThread = gExBootstrapThread;
    EX_PROCESS *process = gExBootstrapProcess;

    gExBootstrapThread = NULL;
    gExBootstrapProcess = NULL;

    if (exThread != NULL)
    {
        exThread->Thread = NULL;
        exThread->Process = NULL;
        kfree(exThread);
    }

    if (process != NULL)
    {
        process->Staging = NULL;
        kfree(process);
    }
}
