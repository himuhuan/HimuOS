# Bootstrap Runtime Debt Index

This index covers runtime and public-ABI debt only. True early boot under
`src/boot/v2` is not debt for this plan. Existing regression log anchors stay
fixed until an explicit replacement contract retires them.

| Debt | Current surface | Owner phase | Deletion condition |
| --- | --- | --- | --- |
| Normal userspace still depends on phase gates and raw bootstrap helpers | `HoUserWaitForP1Gate()` now lives in explicit sentinel-only `src/user/libsys_bringup.h` and remains included by `tick1s`, `fault_de`, `fault_pf`, `user_counter`, and `user_hello`; `HoUserRaw*()` is no longer in `src/user/libsys.h`, but remains in `src/user/libsys_bringup.h` for `user_hello`. | Phase G | Ordinary userspace enters directly and uses only formal Ex syscalls; any remaining raw sentinel is explicit and documented, or raw helpers are deleted entirely. |
| Remaining bootstrap-named runtime headers and helpers | Active runtime headers still include `src/include/kernel/ex/ex_bootstrap.h`, `src/include/kernel/ex/ex_bootstrap_adapter.h`, `src/include/kernel/ex/ex_process.h`, `src/include/kernel/ex/ex_thread.h`, `src/include/kernel/ex/program.h` (`ExProgramBuildBootstrapCreateParams()`), and `src/include/kernel/ke/user_bootstrap.h`. In active runtime source, `src/kernel/ex/user_runtime_bridge.c` still defines the transitional `ExBootstrapAdapter*()` helper family; `src/kernel/ex/syscall.c` still carries the Bootstrap-named exit/dispatch path via `KiAbortBootstrapExit()`, `KiPrepareBootstrapExit()`, `KiPrepareBootstrapKillExit()`, and `ExBootstrapAdapterDispatchSyscall()`; `src/kernel/ex/process.c` still exports `ExBootstrapInitializeProcessObject()`, `ExBootstrapTeardownProcessPayload()`, and `ExBootstrapReleaseProcess()`; `src/kernel/ex/thread.c` still exports `ExBootstrapInitializeThreadObject()` and `ExBootstrapBorrowKernelThread()`; `src/kernel/ex/object.c` still exports `ExBootstrapInitializeStdoutServiceObject()` and `ExBootstrapReleaseStdoutServiceOwner()`; `src/kernel/ex/runtime_table.c` still exports the compatibility `ExBootstrapQueryCurrentProcessId()` wrapper; and `src/kernel/ex/handle.c` still depends on `ExBootstrapReleaseProcess()` and `ExBootstrapReleaseThread()`. | Phase I | Searching active runtime headers and source for `Bootstrap` finds only true early-boot code or historical docs; compatibility wrappers and bootstrap-named helper families are deleted rather than kept. |

## Retired During Phase F

- `user_dual` now launches `user_hello` and `user_counter` through
  `ExSpawnProgram()` and waits for both with `ExWaitProcess()`.
- `user_input` now launches `hsh` and `calc` through `ExSpawnProgram()`,
  performs foreground handoff through `ExSetForegroundProcess()`, and waits for
  both child processes with `ExWaitProcess()`.
- Contract demo profiles no longer call `ExBootstrapCreateProcess()`,
  `ExBootstrapCreateThread()`, `ExBootstrapStartThread()`, or
  `ExBootstrapBorrowKernelThread()` directly.

## Retired During Phase E

- Pilot wait objects were removed. `EX_PROCESS` and `EX_THREAD` now own
  embedded completion events, and `SYS_WAIT_ONE` resolves wait-right handles to
  process or thread objects directly.
- `ExWaitProcess()` and `ExKillProcess()` no longer borrow a process main
  backing `KTHREAD` or call `KeThreadJoin()`. They retain the child process,
  wait on its Ex completion state, restore foreground ownership, and consume
  the completed process table entry.
- Normal Ex-spawned user threads are detached kernel threads; the idle reaper
  finalizes their user-runtime resources and signals Ex process completion.
- The `user_caps` sentinel now proves a valid seeded process wait handle by
  polling it for `EC_TIMEOUT`; it no longer depends on a pre-completed pilot
  companion thread.

## Retired During Phase D

- The runtime alias registry was replaced by `src/kernel/ex/runtime_table.c`.
  `ExRuntimeLookup*()`, `ExRuntimeCapture*()`, and
  `ExRuntimeIsPublishedObject()` are now the active runtime lookup surface.
- The process-control child table was deleted. Parent PID, child PID, main TID,
  kill request, termination state, exit status, termination reason, foreground
  ownership, and foreground restore metadata now live in `EX_PROCESS` /
  `EX_THREAD` state published through the runtime tables.
- `ps`, `wait`, `kill`, fault termination, and foreground restoration now read
  from the same Ex process/thread model.

## Retired During Phase C

- The Ke-to-Ex runtime callback surface was renamed from bootstrap callbacks
  to user-runtime hooks. The permanent contract now lives in
  `src/include/kernel/ke/user_runtime_hooks.h` and
  `src/kernel/ke/user_runtime_hooks.c`.
- Scheduler dispatch, timer observation, and IDT user-fault handoff now call
  `KeRegisterUserRuntimeHooks()` / `KiGetUserRuntime*Hook()` names.
- The Ex hook implementation moved from `src/kernel/ex/bootstrap_compat.c` to
  `src/kernel/ex/user_runtime_bridge.c`; the remaining `ExBootstrapAdapter*()`
  helper names are tracked as Phase I debt, not as the Ke hook contract.

## Retired During Phase B

- The monolithic bootstrap ABI umbrella `src/include/kernel/ex/ex_bootstrap_abi.h`
  was deleted. Its contents now live in focused contracts:
  `src/include/kernel/ex/user_syscall_abi.h`,
  `src/include/kernel/ex/user_sysinfo_abi.h`,
  `src/include/kernel/ex/user_image_abi.h`,
  `src/include/kernel/ex/user_capability_abi.h`,
  `src/include/kernel/ex/user_bringup_sentinel_abi.h`, and
  `src/include/kernel/ex/user_regression_anchors.h`.
- `src/user/libsys.h` includes only the stable user-runtime ABI contracts.
  Raw bring-up helpers and P1 mailbox waits moved to `src/user/libsys_bringup.h`.
- `src/include/kernel/ke/input.h` owns its mechanism capacity directly instead
  of including an Ex bootstrap ABI header for `KE_INPUT_LINE_CAPACITY`.

## Legacy Bring-Up Sentinels Intentionally Kept During Phase A

- `user_hello` preserves raw syscall, phase-gate, and teardown anchors.
- `user_caps` preserves capability-seed, stdout-handle, stale-handle rejection,
  and process wait-handle timeout coverage.

They remain valid only until later phases replace their unique regression value
or retire the underlying bring-up path.
