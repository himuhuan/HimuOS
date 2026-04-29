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

struct KTHREAD;

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
 * Query the kernel-visible ThreadId for a not-yet-consumed bootstrap thread
 * handle. This keeps demo/profile code off Ex internals while still allowing
 * narrow foreground-owner setup.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapQueryThreadId(const EX_THREAD *thread, uint32_t *outThreadId);

/**
 * Query the Ex-owned PID for a bootstrap process. PIDs are independent from
 * KTHREAD::ThreadId and are the user-visible identity for process syscalls.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapQueryProcessId(const EX_PROCESS *process, uint32_t *outProcessId);

/**
 * Query the Ex-owned PID for the current bootstrap process.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapQueryCurrentProcessId(uint32_t *outProcessId);

/**
 * Borrow the underlying KTHREAD for a bootstrap thread before runtime
 * ownership is transferred. Intended for narrow kernel-side coordination such
 * as waiting on a joinable bootstrap demo thread.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapBorrowKernelThread(EX_THREAD *thread, struct KTHREAD **outThread);

/**
 * Cancel a bootstrap thread before it has been started.
 * Only threads still in the NEW state can be torn down here; once started,
 * teardown is owned by the runtime finalizer.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExBootstrapTeardownThread(EX_THREAD *thread);
