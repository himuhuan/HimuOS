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
 * is owned by the bootstrap runtime. Must not return.
 */
typedef void (*KE_BOOTSTRAP_ENTER_FN)(struct KTHREAD *thread) HO_NORETURN;

/**
 * Bootstrap thread ownership query callback - called by Ke-side scheduling
 * paths to determine whether a thread belongs to the bootstrap runtime.
 */
typedef BOOL (*KE_BOOTSTRAP_THREAD_OWNERSHIP_QUERY_FN)(const struct KTHREAD *thread);

/**
 * Bootstrap thread root query callback - called by Ke-side dispatch paths to
 * resolve a thread's target address-space root as an opaque physical identity.
 * May run before the thread's first enter callback, so it must not depend on
 * bootstrap-enter side effects.
 */
typedef HO_STATUS (*KE_BOOTSTRAP_THREAD_ROOT_QUERY_FN)(const struct KTHREAD *thread,
                                                       HO_PHYSICAL_ADDRESS *outRootPageTablePhys);

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
 * Register bootstrap callbacks. All five must be non-NULL.
 * Must be called before any bootstrap user thread can be dispatched.
 * May only be called once.
 */
HO_KERNEL_API HO_STATUS KeRegisterBootstrapCallbacks(KE_BOOTSTRAP_ENTER_FN enterFn,
                                                     KE_BOOTSTRAP_THREAD_OWNERSHIP_QUERY_FN threadOwnershipQueryFn,
                                                     KE_BOOTSTRAP_THREAD_ROOT_QUERY_FN threadRootQueryFn,
                                                     KE_BOOTSTRAP_FINALIZE_FN finalizeFn,
                                                     KE_BOOTSTRAP_TIMER_OBSERVE_FN timerObserveFn);

KE_BOOTSTRAP_ENTER_FN KiGetBootstrapEnterCallback(void);
KE_BOOTSTRAP_THREAD_OWNERSHIP_QUERY_FN KiGetBootstrapThreadOwnershipQueryCallback(void);
KE_BOOTSTRAP_THREAD_ROOT_QUERY_FN KiGetBootstrapThreadRootQueryCallback(void);
KE_BOOTSTRAP_FINALIZE_FN KiGetBootstrapFinalizeCallback(void);
KE_BOOTSTRAP_TIMER_OBSERVE_FN KiGetBootstrapTimerObserveCallback(void);