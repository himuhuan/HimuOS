/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.c
 * Description: Thin Ex bootstrap adapter ownership layer.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ex/ex_process.h>
#include <kernel/ex/ex_thread.h>
#include <kernel/ke/bootstrap_callbacks.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>

struct EX_PROCESS
{
    KE_USER_BOOTSTRAP_STAGING *Staging;
};

struct EX_THREAD
{
    KTHREAD *Thread;
    EX_PROCESS *Process;
};

/* Only one bootstrap process/thread wrapper may exist at a time. */
static EX_PROCESS *gBootstrapProcess = NULL;
static EX_THREAD *gBootstrapThread = NULL;

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

static HO_STATUS
ExBootstrapFinalizeCallback(KTHREAD *thread)
{
    if (thread != NULL && thread->UserBootstrapContext != NULL)
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
                                        ExBootstrapFinalizeCallback,
                                        ExBootstrapTimerObserveCallback);
}

HO_STATUS
ExBootstrapAdapterWrapThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *exThread = NULL;

    if (gBootstrapThread != NULL && gBootstrapThread->Thread == thread)
        return EC_SUCCESS;

    if (gBootstrapThread != NULL)
        return EC_INVALID_STATE;

    if (thread == NULL || thread->UserBootstrapContext == NULL)
        return EC_ILLEGAL_ARGUMENT;

    process = (EX_PROCESS *)kzalloc(sizeof(*process));
    if (process == NULL)
        return EC_OUT_OF_RESOURCE;

    process->Staging = thread->UserBootstrapContext;

    exThread = (EX_THREAD *)kzalloc(sizeof(*exThread));
    if (exThread == NULL)
    {
        kfree(process);
        return EC_OUT_OF_RESOURCE;
    }

    exThread->Thread = thread;
    exThread->Process = process;

    gBootstrapProcess = process;
    gBootstrapThread = exThread;
    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapAdapterFinalizeThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    if (gBootstrapThread == NULL || gBootstrapThread->Thread != thread)
        return EC_SUCCESS;

    process = gBootstrapThread->Process;

    if (process != NULL && process->Staging != NULL)
    {
        if (thread->UserBootstrapContext != NULL)
        {
            HO_KASSERT(process->Staging == thread->UserBootstrapContext, EC_INVALID_STATE);
            status = KeUserBootstrapDestroyStaging(process->Staging);
        }

        /* Destroy consumes the staging object even when teardown reports an error. */
        process->Staging = NULL;
    }

    kfree(gBootstrapThread);
    gBootstrapThread = NULL;

    kfree(gBootstrapProcess);
    gBootstrapProcess = NULL;
    return status;
}

BOOL
ExBootstrapAdapterHasWrapper(const KTHREAD *thread)
{
    return gBootstrapThread != NULL && gBootstrapThread->Thread == thread;
}