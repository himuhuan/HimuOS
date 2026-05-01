/**
 * HimuOperatingSystem
 *
 * File: ex/ex_runtime.h
 * Description: Ex-owned user runtime and launch facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#include <kernel/ex/ex_process.h>
#include <kernel/ex/ex_thread.h>

HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeInit(void);

/**
 * Create an owning runtime process handle for a staged user image.
 * The caller owns the returned handle until it is either destroyed explicitly
 * or consumed by ExRuntimeCreateThread().
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeCreateProcess(const EX_RUNTIME_PROCESS_CREATE_PARAMS *params,
                                                              EX_PROCESS **outProcess);

/**
 * Destroy an owning runtime process handle that has not been transferred to
 * a runtime thread. Runtime table publication is non-owning, so final release
 * still happens through this explicit destroy path or through thread-owned
 * teardown after ExRuntimeCreateThread().
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeDestroyProcess(EX_PROCESS *process);

/**
 * Create a runtime thread and transfer process ownership into it.
 * On success this consumes *processHandle, sets it to NULL, and returns a new
 * owning EX_THREAD handle in *outThread. The runtime tables then publish
 * non-owning process/thread entries for runtime lookup.
 * On failure *processHandle remains owned by the caller.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeCreateThread(EX_PROCESS **processHandle,
                                                             const EX_RUNTIME_THREAD_CREATE_PARAMS *params,
                                                             EX_THREAD **outThread);

/**
 * Start a runtime thread and transfer thread ownership to the runtime.
 * On success this consumes *threadHandle, sets it to NULL, and the runtime
 * finalizer becomes responsible for teardown and final release; published
 * runtime table entries remain non-owning.
 * On failure *threadHandle remains owned by the caller.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeStartThread(EX_THREAD **threadHandle);

/**
 * Query the kernel-visible ThreadId for a not-yet-consumed runtime thread
 * handle. This keeps demo/profile code off Ex internals while still allowing
 * narrow foreground-owner setup.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeQueryThreadId(const EX_THREAD *thread, uint32_t *outThreadId);

/**
 * Query the Ex-owned PID for a runtime process. PIDs are independent from
 * KTHREAD::ThreadId and are the user-visible identity for process syscalls.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeQueryProcessId(const EX_PROCESS *process, uint32_t *outProcessId);

/**
 * Query the Ex-owned PID for the current runtime process.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeQueryCurrentProcessId(uint32_t *outProcessId);

/**
 * Cancel a runtime thread before it has been started.
 * Only threads still in the NEW state can be torn down here; once started,
 * teardown is owned by the runtime finalizer.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS ExRuntimeTeardownThread(EX_THREAD *thread);
