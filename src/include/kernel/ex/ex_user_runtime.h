/**
 * HimuOperatingSystem
 *
 * File: ex/ex_user_runtime.h
 * Description: Ex adapter used by the user-runtime bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

struct KTHREAD;
struct KE_USER_MODE_STAGING;

/**
 * Initialize the Ex adapter subsystem.
 * Must be called before any user-runtime thread is scheduled.
 */
HO_KERNEL_API HO_STATUS ExUserRuntimeInit(void);

/**
 * Validate that the current user-runtime thread already has Ex-owned wrapper
 * state registered. Idempotent - second call for the same thread is a no-op
 * returning EC_SUCCESS.
 * Called by the registered user-runtime enter hook.
 */
HO_KERNEL_API HO_STATUS ExUserRuntimeWrapThread(struct KTHREAD *thread);

/**
 * Finalize the Ex ownership for a terminated user-runtime thread: destroy the
 * staging through ExProcess ownership, then clear non-owning runtime table
 * entries and free Ex objects through the existing final release path.
 * Called by the registered user-runtime finalize hook.
 * Returns EC_SUCCESS if the thread had no Ex wrapper (non-user-runtime path).
 */
HO_KERNEL_API HO_STATUS ExUserRuntimeFinalizeThread(struct KTHREAD *thread);

/**
 * Query whether a KTHREAD currently has an Ex user-runtime wrapper.
 * Returns TRUE if the runtime table currently exposes this KTHREAD through an
 * EX_THREAD entry.
 */
HO_KERNEL_API BOOL ExUserRuntimeHasWrapper(const struct KTHREAD *thread);

/**
 * Query the Ex-owned runtime staging associated with a KTHREAD.
 * Returns NULL if the runtime table has no entry for the thread.
 */
HO_KERNEL_API struct KE_USER_MODE_STAGING *ExUserRuntimeQueryThreadStaging(const struct KTHREAD *thread);

/**
 * Validate a clean EX_USER_SYS_EXIT handoff.
 * Exit must not consume user-mode staging in place; the terminated-thread
 * finalizer remains responsible for payload teardown and final wrapper release.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExUserRuntimeHandleExit(struct KTHREAD *thread);
