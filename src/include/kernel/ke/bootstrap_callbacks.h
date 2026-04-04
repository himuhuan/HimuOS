/**
 * HimuOperatingSystem
 *
 * File: ke/bootstrap_callbacks.h
 * Description: Ke-side bootstrap callback registration contract.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

struct KTHREAD;

/**
 * Bootstrap enter callback - called from thread trampoline when the thread
 * has a non-NULL UserBootstrapContext. Must not return.
 */
typedef void (*KE_BOOTSTRAP_ENTER_FN)(struct KTHREAD *thread) HO_NORETURN;

/**
 * Bootstrap finalize callback - called from thread finalizer when a
 * terminated thread may hold bootstrap resources.
 * Return EC_SUCCESS if nothing needs to be cleaned; error propagates.
 */
typedef HO_STATUS (*KE_BOOTSTRAP_FINALIZE_FN)(struct KTHREAD *thread);

/**
 * Bootstrap timer observe callback - called from the timer ISR when the
 * interrupted context was in user mode (CPL 3).
 */
typedef void (*KE_BOOTSTRAP_TIMER_OBSERVE_FN)(struct KTHREAD *thread);

/**
 * Register bootstrap callbacks. All three must be non-NULL.
 * Must be called before any bootstrap user thread is scheduled.
 * May only be called once.
 */
HO_KERNEL_API HO_STATUS KeRegisterBootstrapCallbacks(KE_BOOTSTRAP_ENTER_FN enterFn,
                                                     KE_BOOTSTRAP_FINALIZE_FN finalizeFn,
                                                     KE_BOOTSTRAP_TIMER_OBSERVE_FN timerObserveFn);

KE_BOOTSTRAP_ENTER_FN KiGetBootstrapEnterCallback(void);
KE_BOOTSTRAP_FINALIZE_FN KiGetBootstrapFinalizeCallback(void);
KE_BOOTSTRAP_TIMER_OBSERVE_FN KiGetBootstrapTimerObserveCallback(void);