/**
 * HimuOperatingSystem
 *
 * File: ke/user_runtime_hooks.h
 * Description: Ke-side user-runtime hook registration contract.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

struct KTHREAD;

typedef struct KE_USER_RUNTIME_FAULT_CONTEXT
{
    uint8_t VectorNumber;
    BOOL HasFaultAddress;
    BOOL IsSafePageFaultContext;
    uint8_t Reserved0[5];
    HO_VIRTUAL_ADDRESS InstructionPointer;
    uint64_t ErrorCode;
    HO_VIRTUAL_ADDRESS FaultAddress;
    uint32_t PageFaultErrorCode;
} KE_USER_RUNTIME_FAULT_CONTEXT;

/**
 * User-runtime enter hook - called from thread trampoline when the thread is
 * owned by the user runtime. Must not return.
 */
typedef void (*KE_USER_RUNTIME_ENTER_HOOK)(struct KTHREAD *thread) HO_NORETURN;

/**
 * User-runtime ownership hook - called by Ke scheduling paths to determine
 * whether a thread belongs to the user runtime.
 */
typedef BOOL (*KE_USER_RUNTIME_OWNS_THREAD_HOOK)(const struct KTHREAD *thread);

/**
 * User-runtime root hook - called by Ke dispatch paths to resolve a thread's
 * target address-space root as an opaque physical identity.
 * May run before the thread's first enter hook, so it must not depend on
 * user-entry side effects.
 */
typedef HO_STATUS (*KE_USER_RUNTIME_RESOLVE_ROOT_HOOK)(const struct KTHREAD *thread,
                                                       HO_PHYSICAL_ADDRESS *outRootPageTablePhys);

/**
 * User-runtime finalization hook - called from thread finalizer when a
 * terminated thread may hold user-runtime resources.
 * Return EC_SUCCESS if nothing needs to be cleaned; error propagates.
 */
typedef HO_STATUS (*KE_USER_RUNTIME_FINALIZE_THREAD_HOOK)(struct KTHREAD *thread);

/**
 * User-runtime timer observe hook - called from the timer ISR when the
 * interrupted context was in user mode (CPL 3).
 */
typedef void (*KE_USER_RUNTIME_OBSERVE_TIMER_HOOK)(struct KTHREAD *thread);

/**
 * User-runtime fault hook - called from the IDT exception entry when a
 * supported user-runtime-owned user-mode fault should terminate only the
 * current thread. Accepted handoffs must not return.
 */
typedef void (*KE_USER_RUNTIME_FAULT_HOOK)(struct KTHREAD *thread,
                                           const KE_USER_RUNTIME_FAULT_CONTEXT *context) HO_NORETURN;

/**
 * Register user-runtime hooks. All six must be non-NULL.
 * Must be called before any user-runtime thread can be dispatched.
 * May only be called once.
 */
HO_KERNEL_API HO_STATUS KeRegisterUserRuntimeHooks(KE_USER_RUNTIME_ENTER_HOOK enterFn,
                                                   KE_USER_RUNTIME_OWNS_THREAD_HOOK threadOwnershipQueryFn,
                                                   KE_USER_RUNTIME_RESOLVE_ROOT_HOOK threadRootQueryFn,
                                                   KE_USER_RUNTIME_FINALIZE_THREAD_HOOK finalizeFn,
                                                   KE_USER_RUNTIME_OBSERVE_TIMER_HOOK timerObserveFn,
                                                   KE_USER_RUNTIME_FAULT_HOOK userExceptionFn);

KE_USER_RUNTIME_ENTER_HOOK KiGetUserRuntimeEnterHook(void);
KE_USER_RUNTIME_OWNS_THREAD_HOOK KiGetUserRuntimeOwnsThreadHook(void);
KE_USER_RUNTIME_RESOLVE_ROOT_HOOK KiGetUserRuntimeResolveRootHook(void);
KE_USER_RUNTIME_FINALIZE_THREAD_HOOK KiGetUserRuntimeFinalizeThreadHook(void);
KE_USER_RUNTIME_OBSERVE_TIMER_HOOK KiGetUserRuntimeObserveTimerHook(void);
KE_USER_RUNTIME_FAULT_HOOK KiGetUserRuntimeFaultHook(void);
