/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap.h
 * Description: Ex-owned bootstrap runtime and launch facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#include <kernel/ex/ex_process.h>
#include <kernel/ex/ex_thread.h>

HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapInit(void);

/**
 * Create an owning bootstrap process handle for a staged user image.
 * The caller owns the returned handle until it is either destroyed explicitly
 * or consumed by ExBootstrapCreateThread().
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapCreateProcess(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params,
                                                              EX_PROCESS **outProcess);

/**
 * Destroy an owning bootstrap process handle that has not been transferred to
 * a bootstrap thread. The runtime registry only exposes a non-owning alias for
 * lookup, so final release still happens through this explicit destroy path or
 * through thread-owned teardown after ExBootstrapCreateThread().
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapDestroyProcess(EX_PROCESS *process);

/**
 * Create a bootstrap thread and transfer process ownership into it.
 * On success this consumes *processHandle, sets it to NULL, and returns a new
 * owning EX_THREAD handle in *outThread. The runtime registry then publishes a
 * non-owning alias for KTHREAD-based lookup only.
 * On failure *processHandle remains owned by the caller.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapCreateThread(EX_PROCESS **processHandle,
                                                             const EX_BOOTSTRAP_THREAD_CREATE_PARAMS *params,
                                                             EX_THREAD **outThread);

/**
 * Start a bootstrap thread and transfer thread ownership to the runtime.
 * On success this consumes *threadHandle, sets it to NULL, and the runtime
 * finalizer becomes responsible for teardown and final release; the published
 * runtime alias remains non-owning.
 * On failure *threadHandle remains owned by the caller.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapStartThread(EX_THREAD **threadHandle);

/**
 * Cancel a bootstrap thread before it has been started.
 * Only threads still in the NEW state can be torn down here; once started,
 * teardown is owned by the runtime finalizer.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapTeardownThread(EX_THREAD *thread);