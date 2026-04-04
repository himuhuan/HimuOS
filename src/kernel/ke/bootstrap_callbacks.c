/**
 * HimuOperatingSystem
 *
 * File: ke/bootstrap_callbacks.c
 * Description: Ke-side bootstrap callback registration storage.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/bootstrap_callbacks.h>

static KE_BOOTSTRAP_ENTER_FN gBootstrapEnterCallback = NULL;
static KE_BOOTSTRAP_FINALIZE_FN gBootstrapFinalizeCallback = NULL;
static KE_BOOTSTRAP_TIMER_OBSERVE_FN gBootstrapTimerObserveCallback = NULL;

HO_KERNEL_API HO_STATUS
KeRegisterBootstrapCallbacks(KE_BOOTSTRAP_ENTER_FN enterFn,
                             KE_BOOTSTRAP_FINALIZE_FN finalizeFn,
                             KE_BOOTSTRAP_TIMER_OBSERVE_FN timerObserveFn)
{
    if (enterFn == NULL || finalizeFn == NULL || timerObserveFn == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    if (gBootstrapEnterCallback != NULL || gBootstrapFinalizeCallback != NULL ||
        gBootstrapTimerObserveCallback != NULL)
    {
        return EC_INVALID_STATE;
    }

    gBootstrapEnterCallback = enterFn;
    gBootstrapFinalizeCallback = finalizeFn;
    gBootstrapTimerObserveCallback = timerObserveFn;
    return EC_SUCCESS;
}

KE_BOOTSTRAP_ENTER_FN
KiGetBootstrapEnterCallback(void)
{
    return gBootstrapEnterCallback;
}

KE_BOOTSTRAP_FINALIZE_FN
KiGetBootstrapFinalizeCallback(void)
{
    return gBootstrapFinalizeCallback;
}

KE_BOOTSTRAP_TIMER_OBSERVE_FN
KiGetBootstrapTimerObserveCallback(void)
{
    return gBootstrapTimerObserveCallback;
}