/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.h
 * Description: Ex bootstrap callback bridge used by the runtime facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

struct KTHREAD;
struct KE_USER_BOOTSTRAP_STAGING;

/**
 * Initialize the Ex bootstrap adapter subsystem.
 * Must be called before any bootstrap user thread is scheduled.
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterInit(void);

/**
 * Validate that the current bootstrap thread already has Ex-owned wrapper
 * state registered. Idempotent - second call for the same thread is a no-op
 * returning EC_SUCCESS.
 * Called by the registered bootstrap enter callback.
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterWrapThread(struct KTHREAD *thread);

/**
 * Finalize the Ex ownership for a terminated bootstrap thread: destroy the
 * staging through ExProcess ownership, then clear the non-owning runtime alias
 * and free Ex objects through the existing final release path.
 * Called by the registered bootstrap finalize callback.
 * Returns EC_SUCCESS if the thread had no Ex wrapper (non-bootstrap path).
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterFinalizeThread(struct KTHREAD *thread);

/**
 * Query whether a KTHREAD currently has an Ex bootstrap wrapper.
 * Returns TRUE if the runtime registry currently exposes a non-owning
 * EX_THREAD alias for this KTHREAD.
 */
HO_KERNEL_API BOOL ExBootstrapAdapterHasWrapper(const struct KTHREAD *thread);

/**
 * Query the Ex-owned bootstrap staging associated with a KTHREAD.
 * Returns NULL if the runtime registry has no alias for the thread or if the
 * clean raw-exit path has already consumed the staging.
 */
HO_KERNEL_API struct KE_USER_BOOTSTRAP_STAGING *ExBootstrapAdapterQueryThreadStaging(const struct KTHREAD *thread);

/**
 * Destroy bootstrap staging for a clean SYS_RAW_EXIT handoff.
 * On success the staging is consumed but the minimal Ex wrapper remains until
 * finalizer/reaper perform the final wrapper release; the runtime alias stays
 * non-owning throughout that handoff.
 * On failure both staging ownership and Ex wrapper state are cleared.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapAdapterHandleRawExit(struct KTHREAD *thread);
