/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.h
 * Description: Thin Ex bootstrap adapter - ownership and callback bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

struct KTHREAD;

/**
 * Initialize the Ex bootstrap adapter subsystem.
 * Must be called before any bootstrap user thread is scheduled.
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterInit(void);

/**
 * Lazily wrap the current thread's existing KTHREAD + staging in Ex objects
 * (EX_PROCESS / EX_THREAD). Idempotent - second call for the same thread
 * is a no-op returning EC_SUCCESS.
 * Called by Phase 3 enter callback.
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterWrapThread(struct KTHREAD *thread);

/**
 * Finalize the Ex ownership for a terminated bootstrap thread: destroy the
 * staging through ExProcess ownership, then free Ex objects.
 * Called by Phase 3 finalize callback; replaces direct KeUserBootstrapDestroyStaging.
 * Returns EC_SUCCESS if the thread had no Ex wrapper (non-bootstrap path).
 */
HO_KERNEL_API HO_STATUS ExBootstrapAdapterFinalizeThread(struct KTHREAD *thread);

/**
 * Query whether a KTHREAD currently has an Ex bootstrap wrapper.
 * Returns TRUE if the adapter owns an EX_THREAD for this KTHREAD.
 */
HO_KERNEL_API BOOL ExBootstrapAdapterHasWrapper(const struct KTHREAD *thread);