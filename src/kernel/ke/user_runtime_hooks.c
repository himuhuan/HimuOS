/**
 * HimuOperatingSystem
 *
 * File: ke/user_runtime_hooks.c
 * Description: Ke-side user-runtime hook registration storage.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/user_runtime_hooks.h>

static KE_USER_RUNTIME_ENTER_HOOK gUserRuntimeEnterHook = NULL;
static KE_USER_RUNTIME_OWNS_THREAD_HOOK gUserRuntimeOwnsThreadHook = NULL;
static KE_USER_RUNTIME_RESOLVE_ROOT_HOOK gUserRuntimeResolveRootHook = NULL;
static KE_USER_RUNTIME_FINALIZE_THREAD_HOOK gUserRuntimeFinalizeThreadHook = NULL;
static KE_USER_RUNTIME_OBSERVE_TIMER_HOOK gUserRuntimeObserveTimerHook = NULL;
static KE_USER_RUNTIME_FAULT_HOOK gUserRuntimeFaultHook = NULL;

HO_KERNEL_API HO_STATUS
KeRegisterUserRuntimeHooks(KE_USER_RUNTIME_ENTER_HOOK enterFn,
                           KE_USER_RUNTIME_OWNS_THREAD_HOOK ownsThreadFn,
                           KE_USER_RUNTIME_RESOLVE_ROOT_HOOK resolveRootFn,
                           KE_USER_RUNTIME_FINALIZE_THREAD_HOOK finalizeThreadFn,
                           KE_USER_RUNTIME_OBSERVE_TIMER_HOOK observeTimerFn,
                           KE_USER_RUNTIME_FAULT_HOOK faultFn)
{
    if (enterFn == NULL || ownsThreadFn == NULL || resolveRootFn == NULL || finalizeThreadFn == NULL ||
        faultFn == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    if (gUserRuntimeEnterHook != NULL || gUserRuntimeOwnsThreadHook != NULL || gUserRuntimeResolveRootHook != NULL ||
        gUserRuntimeFinalizeThreadHook != NULL || gUserRuntimeObserveTimerHook != NULL || gUserRuntimeFaultHook != NULL)
    {
        return EC_INVALID_STATE;
    }

    gUserRuntimeEnterHook = enterFn;
    gUserRuntimeOwnsThreadHook = ownsThreadFn;
    gUserRuntimeResolveRootHook = resolveRootFn;
    gUserRuntimeFinalizeThreadHook = finalizeThreadFn;
    gUserRuntimeObserveTimerHook = observeTimerFn;
    gUserRuntimeFaultHook = faultFn;
    return EC_SUCCESS;
}

KE_USER_RUNTIME_ENTER_HOOK
KiGetUserRuntimeEnterHook(void)
{
    return gUserRuntimeEnterHook;
}

KE_USER_RUNTIME_OWNS_THREAD_HOOK
KiGetUserRuntimeOwnsThreadHook(void)
{
    return gUserRuntimeOwnsThreadHook;
}

KE_USER_RUNTIME_RESOLVE_ROOT_HOOK
KiGetUserRuntimeResolveRootHook(void)
{
    return gUserRuntimeResolveRootHook;
}

KE_USER_RUNTIME_FINALIZE_THREAD_HOOK
KiGetUserRuntimeFinalizeThreadHook(void)
{
    return gUserRuntimeFinalizeThreadHook;
}

KE_USER_RUNTIME_OBSERVE_TIMER_HOOK
KiGetUserRuntimeObserveTimerHook(void)
{
    return gUserRuntimeObserveTimerHook;
}

KE_USER_RUNTIME_FAULT_HOOK
KiGetUserRuntimeFaultHook(void)
{
    return gUserRuntimeFaultHook;
}
