/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.h
 * Description: Transitional Ex adapter used by the user-runtime bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

struct KTHREAD;
struct KE_USER_BOOTSTRAP_STAGING;

/**
 * Initialize the Ex adapter subsystem.
 * Must be called before any user-runtime thread is scheduled.
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterInit(void);

/**
 * Validate that the current user-runtime thread already has Ex-owned wrapper
 * state registered. Idempotent - second call for the same thread is a no-op
 * returning EC_SUCCESS.
 * Called by the registered user-runtime enter hook.
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterWrapThread(struct KTHREAD *thread);

/**
 * Finalize the Ex ownership for a terminated user-runtime thread: destroy the
 * staging through ExProcess ownership, then clear non-owning runtime table
 * entries and free Ex objects through the existing final release path.
 * Called by the registered user-runtime finalize hook.
 * Returns EC_SUCCESS if the thread had no Ex wrapper (non-user-runtime path).
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterFinalizeThread(struct KTHREAD *thread);

/**
 * Query whether a KTHREAD currently has an Ex user-runtime wrapper.
 * Returns TRUE if the runtime table currently exposes this KTHREAD through an
 * EX_THREAD entry.
 */
HO_KERNEL_API BOOL ExBootstrapAdapterHasWrapper(const struct KTHREAD *thread);

/**
 * Query the Ex-owned bootstrap staging associated with a KTHREAD.
 * Returns NULL if the runtime table has no entry for the thread.
 */
HO_KERNEL_API struct KE_USER_BOOTSTRAP_STAGING *ExBootstrapAdapterQueryThreadStaging(const struct KTHREAD *thread);

/**
 * Compatibility dispatcher for the original Ex-facing capability syscall set.
 * New trap entry code should call ExDispatchSyscall(); this wrapper preserves
 * the older capability-only call shape for transitional callers.
 */
HO_KERNEL_API HO_NODISCARD int64_t ExBootstrapAdapterDispatchSyscall(uint64_t syscallNumber,
                                                                     uint64_t arg0,
                                                                     uint64_t arg1,
                                                                     uint64_t arg2);

/**
 * Validate a clean EX_USER_SYS_EXIT / EX_USER_BRINGUP_SYS_RAW_EXIT handoff.
 * Exit must not consume bootstrap staging in place; the terminated-thread
 * finalizer remains responsible for payload teardown and final wrapper release.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapAdapterHandleExit(struct KTHREAD *thread);
